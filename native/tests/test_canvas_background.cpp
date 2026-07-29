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

bool is_canvas_rect(const aether_rectf rect) {
    return same(rect.x, 0.f) && same(rect.y, 0.f) && same(rect.width, 200.f) && same(rect.height, 100.f);
}

aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

aether_bool_t image_dimensions(const char* source, aether_sizef* out_size, void*) {
    if (!source || !out_size || std::strcmp(source, "asset://canvas") != 0) return AETHER_FALSE;
    *out_size = {100.f, 50.f};
    return AETHER_TRUE;
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
                aether_doc_parse_html(frame.document, "<html><body><div id='content'></div></body></html>") == AETHER_OK,
            "canvas fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &frame.runtime) == AETHER_OK,
            "canvas fixture style runtime creation failed");
    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(frame.runtime, css, 0, "canvas-background.css", &sheet, &rule_count) == AETHER_OK,
            "canvas fixture stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto count = aether_style_runtime_resolve_subtree(
        frame.runtime, aether_doc_get_document_element(frame.document), &updates);
    require(count > 0, "canvas fixture style resolution failed");
    aether_style_updates_free(updates, count);
    require(aether_style_runtime_create_resolved_style_map(frame.runtime, frame.document, &frame.styles) == AETHER_OK,
            "canvas fixture style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &frame.engine) == AETHER_OK &&
                aether_layout_layout(frame.engine, frame.document, frame.styles, 200.f, 100.f, nullptr, nullptr, &frame.layout) == AETHER_OK,
            "canvas fixture layout failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &frame.builder) == AETHER_OK,
            "canvas fixture display-list builder creation failed");
    aether_display_list_builder_set_image_dimensions_callback(frame.builder, image_dimensions, nullptr);
    require(aether_display_list_build(frame.builder, frame.layout, nullptr, &frame.list) == AETHER_OK,
            "canvas fixture display-list creation failed");
}

} // namespace

int main() {
    {
        frame_s frame{};
        build(frame,
            "html { display:block; background-color:transparent; }"
            "body { display:block; margin:8px; background-color:#123456; }"
            "#content { display:block; width:20px; height:10px; }");
        int fills = 0;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "canvas color command read failed");
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT &&
                same(command.data.fill_rounded_rect.color.r, 0x12 / 255.f) &&
                same(command.data.fill_rounded_rect.color.g, 0x34 / 255.f) &&
                same(command.data.fill_rounded_rect.color.b, 0x56 / 255.f)) {
                ++fills;
                require(is_canvas_rect(command.data.fill_rounded_rect.rect),
                        "propagated body color must paint the viewport, not body border-box");
            }
            aether_paint_command_free(&command);
        }
        require(fills == 1, "a propagated body background must not be painted again on body itself");
    }

    {
        frame_s frame{};
        build(frame,
            "html { display:block; background-color:transparent; }"
            "body { display:block; margin:8px; background-image:linear-gradient(90deg, red, blue); }"
            "#content { display:block; width:20px; height:10px; }");
        int gradients = 0;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "canvas gradient command read failed");
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT) {
                ++gradients;
                require(is_canvas_rect(command.data.fill_rounded_rect_gradient.rect),
                        "propagated body gradient must be anchored to the viewport canvas");
            }
            aether_paint_command_free(&command);
        }
        require(gradients == 1, "a propagated body gradient must not duplicate on the body box");
    }

    {
        frame_s frame{};
        build(frame,
            "html { display:block; background-color:transparent; }"
            "body { display:block; margin:8px; background:url('asset://canvas') center / cover no-repeat; }"
            "#content { display:block; width:20px; height:10px; }");
        int images = 0;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "canvas image command read failed");
            if (command.tag == AETHER_PAINT_DRAW_BACKGROUND_IMAGE) {
                ++images;
                require(is_canvas_rect(command.data.draw_background_image.paint_rect) &&
                            command.data.draw_background_image.has_resolved_geometry == AETHER_TRUE,
                        "propagated body image must resolve against the viewport canvas");
            }
            aether_paint_command_free(&command);
        }
        require(images == 1, "a propagated body image must not duplicate on the body box");
    }

    {
        frame_s frame{};
        build(frame,
            "html { display:block; background-color:#010203; }"
            "body { display:block; margin:8px; background-color:#aabbcc; }"
            "#content { display:block; width:20px; height:10px; }");
        bool html_canvas = false;
        bool body_box = false;
        for (int32_t index = 0; index < aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, index, &command) == AETHER_OK,
                    "html root command read failed");
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
                const auto color = command.data.fill_rounded_rect.color;
                if (same(color.r, 1.f / 255.f) && same(color.g, 2.f / 255.f) && same(color.b, 3.f / 255.f)) {
                    html_canvas = is_canvas_rect(command.data.fill_rounded_rect.rect);
                }
                if (same(color.r, 0xaa / 255.f) && same(color.g, 0xbb / 255.f) && same(color.b, 0xcc / 255.f)) {
                    body_box = !is_canvas_rect(command.data.fill_rounded_rect.rect);
                }
            }
            aether_paint_command_free(&command);
        }
        require(html_canvas && body_box,
                "an opaque html background owns the canvas while body retains its ordinary box background");
    }
    return 0;
}
