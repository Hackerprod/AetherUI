// Acceptance tests for the final-state R2 invalidation contract. Expected masks
// are independent literals: never derive the oracle from the classifier.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/dom/dom_types.h"
#include "frozen_measure_test_support.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void exact(uint32_t actual, uint32_t expected, const char* name) {
    if (actual != expected) {
        std::fprintf(stderr, "FAIL: %s: expected impact %u, got %u\n", name, expected, actual);
        std::exit(1);
    }
}
static void equal(float actual, float expected, const char* name) {
    if (std::fabs(actual - expected) > .001f) {
        std::fprintf(stderr, "FAIL: %s: expected %g, got %g\n", name, expected, actual);
        std::exit(1);
    }
}
static aether_text_metrics_t measure(const char* t, const aether_computed_style_t*, float, void*) {
    return {float(std::strlen(t)) * 8, 16, 12};
}
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_node_t x{};
    bool contextual;
    aether_style_environment_t env{300, 200, 1, 16, 0, 0, 0};
    std::map<aether_node_t, uint32_t> impacts;
    explicit Fixture(bool mode, const char* markup =
        "<html><body style='width:300px'><div id='x'></div></body></html>") : contextual(mode) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, markup) == AETHER_OK, "parse HTML");
        x = id("x");
        aether_style_runtime_options_t options{};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    aether_node_t id(const char* name) const {
        auto node = aether_doc_get_element_by_id(doc, name);
        require(node != nullptr, "find node"); return node;
    }
    void set(const char* css, aether_node_t node = nullptr) {
        require(aether_element_set_attribute(node ? node : x, "style", css) == AETHER_OK, "set inline style");
    }
    void sheet(const char* css) {
        int64_t handle{};
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "semantic-diff", &handle, nullptr) == AETHER_OK, "add sheet");
    }
    uint32_t resolve(bool environment_changed = false, bool state_changed = false) {
        impacts.clear();
        const auto root = aether_doc_get_document_element(doc);
        if (contextual) {
            aether_style_transaction_t transaction{}; aether_style_revision_t revision{};
            require(aether_style_runtime_prepare_revision(runtime, root, &env, &transaction) == AETHER_OK, "prepare revision");
            require(aether_style_runtime_commit_revision(runtime, transaction, &revision) == AETHER_OK, "commit revision");
            for (int i = 0; i < aether_style_revision_style_count(revision); ++i) {
                aether_node_t node{}; uint32_t impact{};
                require(aether_style_revision_get_style(revision, i, &node, nullptr, &impact) == AETHER_OK, "revision entry");
                impacts[node] = impact;
            }
            aether_style_revision_destroy(revision); aether_style_transaction_destroy(transaction);
        } else {
            aether_style_update_t* updates{};
            const int count = environment_changed ? aether_style_runtime_set_environment(runtime, &env, root, &updates)
                : state_changed ? aether_style_runtime_resolve_state_changes(runtime, root, &x, 1, &updates)
                : aether_style_runtime_resolve_subtree(runtime, root, &updates);
            require(count >= 0, "resolve legacy");
            for (int i = 0; i < count; ++i) {
                aether_node_t node{}; uint32_t impact{};
                require(aether_style_runtime_get_update(updates[i], &node, nullptr, &impact) == AETHER_OK, "legacy entry");
                impacts[node] = impact;
            }
            aether_style_updates_free(updates, count);
        }
        return impacts[x];
    }
    void noops() {
        for (int pass = 0; pass < 2; ++pass) {
            exact(resolve(), 0, "semantic no-op subject");
            for (const auto& item : impacts) exact(item.second, 0, "semantic no-op descendant/ancestor");
        }
    }
    aether_style_map_t map() {
        aether_style_map_t result{};
        if (contextual) {
            aether_style_revision_t revision{};
            require(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK, "retain revision");
            require(aether_style_revision_create_map(revision, &result) == AETHER_OK, "revision map");
            aether_style_revision_destroy(revision);
        } else require(aether_style_runtime_create_resolved_style_map(runtime, doc, &result) == AETHER_OK, "legacy map");
        return result;
    }
    aether_computed_style_handle_t handle(aether_node_t node = nullptr) {
        auto m = map(); auto h = aether_style_map_get(m, node ? node : x);
        require(h != nullptr, "map handle"); aether_style_map_destroy(m); return h;
    }
};
struct Geometry {
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_layout_result_t layout{};
    aether_node_t node{};
    explicit Geometry(Fixture& f) : map(f.map()), node(f.x) {
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "create layout");
        require(aether_layout_layout(engine, f.doc, map, 300, 200, nullptr, nullptr, &layout) == AETHER_OK, "layout");
    }
    ~Geometry() { aether_layout_result_destroy(layout); aether_layout_destroy(engine); aether_style_map_destroy(map); }
    aether_rectf rect() const {
        auto box = aether_layout_result_find_primary_box(layout, node);
        require(box != nullptr, "layout box"); return aether_layout_box_get_border_box(box);
    }
};
static void plan_layout(uint32_t impact, bool expected) {
    aether_render_plan_input_t input{};
    input.invalidation = aether_visual_invalidation_from_style_impact(impact);
    input.display_list_available = AETHER_TRUE; input.renderer_supports_incremental = AETHER_TRUE;
    aether_render_plan_t plan{};
    require(aether_render_plan_evaluate(&input, &plan) == AETHER_OK, "render plan");
    require(bool(plan.requires_layout) == expected, "render-plan layout policy");
}

