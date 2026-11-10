#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_transitions.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
int checks = 0;
void require(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL opacity inheritance: %s\n", label); std::exit(1); }
}
void equal(float actual, float expected, const char* label) {
    if (actual != expected) std::fprintf(stderr, "%s: actual=%g expected=%g\n", label, actual, expected);
    require(std::isfinite(actual) && actual == expected && (expected != 0 || !std::signbit(actual)), label);
}
constexpr uint32_t opacity_impact = AETHER_STYLE_IMPACT_COMPOSITE | AETHER_STYLE_IMPACT_HITTEST;
struct Style {
    aether_computed_style_t value{};
    explicit Style(aether_computed_style_handle_t handle) {
        require(handle && aether_computed_style_copy(handle, &value) == AETHER_OK, "copy style");
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_environment_t env{240, 160, 1, 16, 0, 0, 0};
    aether_style_revision_t revision{};
    aether_style_map_t map{};
    bool contextual;
    std::map<aether_node_t, uint32_t> impacts;
    std::vector<std::pair<std::string, float>> generated;
    explicit Fixture(bool context, const char* html =
        "<html><body><div id='a' style='opacity:.3'><div id='c' class='target' style='opacity:inherit'>"
        "<div id='g' style='opacity:inherit'></div></div><div id='plain'></div>"
        "<div id='own' style='opacity:.6'></div></div><div id='b' style='opacity:.8'></div></body></html>")
        : contextual(context) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse html");
        aether_style_runtime_options_t options{};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    }
    ~Fixture() {
        aether_style_map_destroy(map); aether_style_revision_destroy(revision);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) {
        auto result = aether_doc_get_element_by_id(doc, id); require(result, "find node"); return result;
    }
    void set(const char* id, const char* css) {
        require(aether_element_set_attribute(node(id), "style", css) == AETHER_OK, "set style");
    }
    void sheet(const char* css) {
        int64_t id{};
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "inherit.css", &id, nullptr) == AETHER_OK, "add sheet");
    }
    void capture_generated(aether_node_t n, aether_computed_style_handle_t h) {
        // Generated nodes live in the revision; they are not authored DOM children.
        if (!h || aether_node_get_parent(n) != node("c") || n == node("g")) return;
        char* text = aether_node_get_text_content(aether_node_get_child(n, 0));
        if (text) { Style style(h); generated.emplace_back(text, style.value.opacity); }
        aether_string_free(text);
    }
    void resolve(aether_node_t root = nullptr) {
        impacts.clear(); generated.clear();
        if (!root) root = aether_doc_get_document_element(doc);
        aether_style_map_destroy(map); map = nullptr;
        if (contextual) {
            aether_style_transaction_t tx{};
            require(aether_style_runtime_prepare_revision(runtime, root, &env, &tx) == AETHER_OK, "prepare revision");
            aether_style_revision_t next{};
            require(aether_style_runtime_commit_revision(runtime, tx, &next) == AETHER_OK, "commit revision");
            aether_style_transaction_destroy(tx);
            aether_style_revision_destroy(revision); revision = next;
            for (int i = 0; i < aether_style_revision_change_count(revision); ++i) {
                aether_node_t n{}; aether_computed_style_handle_t h{}; uint32_t flags{};
                require(aether_style_revision_get_change(revision, i, &n, &h, &flags) == AETHER_OK, "revision change");
                impacts[n] |= flags; aether_computed_style_handle_destroy(h);
            }
            for (int i = 0; i < aether_style_revision_style_count(revision); ++i) {
                aether_node_t n{}; aether_computed_style_handle_t h{};
                require(aether_style_revision_get_style(revision, i, &n, &h, nullptr) == AETHER_OK, "revision style");
                capture_generated(n, h); aether_computed_style_handle_destroy(h);
            }
            require(aether_style_revision_create_map(revision, &map) == AETHER_OK, "revision map");
        } else {
            aether_style_update_t* updates{};
            const int count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
            require(count >= 0, "legacy resolve");
            for (int i = 0; i < count; ++i) {
                aether_node_t n{}; aether_computed_style_handle_t h{}; uint32_t flags{};
                require(aether_style_runtime_get_update(updates[i], &n, &h, &flags) == AETHER_OK, "legacy update");
                capture_generated(n, h);
                impacts[n] |= flags; aether_computed_style_handle_destroy(h);
            }
            aether_style_updates_free(updates, count);
            require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK, "legacy map");
        }
    }
    void expect(const char* id, float opacity, uint32_t impact = UINT32_MAX) {
        auto n = node(id); auto h = aether_style_map_get(map, n); Style style(h);
        equal(style.value.opacity, opacity, id);
        if (impact != UINT32_MAX) require(impacts[n] == impact, "exact opacity impact");
        aether_computed_style_handle_destroy(h);
    }
};
void priorities() {
    struct Case { const char* sheet; const char* inlined; float expected; };
    for (bool context : {false, true}) for (const auto& c : {
        Case{"#c{opacity:.2;opacity:inherit}", "", .3f},
        Case{"#c{opacity:inherit;opacity:.2}", "", .2f},
        Case{"#c{opacity:inherit}.target{opacity:.2}", "", .3f},
        Case{"#c{opacity:inherit}.target{opacity:.2!important}", "", .2f},
        Case{"#c{opacity:inherit!important}", "opacity:.2", .3f},
        Case{"#c{opacity:.2}", "opacity:inherit", .3f},
        Case{"#c{opacity:inherit!important}", "opacity:.2!important", .2f},
        Case{"#c{opacity:.2!important}", "opacity:inherit!important", .3f}}) {
        Fixture f(context); f.set("c", c.inlined); f.sheet(c.sheet); f.resolve(); f.expect("c", c.expected);
        f.set("a", "opacity:.7"); f.resolve();
        f.expect("c", c.expected == .3f ? .7f : .2f, c.expected == .3f ? opacity_impact : 0);
    }
}
void lifecycle(bool context) {
    Fixture f(context); f.resolve();
    f.expect("a", .3f); f.expect("c", .3f); f.expect("g", .3f); f.expect("plain", 1); f.expect("own", .6f);
    aether_style_map_t retained{};
    if (context) require(aether_style_revision_create_map(f.revision, &retained) == AETHER_OK, "retain revision map");
    else require(aether_style_runtime_create_resolved_style_map(f.runtime, f.doc, &retained) == AETHER_OK, "retain legacy map");
    auto old = aether_style_map_get(retained, f.node("c")); Style copied(old);
    aether_computed_style_handle_t clone{};
    require(aether_computed_style_handle_clone(old, &clone) == AETHER_OK, "clone snapshot");
    aether_style_revision_t revision{};
    if (context) require(aether_style_revision_clone(f.revision, &revision) == AETHER_OK, "clone revision");
    for (const char* parent : {"opacity:.7", "opacity:1e-7", "opacity:-0", "opacity:150%"}) {
        f.set("a", parent); f.resolve();
        auto h = aether_style_map_get(f.map, f.node("a")); Style a(h); const float opacity = a.value.opacity;
        aether_computed_style_handle_destroy(h);
        f.expect("c", opacity, opacity_impact); f.expect("g", opacity, opacity_impact);
        f.expect("plain", 1, 0); f.expect("own", .6f, 0);
        f.resolve(); f.expect("c", opacity, 0); f.expect("g", opacity, 0);
    }
    f.set("a", "opacity:.7"); f.resolve();
    f.set("c", "opacity:.7"); f.resolve(); f.expect("c", .7f, 0);
    f.set("a", "opacity:.3"); f.resolve(); f.expect("c", .7f, 0); f.expect("g", .7f, 0);
    f.set("c", "opacity:inherit"); f.resolve(); f.expect("c", .3f, opacity_impact);
    f.resolve(); f.resolve();
    // Isolated removal. Mixed geometric removals have a pre-existing runtime
    // limitation documented in docs/opacity-inheritance.md.
    f.set("c", ""); f.resolve(); f.expect("c", 1, opacity_impact); f.expect("g", 1, opacity_impact);
    f.set("c", "opacity:inherit"); f.resolve();
    require(aether_node_append_child(f.node("b"), f.node("c")) == AETHER_OK, "reparent");
    f.resolve(); f.expect("c", .8f, opacity_impact); f.expect("g", .8f, opacity_impact);
    for (auto h : {old, clone}) { Style saved(h); equal(saved.value.opacity, .3f, "retained handle immutable"); }
    equal(copied.value.opacity, .3f, "exported copy immutable");
    auto saved = aether_style_map_get(retained, f.node("c")); { Style s(saved); equal(s.value.opacity, .3f, "retained map immutable"); }
    aether_computed_style_handle_destroy(saved);
    if (revision) {
        aether_style_map_t m{}; require(aether_style_revision_create_map(revision, &m) == AETHER_OK, "old revision map");
        auto h = aether_style_map_get(m, f.node("c")); { Style s(h); equal(s.value.opacity, .3f, "retained revision immutable"); }
        aether_computed_style_handle_destroy(h); aether_style_map_destroy(m);
    }
    aether_style_revision_destroy(revision); aether_computed_style_handle_destroy(clone);
    aether_computed_style_handle_destroy(old); aether_style_map_destroy(retained);
}
void boundaries(bool context) {
    Fixture f(context); f.sheet("html{opacity:inherit}"); f.resolve();
    auto root = aether_style_map_get(f.map, aether_doc_get_document_element(f.doc));
    { Style style(root); equal(style.value.opacity, 1, "root inherit uses initial"); }
    aether_computed_style_handle_destroy(root);
    f.resolve(f.node("c")); f.expect("c", context ? .3f : 1.f);
    f.expect("g", context ? .3f : 1.f);
    f.resolve(); f.expect("c", .3f);
    f.sheet("#c::before{content:'before';opacity:inherit}#c::after{content:'after';opacity:unset}");
    for (const auto& parent : {std::pair{"opacity:.3", .3f}, {"opacity:.7", .7f}}) {
        f.set("a", parent.first); f.resolve(); int generated = 0;
        for (const auto& item : f.generated) {
            if (item.first == "before") { equal(item.second, parent.second, "before inherits host"); ++generated; }
            if (item.first == "after") { equal(item.second, 1, "after unset stays initial"); ++generated; }
        }
        require(generated == 2, "both generated pseudos checked");
    }
}
aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) { return {0, 1, 1}; }
void paint_and_transition(bool context) {
    Fixture f(context); f.set("a", "opacity:.5");
    f.sheet("html,body,div{display:block;margin:0;padding:0}#a{width:100px;height:100px}"
        "#c{width:30px;height:20px;background-color:rgba(0,0,255,.8);transition:opacity 1s linear}");
    f.resolve(); auto before = aether_style_map_get(f.map, f.node("c"));
    f.expect("c", .5f);
    aether_layout_engine_t engine{}; aether_layout_result_t layout{}; aether_display_list_builder_t builder{};
    aether_display_list_t list{};
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine");
    require(aether_layout_layout(engine, f.doc, f.map, 240, 160, nullptr, nullptr, &layout) == AETHER_OK, "layout");
    require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "builder");
    require(aether_display_list_build_with_styles(builder, layout, f.map, nullptr, &list) == AETHER_OK, "paint");
    int matches = 0;
    for (int i = 0; i < aether_display_list_get_count(list); ++i) {
        aether_paint_command_t cmd{}; require(aether_display_list_get_command(list, i, &cmd) == AETHER_OK, "paint command");
        if (cmd.tag == AETHER_PAINT_FILL_ROUNDED_RECT && cmd.data.fill_rounded_rect.color.b == 1 &&
            cmd.data.fill_rounded_rect.rect.width == 30) {
            equal(cmd.data.fill_rounded_rect.color.a, .8f * .5f * .5f, "paint multiplies computed child and ancestor once"); ++matches;
        }
        aether_paint_command_free(&cmd);
    }
    require(matches == 1, "paint sample exists");
    aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout); aether_layout_destroy(engine);
    f.set("a", "opacity:.75"); f.resolve(); auto after = aether_style_map_get(f.map, f.node("c"));
    f.expect("c", .75f, opacity_impact);
    aether_contextual_transition_runtime_t transitions{};
    require(aether_contextual_transition_create(&transitions) == AETHER_OK, "create transition");
    const aether_contextual_transition_event event{1, 0, f.node("c"), before, after};
    for (const auto& item : {std::pair<int64_t, float>{0, .5f}, {500, .625f}, {1000, .75f}}) {
        aether_contextual_transition_batch_t batch{};
        require(aether_contextual_transition_prepare(transitions, &event, 1, item.first, &batch) == AETHER_OK, "transition prepare");
        aether_contextual_transition_sample sample{};
        require(aether_contextual_transition_sample_box(batch, after, {0, 0, 30, 20}, &sample) == AETHER_OK, "transition sample");
        equal(sample.opacity, item.second, "inherited transition endpoint/interior");
        aether_contextual_transition_batch_destroy(batch);
    }
    aether_contextual_transition_destroy(transitions);
    aether_computed_style_handle_destroy(before); aether_computed_style_handle_destroy(after);
}
} // namespace
int main() {
    priorities();
    for (bool context : {false, true}) { lifecycle(context); boundaries(context); paint_and_transition(context); }
    std::printf("opacity inheritance: %d checks (active with NDEBUG)\n", checks);
}
