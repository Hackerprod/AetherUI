#include <aether/aether.h>
#include "stacking.h"
#include "immutable_style_test_support.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    const float width = text ? static_cast<float>(std::strlen(text)) : 0.f;
    return {width, 1.f, .8f};
}

/* 0 = red, 1 = green, 2 = blue, -1 = unexpected fill. */
static void collect_fill_colors(aether_display_list_t list, std::vector<int>& out) {
    const int32_t count = aether_display_list_get_count(list);
    for (int32_t index = 0; index < count; ++index) {
        aether_paint_command_t command{};
        if (aether_display_list_get_command(list, index, &command) != AETHER_OK) continue;
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const aether_color_rgba& color = command.data.fill_rounded_rect.color;
            if (color.r > 0.5f && color.g < 0.5f && color.b < 0.5f) out.push_back(0);
            else if (color.r < 0.5f && color.g > 0.5f && color.b < 0.5f) out.push_back(1);
            else if (color.r < 0.5f && color.g < 0.5f && color.b > 0.5f) out.push_back(2);
            else out.push_back(-1);
        }
        aether_paint_command_free(&command);
    }
}

static void build_display_list(const char* html, const char* css,
                               aether_doc_t* out_document,
                               std::vector<std::pair<std::string, aether_computed_style_t>>* out_styles,
                               aether_display_list_t* out_list) {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t layout_engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;

    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document, html) == AETHER_OK, "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "z-index-test.css",
                                                &stylesheet, &rule_count) == AETHER_OK,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    require(update_count > 0, "style resolution failed");
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(updates[index], &updated, &candidate, nullptr) == AETHER_OK,
            "style update read failed");
        if (candidate) {
            char* id = aether_element_get_attribute(updated, "id");
            if (id && (std::strcmp(id, "a") == 0 || std::strcmp(id, "b") == 0 || std::strcmp(id, "c") == 0)) {
                aether_computed_style_t style{};
                if (aether_computed_style_copy(candidate, &style) == AETHER_OK)
                    out_styles->emplace_back(id, style);
            }
            aether_string_free(id);
            aether_computed_style_handle_destroy(candidate);
        }
    }
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
        "resolved style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &layout_engine) == AETHER_OK,
        "layout engine creation failed");
    require(aether_layout_layout(layout_engine, document, styles, 200.f, 100.f,
                                 nullptr, nullptr, &layout) == AETHER_OK,
        "layout execution failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &builder) == AETHER_OK,
        "display-list builder creation failed");
    require(aether_display_list_build(builder, layout, nullptr, out_list) == AETHER_OK,
        "display-list build failed");

    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(layout_engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    *out_document = document;
}

static const aether_computed_style_t* find_style(
    const std::vector<std::pair<std::string, aether_computed_style_t>>& styles, const char* id) {
    for (const auto& entry : styles) {
        if (entry.first == id) return &entry.second;
    }
    return nullptr;
}