// Original investigation: 15 scenarios in each API = 30 independently asserted cases.
static void original_harness(bool contextual) {
    const char* initial = "width:100px;color:red;opacity:.4";
    struct Case { const char* name; const char* before; const char* after; uint32_t expected; };
    const Case cases[] = {
        {"remove opacity", initial, "width:100px;color:red", 12},
        {"remove width/change opacity", initial, "color:red;opacity:.5", 15},
        {"remove width/change cursor", initial, "color:red;opacity:.4;cursor:pointer", 103},
        {"remove opacity/change background", initial, "width:100px;color:red;background-color:blue", 14},
        {"remove font/change opacity", "font-size:30px;opacity:.4", "opacity:.5", 95},
        {"remove background/change cursor", "background-color:red;cursor:pointer", "cursor:crosshair", 98},
        {"width invalid var/change opacity", initial, "width:var(--missing);color:red;opacity:.5", 15},
        {"opacity invalid var/change cursor", initial, "width:100px;color:red;opacity:var(--missing);cursor:pointer", 108},
        {"remove width/change origin", "width:100px;transform-origin:10% 20%", "transform-origin:30% 40%", 15},
        {"remove width/change visibility", "width:100px;visibility:visible", "visibility:hidden", 71},
    };
    for (const auto& c : cases) {
        Fixture f(contextual); f.set(c.before); f.resolve(); f.noops();
        f.set(c.after); exact(f.resolve(), c.expected, c.name); plan_layout(c.expected, (c.expected & 17) != 0); f.noops();
    }
    for (bool remove_width : {false, true}) {
        Fixture f(contextual); f.set(initial); f.resolve();
        f.set("width:100px;color:red;opacity:.3"); exact(f.resolve(), 12, "opacity-only history");
        f.set(initial); exact(f.resolve(), 12, "restore history"); f.noops();
        f.set(remove_width ? "color:red;opacity:.4" : "width:100px;color:red");
        exact(f.resolve(), remove_width ? 7 : 12, "history-independent removal"); f.noops();
    }
    {
        Fixture f(contextual); f.sheet("#x{color:red;opacity:.5}#x.on{width:100px;opacity:.4}");
        require(aether_element_set_attribute(f.x, "class", "on") == AETHER_OK, "class on"); f.resolve(); f.noops();
        require(aether_element_set_attribute(f.x, "class", "") == AETHER_OK, "class off");
        exact(f.resolve(), 15, "class loss width/opacity"); f.noops();
    }
    {
        Fixture f(contextual); f.sheet("#x{color:red;opacity:.5}@media(min-width:250px){#x{width:100px;opacity:.4}}");
        f.resolve(); f.noops(); f.env.viewport_width = 200;
        exact(f.resolve(true), 15, "media loss width/opacity"); f.noops();
    }
    {
        Fixture f(contextual); f.sheet("html,body,div{display:block;margin:0;padding:0}#x{height:20px}");
        f.set(initial); f.resolve(); Geometry before(f); equal(before.rect().width, 100, "old width"); f.noops();
        f.set("color:red;opacity:.5"); const auto impact = f.resolve(); exact(impact, 15, "geometry witness"); plan_layout(impact, true);
        Geometry after(f); equal(after.rect().width, 300, "new width"); equal(before.rect().width, 100, "retained old width"); f.noops();
    }
}

