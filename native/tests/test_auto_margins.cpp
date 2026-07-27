#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) {
    return std::fabs(actual - expected) < 0.001f;
}

static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 1.f, .8f};
}

static aether_layout_box_t box_for(aether_doc_t doc, aether_layout_result_t result, const char* id) {
    const auto element = aether_doc_get_element_by_id(doc, id);
    require(element != nullptr, "fixture element lookup failed");
    const auto box = aether_layout_result_find_primary_box(result, element);
    require(box != nullptr, "fixture layout box missing");
    return box;
}

int main() {
    const auto defaults = aether_computed_style_default();
    require(defaults.margin.left.unit == AETHER_CSS_UNIT_PX && same(defaults.margin.left.value, 0.f) &&
            defaults.margin.right.unit == AETHER_CSS_UNIT_PX && same(defaults.margin.right.value, 0.f),
        "computed-style default margins must be CSS 0px rather than auto");

    const char* markup =
        "<body>"
        "<div id='block'><div id='block-child'></div></div>"
        "<div id='vertical'><div id='vertical-child'></div></div>"
        "<div id='row-main'><div id='row-main-child'></div></div>"
        "<div id='row-cross'><div id='row-cross-child'></div></div>"
        "<div id='column-main'><div id='column-main-child'></div></div>"
        "<div id='column-cross'><div id='column-cross-child'></div></div>"
        "</body>";
    const char* css =
        "body { display:block; margin:0; box-sizing:border-box; }"
        "#block { display:block; width:80px; box-sizing:border-box; }"
        "#block-child { display:block; width:50px; height:20px; box-sizing:border-box; margin:0 auto 8px; }"
        "#vertical { display:block; width:80px; height:100px; box-sizing:border-box; }"
        "#vertical-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; margin-bottom:auto; }"
        "#row-main { display:flex; width:100px; height:60px; box-sizing:border-box; justify-content:start; align-items:center; }"
        "#row-main-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-left:auto; }"
        "#row-cross { display:flex; width:100px; height:100px; box-sizing:border-box; align-items:end; }"
        "#row-cross-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; margin-bottom:auto; }"
        "#column-main { display:flex; flex-direction:column; width:100px; height:100px; box-sizing:border-box; justify-content:start; }"
        "#column-main-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-top:auto; }"
        "#column-cross { display:flex; flex-direction:column; width:100px; height:60px; box-sizing:border-box; align-items:end; }"
        "#column-cross-child { display:block; width:20px; height:20px; box-sizing:border-box; margin-left:auto; margin-right:auto; }";

    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, markup) == AETHER_OK,
        "fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300.f, 600.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "auto-margins.css", &stylesheet, &rule_count) == AETHER_OK,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "style resolution failed");
    aether_style_updates_free(updates, update_count);
    aether_style_map_t styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
        "resolved style-map creation failed");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK,
        "layout engine creation failed");
    aether_layout_result_t result = nullptr;
    require(aether_layout_layout(engine, doc, styles, 300.f, 600.f, nullptr, nullptr, &result) == AETHER_OK,
        "layout failed");

    const auto block = box_for(doc, result, "block");
    const auto block_child = box_for(doc, result, "block-child");
    const auto block_content = aether_layout_box_get_content_box(block);
    const auto block_child_margin = aether_layout_box_get_margin_box(block_child);
    const auto block_child_border = aether_layout_box_get_border_box(block_child);
    require(same(block_child_margin.width, 80.f) && same(block_child_border.x, block_content.x + 15.f),
        "a definite block width with two horizontal auto margins must center its child");

    const auto vertical = box_for(doc, result, "vertical");
    const auto vertical_child = box_for(doc, result, "vertical-child");
    require(same(aether_layout_box_get_border_box(vertical_child).y, aether_layout_box_get_content_box(vertical).y),
        "vertical auto margins in normal block flow must resolve to zero");

    const auto row_main = box_for(doc, result, "row-main");
    const auto row_main_child = box_for(doc, result, "row-main-child");
    require(same(aether_layout_box_get_border_box(row_main_child).x,
                 aether_layout_box_get_content_box(row_main).x + 80.f),
        "a row flex-item main-axis auto margin must consume free space before justify-content");

    const auto row_cross = box_for(doc, result, "row-cross");
    const auto row_cross_child = box_for(doc, result, "row-cross-child");
    require(same(aether_layout_box_get_border_box(row_cross_child).y,
                 aether_layout_box_get_content_box(row_cross).y + 40.f),
        "row flex cross-axis auto margins must override align-items using available cross space");

    const auto column_main = box_for(doc, result, "column-main");
    const auto column_main_child = box_for(doc, result, "column-main-child");
    require(same(aether_layout_box_get_border_box(column_main_child).y,
                 aether_layout_box_get_content_box(column_main).y + 80.f),
        "a column flex-item main-axis auto margin must consume free space before justify-content");

    const auto column_cross = box_for(doc, result, "column-cross");
    const auto column_cross_child = box_for(doc, result, "column-cross-child");
    require(same(aether_layout_box_get_border_box(column_cross_child).x,
                 aether_layout_box_get_content_box(column_cross).x + 40.f),
        "column flex cross-axis auto margins must override align-items using available cross space");

    aether_layout_result_destroy(result);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    return 0;
}