static void module_unit_checks() {
    using aether::stacking::classify_element_paint;
    using aether::stacking::creates_stacking_context;
    using aether::stacking::needs_stacking_order;
    using aether::stacking::paint_class_t;
    using aether::stacking::stable_sort_stack_items;
    using aether::stacking::stack_item_t;

    aether_computed_style_t s = aether_computed_style_default();
    aether::stacking::paint_classification_t classification = classify_element_paint(s);
    require(classification.cls == paint_class_t::in_flow && !classification.has_explicit_z_index,
        "static + auto z-index classifies as in-flow");
    s.z_index = 5;
    s.z_index_kind = AETHER_Z_INDEX_INTEGER;
    require(classify_element_paint(s).cls == paint_class_t::in_flow,
        "static ignores an integer z-index (CSS: z-index applies to positioned only)");

    s.position = AETHER_POSITION_RELATIVE;
    s.z_index_kind = AETHER_Z_INDEX_AUTO;
    require(classify_element_paint(s).cls == paint_class_t::auto_,
        "positioned + auto z-index classifies as auto");
    s.z_index = 5;
    s.z_index_kind = AETHER_Z_INDEX_INTEGER;
    classification = classify_element_paint(s);
    require(classification.cls == paint_class_t::positive && classification.z_value == 5 &&
                classification.has_explicit_z_index,
        "positioned + integer > 0 classifies as positive");
    s.position = AETHER_POSITION_ABSOLUTE;
    s.z_index = -1;
    classification = classify_element_paint(s);
    require(classification.cls == paint_class_t::negative && classification.z_value == -1,
        "positioned + integer < 0 classifies as negative");
    s.z_index = 0;
    classification = classify_element_paint(s);
    require(classification.cls == paint_class_t::auto_ && classification.has_explicit_z_index,
        "positioned + integer 0 classifies as auto with explicit z-index");

    require(!creates_stacking_context(aether_computed_style_default()),
        "default style does not create a stacking context");
    s = aether_computed_style_default();
    s.opacity = 0.5f;
    require(creates_stacking_context(s), "opacity < 1 creates a stacking context");
    s = aether_computed_style_default();
    s.transforms.count = 1;
    require(creates_stacking_context(s), "a transform creates a stacking context");
    s = aether_computed_style_default();
    s.backdrop_filters.count = 1;
    require(creates_stacking_context(s), "a backdrop filter creates a stacking context");
    s = aether_computed_style_default();
    s.position = AETHER_POSITION_FIXED;
    require(creates_stacking_context(s), "position:fixed creates a stacking context");
    s = aether_computed_style_default();
    s.position = AETHER_POSITION_ABSOLUTE;
    s.z_index_kind = AETHER_Z_INDEX_INTEGER;
    require(creates_stacking_context(s), "positioned + integer z-index creates a stacking context");
    s = aether_computed_style_default();
    s.position = AETHER_POSITION_ABSOLUTE;
    require(!creates_stacking_context(s), "positioned + auto z-index does not create a stacking context");
    s = aether_computed_style_default();
    s.z_index_kind = AETHER_Z_INDEX_INTEGER;
    require(!creates_stacking_context(s), "static + integer z-index does not create a stacking context");

    stack_item_t i1; i1.dom_index = 0; i1.is_inline_content = true; i1.cls = paint_class_t::in_flow;
    stack_item_t i2; i2.dom_index = 1; i2.cls = paint_class_t::positive; i2.z_value = 10;
    stack_item_t i3; i3.dom_index = 2; i3.cls = paint_class_t::negative; i3.z_value = -3;
    stack_item_t i4; i4.dom_index = 3; i4.cls = paint_class_t::auto_;
    stack_item_t i5; i5.dom_index = 4; i5.cls = paint_class_t::negative; i5.z_value = -1;
    stack_item_t i6; i6.dom_index = 5; i6.cls = paint_class_t::positive; i6.z_value = 2;
    std::vector<stack_item_t> items = {i1, i2, i3, i4, i5, i6};
    require(!needs_stacking_order(items), "no explicit z-index keeps DOM order");
    items[1].has_explicit_z_index = true;
    require(needs_stacking_order(items), "an explicit z-index requires stacking order");
    stable_sort_stack_items(items);
    const int expected[] = {2, 4, 0, 3, 5, 1};
    for (size_t index = 0; index < items.size(); ++index)
        require(items[index].dom_index == expected[index],
            "stable sort emits negative, in-flow, auto, then positive with ties by DOM index");
}

// Retained-layout regressions use the public hit APIs and real resolved updates.
// Internal node metadata is read only to identify generated/anonymous boxes.
#include "../src/layout/layout_types.h"

