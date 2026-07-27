#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

struct diagnostic_capture_s { int count = 0; const char* code = nullptr; const char* property = nullptr; };
static void diagnostic(const char* code, const char* property, const char*, const char*, void* data) {
    auto* capture = static_cast<diagnostic_capture_s*>(data);
    ++capture->count; capture->code = code; capture->property = property;
}

int main() {
    aether_backdrop_filter_list source{};
    source.items = static_cast<aether_backdrop_filter*>(std::calloc(2, sizeof(aether_backdrop_filter)));
    require(source.items, "filter-list allocation failed");
    source.count = 2;
    auto two_px = aether_css_length_zero(); two_px.value = 2.f;
    auto four_px = aether_css_length_zero(); four_px.value = 4.f;
    source.items[0] = {AETHER_BACKDROP_FILTER_BLUR, two_px, 0};
    source.items[1] = {AETHER_BACKDROP_FILTER_BLUR, four_px, 0};
    aether_backdrop_filter_list copied{};
    require(aether_backdrop_filter_list_copy(&source, &copied) == AETHER_TRUE && copied.items != source.items,
        "filter-list copy must own independent storage");
    require(aether_backdrop_filter_list_equal(&source, &copied) == AETHER_TRUE, "filter-list equality must compare contents");
    copied.items[1].length_amount.value = 9;
    require(aether_backdrop_filter_list_equal(&source, &copied) == AETHER_FALSE, "filter-list equality must detect a changed operation");
    aether_backdrop_filter_list_free(&source); aether_backdrop_filter_list_free(&copied);

    aether_doc_t doc = nullptr; aether_style_runtime_t runtime = nullptr; aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr; aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr; aether_display_list_t list = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(doc, "<body><div id='panel'></div><div id='webkit'></div><div id='invalid'></div></body>") == AETHER_OK,
        "document parsing failed");
    diagnostic_capture_s capture{};
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200, 100, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, diagnostic, &capture, &runtime) == AETHER_OK, "runtime creation failed");
    int64_t sheet = 0; int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "body{display:block;margin:0;background:#123456}"
        "#panel{display:block;width:100px;height:40px;background:rgba(255,255,255,.5);border-radius:8px;backdrop-filter:blur(2px) blur(4px)}"
        "#webkit{-webkit-backdrop-filter:blur(3px)}"
        "#invalid{backdrop-filter:brightness(1.2)}",
        0, "backdrop.css", &sheet, &rules) == AETHER_OK, "stylesheet creation failed");
    const auto body = aether_doc_get_body(doc);
    const auto panel = aether_doc_get_element_by_id(doc, "panel");
    const auto webkit = aether_doc_get_element_by_id(doc, "webkit");
    require(panel && webkit, "fixture element lookup failed");
    // Style updates are an incremental stream. Resolve once, then retain the
    // two styles needed by this test before destroying the update handles.
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
    require(update_count > 0 && updates, "initial style resolution failed");
    aether_computed_style_t panel_style{};
    aether_computed_style_t webkit_style{};
    bool found_panel = false;
    bool found_webkit = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK, "style update read failed");
        if (node == panel) {
            require(aether_computed_style_copy(handle, &panel_style) == AETHER_OK, "panel style copy failed");
            found_panel = true;
        } else if (node == webkit) {
            require(aether_computed_style_copy(handle, &webkit_style) == AETHER_OK, "webkit style copy failed");
            found_webkit = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, update_count);
    require(found_panel && found_webkit, "fixture styles missing from initial update stream");
    require(panel_style.backdrop_filters.count == 2 && panel_style.backdrop_filters.items,
        "two blur operations must survive cascade in declaration order");
    require(panel_style.backdrop_filters.items[0].kind == AETHER_BACKDROP_FILTER_BLUR &&
            std::fabs(panel_style.backdrop_filters.items[0].length_amount.value - 2.f) < .001f &&
            std::fabs(panel_style.backdrop_filters.items[1].length_amount.value - 4.f) < .001f,
        "backdrop-filter blur amounts were parsed incorrectly");
    aether_computed_style_free(&panel_style);
    require(webkit_style.backdrop_filters.count == 1 && std::fabs(webkit_style.backdrop_filters.items[0].length_amount.value - 3.f) < .001f,
        "-webkit-backdrop-filter alias was not cascaded");
    aether_computed_style_free(&webkit_style);
    require(capture.count == 1 && std::strcmp(capture.code, "AETHER-CSS-BACKDROP-FILTER") == 0 &&
            std::strcmp(capture.property, "backdrop-filter") == 0,
        "unsupported backdrop operation must report a dedicated diagnostic");

    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK, "style map creation failed");
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine creation failed");
    require(aether_layout_layout(engine, doc, styles, 200, 100, nullptr, nullptr, &layout) == AETHER_OK, "layout failed");
    require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "builder creation failed");
    require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK, "display-list build failed");
    int32_t backdrop_index = -1;
    for (int32_t index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, index, &command) == AETHER_OK, "command read failed");
        if (command.tag == AETHER_PAINT_APPLY_BACKDROP_FILTER) {
            backdrop_index = index;
            require(command.data.apply_backdrop_filter.filters.count == 2 &&
                    command.data.apply_backdrop_filter.radii.top_left_x > 0,
                "backdrop command must own filters and use the element clip radii");
        }
        aether_paint_command_free(&command);
    }
    require(backdrop_index >= 0, "backdrop-filter must emit an ApplyBackdropFilter command");
    aether_paint_command_t next{};
    require(aether_display_list_get_command(list, backdrop_index + 1, &next) == AETHER_OK &&
            next.tag == AETHER_PAINT_FILL_ROUNDED_RECT,
        "backdrop command must precede the element's own background fill");
    aether_paint_command_free(&next);

    aether_display_list_destroy(list); aether_display_list_builder_destroy(builder); aether_layout_result_destroy(layout);
    aether_layout_destroy(engine); aether_style_map_destroy(styles); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    return 0;
}
