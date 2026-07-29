#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void require(const bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool same(const float actual, const float expected) {
    return std::fabs(actual - expected) < .001f;
}

aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

struct frame_s {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;

    ~frame_s() {
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout);
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(document);
    }
};

void build(frame_s& frame, const char* css) {
    require(aether_doc_create(&frame.document) == AETHER_OK &&
                aether_doc_parse_html(frame.document, "<html><body><div id='grid'></div></body></html>") == AETHER_OK,
            "gradient tile fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 120.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &frame.runtime) == AETHER_OK,
            "gradient tile fixture runtime creation failed");
    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(frame.runtime, css, 0, "gradient-tiles.css", &sheet, &rule_count) == AETHER_OK,
            "gradient tile fixture stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(
        frame.runtime, aether_doc_get_document_element(frame.document), &updates);
    require(update_count > 0, "gradient tile fixture style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(frame.runtime, frame.document, &frame.styles) == AETHER_OK,
            "gradient tile fixture style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &frame.engine) == AETHER_OK &&
                aether_layout_layout(frame.engine, frame.document, frame.styles, 200.f, 120.f, nullptr, nullptr, &frame.layout) == AETHER_OK,
            "gradient tile fixture layout failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &frame.builder) == AETHER_OK &&
                aether_display_list_build(frame.builder, frame.layout, nullptr, &frame.list) == AETHER_OK,
            "gradient tile fixture display-list creation failed");
}

} // namespace

int main() {
    {
        frame_s frame{};
        build(frame,
            "html, body { display:block; margin:0; background:transparent; }"
            "#grid { display:block; width:100px; height:80px; border-radius:12px;"
            "background-image:linear-gradient(red 1px, transparent 1px),"
            "linear-gradient(90deg, red 1px, transparent 1px);"
            "background-size:40px 40px; background-repeat:repeat; }");

        int gradients = 0;
        int clips = 0;
        bool found_partial_x = false;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "gradient tile command read failed");
            if (command.tag == AETHER_PAINT_PUSH_CLIP && same(command.data.push_clip.rect.width, 100.f) &&
                same(command.data.push_clip.rect.height, 80.f)) {
                ++clips;
                require(same(command.data.push_clip.radii.top_left_x, 12.f),
                        "repeated background layers must be clipped by the box radius");
            }
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT) {
                ++gradients;
                const auto& tile = command.data.fill_rounded_rect_gradient;
                require(tile.radii.top_left_x == 0.f && tile.radii.top_left_y == 0.f,
                        "individual gradient tiles must not receive the box corner radius");
                require(same(tile.gradient_rect.width, 40.f) && same(tile.gradient_rect.height, 40.f),
                        "each procedural tile must retain the declared background-size coordinate box");
                require(tile.gradient.stop_count == 2 && same(tile.gradient.stops[0].offset, .025f) &&
                            same(tile.gradient.stops[1].offset, .025f),
                        "length-based stops must resolve against each gradient tile's final line before painting");
                if (same(tile.rect.x, 80.f) && same(tile.rect.width, 20.f)) {
                    found_partial_x = true;
                    require(same(tile.gradient_rect.x, 80.f) && same(tile.gradient_rect.width, 40.f),
                            "a clipped edge tile must preserve the phase of its complete gradient tile");
                }
            }
            aether_paint_command_free(&command);
        }
        require(gradients == 12, "two repeated 40px gradient layers over 100x80 must emit twelve tiles");
        require(clips == 1, "all repeated background layers must share one rounded clip");
        require(found_partial_x, "expected a clipped 20px edge tile at x=80");
    }

    {
        frame_s frame{};
        build(frame,
            "html, body { display:block; margin:0; background:transparent; }"
            "#grid { display:block; width:100px; height:80px;"
            "background-image:linear-gradient(90deg, red, blue);"
            "background-size:40px 40px; background-position:center; background-repeat:no-repeat; }");
        int gradients = 0;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "positioned gradient command read failed");
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT) {
                ++gradients;
                const auto& tile = command.data.fill_rounded_rect_gradient;
                require(same(tile.rect.x, 30.f) && same(tile.rect.y, 20.f) &&
                            same(tile.rect.width, 40.f) && same(tile.rect.height, 40.f),
                        "background-position:center must position a no-repeat gradient tile inside its paint area");
                require(same(tile.gradient_rect.x, 30.f) && same(tile.gradient_rect.y, 20.f),
                        "an unclipped gradient tile must use its own positioned coordinate box");
            }
            aether_paint_command_free(&command);
        }
        require(gradients == 1, "no-repeat gradient must emit exactly one tile");
    }
    return 0;
}
