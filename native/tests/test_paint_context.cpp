#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool near(float a, float b, float tolerance = 0.01f) {
    return std::fabs(a - b) <= tolerance;
}

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    const float width = text ? static_cast<float>(std::strlen(text)) : 0.f;
    return {width, 1.f, .8f};
}

/* Regression for the centralized paint_context_s: the effective opacity a box
   paints at flows through ONE context struct, so descendant opacity is the
   product of every ancestor's opacity and a control nested in the chain is
   dimmed to the inherited value. No transition runtime is involved: the
   inherited chain itself must prove the context math. */
int main() {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t layout_engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t display_list = nullptr;

    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document,
        "<body><div id=\"a\" style=\"opacity:0.5\">"
        "<div id=\"b\" style=\"opacity:0.5\"></div>"
        "<input value=\"OK\"/>"
        "</div></body>") == AETHER_OK,
        "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "body { display: block; margin: 0; } "
        "#a { display: block; width: 100px; height: 80px; background-color: rgb(255, 0, 0); } "
        "#b { display: block; width: 50px; height: 30px; background-color: rgb(0, 255, 0); }",
        0, "paint-context.css", &stylesheet, &rule_count) == AETHER_OK && rule_count == 3,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    require(update_count > 0, "style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
        "resolved style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &layout_engine) == AETHER_OK,
        "layout engine creation failed");
    require(aether_layout_layout(layout_engine, document, styles, 200.f, 100.f, nullptr, nullptr, &layout) == AETHER_OK,
        "layout execution failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &builder) == AETHER_OK,
        "display-list builder creation failed");
    require(aether_display_list_build(builder, layout, nullptr, &display_list) == AETHER_OK,
        "display-list build failed");

    // #a paints at 0.5, #b (child of #a) at 0.5 x 0.5 = 0.25, and the control's
    // text inside #a at the inherited 0.5. Each element has a distinct color so
    // the fills are identified unambiguously.
    bool a_found = false;
    bool b_found = false;
    bool control_text_found = false;
    const int32_t command_count = aether_display_list_get_count(display_list);
    for (int32_t index = 0; index < command_count; ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(display_list, index, &command) == AETHER_OK,
            "display-list command read failed");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto color = command.data.fill_rounded_rect.color;
            if (near(color.r, 1.f) && near(color.g, 0.f) && near(color.b, 0.f)) {
                require(near(color.a, 0.5f),
                    "#a fill must paint at its own opacity (0.5)");
                a_found = true;
            }
            if (near(color.r, 0.f) && near(color.g, 1.f) && near(color.b, 0.f)) {
                require(near(color.a, 0.25f),
                    "#b fill must paint at parent x own opacity (0.5 x 0.5 = 0.25)");
                b_found = true;
            }
        }
        if (command.tag == AETHER_PAINT_DRAW_TEXT) {
            const auto color = command.data.draw_text.color;
            if (near(color.r, 0.f) && near(color.g, 0.f) && near(color.b, 0.f)) {
                require(std::strcmp(command.data.draw_text.text, "OK") == 0,
                    "control text command must be the input value");
                require(near(color.a, 0.5f),
                    "control text inside #a must paint at the inherited opacity (0.5)");
                control_text_found = true;
            }
        }
        aether_paint_command_free(&command);
    }

    require(a_found, "#a background fill missing from the display list");
    require(b_found, "#b background fill missing from the display list");
    require(control_text_found, "control text fill missing from the display list");

    aether_display_list_destroy(display_list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(layout_engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
