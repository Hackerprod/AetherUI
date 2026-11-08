#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

static int image_calls = 0;
static const char* expected_source = "hero.png";

static aether_bool_t image_dimensions(const char* source, aether_sizef* out_size, void*) {
    require(source && out_size, "image callback arguments");
    require(std::strcmp(source, expected_source) == 0, "callback sees decoded identity");
    ++image_calls;
    *out_size = {100.f, 100.f};
    return AETHER_TRUE;
}

static void run_case(const char* declaration) {
    image_calls = 0;
    expected_source = "hero.png";
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t layout_engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t display_list = nullptr;

    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document, "<body><div id=\"image\"></div></body>") == AETHER_OK,
        "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    const std::string css = std::string(
        "body { display: block; margin: 0; } #image { display: block; width: 100px; height: 40px; ") +
        declaration + " background-color: #123456; }";
    require(aether_style_runtime_add_stylesheet(runtime, css.c_str(),
        0, "background-image-display-list.css", &stylesheet, &rule_count) == AETHER_OK && rule_count == 2,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    require(update_count > 0, "style resolution failed");
    aether_node_t image_element = aether_doc_get_element_by_id(document, "image");
    require(image_element != nullptr, "image element lookup failed");
    aether_computed_style_handle_t image_style = nullptr;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(updates[index], &updated, &candidate, nullptr) == AETHER_OK,
            "style update read failed");
        if (updated == image_element) image_style = candidate;
        else aether_computed_style_handle_destroy(candidate);
    }
    require(image_style != nullptr, "image element style update missing");
    aether_computed_style_t copied_style{};
    require(aether_computed_style_copy(image_style, &copied_style) == AETHER_OK,
        "image computed-style copy failed");
    require(copied_style.background_layers.count == 1 && copied_style.background_layers.items &&
                copied_style.background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_IMAGE_URL,
        "background image layer was lost before layout");
    require(copied_style.background_layers.items[0].image_source &&
        std::strcmp(copied_style.background_layers.items[0].image_source, "hero.png") == 0,
        "computed style stores decoded identity");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
        "resolved style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &layout_engine) == AETHER_OK,
        "layout engine creation failed");
    require(aether_layout_layout(layout_engine, document, styles, 200.f, 100.f, nullptr, nullptr, &layout) == AETHER_OK,
        "layout execution failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &builder) == AETHER_OK,
        "display-list builder creation failed");
    aether_display_list_builder_set_image_dimensions_callback(builder, image_dimensions, nullptr);
    require(aether_display_list_build(builder, layout, nullptr, &display_list) == AETHER_OK,
        "display-list build failed");

    require(image_calls > 0, "image dimensions callback must actually run");
    const int32_t command_count = aether_display_list_get_count(display_list);
    int32_t color_index = -1;
    int32_t image_index = -1;
    aether_paint_command_t image_command{};
    for (int32_t index = 0; index < command_count; ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(display_list, index, &command) == AETHER_OK,
            "display-list command read failed");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) color_index = index;
        if (command.tag == AETHER_PAINT_DRAW_BACKGROUND_IMAGE) {
            image_index = index;
            image_command = command;
            continue;
        }
        aether_paint_command_free(&command);
    }

    require(image_index >= 0, "image background must emit DRAW_BACKGROUND_IMAGE");
    require(color_index >= 0 && color_index < image_index,
        "background color must paint below its image layer");
    require(image_command.data.draw_background_image.source &&
                std::strcmp(image_command.data.draw_background_image.source, "hero.png") == 0,
        "image command must retain the declared URL");
    require(image_command.data.draw_background_image.has_resolved_geometry == AETHER_TRUE &&
                image_command.data.draw_background_image.destination_rect.width > 0.f &&
                image_command.data.draw_background_image.source_rect.width > 0.f,
        "known image dimensions must produce resolved source and destination geometry");

    // Keep the old handle/map/layout and command alive across a new declaration.
    int64_t changed_sheet = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "#image { background-image: url('next\\2e png'); }", 0,
        "mutated-image.css", &changed_sheet, nullptr) == AETHER_OK, "mutation stylesheet");
    aether_style_update_t* changed_updates = nullptr;
    const int32_t changed_count = aether_style_runtime_resolve_subtree(
        runtime, aether_doc_get_body(document), &changed_updates);
    require(changed_count > 0, "mutated styles resolved before snapshot");
    aether_style_updates_free(changed_updates, changed_count);
    aether_style_map_t changed_styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &changed_styles) == AETHER_OK,
        "mutated style map");
    aether_layout_result_t changed_layout = nullptr;
    require(aether_layout_layout(layout_engine, document, changed_styles, 200.f, 100.f,
        nullptr, nullptr, &changed_layout) == AETHER_OK, "mutated layout");
    expected_source = "next.png";
    image_calls = 0;
    aether_display_list_t changed_list = nullptr;
    require(aether_display_list_build(builder, changed_layout, nullptr, &changed_list) == AETHER_OK,
        "mutated display list");
    require(image_calls > 0, "new identity reaches callback");
    bool saw_changed_image = false;
    for (int32_t i = 0; i < aether_display_list_get_count(changed_list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(changed_list, i, &command) == AETHER_OK, "changed command");
        if (command.tag == AETHER_PAINT_DRAW_BACKGROUND_IMAGE) {
            require(command.data.draw_background_image.source &&
                std::strcmp(command.data.draw_background_image.source, "next.png") == 0,
                "new command identity");
            require(command.data.draw_background_image.has_resolved_geometry &&
                command.data.draw_background_image.destination_rect.width > 0.f,
                "new command geometry");
            saw_changed_image = true;
        }
        aether_paint_command_free(&command);
    }
    require(saw_changed_image, "new image command observed");
    aether_computed_style_t retained{};
    require(aether_computed_style_copy(image_style, &retained) == AETHER_OK, "retained style copy");
    require(retained.background_layers.count == 1 && retained.background_layers.items &&
        retained.background_layers.items[0].image_source &&
        std::strcmp(retained.background_layers.items[0].image_source, "hero.png") == 0,
        "retained handle keeps old decoded identity");
    require(std::strcmp(copied_style.background_layers.items[0].image_source, "hero.png") == 0,
        "owned style copy keeps old identity");
    aether_computed_style_free(&retained);
    expected_source = "hero.png";
    image_calls = 0;
    aether_display_list_t retained_list = nullptr;
    require(aether_display_list_build(builder, layout, nullptr, &retained_list) == AETHER_OK,
        "old layout can still paint");
    require(image_calls > 0, "old frozen layout retains decoded callback identity");
    aether_display_list_destroy(retained_list);
    aether_display_list_destroy(changed_list);
    aether_layout_result_destroy(changed_layout);
    aether_style_map_destroy(changed_styles);

    // get_command returns caller-owned data. Its URL must outlive the display list it was copied from.
    aether_display_list_destroy(display_list);
    display_list = nullptr;
    require(std::strcmp(image_command.data.draw_background_image.source, "hero.png") == 0,
        "image command URL must be independent from the destroyed display list");
    aether_paint_command_free(&image_command);

    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(layout_engine);
    aether_style_map_destroy(styles);
    aether_computed_style_free(&copied_style);
    aether_computed_style_handle_destroy(image_style);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
}

int main() {
    for (const char* declaration : {
        "background-image: url('hero.png');",
        "background-image: url('hero\\2e png');",
        "background-image: url(hero\\.png);",
        "background: url('hero\\00002e png') center / cover no-repeat;",
        "--image: url('hero\\2e\npng'); background-image: var(--image);"
    }) run_case(declaration);
    return 0;
}