static bool same_length(aether_css_length a, aether_css_length b) {
    return a.unit == b.unit && a.value == b.value && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem &&
        a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
static void percentage_height_snapshots(bool contextual) {
    Fixture f(contextual); f.sheet("html,body,div{display:block;margin:0;padding:0}body{height:auto}");
    f.set("height:calc(0% + 100px)"); f.resolve(); f.noops();
    auto old = f.handle(); Geometry before(f);
    require(frozen_measure_test::has_percentage(old->measures.get(), "height", old->style.height), "old height category");
    equal(before.rect().height, 0, "indefinite percentage height");
    f.set("height:calc(100px)"); auto impact = f.resolve(); exact(impact, 7, "flat-equal percentage removal"); plan_layout(impact, true);
    auto current = f.handle(); Geometry after(f);
    require(same_length(old->style.height, current->style.height), "flat height remains identical");
    require(!frozen_measure_test::has_percentage(current->measures.get(), "height", current->style.height), "new height category");
    equal(after.rect().height, 100, "absolute height"); f.noops();
    f.set("height:calc(0% + 100px)"); impact = f.resolve(); exact(impact, 7, "flat-equal percentage addition"); plan_layout(impact, true);
    Geometry restored(f); equal(restored.rect().height, 0, "restored deferred height"); f.noops();
    require(frozen_measure_test::has_percentage(old->measures.get(), "height", old->style.height), "retained old category");
    require(!frozen_measure_test::has_percentage(current->measures.get(), "height", current->style.height), "retained middle category");
    equal(before.rect().height, 0, "retained old geometry"); equal(after.rect().height, 100, "retained middle geometry");
    aether_computed_style_handle_destroy(old); aether_computed_style_handle_destroy(current);
}

static void percentage_matrix(bool contextual) {
    for (const char* property : {"width", "height", "min-width", "min-height", "max-width", "max-height", "flex-basis", "row-gap", "column-gap"}) {
        const auto expected = std::strcmp(property, "flex-basis") == 0 ? 1u : std::strstr(property, "gap") ? 17u : 7u;
        for (const char* expression : {"calc(0% + 100px)", "calc(50% - 50% + 100px)"}) {
            Fixture f(contextual); const std::string prefix = std::string(property) + ":";
            f.set((prefix + expression).c_str()); f.resolve(); f.noops();
            f.set((prefix + "calc(100px)").c_str()); exact(f.resolve(), expected, property); f.noops();
            f.set((prefix + expression).c_str()); exact(f.resolve(), expected, property); f.noops();
        }
    }
}

static void dependencies_and_inheritance(bool contextual) {
    {
        Fixture f(contextual); f.sheet("#x{font-size:16px}"); f.set("width:16px"); f.resolve();
        auto old = f.handle(); f.set("width:1rem"); exact(f.resolve(), 0, "equivalent px to rem");
        auto rem = f.handle(); require((rem->measures->dependency("width") & MEASURE_ROOT_FONT) != 0, "publish root dependency");
        require((old->measures->dependency("width") & MEASURE_ROOT_FONT) == 0, "old dependency unchanged");
        f.set("font-size:20px", aether_doc_get_document_element(f.doc)); exact(f.resolve(), 7, "root change updates dependent width only");
        auto resized = f.handle(); equal(resized->style.width.value, 20, "rem width updated"); equal(rem->style.width.value, 16, "old rem width retained");
        f.set("width:20px"); exact(f.resolve(), 0, "equivalent rem to px"); auto px = f.handle();
        require((px->measures->dependency("width") & MEASURE_ROOT_FONT) == 0, "remove root dependency");
        f.set("font-size:24px", aether_doc_get_document_element(f.doc)); exact(f.resolve(), 0, "removed dependency stays removed");
        auto stable = f.handle(); equal(stable->style.width.value, 20, "px width independent");
        equal(rem->measures->document().values().root_font_size, 16, "retained document context");
        for (auto h : {old, rem, resized, px, stable}) aether_computed_style_handle_destroy(h);
    }
    {
        Fixture f(contextual, "<html><body><div id='x'><div id='inherited'></div><div id='override'></div><div id='explicit'></div></div></body></html>");
        f.sheet("#override{color:blue;cursor:crosshair}#explicit{width:inherit;color:blue;cursor:crosshair}");
        f.set("width:100px;color:red;cursor:pointer"); f.resolve(); f.noops();
        f.set("width:120px;color:green;cursor:wait"); exact(f.resolve(), 103, "parent mixed impact");
        exact(f.impacts[f.id("inherited")], 98, "inherited child own color/cursor only");
        exact(f.impacts[f.id("override")], 0, "overridden child avoids parent OR");
        exact(f.impacts[f.id("explicit")], 7, "explicit noninherited width inherit"); f.noops();
    }
    {
        Fixture f(contextual, "<html><body><div id='x'><div id='child' style='height:inherit'></div></div></body></html>");
        f.set("height:calc(0% + 100px)"); f.resolve(); f.set("height:calc(100px)"); exact(f.resolve(), 7, "parent percentage change");
        exact(f.impacts[f.id("child")], 7, "inherited percentage category change"); f.noops();
    }
}

static void payloads_and_sidecars(bool contextual) {
    struct Case { const char* before; const char* after; uint32_t expected; };
    const Case cases[] = {
        {"opacity:.5", "opacity:.6", 12},
        {"transform:translateX(1px)", "transform:translateX(2px)", 12},
        {"", "transform:translateX(0px)", 13},
        {"backdrop-filter:blur(1px)", "backdrop-filter:blur(2px)", 10},
        {"", "backdrop-filter:blur(0px)", 11},
        {"background-image:url(a)", "background-image:url(b)", 2},
        {"background-image:linear-gradient(red,blue)", "background-image:linear-gradient(red,green)", 2},
        {"background-image:url(a),url(b)", "background-image:url(b),url(a)", 2},
        {"box-shadow:1px 2px 3px red", "box-shadow:1px 2px 4px red", 2},
        {"text-shadow:1px 2px 3px red", "text-shadow:1px 2px 4px red", 66},
        {"line-height:2;font-size:16px", "line-height:32px;font-size:16px", 81},
        {"row-gap:1px;column-gap:2px", "row-gap:1px;column-gap:3px", 17},
        {"transition-duration:1s", "transition-duration:2s", 2},
        {"width:100px", "width:garbage;width:100px;font-style:italic;letter-spacing:3px", 0},
        {"order:0", "order:2", 7}, // H7 made order a real geometric sidecar.
        {"color:red;background-color:blue", "background-color:blue;color:red", 0},
        {"--w:100px;width:var(--w)", "width:100px", 0},
        {"opacity:.5", "opacity:initial", 12},
        {"width:100px", "width:unset", 7},
    };
    for (const auto& c : cases) {
        Fixture f(contextual); f.set(c.before); f.resolve(); f.noops(); f.set(c.after);
        exact(f.resolve(), c.expected, c.after); f.noops(); f.set(c.before);
        exact(f.resolve(), c.expected, c.before); f.noops();
    }
    {
        Fixture f(contextual); f.set("background-position:1px 2px,3px 4px;background-size:5px 6px,7px 8px"); f.resolve(); auto old = f.handle();
        f.set("background-position:1px 2px,30px 40px;background-size:5px 6px,70px 80px"); exact(f.resolve(), 0, "latent background lists");
        auto latent = f.handle(); require(latent->style.background_layers.count == 0, "latent lists have no image");
        equal(latent->measures->background_positions()[1].x.value, 30, "publish unused position");
        equal(latent->measures->background_sizes()[1].x.value, 70, "publish unused size");
        equal(old->measures->background_positions()[1].x.value, 3, "retain old position");
        equal(old->measures->background_sizes()[1].x.value, 7, "retain old size");
        f.set("background-position:1px 2px,30px 40px;background-size:5px 6px,70px 80px;background-image:url(a),url(b)");
        exact(f.resolve(), 2, "activate latent lists"); auto active = f.handle();
        equal(active->style.background_layers.items[1].position_x.value, 30, "active second position");
        equal(active->style.background_layers.items[1].size_x.value, 70, "active second size");
        for (auto h : {old, latent, active}) aether_computed_style_handle_destroy(h);
    }
    {
        Fixture f(contextual); f.sheet("#x:hover{opacity:.5;cursor:pointer}"); f.resolve();
        aether_element_set_state(f.x, AETHER_STATE_HOVER); exact(f.resolve(false, true), 108, "state-change path");
        aether_element_set_state(f.x, AETHER_STATE_NONE); exact(f.resolve(false, true), 108, "state-change reset"); f.noops();
    }
}
static void symbolic_colors(bool contextual) {
    Fixture f(contextual, "<html><body><div id='x'><div id='middle'><div id='child'></div></div></div></body></html>");
    f.sheet("#x,#middle{color:red}#child{color:blue}");
    const char* literal = "background-color:red;border-color:red;text-shadow:1px 2px red;box-shadow:1px 2px red";
    const char* symbolic = "background-color:currentColor;border-color:currentColor;text-shadow:1px 2px currentColor;box-shadow:1px 2px currentColor";
    f.set(literal); f.resolve(); auto old = f.handle();
    f.set(symbolic); exact(f.resolve(), 0, "same RGBA symbolic parent");
    exact(f.impacts[f.id("middle")], 0, "same RGBA symbolic intermediate");
    exact(f.impacts[f.id("child")], 66, "symbol propagates through unchanged intermediate");
    auto current = f.handle(); auto child = f.handle(f.id("child"));
    const auto& refs = current->record->color_references();
    require(refs && refs->background() && refs->border(0) && refs->text_shadow_at(0) && refs->box_shadow_at(0), "publish all symbolic references");
    const auto& old_refs = old->record->color_references();
    require(old_refs && !old_refs->background() && !old_refs->border(0) && !old_refs->text_shadow_at(0) && !old_refs->box_shadow_at(0), "retain literal references");
    equal(child->style.text_shadows.items[0].color.b, 1, "child uses own blue producer");
    equal(old->style.text_shadows.items[0].color.r, 1, "old literal remains red"); f.noops();
    f.set(literal); exact(f.resolve(), 0, "restore literal metadata");
    exact(f.impacts[f.id("child")], 66, "literal producer reaches child"); f.noops();
    for (auto h : {old, current, child}) aether_computed_style_handle_destroy(h);
}

// Exercise each scalar family independently and in mixed deltas. This is not a
// declaration-impact oracle: all values chosen here really alter final state.
static void scalar_families(bool contextual) {
    struct Case { const char* property; const char* before; const char* after; uint32_t expected; };
    const Case cases[] = {
        {"display", "block", "flex", 7}, {"position", "static", "relative", 7},
        {"top", "1px", "2px", 7}, {"right", "1px", "2px", 7}, {"bottom", "1px", "2px", 7}, {"left", "1px", "2px", 7},
        {"width", "10px", "20px", 7}, {"height", "10px", "20px", 7},
        {"min-width", "10px", "20px", 7}, {"min-height", "10px", "20px", 7},
        {"max-width", "10px", "20px", 7}, {"max-height", "10px", "20px", 7},
        {"margin-top", "1px", "2px", 7}, {"margin-right", "1px", "2px", 7},
        {"margin-bottom", "1px", "2px", 7}, {"margin-left", "1px", "2px", 7},
        {"padding-top", "1px", "2px", 7}, {"padding-right", "1px", "2px", 7},
        {"padding-bottom", "1px", "2px", 7}, {"padding-left", "1px", "2px", 7},
        {"border-top-width", "1px", "2px", 7}, {"border-right-width", "1px", "2px", 7},
        {"border-bottom-width", "1px", "2px", 7}, {"border-left-width", "1px", "2px", 7},
        {"overflow-x", "hidden", "scroll", 7}, {"overflow-y", "hidden", "scroll", 7},
        {"box-sizing", "content-box", "border-box", 1}, {"flex-direction", "row", "column", 1},
        {"justify-content", "flex-start", "center", 1}, {"align-items", "stretch", "center", 1},
        {"align-self", "auto", "center", 1}, {"flex-grow", "1", "2", 1}, {"flex-shrink", "1", "2", 1},
        {"flex-basis", "10px", "20px", 1}, {"flex-wrap", "nowrap", "wrap", 1},
        {"border-top-style", "solid", "dashed", 3}, {"border-right-style", "solid", "dashed", 3},
        {"border-bottom-style", "solid", "dashed", 3}, {"border-left-style", "solid", "dashed", 3},
        {"color", "red", "blue", 66}, {"background-color", "red", "blue", 2},
        {"border-top-color", "red", "blue", 2}, {"border-right-color", "red", "blue", 2},
        {"border-bottom-color", "red", "blue", 2}, {"border-left-color", "red", "blue", 2},
        {"border-top-left-radius", "1px 2px", "1px 3px", 6}, {"border-top-right-radius", "1px 2px", "1px 3px", 6},
        {"border-bottom-left-radius", "1px 2px", "1px 3px", 6}, {"border-bottom-right-radius", "1px 2px", "1px 3px", 6},
        {"font-size", "16px", "20px", 83}, {"font-family", "Arial", "monospace", 82},
        {"font-weight", "400", "700", 82}, {"text-align", "left", "right", 82},
        {"text-transform", "none", "uppercase", 82}, {"white-space", "normal", "nowrap", 81},
        {"line-height", "1", "2", 81}, {"pointer-events", "auto", "none", 68},
        {"visibility", "visible", "hidden", 70}, {"z-index", "1", "2", 6},
        {"transform-origin", "10% 20%", "30% 40%", 12},
        {"transition-property", "opacity", "width", 2}, {"transition-delay", "1s", "2s", 2},
        {"transition-timing-function", "linear", "ease", 2},
    };
    for (const auto& c : cases) {
        for (bool mixed : {false, true}) {
            Fixture f(contextual);
            const std::string prefix = std::string(c.property) + ":";
            const auto before = prefix + c.before + (mixed ? ";opacity:.3;cursor:pointer" : "");
            const auto after = prefix + c.after + (mixed ? ";opacity:.6;cursor:wait" : "");
            f.set(before.c_str()); f.resolve(); f.noops();
            f.set(after.c_str()); exact(f.resolve(), c.expected | (mixed ? 108 : 0), c.property); f.noops();
            f.set(before.c_str()); exact(f.resolve(), c.expected | (mixed ? 108 : 0), c.property); f.noops();
        }
    }
}
static aether_render_plan_t external_plan(uint32_t own, uint32_t external) {
    aether_render_plan_input_t input{};
    input.invalidation = aether_visual_invalidation_from_style_impact(own) | external;
    input.display_list_available = AETHER_TRUE; input.renderer_supports_incremental = AETHER_TRUE;
    aether_render_plan_t plan{};
    require(aether_render_plan_evaluate(&input, &plan) == AETHER_OK, "external render plan");
    require((plan.normalized_invalidation & external) == external, "style no-op preserves external invalidation");
    return plan;
}
static aether_text_metrics_t variable_measure(const char* text, const aether_computed_style_t*, float, void* data) {
    const float scale = *static_cast<float*>(data);
    return {float(std::strlen(text)) * scale, 16, 12};
}
static aether_bool_t variable_image(const char* source, aether_sizef* size, void* data) {
    require(source && std::strcmp(source, "same.png") == 0, "unchanged image URL");
    *size = {*static_cast<float*>(data), 10}; return AETHER_TRUE;
}
static float image_command_width(aether_display_list_t list) {
    for (int i = 0; i < aether_display_list_get_count(list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read image command");
        const bool image = command.tag == AETHER_PAINT_DRAW_BACKGROUND_IMAGE;
        const float width = image ? command.data.draw_background_image.destination_rect.width : 0;
        aether_paint_command_free(&command);
        if (image) return width;
    }
    require(false, "expected image command"); return 0;
}
static void external_consumers(bool contextual) {
    {
        Fixture f(contextual, "<html><body><div id='x'>word</div></body></html>");
        f.sheet("html,body{display:block;margin:0;padding:0}#x{display:inline-block}"); f.resolve();
        float scale = 5;
        aether_layout_engine_t engine{};
        require(aether_layout_create(variable_measure, &scale, &engine) == AETHER_OK, "metric engine");
        auto map = f.map(); aether_layout_result_t before{}, after{};
        require(aether_layout_layout(engine, f.doc, map, 300, 200, nullptr, nullptr, &before) == AETHER_OK, "initial metric layout");
        scale = 10; exact(f.resolve(), 0, "font callback change has no style delta");
        auto plan = external_plan(0, AETHER_VISUAL_INVALIDATION_LAYOUT);
        require(plan.requires_layout && !plan.allow_incremental_render, "font metrics force layout");
        aether_style_map_destroy(map); map = f.map();
        require(aether_layout_layout(engine, f.doc, map, 300, 200, nullptr, nullptr, &after) == AETHER_OK, "changed metric layout");
        equal(aether_layout_box_get_width(aether_layout_result_find_primary_box(before, f.x)), 20, "retained old font metrics");
        equal(aether_layout_box_get_width(aether_layout_result_find_primary_box(after, f.x)), 40, "new font metrics consumed");
        aether_layout_result_destroy(before); aether_layout_result_destroy(after); aether_style_map_destroy(map); aether_layout_destroy(engine);
    }
    {
        Fixture f(contextual, "<html><body><div id='x'></div></body></html>");
        f.sheet("html,body,div{display:block;margin:0;padding:0}"); f.resolve(); Geometry old(f);
        f.env.viewport_width = 500; exact(f.resolve(true), 0, "viewport input without CSS delta");
        auto plan = external_plan(0, AETHER_VISUAL_INVALIDATION_LAYOUT);
        require(plan.requires_layout, "host viewport still requests layout");
        auto map = f.map(); aether_layout_result_t resized{};
        require(aether_layout_layout(old.engine, f.doc, map, 500, 200, nullptr, nullptr, &resized) == AETHER_OK, "viewport relayout");
        equal(old.rect().width, 300, "retained old viewport width");
        equal(aether_layout_box_get_width(aether_layout_result_find_primary_box(resized, f.x)), 500, "host viewport used");
        aether_layout_result_destroy(resized); aether_style_map_destroy(map);
    }
    {
        Fixture f(contextual); f.sheet("html,body,div{display:block;margin:0;padding:0}#x{width:100px;height:40px;background-image:url(same.png);background-repeat:no-repeat}");
        f.resolve(); Geometry geometry(f); aether_display_list_builder_t builder{};
        require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "image builder");
        float size = 10; aether_display_list_builder_set_image_dimensions_callback(builder, variable_image, &size);
        aether_display_list_t before{}, after{};
        require(aether_display_list_build(builder, geometry.layout, nullptr, &before) == AETHER_OK, "initial image paint");
        size = 20; exact(f.resolve(), 0, "same URL with changed image resource");
        auto plan = external_plan(0, AETHER_VISUAL_INVALIDATION_DISPLAY_LIST);
        require(plan.requires_display_list && !plan.allow_incremental_render, "resource rebuild survives style no-op");
        require(aether_display_list_build(builder, geometry.layout, nullptr, &after) == AETHER_OK, "repaint image resource");
        equal(image_command_width(before), 10, "old image command immutable"); equal(image_command_width(after), 20, "new dimensions consumed");
        aether_display_list_destroy(before); aether_display_list_destroy(after); aether_display_list_builder_destroy(builder);
    }
    // Animation scheduling is a host input, independent of unchanged CSS.
    auto tick = external_plan(0, AETHER_VISUAL_INVALIDATION_PAINT_CHUNKS);
    require(tick.requires_paint_chunks && !tick.requires_layout, "host animation paint tick survives style no-op");
}

static void generated_lifecycle(bool contextual) {
    Fixture f(contextual); f.sheet("#x:hover::before{content:'retained';width:12px}"); f.resolve();
    require(f.x->children.size() == 0, "pseudo initially absent");
    aether_element_set_state(f.x, AETHER_STATE_HOVER); f.resolve();
    require(f.x->children.size() == 1, "pseudo created");
    auto pseudo = f.x->children.front(); exact(f.impacts[pseudo], 127, "new pseudo retains ALL lifecycle policy");
    auto old = f.handle(pseudo); equal(old->style.width.value, 12, "retained pseudo width");
    aether_element_set_state(f.x, AETHER_STATE_NONE); f.resolve();
    require(f.x->children.size() == 0, "pseudo removed");
    aether_style_revision_t revision{};
    require(aether_style_runtime_retain_revision(f.runtime, &revision) == AETHER_OK, "retain pseudo removal revision");
    bool removed = false;
    for (int i = 0; i < aether_style_revision_change_count(revision); ++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{}; uint32_t impact{};
        require(aether_style_revision_get_change(revision, i, &node, &handle, &impact) == AETHER_OK, "pseudo removal change");
        if (node == pseudo) { require(handle == nullptr, "retired pseudo has no replacement style"); exact(impact, 127, "retired pseudo retains ALL"); removed = true; }
        aether_computed_style_handle_destroy(handle);
    }
    require(removed, "retirement present in change list");
    equal(old->style.width.value, 12, "removed pseudo snapshot remains immutable");
    aether_style_revision_destroy(revision); aether_computed_style_handle_destroy(old); f.noops();
}

static void canceled_prepare_preserves_sidecars() {
    Fixture f(true); f.set("height:calc(0% + 100px);width:16px"); f.resolve();
    auto old = f.handle(); aether_style_revision_t before{};
    require(aether_style_runtime_retain_revision(f.runtime, &before) == AETHER_OK, "retain before cancellation");
    const auto generation = aether_style_revision_generation(before);
    f.set("height:calc(100px);width:1rem");
    aether_style_transaction_t canceled{};
    require(aether_style_runtime_prepare_revision(f.runtime, aether_doc_get_document_element(f.doc), &f.env, &canceled) == AETHER_OK, "prepare canceled semantic delta");
    aether_style_transaction_destroy(canceled);
    aether_style_revision_t published{};
    require(aether_style_runtime_retain_revision(f.runtime, &published) == AETHER_OK, "retain after cancellation");
    require(aether_style_revision_generation(published) == generation, "cancellation publishes no revision");
    require(frozen_measure_test::has_percentage(old->measures.get(), "height", old->style.height), "cancellation retains percentage metadata");
    require((old->measures->dependency("width") & MEASURE_ROOT_FONT) == 0, "cancellation retains absent rem dependency");
    exact(f.resolve(), 7, "retry canceled semantic delta"); auto current = f.handle();
    require(!frozen_measure_test::has_percentage(current->measures.get(), "height", current->style.height), "retry publishes percentage removal");
    require((current->measures->dependency("width") & MEASURE_ROOT_FONT) != 0, "retry publishes rem dependency");
    aether_computed_style_handle_destroy(old); aether_computed_style_handle_destroy(current);
    aether_style_revision_destroy(before); aether_style_revision_destroy(published);
}
int main() {
    for (bool contextual : {false, true}) {
        original_harness(contextual);
        percentage_height_snapshots(contextual);
        percentage_matrix(contextual);
        dependencies_and_inheritance(contextual);
        payloads_and_sidecars(contextual);
        symbolic_colors(contextual);
        scalar_families(contextual);
        external_consumers(contextual);
        generated_lifecycle(contextual);
    }
    canceled_prepare_preserves_sidecars();
    std::puts("PASS: final semantic diff acceptance (both APIs)");
}
