#include <aether/aether.h>

#include "transforms.h"
#include "transform_matrix.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

aether_css_length percentage(float value) {
    auto result = aether_css_length_zero();
    result.value = value;
    result.unit = AETHER_CSS_UNIT_PERCENT;
    return result;
}

aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

// Callback payloads are borrowed only for the duration of the call.
struct diagnostics_s { int count = 0; std::string code, property; };
void diagnostic(const char* code, const char* property, const char*, const char*, void* user_data) {
    auto* diagnostics = static_cast<diagnostics_s*>(user_data);
    ++diagnostics->count;
    diagnostics->code = code ? code : "";
    diagnostics->property = property ? property : "";
}

} // namespace

int main() {
    aether_transform_list parsed{};
    require(aether_transform_list_try_parse(
        "translate(10px, 25%) rotate(.25turn) scale(2, .5) skewX(45deg) matrix(1, 0, 0, 1, 3, 4)",
        &parsed), "complete 2D transform list parsing failed");
    require(parsed.count == 5 && parsed.items, "transform operations must retain declaration order");
    require(parsed.items[0].kind == AETHER_TRANSFORM_TRANSLATE &&
            same(parsed.items[0].x.value, 10.f) && parsed.items[0].x.unit == AETHER_CSS_UNIT_PX &&
            same(parsed.items[0].y.value, 25.f) && parsed.items[0].y.unit == AETHER_CSS_UNIT_PERCENT,
            "translate must preserve length and percentage components symbolically");
    require(parsed.items[1].kind == AETHER_TRANSFORM_ROTATE && same(parsed.items[1].angle_radians, 1.5707963f),
            "turn angles must normalize to radians");
    require(parsed.items[2].kind == AETHER_TRANSFORM_SCALE && same(parsed.items[2].scalar_x, 2.f) && same(parsed.items[2].scalar_y, .5f),
            "scale must retain independent axes");
    require(parsed.items[3].kind == AETHER_TRANSFORM_SKEW_X && parsed.items[4].kind == AETHER_TRANSFORM_MATRIX &&
            same(parsed.items[4].matrix_e, 3.f) && same(parsed.items[4].matrix_f, 4.f),
            "skew and matrix operations must be represented without parser strings");

    aether_transform_list copied{};
    require(aether_transform_list_copy(&parsed, &copied) == AETHER_TRUE && copied.items != parsed.items &&
            aether_transform_list_equal(&parsed, &copied) == AETHER_TRUE,
            "transform-list copy must own independent equal storage");
    copied.items[0].x.value = 11.f;
    require(aether_transform_list_equal(&parsed, &copied) == AETHER_FALSE,
            "transform-list equality must compare operation contents");
    aether_transform_list_free(&parsed);
    aether_transform_list_free(&copied);

    aether_transform_list matrix_operations{};
    require(aether_transform_list_try_parse("translate(10px) rotate(90deg)", &matrix_operations),
            "matrix-composition fixture parse failed");
    const aether_transform_origin zero_origin{aether_css_length_zero(), aether_css_length_zero()};
    auto matrix = aether_transform_matrix_resolve(matrix_operations, zero_origin, {0, 0, 200, 100}, 16, 16, {800, 600});
    auto point = aether_transform_matrix_map_point(matrix, {1, 0});
    require(same(point.x, 10.f) && same(point.y, 1.f),
            "CSS transform lists must apply rightmost operations first");
    aether_transform_list_free(&matrix_operations);
    require(aether_transform_list_try_parse("translate(25%, 50%) rotate(90deg)", &matrix_operations),
            "percentage translation matrix fixture parse failed");
    const aether_transform_origin center_origin{percentage(50.f), percentage(50.f)};
    matrix = aether_transform_matrix_resolve(matrix_operations, center_origin, {10, 20, 200, 100}, 16, 16, {800, 600});
    point = aether_transform_matrix_map_point(matrix, {110, 70});
    require(same(point.x, 160.f) && same(point.y, 120.f),
            "transform-origin and percentage translation must resolve against the border box at paint time");
    aether_transform_list_free(&matrix_operations);

    aether_transform_origin origin{};
    require(aether_transform_origin_try_parse("top right", &origin) &&
            origin.x.unit == AETHER_CSS_UNIT_PERCENT && same(origin.x.value, 100.f) &&
            origin.y.unit == AETHER_CSS_UNIT_PERCENT && same(origin.y.value, 0.f),
            "keyword transform-origin must map against the border-box percentage axes");
    require(aether_transform_origin_try_parse("calc(25% + 4px) 10px", &origin) &&
            origin.x.unit == AETHER_CSS_UNIT_LINEAR && same(origin.x.linear_percent, 25.f) && same(origin.x.value, 4.f) &&
            origin.y.unit == AETHER_CSS_UNIT_PX && same(origin.y.value, 10.f),
            "transform-origin must preserve symbolic calc lengths until post-layout resolution");

    require(!aether_transform_list_try_parse("translateZ(1px)", &parsed),
            "unsupported 3D transform functions must not be approximated as 2D");
    require(!aether_transform_list_try_parse("rotate(10deg 20deg)", &parsed),
            "invalid transform arity must be rejected");
    require(!aether_transform_list_try_parse("matrix(1, 0, 0, 1, nope, 0)", &parsed),
            "invalid matrix components must be rejected");
    require(!aether_transform_list_try_parse("translate(1px,)", &parsed),
            "a trailing transform-function delimiter must be rejected");
    require(!aether_transform_origin_try_parse("left right", &origin),
            "transform-origin must reject duplicate horizontal components");
    require(!aether_transform_origin_try_parse("left, top", &origin),
            "transform-origin must reject comma-separated position components");

    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    require(aether_doc_create(&document) == AETHER_OK &&
            aether_doc_parse_html(document, "<body><div id='item'><div id='child'>nested</div></div><div id='invalid'></div></body>") == AETHER_OK,
            "transform cascade fixture creation failed");
    diagnostics_s diagnostics{};
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{400.f, 300.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, diagnostic, &diagnostics, &runtime) == AETHER_OK,
            "transform runtime creation failed");
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "body { display:block; margin:0; }"
        "#item { display:block; width:100px; height:50px; transform: translateX(25%) rotate(90deg); transform-origin: bottom left; }"
        "#child { display:block; width:20px; height:10px; transform: scale(2); }"
        "#invalid { transform: matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1); }",
        0, "transforms.css", &sheet, &rules) == AETHER_OK,
        "transform stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    const auto item = aether_doc_get_element_by_id(document, "item");
    bool found_item = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
                "transform style update read failed");
        if (node == item) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "transform style copy failed");
            require(style.transforms.count == 2 && style.transforms.items &&
                    style.transforms.items[0].kind == AETHER_TRANSFORM_TRANSLATE &&
                    style.transforms.items[0].x.unit == AETHER_CSS_UNIT_PERCENT && same(style.transforms.items[0].x.value, 25.f) &&
                    style.transforms.items[1].kind == AETHER_TRANSFORM_ROTATE &&
                    same(style.transform_origin.x.value, 0.f) && same(style.transform_origin.y.value, 100.f),
                    "cascade must retain a typed transform list and independent transform origin");
            aether_computed_style_free(&style);
            found_item = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, update_count);
    require(found_item, "transform fixture item did not receive a computed style");
    require(diagnostics.count == 1 && diagnostics.code == "AETHER-CSS-TRANSFORM" &&
            diagnostics.property == "transform",
            "unsupported 3D transforms must emit a dedicated diagnostic instead of silently disappearing");

    aether_style_map_t styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
            "transform display-list style map creation failed");
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK &&
            aether_layout_layout(engine, document, styles, 400.f, 300.f, nullptr, nullptr, &layout) == AETHER_OK &&
            aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK &&
            aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK,
            "transform display-list construction failed");
    int transform_depth = 0;
    int maximum_depth = 0;
    int push_count = 0;
    int pop_count = 0;
    for (int32_t index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, index, &command) == AETHER_OK,
                "transform display-list command read failed");
        if (command.tag == AETHER_PAINT_PUSH_TRANSFORM) {
            ++push_count;
            maximum_depth = std::max(maximum_depth, ++transform_depth);
        } else if (command.tag == AETHER_PAINT_POP_TRANSFORM) {
            ++pop_count;
            require(transform_depth > 0, "transform brackets must never pop an empty stack");
            --transform_depth;
        }
        aether_paint_command_free(&command);
    }
    require(push_count == 2 && pop_count == 2 && maximum_depth == 2 && transform_depth == 0,
            "nested transformed elements must emit balanced brackets without affecting untransformed siblings");
    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