namespace hit_stacking_tests {
static void ok(aether_status_t status) { require(status == AETHER_OK, "hit fixture ABI call"); }
struct Fixture {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    std::vector<aether_style_map_t> retired;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    std::vector<std::pair<aether_node_t, int>> z_values;
    Fixture(const char* html, const std::string& css) {
        ok(aether_doc_create(&document)); ok(aether_doc_parse_html(document, html));
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{200, 100, 1, 16, 0, 0, 0};
        ok(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime));
        int64_t handle = 0;
        const std::string sheet = "html,body{display:block;margin:0}body{width:200px;height:100px}div{display:block}" + css;
        ok(aether_style_runtime_add_stylesheet(runtime, sheet.c_str(), 2, "hit-stacking.css", &handle, nullptr));
        resolve(); fresh_map();
        ok(aether_layout_create(measure_text, nullptr, &engine));
        ok(aether_layout_layout(engine, document, styles, 200, 100, nullptr, nullptr, &layout));
    }
    ~Fixture() {
        aether_layout_result_destroy(layout); aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        for (auto old : retired) aether_style_map_destroy(old);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(document);
    }
    aether_node_t node(const char* id) { return aether_doc_get_element_by_id(document, id); }
    void fresh_map() {
        if (styles) retired.push_back(styles);
        styles = nullptr;
        ok(aether_style_runtime_create_resolved_style_map(runtime, document, &styles));
    }
    void resolve(aether_node_t changed = nullptr) {
        aether_style_update_t* updates = nullptr;
        auto root = aether_doc_get_document_element(document);
        const int count = changed ? aether_style_runtime_resolve_state_changes(runtime, root, &changed, 1, &updates)
                                  : aether_style_runtime_resolve_subtree(runtime, root, &updates);
        require(count >= 0, "real style resolution succeeds");
        std::vector<aether_node_t> nodes;
        for (int i = 0; i < count; ++i) {
            aether_node_t n = nullptr; aether_computed_style_handle_t handle = nullptr;
            ok(aether_style_runtime_get_update(updates[i], &n, &handle, nullptr));
            aether_computed_style_t value{}; ok(aether_computed_style_copy(handle, &value));
            z_values.emplace_back(n, value.z_index);
            aether_computed_style_free(&value); aether_computed_style_handle_destroy(handle);
            nodes.push_back(n);
        }
        if (styles) ok(aether_style_runtime_update_resolved_style_map(runtime, styles, nodes.data(), int(nodes.size())));
        aether_style_updates_free(updates, count);
    }
    std::vector<int> paint(aether_style_map_t current) {
        aether_display_list_builder_t builder = nullptr; aether_display_list_t list = nullptr;
        ok(aether_display_list_builder_create(measure_text, nullptr, &builder));
        ok(aether_display_list_build_with_styles(builder, layout, current, nullptr, &list));
        std::vector<int> fills; collect_fill_colors(list, fills);
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder); return fills;
    }
    aether_node_t hit(int api, float x = 10, float y = 10, aether_scroll_offset_fn offset = nullptr, void* data = nullptr) {
        if (api == 0) return aether_layout_result_hit_test(layout, x, y);
        if (api == 1) return aether_layout_result_hit_test_with_scroll_offsets(layout, x, y, offset, data);
        return aether_layout_result_hit_test_with_styles_and_scroll_offsets(layout, styles, x, y, offset, data);
    }
    void expect(const char* label, const char* target, std::vector<int> fills, float x = 10, float y = 10) {
        const auto actual = paint(styles);
        if (actual != fills) {
            std::fprintf(stderr, "Case %s actual fills:", label);
            for (int fill : actual) std::fprintf(stderr, " %d", fill);
            std::fprintf(stderr, ": "); require(false, "explicit paint command order");
        }
        for (int api = 0; api < 3; ++api) {
            if (hit(api, x, y) != node(target)) { std::fprintf(stderr, "Case %s API %d: ", label, api); require(false, "explicit native hit identity"); }
        }
    }
};
static const char* siblings = "<body><div id='a'></div><div id='b'></div></body>";
static const char* common = "div{position:absolute;left:0;top:0;width:50px;height:50px}#a{background:#ff0000}#b{background:#00ff00}#c{background:#0000ff}";
static aether_scroll_offset_t offset(aether_node_t n, void* data) {
    return n == data ? aether_scroll_offset_t{0, 20} : aether_scroll_offset_t{0, 0};
}
static aether_scroll_offset_t excessive_offset(aether_node_t n, void* data) {
    return n == data ? aether_scroll_offset_t{-500, 5000} : aether_scroll_offset_t{0, 0};
}
static void input_hit_checks();
static void run() {
    input_hit_checks();
    struct Case { const char* label; const char* css; const char* target; std::vector<int> fills; };
    const Case cases[] = {
        {"positive", "#a{z-index:9}#b{z-index:1}", "a", {1,0}},
        {"negative", "#a{z-index:-1}#b{z-index:-9}", "a", {1,0}},
        {"positive-negative", "#a{z-index:1}#b{z-index:-1}", "a", {1,0}},
        {"zero-negative", "#a{z-index:0}#b{z-index:-1}", "a", {1,0}},
        {"ties-positive", "#a,#b{z-index:3}", "b", {0,1}},
        {"ties-negative", "#a,#b{z-index:-3}", "b", {0,1}},
        {"ties-zero", "#a,#b{z-index:0}", "b", {0,1}},
        {"auto", "", "b", {0,1}},
        {"static-integer-ignored", "#a{position:static;z-index:99}#b{z-index:1}", "b", {0,1}},
        {"static-does-not-gate", "#a{z-index:auto}#b{position:static;z-index:99}", "a", {1,0}},
        {"explicit-zero-gates", "#a{z-index:0}#b{position:static}", "a", {1,0}},
        {"hidden-top", "#a{z-index:1}#b{z-index:9;visibility:hidden}", "a", {0}},
        {"pointer-none-top", "#a{z-index:1}#b{z-index:9;pointer-events:none}", "a", {0,1}},
        {"display-none-top", "#a{z-index:1}#b{z-index:9;display:none}", "a", {0}},
        {"hidden-gates-order", "#a{z-index:auto}#b{position:static}body::after{content:'';position:absolute;z-index:1;visibility:hidden}", "a", {1,0}},
        {"opacity-zero-hittable", "#a{z-index:9;opacity:0}#b{z-index:1}", "a", {1}},
    };
    for (const auto& c : cases) { Fixture f(siblings, std::string(common) + c.css); f.expect(c.label, c.target, c.fills); }
    {
        Fixture f("<body><div id='a'></div><div id='b'></div><div id='c'></div></body>",
            std::string(common) + "#a{z-index:0}#b{position:static}#c{z-index:-1}");
        f.expect("mixed-classes", "a", {2,1,0});
    }
    for (const char* parent_z : {"1", "auto"}) {
        Fixture f("<body><div id='parent'><div id='a'></div></div><div id='b'></div></body>",
            std::string(common) + "#parent{z-index:" + parent_z + "}#a{z-index:999}#b{z-index:2}");
        f.expect("child-cannot-escape-local-parent", "b", {0,1});
    }
    for (const char* visibility : {"visibility:hidden", "pointer-events:none"}) {
        Fixture f("<body><div id='parent'><div id='a'></div></div><div id='b'></div></body>",
            std::string(common) + "#parent{z-index:9;" + visibility + "}#a{visibility:visible;pointer-events:auto}#b{z-index:1}");
        f.expect("transparent-parent-eligible-child", "a", {1,0});
    }
    {
        Fixture f("<body><div id='a'></div><div id='parent'><div id='b'></div></div></body>",
            std::string(common) + "#a{z-index:1}#parent{z-index:9;width:20px;height:20px;overflow:hidden}#b{left:30px}");
        f.expect("clip-rejects-top-descendant", "a", {0,1}, 35, 10);
    }
    {
        Fixture f("<body><div id='parent'><div id='a'></div><div id='b'></div><div id='spacer'></div></div></body>",
            std::string(common) + "#parent{width:50px;height:20px;overflow:auto}#spacer{position:static;height:70px}#a,#b{top:20px}#a{z-index:9}#b{z-index:1}");
        require(f.paint(f.styles) == std::vector<int>({1,0}), "scroll paint order");
        require(f.hit(0, 10, 5) == f.node("spacer"), "snapshot coordinates do not invent scroll offset");
        for (int api : {1,2}) {
            require(f.hit(api, 10, 5, offset, f.node("parent")) == f.node("a"), "scroll descendant uses shared stacking order");
            require(f.hit(api, 10, 5, excessive_offset, f.node("parent")) == f.node("a"), "offset clamps to retained range and nonnegative x");
            require(f.hit(api, 10, 25, offset, f.node("parent")) != f.node("a"), "viewport padding clip precedes scroll offset");
        }
    }
    for (bool initially_high : {false,true}) {
        Fixture f(siblings, std::string(common) + (initially_high
            ? "#a{z-index:9}#b{z-index:2}#a:hover{z-index:1}"
            : "#a{z-index:1}#b{z-index:2}#a:hover{z-index:9}"));
        for (int step = 0; step < 4; ++step) {
            const bool hovered = step % 2 == 0, high = hovered != initially_high;
            aether_element_set_state(f.node("a"), hovered ? AETHER_STATE_HOVER : AETHER_STATE_NONE);
            f.resolve(f.node("a")); if (step % 2) f.fresh_map();
            require(f.paint(f.styles) == (high ? std::vector<int>({1,0}) : std::vector<int>({0,1})), "live paint reverses and returns without layout");
            require(f.hit(2) == f.node(high ? "a" : "b"), "current-map hit follows live z in both directions");
            for (int api : {0,1}) require(f.hit(api) == f.node(initially_high ? "a" : "b"), "snapshot APIs retain captured z");
            require(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout, nullptr, 10, 10, nullptr, nullptr) == f.node(initially_high ? "a" : "b"), "explicit null map retains snapshot");
        }
        // First put the current value opposite the snapshot, making fallback
        // observable instead of testing two already equal styles.
        aether_element_set_state(f.node("a"), AETHER_STATE_HOVER);
        f.resolve(f.node("a"));
        require(f.hit(2) == f.node(initially_high ? "b" : "a"), "current value differs before missing-entry fallback");
        // A current map lacking an entry falls back to the captured box style.
        require(f.styles->current->backing()->find(f.node("a")) != nullptr,
            "entry exists before missing-entry probe");
        immutable_style_test::erase(f.styles, f.node("a"));
        require(f.hit(2) == f.node(initially_high ? "a" : "b"), "missing current entry uses layout snapshot");
    }
    {
        Fixture f("<body><div id='parent'><div id='b'></div></div></body>", std::string(common) +
            "#parent::before{content:'';position:absolute;left:0;top:0;width:50px;height:50px;background:#ff0000;z-index:9}#b{z-index:1}");
        aether_node_t pseudo = nullptr;
        for (auto entry : f.z_values) if (entry.first->is_generated && entry.second == 9) pseudo = entry.first;
        require(pseudo && pseudo != f.node("parent"), "real pseudo update owns its z style and identity");
        require(f.paint(f.styles) == std::vector<int>({1,0}), "pseudo own z paints above real sibling");
        for (int api = 0; api < 3; ++api) require(f.hit(api) == pseudo, "hit preserves pseudo identity");
    }
    {
        Fixture f("<body><div id='parent'>ANON<div id='b'></div></div></body>",
            std::string(common) + "#parent{display:flex;z-index:99}#b{z-index:-1}");
        aether_layout_box_t anonymous = nullptr;
        for (int i = 0; i < aether_layout_result_get_paint_order_count(f.layout); ++i) {
            auto box = aether_layout_result_get_paint_order_box(f.layout, i);
            if (box->element && box->element->anonymous_flex_owner) anonymous = box;
        }
        require(anonymous != nullptr, "fixture contains real anonymous flex box");
        const aether::layout::StyleLookup current(f.layout->assets, f.styles->current, true);
        const auto& classification = aether::stacking::classify_element_paint(
            layout_box_style(anonymous, current, aether::layout::BoxStylePolicy::effective_map).scalars());
        require(!classification.has_explicit_z_index, "anonymous does not borrow owner's z-index");
        for (int api = 0; api < 3; ++api) require(f.hit(api, 0.5f, 0.5f) == f.node("parent"), "anonymous text hits DOM owner above negative sibling");
    }
    {
        Fixture f("<body><div id='a'></div><div id='b'></div><div id='c'></div></body>", common);
        auto* a = aether_layout_result_find_primary_box(f.layout, f.node("a"));
        auto* b = aether_layout_result_find_primary_box(f.layout, f.node("b"));
        auto* c = aether_layout_result_find_primary_box(f.layout, f.node("c"));
        auto* parent = aether_layout_result_get_root(f.layout);
        require(parent->children.size() == 3, "helper fixture has three direct real children");
        // Text and atomic flags are layout-internal classifications. Use real
        // boxes and styles to probe the shared helper without a synthetic ABI.
        const auto a_kind = a->kind;
        a->kind = LAYOUT_BOX_TEXT; b->is_atomic_control = true;
        for (auto* box : {a,b,c}) {
            immutable_style_test::edit_scalars(f.styles, box->element, [](auto& style) {
                style.z_index_kind = AETHER_Z_INDEX_INTEGER; style.z_index = 99;
            });
        }
        immutable_style_test::edit_scalars(f.styles, c->element, [](auto& style) {
            style.position = AETHER_POSITION_STATIC;
        });
        const aether::layout::StyleLookup before_order(f.layout->assets, f.styles->current, true);
        require(layout_children_paint_order(parent, before_order, aether::layout::BoxStylePolicy::effective_map).empty(), "text/atomic explicit z and static integer do not activate ordering");
        immutable_style_test::edit_scalars(f.styles, c->element, [](auto& style) {
            style.position = AETHER_POSITION_ABSOLUTE; style.z_index = -1;
        });
        const aether::layout::StyleLookup after_order(f.layout->assets, f.styles->current, true);
        const auto ordered = layout_children_paint_order(parent, after_order, aether::layout::BoxStylePolicy::effective_map);
        require(ordered.size() == 3 && ordered[0].dom_index == 2 && ordered[1].dom_index == 0 && ordered[2].dom_index == 1,
            "classified gate orders negative before text and atomic in-flow");
        for (size_t i : {size_t(1),size_t(2)}) require(ordered[i].is_inline_content && ordered[i].cls == aether::stacking::paint_class_t::in_flow && !ordered[i].has_explicit_z_index,
            "shared classification keeps text and atomic in-flow even when another child gates sorting");
        a->kind = a_kind; b->is_atomic_control = false;
    }
    std::puts("PASS: shared local paint/hit order, three APIs, retained styles, clipping, scroll, pseudo and anonymous ownership");
}
static void input_hit_checks() {
    require((aether_abi_capabilities() & AETHER_CAPABILITY_INPUT_HIT_TEST) != 0, "input capability advertised");
    const char* html = "<body><div id='a'><div class='large'></div></div><div id='b'></div></body>";
    const std::string css = "#a,#b{position:absolute;left:10px;top:10px;width:80px;height:70px}#a{overflow:scroll} .large{width:300px;height:300px}";
    const auto query = [](Fixture& f, float x, float y, int part, aether_node_t owner,
        aether_style_map_t styles = nullptr, aether_scroll_offset_fn callback = nullptr, void* data = nullptr) {
        aether_input_hit_part_t actual = -1; aether_node_t found = reinterpret_cast<aether_node_t>(1);
        ok(aether_layout_result_hit_test_input(f.layout, styles, x, y, callback, data, &actual, &found));
        if (actual != part || found != owner) std::fprintf(stderr,"input (%g,%g) expected part=%d actual=%d expectedOwner=%p actualOwner=%p\n",x,y,part,actual,owner,found);
        require(actual == part && found == owner, "public input part and owner identity");
    };
    {
        Fixture f(html, css + "#b{display:none}");
        auto* box = aether_layout_result_find_primary_box(f.layout, f.node("a"));
        const auto g = aether_layout_box_get_scrollbar_geometry(box);
        query(f, g.horizontal_thumb.x+1,g.horizontal_thumb.y+1,2,f.node("a"));
        query(f, g.vertical_thumb.x+1,g.vertical_thumb.y+1,3,f.node("a"));
        query(f, g.horizontal_track.x+g.horizontal_track.width-1,g.horizontal_track.y+1,4,f.node("a"));
        query(f, g.vertical_track.x+1,g.vertical_track.y+g.vertical_track.height-1,5,f.node("a"));
        query(f, g.corner.x+1,g.corner.y+1,6,f.node("a"));
        query(f, -100,-100,0,nullptr);
        query(f, 12,12,1,layout_box_hit_owner(box->children.front()));
        aether_input_hit_part_t part=99; aether_node_t owner=f.node("a");
        require(aether_layout_result_hit_test_input(nullptr,nullptr,0,0,nullptr,nullptr,&part,&owner)==AETHER_ERROR_NULL_ARG && part==0 && !owner,"null result initializes outputs");
        owner=f.node("a"); require(aether_layout_result_hit_test_input(f.layout,nullptr,0,0,nullptr,nullptr,nullptr,&owner)==AETHER_ERROR_NULL_ARG && !owner,"null part initializes owner");
        part=99; require(aether_layout_result_hit_test_input(f.layout,nullptr,0,0,nullptr,nullptr,&part,nullptr)==AETHER_ERROR_NULL_ARG && part==0,"null owner initializes part");
        require(aether_layout_result_hit_test_input(f.layout,nullptr,0,0,nullptr,nullptr,nullptr,nullptr)==AETHER_ERROR_NULL_ARG,"both outputs required");
        const auto old0=f.hit(0,g.vertical_thumb.x+1,g.vertical_thumb.y+1);
        const auto old1=f.hit(1,g.vertical_thumb.x+1,g.vertical_thumb.y+1);
        const auto old2=f.hit(2,g.vertical_thumb.x+1,g.vertical_thumb.y+1);
        // Re-layout with real snapshot offsets; no callback uses those offsets only for the new API.
        aether_layout_result_destroy(f.layout); f.layout=nullptr;
        ok(aether_layout_layout(f.engine,f.document,f.styles,200,100,offset,f.node("a"),&f.layout));
        box=aether_layout_result_find_primary_box(f.layout,f.node("a"));
        const auto live=aether_layout_box_get_scrollbar_geometry(box);
        query(f,live.vertical_thumb.x+1,live.vertical_thumb.y+1,3,f.node("a"));
        query(f,g.vertical_thumb.x+1,g.vertical_thumb.y+1,3,f.node("a"),nullptr,[](aether_node_t,void*){return aether_scroll_offset_t{0,0};});
        query(f,g.vertical_track.x+1,g.vertical_track.y+g.vertical_track.height-1,5,f.node("a"));
        require(f.hit(0,g.vertical_thumb.x+1,g.vertical_thumb.y+1)==old0 && f.hit(1,g.vertical_thumb.x+1,g.vertical_thumb.y+1)==old1 && f.hit(2,g.vertical_thumb.x+1,g.vertical_thumb.y+1)==old2,"legacy DOM queries unchanged by input query snapshot default");
    }
    for (const auto& rules : {"#a{z-index:1}#b{z-index:9}","#a{z-index:-9}#b{z-index:-1}","#a,#b{z-index:0}","#a,#b{z-index:auto}"}) {
        Fixture f(html,css+rules); query(f,85,12,1,f.node("b"));
    }
    for (const auto& rules : {"#a{z-index:9}#b{z-index:1}","#a{z-index:-1}#b{z-index:-9}","#b{visibility:hidden}","#b{pointer-events:none}"}) {
        Fixture f(html,css+rules); query(f,85,12,3,f.node("a"));
    }
    for (const auto& prop : {"z-index:20", "visibility:hidden", "pointer-events:none", "display:none"}) {
        Fixture f(html,css+"#a{z-index:9}#b{z-index:1}#b:hover{"+prop+"}");
        aether_element_set_state(f.node("b"),AETHER_STATE_HOVER);f.resolve(f.node("b"));
        const bool content=std::string(prop)=="z-index:20";
        query(f,85,12,content?1:3,f.node(content?"b":"a"),f.styles);
        query(f,85,12,3,f.node("a")); // snapshot fallback; no display list was built
    }
    for (const auto& prop : {"visibility:hidden", "pointer-events:none"}) {
        Fixture f("<body><div id='b'><div id='a'><div class='large'></div></div></div></body>",css+"#b{"+prop+";z-index:9}#a{left:0;top:0;visibility:visible;pointer-events:auto}");
        auto g=aether_layout_box_get_scrollbar_geometry(aether_layout_result_find_primary_box(f.layout,f.node("a")));
        query(f,g.vertical_thumb.x+1,g.vertical_thumb.y+1,3,f.node("a"),f.styles);
    }
    {
        Fixture f("<body><div id='a'><div id='b'><div class='large'></div></div><div class='large'></div></div></body>",css+"#b{left:0;top:0;height:80px;overflow:scroll}");
        auto* box=aether_layout_result_find_primary_box(f.layout,f.node("a"));auto g=aether_layout_box_get_scrollbar_geometry(box);
        for(auto entry : {std::pair<aether_rectf,int>{g.horizontal_thumb,2},{g.vertical_thumb,3},{g.horizontal_track,4},{g.vertical_track,5},{g.corner,6}}) {
            auto rect=entry.first; query(f,rect.x+(entry.second==4?rect.width-1:1),rect.y+(entry.second==5?rect.height-1:1),entry.second,f.node("a"));
        }
    }
    {
        Fixture f("<body><div id='parent'><div id='a'><div class='large'></div></div></div></body>",css+"#parent{width:30px;height:30px;overflow:hidden}");
        query(f,85,12,1,aether_doc_get_body(f.document)); // ancestor clip rejects invisible chrome
    }
    {
        Fixture f("<body><div id='parent'><div id='a'><div class='large'></div></div><div class='large'></div></div></body>",css+"#parent{width:180px;height:90px;overflow:hidden}");
        query(f,85,12,3,f.node("a"));
        query(f,85,25,5,f.node("a"),nullptr,offset,f.node("parent"));
        query(f,85,-8,0,nullptr,nullptr,offset,f.node("parent"));
    }
    {
        Fixture f("<body><div id='a'><div class='large'></div></div></body>",css+"#a{pointer-events:none}#a::after{content:'';position:absolute;left:0;top:0;width:80px;height:70px;overflow:scroll;z-index:9;pointer-events:auto}");
        aether_node_t pseudo=nullptr;for(auto entry:f.z_values)if(entry.first->is_generated)pseudo=entry.first;
        require(pseudo,"generated fixture exists");
        auto* pb=aether_layout_result_find_primary_box(f.layout,pseudo);require(pb,"pseudo box exists");
        auto g=aether_layout_box_get_scrollbar_geometry(pb);
        query(f,g.vertical_thumb.x+1,g.vertical_thumb.y+1,3,pseudo);
    }
    {
        Fixture f("<body><div id='parent'>ANON<div id='b'></div></div></body>",std::string(common)+"#parent{display:flex;z-index:99}#b{z-index:-1}");
        query(f,.5f,.5f,1,f.node("parent"));
    }
    std::puts("PASS: public input hit parts, output validation, local order, live styles, offsets, clip and ownership");
}
} // namespace hit_stacking_tests


