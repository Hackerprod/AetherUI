#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Collect geometric failures so the base implementation exercises both axes and
// every relayout, rather than stopping at the first incorrect shrink result.
static void expect(float actual, float expected, const char* scenario, const char* field) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.001f) {
        std::fprintf(stderr, "FAIL: %s: %s = %g, expected %g (finite)\n",
                     scenario, field, actual, expected);
        ++failures;
    }
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

    Fixture(const char* html, const char* css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-fractional.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK, "create layout engine");
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

    void check_shrink(const char* id, const char* scenario) const {
        const auto element = node(id);
        bool found = false;
        for (int32_t i = 0; i < update_count; ++i) {
            aether_node_t updated = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &updated, &handle, nullptr) == AETHER_OK,
                    "get runtime style update");
            if (updated == element) {
                aether_computed_style_t style{};
                require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy computed style");
                expect(style.flex_shrink, .25f, scenario, id);
                aether_computed_style_free(&style);
                found = true;
            }
            aether_computed_style_handle_destroy(handle);
        }
        require(found, "find computed style for fixture item");
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

static void check_geometry(const Fixture& fixture, aether_layout_result_t result, bool row,
                           const char* container_id, const char* first_id, const char* second_id,
                           float available, float cross, float expected, const char* scenario) {
    const auto container = fixture.rect(result, container_id);
    const auto first = fixture.rect(result, first_id);
    const auto second = fixture.rect(result, second_id);
    expect(row ? container.width : container.height, available, scenario, "container main size");
    expect(row ? container.height : container.width, cross, scenario, "container cross size");
    expect(row ? first.width : first.height, expected, scenario, "first main size");
    expect(row ? second.width : second.height, expected, scenario, "second main size");
    expect(row ? first.x - container.x : first.y - container.y, 0.f, scenario, "first main offset");
    expect(row ? second.x - first.x : second.y - first.y, expected, scenario, "main separation");
    expect(row ? first.y - container.y : first.x - container.x, 0.f, scenario, "first cross offset");
    expect(row ? second.y - container.y : second.x - container.x, 0.f, scenario, "second cross offset");
}

static void fixed_reproduction() {
    // Preserve the original investigation's markup and stylesheet. The column's
    // absolute y depends on the preceding row, so assertions use relative offsets.
    Fixture fixture(
        "<body><div id='row'><div id='a'></div><div id='b'></div></div>"
        "<div id='column'><div id='c'></div><div id='d'></div></div></body>",
        "body {margin:0;display:block;} "
        "#row {display:flex;width:100px;height:20px;} "
        "#row>div {width:100px;height:20px;flex-shrink:0.25;min-width:0;} "
        "#column {display:flex;flex-direction:column;width:20px;height:100px;} "
        "#column>div {width:20px;height:100px;flex-shrink:0.25;min-height:0;}");
    fixture.check_shrink("a", "fixed row");
    fixture.check_shrink("b", "fixed row");
    fixture.check_shrink("c", "fixed column");
    fixture.check_shrink("d", "fixed column");
    const auto result = fixture.layout(640.f, 480.f);
    check_geometry(fixture, result, true, "row", "a", "b", 100.f, 20.f, 75.f, "fixed row");
    check_geometry(fixture, result, false, "column", "c", "d", 100.f, 20.f, 75.f, "fixed column");
    aether_layout_result_destroy(result);
}

static void relayout_reproduction(bool row) {
    Fixture fixture("<body id='container'><div id='a'></div><div id='b'></div></body>", row ?
        "body {display:flex;flex-direction:row;width:100%;height:100%;margin:0;padding:0;border:0;box-sizing:border-box;} "
        "body>div {width:100px;height:20px;flex-shrink:.25;min-width:0;}" :
        "body {display:flex;flex-direction:column;width:100%;height:100%;margin:0;padding:0;border:0;box-sizing:border-box;} "
        "body>div {width:20px;height:100px;flex-shrink:.25;min-height:0;}");
    fixture.check_shrink("a", row ? "relayout row" : "relayout column");
    fixture.check_shrink("b", row ? "relayout row" : "relayout column");
    const float available[] = {100.f, 200.f, 50.f, 100.f};
    const float expected[] = {75.f, 100.f, 62.5f, 75.f};
    // Keep the DOM, runtime, resolved styles and engine intact. Only the layout
    // viewport changes. Copy rectangles and destroy each result before the next.
    for (int i = 0; i < 4; ++i) {
        char scenario[96];
        std::snprintf(scenario, sizeof(scenario), "relayout %s step %d viewport %g",
                      row ? "row" : "column", i + 1, available[i]);
        const auto result = fixture.layout(row ? available[i] : 400.f, row ? 400.f : available[i]);
        check_geometry(fixture, result, row, "container", "a", "b", available[i], 400.f, expected[i], scenario);
        aether_layout_result_destroy(result);
    }
}

int main() {
    require(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
            "compatible ABI major version");
    const auto required = AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME | AETHER_CAPABILITY_LAYOUT;
    require((aether_abi_capabilities() & required) == required, "required ABI capabilities");
    fixed_reproduction();
    relayout_reproduction(true);
    relayout_reproduction(false);
    if (failures != 0) {
        std::fprintf(stderr, "%d fractional flex ABI checks failed\n", failures);
        return 1;
    }
    std::puts("fractional flex ABI tests passed");
    return 0;
}
