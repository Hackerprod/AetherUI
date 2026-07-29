#include <aether/aether.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool same_color(aether_color_rgba actual, aether_color_rgba expected) {
    constexpr float epsilon = 0.0001f;
    return std::fabs(actual.r - expected.r) < epsilon &&
           std::fabs(actual.g - expected.g) < epsilon &&
           std::fabs(actual.b - expected.b) < epsilon &&
           std::fabs(actual.a - expected.a) < epsilon;
}

struct diagnostic_capture_s {
    int count = 0;
    const char* code = nullptr;
    const char* property = nullptr;
};

static void capture_diagnostic(const char* code, const char* property, const char*, const char*, void* user_data) {
    auto* capture = static_cast<diagnostic_capture_s*>(user_data);
    ++capture->count;
    capture->code = code;
    capture->property = property;
}

static aether_computed_style_t find_style(aether_style_runtime_t runtime,
                                          aether_node_t root, aether_node_t target) {
    aether_style_update_t* updates = nullptr;
    const int32_t count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
    require(count > 0 && updates, "style resolution returned no updates");

    aether_computed_style_t result{};
    bool found = false;
    for (int32_t index = 0; index < count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
                "style update access failed");
        if (node == target) {
            require(aether_computed_style_copy(handle, &result) == AETHER_OK,
                    "computed-style copy failed");
            found = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, count);
    require(found, "target did not receive a computed style");
    return result;
}

