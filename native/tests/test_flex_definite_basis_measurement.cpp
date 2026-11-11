// Layout callback budget, with geometry and retained-result assertions.
// Release/NDEBUG must enforce every assertion. Wall-clock is never a gate.
#include "../src/layout/layout_internal.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <initializer_list>
#include <algorithm>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 0.f, 0.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t update_count = 0;

    Fixture(const char* html, const char* css, aether_text_measurer_fn measurer = measure_empty) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-wrap-intrinsic.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measurer, nullptr, &engine) == AETHER_OK, "create layout engine");
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t node(const char* id) const {
        const auto element = aether_doc_get_element_by_id(doc, id);
        require(element != nullptr, "find fixture element");
        return element;
    }

    aether_layout_result_t layout(float width, float height) const {
        aether_layout_result_t result = nullptr;
        require(aether_layout_layout(engine, doc, styles, width, height, nullptr, nullptr, &result)
                    == AETHER_OK && result != nullptr, "perform layout");
        return result;
    }

    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        const auto box = aether_layout_result_find_primary_box(result, node(id));
        require(box != nullptr, "find primary layout box");
        return aether_layout_box_get_border_box(box);
    }
};


// The runtime's min-width/min-height parser accepts only nonnegative lengths.
// Exercise real auto minima with new immutable legacy records, never by mutating
// a frozen style or relying on a rejected CSS declaration.
static void set_automatic_minima(Fixture& fixture, bool column = false, bool horizontal_overflow_only = false) {
    using namespace aether::style;
    BackingBuildInput backing{fixture.styles->current->backing()->entries()};
    for (auto& entry : backing.entries) {
        auto* node = entry.first;
        if (node->tag_name != "div") continue;
        const auto original = entry.second;
        auto scalars = original->style_view().scalars();
        if (column) scalars.min_height = aether_css_length_auto();
        else scalars.min_width = aether_css_length_auto();
        if (horizontal_overflow_only) {
            // Preserve the axis distinction without CSS overflow normalization.
            scalars.overflow_x = AETHER_OVERFLOW_HIDDEN;
            scalars.overflow_y = AETHER_OVERFLOW_VISIBLE;
        }
        NodeRecordInput input;
        input.origin=original->origin(); input.node=original->node();
        input.style=std::make_shared<const FrozenStyle>(scalars,original->style()->payload_owner());
        input.measures=original->measures(); input.custom_properties=original->custom_properties();
        input.color_references=original->color_references();
        input.declared_impact=original->declared_impact();
        input.declared_inherited_impact=original->declared_inherited_impact();
        input.inherited_change_impact=original->inherited_change_impact();
        input.document_identity=original->document_identity(); input.node_identity=original->node_identity();
        input.node_lifetime=original->node_lifetime(); input.document_lifetime=original->document_lifetime();
        require(freeze_node_record(input,entry.second)==AETHER_OK,"freeze real automatic minimum");
    }
    MapVersionInput version;
    require(freeze_backing(backing,version.backing)==AETHER_OK,"freeze automatic minimum backing");
    version.mode=MapMode::legacy_partial;
    require(make_map_version(version,fixture.styles->current)==AETHER_OK,"publish automatic minimum map");
}

static unsigned long long calls = 0;
static aether_text_metrics_t counted(const char* text, const aether_computed_style_t*, float, void*) {
    ++calls;
    return {float(std::strlen(text)) * 10.f, 20.f, 15.f};
}
static bool near(float a, float b) { return std::isfinite(a) && std::fabs(a-b) < .001f; }

