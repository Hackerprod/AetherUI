#include <aether/aether.h>
#include "stacking.h"

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

int main() {
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