int main() {
    hit_stacking_tests::run();
    module_unit_checks();

    // ── z-index reorders paint over DOM order ──
    {
        const char* html =
            "<body><div id=\"a\"></div><div id=\"b\"></div><div id=\"c\"></div></body>";
        const char* css =
            "body { margin: 0; }"
            "div { position: absolute; width: 50px; height: 50px; }"
            "#a { z-index: 1; top: 0; left: 0; background-color: #ff0000; }"
            "#b { z-index: -1; top: 0; left: 0; background-color: #00ff00; }"
            "#c { z-index: 2; top: 0; left: 0; background-color: #0000ff; }";
        aether_doc_t document = nullptr;
        std::vector<std::pair<std::string, aether_computed_style_t>> element_styles;
        aether_display_list_t display_list = nullptr;
        build_display_list(html, css, &document, &element_styles, &display_list);

        const aether_computed_style_t* a_style = find_style(element_styles, "a");
        const aether_computed_style_t* b_style = find_style(element_styles, "b");
        const aether_computed_style_t* c_style = find_style(element_styles, "c");
        require(a_style && a_style->z_index_kind == AETHER_Z_INDEX_INTEGER && a_style->z_index == 1,
            "the z-index applier stores #a as integer 1");
        require(b_style && b_style->z_index_kind == AETHER_Z_INDEX_INTEGER && b_style->z_index == -1,
            "the z-index applier stores #b as integer -1");
        require(c_style && c_style->z_index_kind == AETHER_Z_INDEX_INTEGER && c_style->z_index == 2,
            "the z-index applier stores #c as integer 2");
        for (auto& entry : element_styles) aether_computed_style_free(&entry.second);

        std::vector<int> fills;
        collect_fill_colors(display_list, fills);
        const int expected[] = {1, 0, 2};  // b(negative) -> a(z=1) -> c(z=2)
        require(fills.size() == 3 && fills[0] == expected[0] && fills[1] == expected[1] &&
                    fills[2] == expected[2],
            "z-index must paint negative first, then ascending positive (b, a, c)");

        aether_display_list_destroy(display_list);
        aether_doc_destroy(document);
    }

    // ── control: no z-index keeps exact DOM order ──
    {
        const char* html =
            "<body><div id=\"a\"></div><div id=\"b\"></div><div id=\"c\"></div></body>";
        const char* css =
            "body { margin: 0; }"
            "div { position: absolute; width: 50px; height: 50px; }"
            "#a { top: 0; left: 0; background-color: #ff0000; }"
            "#b { top: 0; left: 0; background-color: #00ff00; }"
            "#c { top: 0; left: 0; background-color: #0000ff; }";
        aether_doc_t document = nullptr;
        std::vector<std::pair<std::string, aether_computed_style_t>> element_styles;
        aether_display_list_t display_list = nullptr;
        build_display_list(html, css, &document, &element_styles, &display_list);
        for (auto& entry : element_styles) aether_computed_style_free(&entry.second);

        std::vector<int> fills;
        collect_fill_colors(display_list, fills);
        const int expected[] = {0, 1, 2};  // DOM order a, b, c
        require(fills.size() == 3 && fills[0] == expected[0] && fills[1] == expected[1] &&
                    fills[2] == expected[2],
            "no z-index must keep exact DOM paint order (a, b, c)");

        aether_display_list_destroy(display_list);
        aether_doc_destroy(document);
    }

    std::printf("PASS: z-index stacking reorders paint; no-z-index keeps DOM order\n");
    return 0;
}