int main() {
    int cases = 0;
    for (const char* mode : {"fixed", "auto-width", "percentage", "calc-zero", "variable",
                             "overflow-hidden", "overflow-auto", "overflow-scroll",
                             "explicit-min", "percent-min", "auto-min", "content", "column-fixed",
                             "column-auto-min", "column-overflow-hidden", "column-overflow-auto",
                             "column-overflow-scroll", "column-overflow-x", "column-margins", "column-stretch", "stretch"}) {
        const std::string kind(mode);
        const bool column = kind.rfind("column-", 0) == 0;
        const bool stretched = kind == "stretch" || kind == "column-stretch";
        const bool column_auto_min = kind == "column-auto-min" || kind == "column-overflow-x";
        const bool optimized = kind != "auto-min" && kind != "content" &&
            !column_auto_min && kind != "column-margins" && !stretched;
        const int maximum = optimized ? 64 : stretched ? 8 : 16;
        for (int depth = 1; depth <= maximum; ++depth) {
            if (depth > 16 && depth != 32 && depth != 64) continue;
            std::string html = "<body>";
            for (int i=0; i<depth; ++i) html += i == 0 ? "<div class='n' id='root'>" : "<div class='n'>";
            html += "<div id='leaf'>aaaa bb cc</div>";
            for (int i=0; i<depth; ++i) html += "</div>";
            html += "</body>";
            std::string css = "*{margin:0;padding:0;border:0;box-sizing:border-box;}"
                "body,div{display:block;}body{width:800px;--basis:200px;}"
                ".n{display:flex;align-items:flex-start;min-width:0;width:200px;flex:0 0 200px;}"
                "#leaf{min-width:0;flex:0 0 200px;width:200px;height:20px;}";
            if (kind == "auto-width") css += ".n,#leaf{flex-basis:auto}";
            if (kind == "percentage") css += ".n,#leaf{flex-basis:100%}";
            if (kind == "calc-zero") css += ".n,#leaf{flex-basis:calc(0% + 200px)}";
            if (kind == "variable") css += ".n,#leaf{flex-basis:var(--basis)}";
            if (kind.rfind("overflow-", 0) == 0)
                css += ".n,#leaf{overflow-x:" + kind.substr(9) + ";overflow-y:visible}";
            if (kind == "explicit-min") css += ".n,#leaf{min-width:50px}";
            if (kind == "percent-min") css += ".n,#leaf{min-width:25%}";
            if (kind == "content") css += ".n,#leaf{flex-basis:content}";
            if (column) css += ".n{flex-direction:column;height:200px;min-height:0;}"
                "#leaf{height:200px;min-height:0;}";
            if (kind == "column-margins") css += ".n:not(#root),#leaf{margin-top:-.125px;margin-bottom:.125px}";
            if (stretched) css += ".n{align-items:stretch}";
            if (kind == "column-stretch") css += ".n:not(#root),#leaf{width:auto}";
            if (kind.rfind("column-overflow-", 0) == 0) {
                if (kind == "column-overflow-x") css += ".n,#leaf{overflow-x:hidden;overflow-y:visible}";
                else css += ".n,#leaf{overflow-y:" + kind.substr(16) + ";overflow-x:visible}";
            }
            Fixture fixture(html.c_str(), css.c_str(), counted);
            if (kind == "auto-min" || kind.rfind("overflow-", 0) == 0 || column_auto_min ||
                kind.rfind("column-overflow-", 0) == 0) set_automatic_minima(fixture, column, kind == "column-overflow-x");
            aether_layout_result_t first = nullptr;
            for (int pass=0; pass<2; ++pass) {
                calls = 0;
                auto result = fixture.layout(pass ? 1100.f : 900.f, 700);
                const auto leaf = fixture.rect(result, "leaf");
                require(near(leaf.x, 0) && near(leaf.y, kind == "column-margins" ? -.125f * depth : 0.f),
                        "nested leaf position retained");
                require(near(leaf.width, kind == "content" ? 100.f : 200.f), "nested leaf width retained");
                require(near(leaf.height, column ? 200.f : 20.f), "nested leaf height retained");
                if (optimized) require(calls == 5, "exactly one five-token final layout, independent of depth");
                if (kind == "auto-min" || column_auto_min) require(calls >= 10, "needed visible automatic minima retain content measurement");
                if (kind == "content" || kind == "column-margins" || (stretched && depth > 1))
                    require(calls > 5, "noneligible control retains required extra layout work");
                std::printf("mode=%s depth=%d pass=%d callbacks=%llu leaf=%g,%g,%g,%g\n",
                    mode, depth, pass, calls, leaf.x, leaf.y, leaf.width, leaf.height);
                if (pass == 0) first = result;
                else {
                    const auto retained = fixture.rect(first, "leaf");
                    require(near(retained.width, leaf.width) && near(retained.height, leaf.height),
                            "relayout does not change retained geometry");
                    aether_layout_result_destroy(result);
                    aether_layout_result_destroy(first);
                }
                ++cases;
            }
        }
    }
    std::printf("flex definite basis measurement: %d cases passed\n", cases);
}