int main() {
    // ABI list contract: deep copies never alias the source, equality compares
    // list contents, and free resets ownership to the canonical empty state.
    aether_shadow_list source{};
    source.items = static_cast<aether_shadow*>(std::malloc(sizeof(aether_shadow) * 2));
    require(source.items != nullptr, "shadow-list source allocation failed");
    source.count = 2;
    source.items[0] = {1, 2, 3, 4, {1, 0, 0, 1}, AETHER_FALSE};
    source.items[1] = {5, 6, 7, 8, {0, 1, 0, 1}, AETHER_TRUE};
    aether_shadow_list copy{};
    require(aether_shadow_list_copy(&source, &copy) == AETHER_TRUE,
            "shadow-list deep copy failed");
    require(copy.items != source.items && aether_shadow_list_equal(&source, &copy) == AETHER_TRUE,
            "shadow-list copy must own equivalent independent items");
    copy.items[0].offset_x = 99;
    require(aether_shadow_list_equal(&source, &copy) == AETHER_FALSE,
            "shadow-list equality must compare every item");
    aether_shadow_list_free(&copy);
    require(copy.items == nullptr && copy.count == 0,
            "shadow-list free must reset the list");
    aether_shadow_list_free(&source);

    // Background layers use the same owned-list contract, but each entry has nested gradient or
    // image-source storage. Both forms must survive a style copy independently.
    aether_background_layer_list backgrounds{};
    backgrounds.items = static_cast<aether_background_layer*>(std::calloc(2, sizeof(aether_background_layer)));
    require(backgrounds.items != nullptr, "background-layer source allocation failed");
    backgrounds.count = 2;
    backgrounds.items[0].kind = AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT;
    backgrounds.items[0].gradient.direction_x = 1;
    backgrounds.items[0].gradient.stops = static_cast<aether_gradient_stop*>(std::malloc(sizeof(aether_gradient_stop)));
    require(backgrounds.items[0].gradient.stops != nullptr, "background gradient stop allocation failed");
    backgrounds.items[0].gradient.stop_count = 1;
    backgrounds.items[0].gradient.stops[0] = {{1, 0, 0, 1}, aether_css_length_zero(), 0.f, AETHER_TRUE};
    backgrounds.items[1].kind = AETHER_BACKGROUND_LAYER_IMAGE_URL;
    backgrounds.items[1].image_source = static_cast<char*>(std::malloc(18));
    require(backgrounds.items[1].image_source != nullptr, "background image source allocation failed");
    std::strcpy(backgrounds.items[1].image_source, "asset://mountain");
    aether_background_layer_list background_copy{};
    require(aether_background_layer_list_copy(&backgrounds, &background_copy) == AETHER_TRUE,
            "background-layer deep copy failed");
    require(background_copy.items != backgrounds.items &&
            background_copy.items[0].gradient.stops != backgrounds.items[0].gradient.stops &&
            background_copy.items[1].image_source != backgrounds.items[1].image_source &&
            aether_background_layer_list_equal(&backgrounds, &background_copy) == AETHER_TRUE,
            "background-layer copy must own independent nested resources");
    backgrounds.items[1].image_source[0] = 'X';
    require(aether_background_layer_list_equal(&backgrounds, &background_copy) == AETHER_FALSE,
            "background-layer equality must compare resource identity by source text");
    aether_background_layer_list_free(&background_copy);
    require(background_copy.items == nullptr && background_copy.count == 0,
            "background-layer free must reset the list");
    aether_background_layer_list_free(&backgrounds);

    aether_doc_t document = nullptr;
    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document,
        "<body>"
        "<div id='before'></div><div id='after'></div>"
         "<div id='parent'><div id='inherited'></div></div>"
         "<div id='text-parent'><div id='text-inherited'></div><div id='text-none'></div></div>"
         "<div id='initial'></div><div id='shadows'></div>"
         "<div id='text-spread'></div><div id='text-inset'></div>"
        "</body>") == AETHER_OK, "fixture parsing failed");

    aether_style_runtime_options_t options{};
    options.max_variable_depth = 32;
    options.max_variable_expansion_length = 65536;
    aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "style runtime creation failed");
    diagnostic_capture_s diagnostics{};
    aether_style_runtime_set_diagnostics_callback(runtime, capture_diagnostic, &diagnostics);

    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "#before { border: 1px solid currentColor; color: #123456; }"
        "#after { color: #abcdef; border: 1px solid currentColor; }"
        "#parent { color: #224466; }"
         "#inherited { border: 1px solid currentColor; }"
         "#text-parent { color: #446688; text-shadow: 1px 2px 3px currentColor, 4px 5px #ff0000; }"
         "#text-none { text-shadow: none; }"
         "#initial { border: 1px solid; }"
        "#shadows { color: #112233; box-shadow: 1px 2px 3px #ff0000, "
         "inset 4px 5px 6px 7px currentColor, 8px 9px rgba(0,255,0,0.5); }"
         "#text-spread { text-shadow: 1px 2px 3px 4px red; }"
         "#text-inset { text-shadow: inset 1px 2px red; }",
        2, "current-color.css", &sheet, &rules) == AETHER_OK,
        "stylesheet compilation failed");

    const auto* body = aether_doc_get_body(document);
    auto* before = aether_doc_get_element_by_id(document, "before");
    auto* after = aether_doc_get_element_by_id(document, "after");
    auto* inherited = aether_doc_get_element_by_id(document, "inherited");
    auto* text_inherited = aether_doc_get_element_by_id(document, "text-inherited");
    auto* text_none = aether_doc_get_element_by_id(document, "text-none");
    auto* initial = aether_doc_get_element_by_id(document, "initial");
    auto* shadows = aether_doc_get_element_by_id(document, "shadows");
    require(body && before && after && inherited && text_inherited && text_none && initial && shadows, "fixture lookup failed");

    const aether_color_rgba before_color{0x12 / 255.0f, 0x34 / 255.0f, 0x56 / 255.0f, 1};
    const aether_color_rgba after_color{0xab / 255.0f, 0xcd / 255.0f, 0xef / 255.0f, 1};
    const aether_color_rgba inherited_color{0x22 / 255.0f, 0x44 / 255.0f, 0x66 / 255.0f, 1};
    const aether_color_rgba black{0, 0, 0, 1};

    const auto before_style = find_style(runtime, const_cast<aether_node_t>(body), before);
    require(diagnostics.count == 2 && diagnostics.code && diagnostics.property &&
            std::strcmp(diagnostics.code, "AETHER-CSS-TEXT-SHADOW") == 0 &&
            std::strcmp(diagnostics.property, "text-shadow") == 0,
            "text-shadow spread and inset must be rejected with dedicated diagnostics");
    require(same_color(before_style.border_colors.top, before_color) &&
            same_color(before_style.border_colors.right, before_color) &&
            same_color(before_style.border_colors.bottom, before_color) &&
            same_color(before_style.border_colors.left, before_color),
            "currentColor must resolve after color regardless of declaration order");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&before_style));

    // The cache has all node styles after the first query, so each following
    // target is read from the same final cascade pass through a fresh fixture.
    aether_style_runtime_clear_cache(runtime);
    const auto after_style = find_style(runtime, const_cast<aether_node_t>(body), after);
    require(same_color(after_style.border_colors.left, after_color),
            "currentColor must not depend on the property being declared after color");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&after_style));

    aether_style_runtime_clear_cache(runtime);
    const auto inherited_style = find_style(runtime, const_cast<aether_node_t>(body), inherited);
    require(same_color(inherited_style.color, inherited_color) &&
            same_color(inherited_style.border_colors.left, inherited_color),
            "currentColor must resolve from the inherited final color of the same node");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&inherited_style));

    aether_style_runtime_clear_cache(runtime);
    const auto text_inherited_style = find_style(runtime, const_cast<aether_node_t>(body), text_inherited);
    require(text_inherited_style.text_shadows.count == 2 && text_inherited_style.text_shadows.items &&
            text_inherited_style.text_shadows.items[0].offset_x == 1 &&
            text_inherited_style.text_shadows.items[0].offset_y == 2 &&
            text_inherited_style.text_shadows.items[0].blur_radius == 3 &&
            same_color(text_inherited_style.text_shadows.items[0].color,
                       {0x44 / 255.0f, 0x66 / 255.0f, 0x88 / 255.0f, 1}),
            "text-shadow must inherit as an independent computed list after currentColor resolution");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&text_inherited_style));

    aether_style_runtime_clear_cache(runtime);
    const auto text_none_style = find_style(runtime, const_cast<aether_node_t>(body), text_none);
    require(text_none_style.text_shadows.count == 0 && text_none_style.text_shadows.items == nullptr,
            "text-shadow: none on a child must cancel the inherited shadow list");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&text_none_style));

    aether_style_runtime_clear_cache(runtime);
    const auto initial_style = find_style(runtime, const_cast<aether_node_t>(body), initial);
    require(same_color(initial_style.border_colors.left, black),
            "the initial border color must resolve to currentColor rather than transparent");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&initial_style));

    aether_style_runtime_clear_cache(runtime);
    const auto shadow_style = find_style(runtime, const_cast<aether_node_t>(body), shadows);
    require(shadow_style.box_shadows.count == 3 && shadow_style.box_shadows.items,
            "box-shadow must preserve every comma-separated shadow");
    const auto& first_shadow = shadow_style.box_shadows.items[0];
    const auto& second_shadow = shadow_style.box_shadows.items[1];
    const auto& third_shadow = shadow_style.box_shadows.items[2];
    require(first_shadow.offset_x == 1 && first_shadow.offset_y == 2 && first_shadow.blur_radius == 3 &&
            same_color(first_shadow.color, {1, 0, 0, 1}) && first_shadow.inset == AETHER_FALSE,
            "first box shadow must retain declaration order and values");
    require(second_shadow.offset_x == 4 && second_shadow.offset_y == 5 && second_shadow.blur_radius == 6 &&
            second_shadow.spread_radius == 7 && second_shadow.inset == AETHER_TRUE &&
            same_color(second_shadow.color, {0x11 / 255.0f, 0x22 / 255.0f, 0x33 / 255.0f, 1}),
            "box-shadow currentColor must resolve after the final node color");
    require(third_shadow.offset_x == 8 && third_shadow.offset_y == 9 && third_shadow.blur_radius == 0 &&
            std::fabs(third_shadow.color.g - 1) < 0.0001f && std::fabs(third_shadow.color.a - .5f) < 0.0001f,
            "subsequent box shadows must retain their own values");
    aether_computed_style_free(const_cast<aether_computed_style_t*>(&shadow_style));

    require(aether_style_runtime_remove_stylesheet(runtime, sheet) == AETHER_TRUE,
            "stylesheet removal failed");
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    std::puts("OK: currentColor test passed");
    return 0;
}
