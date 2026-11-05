// Headless ABI integration, not a pixel/visual rendering test.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style, float, void*) {
    return {static_cast<float>(text ?  std::strlen(text) : 0) * style->font_size / 2,
            style->font_size, style->font_size * .8f};
}
int main() {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "create document");
    require(aether_doc_parse_html(doc,
        "<body><section id='weather'><div id='temperature'>24</div></section></body>") == AETHER_OK,
        "parse document");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "create runtime");
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "body{margin:0} #temperature{font-size:23px}"
        "#weather::before{content:'['} #weather::after{content:']'}"
        "#temperature:first-child::before{content:'B';font-size:17px}"
        "#temperature:last-child::after{content:'A';font-size:19px}",
        0, "structural-display-list", &sheet, &rules) == AETHER_OK && rules == 6,
        "compile all rules");
    auto temperature = aether_doc_get_element_by_id(doc, "temperature");
    auto weather = aether_doc_get_element_by_id(doc, "weather");
    for (int pass = 0; pass < 3; ++pass) {
        aether_style_update_t* updates = nullptr;
        int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
        require(count >= 0, "resolve styles");
        aether_style_updates_free(updates, count);
        require(aether_node_get_child_count(weather) == 1 && aether_node_get_child(weather, 0) == temperature,
                "generated content excluded from public children");
        require(aether_node_get_parent(temperature) == weather, "real parent unchanged");
        aether_style_map_t styles = nullptr;
        aether_layout_engine_t engine = nullptr;
        aether_layout_result_t layout = nullptr;
        aether_display_list_builder_t builder = nullptr;
        aether_display_list_t list = nullptr;
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK, "style map");
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine");
        require(aether_layout_layout(engine, doc, styles, 640, 480, nullptr, nullptr, &layout) == AETHER_OK, "layout");
        require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "builder");
        require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK, "display list");
        int before = 0, after = 0, real = 0, parent_before = 0, parent_after = 0;
        for (int i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read command");
            if (command.tag == AETHER_PAINT_DRAW_TEXT && command.data.draw_text.text) {
                const auto& text = command.data.draw_text;
                const std::string value = text.text;
                if (value == "B") { ++before; require(std::fabs(text.font_size - 17) < .001f, "before style reaches paint"); }
                if (value == "A") { ++after; require(std::fabs(text.font_size - 19) < .001f, "after style reaches paint"); }
                if (value == "24") { ++real; require(std::fabs(text.font_size - 23) < .001f, "real text style reaches paint"); }
                if (value == "[") ++parent_before;
                if (value == "]") ++parent_after;
            }
            aether_paint_command_free(&command);
        }
        require(before == 1 && after == 1 && real == 1 && parent_before == 1 && parent_after == 1,
                "structural before/after and real text each reach display list exactly once");
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); // layout borrows styles
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
    }
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    std::puts("PASS: generated structural selectors reach the headless display list in all 3 passes");
}
