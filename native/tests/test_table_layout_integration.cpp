#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {static_cast<float>(std::strlen(text)) * 10.f, 12.f, 9.f};
}

static aether_layout_box_t box_for(aether_doc_t doc, aether_layout_result_t result, const char* id) {
    const auto element = aether_doc_get_element_by_id(doc, id);
    if (!element) { std::fprintf(stderr, "FAIL: fixture element '%s' lookup failed\n", id); std::exit(1); }
    const auto box = aether_layout_result_find_primary_box(result, element);
    if (!box) { std::fprintf(stderr, "FAIL: fixture layout box '%s' missing\n", id); std::exit(1); }
    return box;
}

int main() {
    const char* markup =
        "<body><table id='table'><thead><tr id='header'><th id='h1'>Invoice</th><th id='h2'>Customer</th></tr></thead>"
        "<tbody><tr id='detail'><td id='d1'>Alexander</td><td id='d2'>Date</td></tr></tbody></table></body>";
    const char* css =
        "body { display:block; width:300px; margin:0; box-sizing:border-box; }"
        "table { display:table; width:300px; box-sizing:border-box; }"
        "tr { display:table-row; }"
        "td, th { display:table-cell; padding:5px; border:1px solid #000; box-sizing:border-box; }";

    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, markup) == AETHER_OK,
        "table fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300.f, 200.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "table fixture style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "table.css", &stylesheet, &rule_count) == AETHER_OK,
        "table stylesheet creation failed");
    aether_style_map_t styles = nullptr;
    const auto table_element = aether_doc_get_element_by_id(doc, "table");
    const auto header_element = aether_doc_get_element_by_id(doc, "header");
    const auto cell_element = aether_doc_get_element_by_id(doc, "h1");
    aether_computed_style_t table_style{};
    aether_computed_style_t row_style{};
    aether_computed_style_t cell_style{};
    require(table_element && header_element && cell_element, "table fixture style lookup failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    bool table_found = false, row_found = false, cell_found = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
            "table style update access failed");
        if (node == table_element) { require(aether_computed_style_copy(handle, &table_style) == AETHER_OK, "table style copy failed"); table_found = true; }
        if (node == header_element) { require(aether_computed_style_copy(handle, &row_style) == AETHER_OK, "row style copy failed"); row_found = true; }
        if (node == cell_element) { require(aether_computed_style_copy(handle, &cell_style) == AETHER_OK, "cell style copy failed"); cell_found = true; }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, update_count);
    require(table_found && row_found && cell_found, "table display-role styles were not resolved");
    require(table_style.display == AETHER_DISPLAY_TABLE && row_style.display == AETHER_DISPLAY_TABLE_ROW &&
            cell_style.display == AETHER_DISPLAY_TABLE_CELL,
        "table display roles did not survive the cascade");
    aether_computed_style_free(&table_style);
    aether_computed_style_free(&row_style);
    aether_computed_style_free(&cell_style);
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
        "table style-map creation failed");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure_text, nullptr, &engine) == AETHER_OK,
        "table layout engine creation failed");
    aether_layout_result_t result = nullptr;
    require(aether_layout_layout(engine, doc, styles, 300.f, 200.f, nullptr, nullptr, &result) == AETHER_OK,
        "table layout failed");

    const auto table = box_for(doc, result, "table");
    const auto header = box_for(doc, result, "header");
    const auto detail = box_for(doc, result, "detail");
    const auto h1 = box_for(doc, result, "h1");
    const auto h2 = box_for(doc, result, "h2");
    const auto d1 = box_for(doc, result, "d1");
    const auto d2 = box_for(doc, result, "d2");
    const auto table_content = aether_layout_box_get_content_box(table);
    const auto h1_border = aether_layout_box_get_border_box(h1);
    const auto h2_border = aether_layout_box_get_border_box(h2);
    const auto d1_border = aether_layout_box_get_border_box(d1);
    const auto d2_border = aether_layout_box_get_border_box(d2);

    require(same(h1_border.x, table_content.x) && same(d1_border.x, table_content.x),
        "first table column must begin at the table content edge for every row");
    require(same(h2_border.x, h1_border.x + h1_border.width) && same(d2_border.x, d1_border.x + d1_border.width),
        "table cells must share one resolved column boundary across rows");
    require(same(h1_border.width, d1_border.width) && same(h2_border.width, d2_border.width) &&
            same(h1_border.width + h2_border.width, table_content.width),
        "automatic table columns must fill the table width without per-row drift");
    require(h1_border.width > h2_border.width,
        "max-content contributions must give the longer first column more width before surplus distribution");
    require(aether_layout_box_get_border_box(detail).y >= aether_layout_box_get_border_box(header).y +
            aether_layout_box_get_border_box(header).height,
        "table rows must stack vertically after their preceding row's final height");

    aether_layout_result_destroy(result);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
