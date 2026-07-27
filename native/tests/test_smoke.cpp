#include <aether/aether.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct diagnostic_capture_s {
    int count = 0;
    const char* code = nullptr;
    const char* property = nullptr;
    const char* value = nullptr;
};

static void capture_diagnostic(const char* code, const char* property, const char* value,
                               const char*, void* user_data) {
    auto* capture = static_cast<diagnostic_capture_s*>(user_data);
    ++capture->count;
    capture->code = code;
    capture->property = property;
    capture->value = value;
}

int main() {
    const auto abi_version = aether_abi_version();
    require(abi_version == AETHER_ABI_VERSION_CURRENT,
        "runtime ABI version differs from the public header");
    require(AETHER_ABI_VERSION_GET_MAJOR(abi_version) == AETHER_ABI_VERSION_MAJOR &&
            AETHER_ABI_VERSION_GET_MINOR(abi_version) == AETHER_ABI_VERSION_MINOR &&
            AETHER_ABI_VERSION_GET_PATCH(abi_version) == AETHER_ABI_VERSION_PATCH,
        "ABI version packing is incorrect");

    constexpr aether_capabilities_t required_capabilities =
        AETHER_CAPABILITY_DOM |
        AETHER_CAPABILITY_STYLE_RUNTIME |
        AETHER_CAPABILITY_LAYOUT |
        AETHER_CAPABILITY_DISPLAY_LIST;
    require((aether_abi_capabilities() & required_capabilities) == required_capabilities,
        "runtime does not advertise all compiled core capabilities");
    require(aether_paint_command_size() == sizeof(aether_paint_command_t),
        "runtime paint-command size differs from the public header");


    const char* msg = aether_status_message(AETHER_OK);
    if (!msg) {
        fprintf(stderr, "FAIL: aether_status_message returned null\n");
        return 1;
    }

    aether_doc_t doc = nullptr;
    aether_status_t s = aether_doc_create(&doc);
    if (s != AETHER_OK || !doc) {
        fprintf(stderr, "FAIL: aether_doc_create returned %d\n", s);
        return 1;
    }
    require(aether_doc_parse_html(
        doc, "<body><button id=\"send\" style=\"width: 180px\">Send</button></body>") == AETHER_OK,
        "HTML parsing failed");
    auto button = aether_doc_get_element_by_id(doc, "send");
    require(button != nullptr, "button lookup failed");
    auto inline_style = aether_element_get_attribute(button, "style");
    require(inline_style != nullptr, "inline style attribute missing");
    aether_string_free(inline_style);

    // Reusing a document must replace its tree. This is exercised by the managed
    // native-style bridge on every component markup rebuild.
    require(aether_doc_parse_html(doc, "<body><div id=\"replacement\">New</div></body>") == AETHER_OK,
        "HTML replacement parsing failed");
    require(aether_doc_get_element_by_id(doc, "send") == nullptr,
        "old nodes survived replacement parsing");
    require(aether_doc_get_element_by_id(doc, "replacement") != nullptr,
        "replacement tree was not parsed");
    require(aether_doc_parse_html(
        doc, "<body><button id=\"send\" style=\"width: 180px\">Send</button></body>") == AETHER_OK,
        "HTML restoration parsing failed");
    button = aether_doc_get_element_by_id(doc, "send");
    require(button != nullptr, "restored button lookup failed");

    aether_style_runtime_options_t options{};
    options.max_variable_depth = 32;
    options.max_variable_expansion_length = 65536;
    aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(
        &options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");

    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(
        runtime,
        "body { --accent: red; } button { color: var(--accent); background-color: white; "
        "background: linear-gradient(45deg, #0f172a, #1e293b, #334155); } "
        "#send:hover { color: blue; }",
        2, "smoke.css", &sheet, &rule_count) == AETHER_OK && rule_count == 3,
        "stylesheet compilation failed");

    aether_style_update_t* updates = nullptr;
    auto update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count == 2 && updates != nullptr, "initial style resolution failed");
    aether_computed_style_handle_t style_handle = nullptr;
    require(aether_style_runtime_get_update(
        updates[1], nullptr, &style_handle, nullptr) == AETHER_OK,
        "style update access failed");
    aether_computed_style_t style{};
    require(aether_computed_style_copy(style_handle, &style) == AETHER_OK,
        "computed style copy failed");
    require(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == 180,
        "inline style did not win cascade");
    require(style.color.r > style.color.b, "custom property did not resolve to red");
    require(style.background_layers.count == 1 && style.background_layers.items &&
            style.background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT &&
            style.background_layers.items[0].gradient.stop_count == 3,
        "numeric-angle background shorthand gradient was not parsed");
    require(std::fabs(style.background_layers.items[0].gradient.direction_x - 0.70710678f) < 0.001f &&
            std::fabs(style.background_layers.items[0].gradient.direction_y + 0.70710678f) < 0.001f,
        "numeric-angle gradient direction is incorrect");
    aether_computed_style_free(&style);
    aether_computed_style_handle_destroy(style_handle);
    aether_style_updates_free(updates, update_count);

    aether_element_set_state(button, AETHER_STATE_HOVER);
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count == 1, "hover mutation produced no style update");
    require(aether_style_runtime_get_update(
        updates[0], nullptr, &style_handle, nullptr) == AETHER_OK,
        "hover update access failed");
    require(aether_computed_style_copy(style_handle, &style) == AETHER_OK,
        "hover style copy failed");
    require(style.color.b > style.color.r, "hover selector did not win cascade");
    aether_computed_style_free(&style);
    aether_computed_style_handle_destroy(style_handle);
    aether_style_updates_free(updates, update_count);

    int64_t image_sheet = 0;
    require(aether_style_runtime_add_stylesheet(
        runtime, "#send { background: url('asset://mountain') center / cover no-repeat; }",
        2, "background-image.css", &image_sheet, &rule_count) == AETHER_OK && rule_count == 1,
        "background image stylesheet compilation failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count == 2, "background image stylesheet did not resolve the subtree");
    aether_node_t updated_node = nullptr;
    style_handle = nullptr;
    for (int32_t i = 0; i < update_count; ++i) {
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(updates[i], &updated_node, &candidate, nullptr) == AETHER_OK,
            "background image style update access failed");
        if (updated_node == button) style_handle = candidate;
        else aether_computed_style_handle_destroy(candidate);
    }
    require(style_handle != nullptr, "background image fixture did not update its button");
    require(aether_computed_style_copy(style_handle, &style) == AETHER_OK,
        "background image computed-style copy failed");
    require(style.background_layers.count == 1 && style.background_layers.items &&
                style.background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_IMAGE_URL &&
                std::strcmp(style.background_layers.items[0].image_source, "asset://mountain") == 0,
        "background shorthand did not retain the URL layer source");
    const auto& image_layer = style.background_layers.items[0];
    require(image_layer.position_x.unit == AETHER_CSS_UNIT_PERCENT && image_layer.position_x.value == 50.f &&
                image_layer.position_y.unit == AETHER_CSS_UNIT_PERCENT && image_layer.position_y.value == 50.f &&
                image_layer.size_kind == AETHER_BACKGROUND_SIZE_COVER &&
                image_layer.repeat_x == AETHER_BACKGROUND_NO_REPEAT && image_layer.repeat_y == AETHER_BACKGROUND_NO_REPEAT,
        "background shorthand did not preserve position, size, and repeat semantics");
    aether_computed_style_free(&style);
    aether_computed_style_handle_destroy(style_handle);
    aether_style_updates_free(updates, update_count);

    require(aether_style_runtime_remove_stylesheet(runtime, image_sheet) == AETHER_TRUE,
        "background shorthand stylesheet removal failed");

    int64_t cycle_sheet = 0;
    require(aether_style_runtime_add_stylesheet(
        runtime,
        "#send { background-image: url('asset://a'), url('asset://b'), url('asset://c'); "
        "background-repeat: repeat-x, no-repeat; }",
        2, "background-cycle.css", &cycle_sheet, &rule_count) == AETHER_OK && rule_count == 1,
        "background list-cycling stylesheet compilation failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count == 2, "background list-cycling stylesheet did not resolve the subtree");
    style_handle = nullptr;
    for (int32_t i = 0; i < update_count; ++i) {
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(updates[i], &updated_node, &candidate, nullptr) == AETHER_OK,
            "background list-cycling style update access failed");
        if (updated_node == button) style_handle = candidate;
        else aether_computed_style_handle_destroy(candidate);
    }
    require(style_handle != nullptr, "background list-cycling fixture did not update its button");
    require(aether_computed_style_copy(style_handle, &style) == AETHER_OK,
        "background list-cycling computed-style copy failed");
    require(style.background_layers.count == 3 && style.background_layers.items[2].repeat_x == AETHER_BACKGROUND_REPEAT &&
                style.background_layers.items[2].repeat_y == AETHER_BACKGROUND_NO_REPEAT,
        "background list values must cycle to the first repeat value instead of clamping to the last");
    aether_computed_style_free(&style);
    aether_computed_style_handle_destroy(style_handle);
    aether_style_updates_free(updates, update_count);

    diagnostic_capture_s diagnostics{};
    aether_style_runtime_set_diagnostics_callback(runtime, capture_diagnostic, &diagnostics);
    require(aether_doc_parse_html(
        doc, "<body><div id=\"bad-gradient\" style=\"background-image: linear-gradient(to right, red 10px, blue 90%)\"></div></body>") == AETHER_OK,
        "invalid gradient fixture parsing failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "invalid gradient fixture style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(diagnostics.count == 1, "unsupported gradient stop must emit exactly one diagnostic");
    require(diagnostics.code && std::strcmp(diagnostics.code, "AETHER-CSS-GRADIENT") == 0,
        "unsupported gradient stop diagnostic code is incorrect");
    require(diagnostics.property && std::strcmp(diagnostics.property, "background-image") == 0,
        "gradient diagnostic must identify the longhand property");
    require(diagnostics.value && std::strstr(diagnostics.value, "10px"),
        "gradient diagnostic must retain the unsupported source value");

    diagnostics = {};
    require(aether_doc_parse_html(
        doc, "<body><div style=\"background-image: url(\"></div></body>") == AETHER_OK,
        "invalid URL fixture parsing failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    aether_style_updates_free(updates, update_count);
    require(diagnostics.count == 1 && diagnostics.code &&
                std::strcmp(diagnostics.code, "AETHER-CSS-BACKGROUND-IMAGE-URL") == 0,
        "malformed url() must report its dedicated background-image diagnostic");

    diagnostics = {};
    require(aether_doc_parse_html(
        doc, "<body><div style=\"background-image: linear-gradient(to bottom left, red, blue)\"></div></body>") == AETHER_OK,
        "corner-gradient fixture parsing failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    aether_style_updates_free(updates, update_count);
    require(diagnostics.count == 1 && diagnostics.code &&
                std::strcmp(diagnostics.code, "AETHER-CSS-GRADIENT-APPROXIMATION") == 0,
        "corner keyword gradient must report the current fixed-vector approximation");

    require(aether_doc_parse_html(
        doc, "<body><div>first<br id=\"line-break\">second</div></body>") == AETHER_OK,
        "line-break fixture parsing failed");
    auto* line_break = aether_doc_get_element_by_id(doc, "line-break");
    require(line_break != nullptr, "line-break fixture lookup failed");
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    bool found_line_break = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t updated_style = nullptr;
        require(aether_style_runtime_get_update(updates[index], &updated, &updated_style, nullptr) == AETHER_OK,
            "line-break style update access failed");
        if (updated == line_break) {
            aether_computed_style_t copied{};
            require(aether_computed_style_copy(updated_style, &copied) == AETHER_OK,
                "line-break computed-style copy failed");
            require(copied.display == AETHER_DISPLAY_LINE_BREAK,
                "unstyled br must use its native line-break display semantic");
            aether_computed_style_free(&copied);
            found_line_break = true;
        }
        aether_computed_style_handle_destroy(updated_style);
    }
    aether_style_updates_free(updates, update_count);
    require(found_line_break, "line-break fixture did not produce a style update");

    require(aether_doc_parse_html(
        doc,
        "<body>"
        "<div id=\"align-start\" style=\"text-align:start\">start</div>"
        "<div id=\"align-end\" style=\"text-align:end\">end</div>"
        "<div id=\"align-left\" style=\"text-align:left\">left</div>"
        "<div id=\"align-right\" style=\"text-align:right\">right</div>"
        "<div id=\"align-center\" style=\"text-align:center\">center</div>"
        "<div id=\"align-justify\" style=\"text-align:justify\">justify</div>"
        "</body>") == AETHER_OK,
        "text-align fixture parsing failed");
    const char* align_ids[] = {
        "align-start", "align-end", "align-left", "align-right", "align-center", "align-justify"
    };
    const int32_t expected_alignments[] = {
        AETHER_TEXT_ALIGN_START, AETHER_TEXT_ALIGN_END, AETHER_TEXT_ALIGN_LEFT,
        AETHER_TEXT_ALIGN_RIGHT, AETHER_TEXT_ALIGN_CENTER, AETHER_TEXT_ALIGN_JUSTIFY
    };
    aether_node_t align_nodes[6]{};
    for (int index = 0; index < 6; ++index) {
        align_nodes[index] = aether_doc_get_element_by_id(doc, align_ids[index]);
        require(align_nodes[index] != nullptr, "text-align fixture lookup failed");
    }
    // StyleRuntimeInterop invalidates this cache whenever it loads replacement
    // markup. The standalone ABI exposes the same explicit lifecycle operation.
    aether_style_runtime_clear_cache(runtime);
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    int32_t resolved_alignments[6] = { -1, -1, -1, -1, -1, -1 };
    for (int32_t update_index = 0; update_index < update_count; ++update_index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t updated_style = nullptr;
        require(aether_style_runtime_get_update(updates[update_index], &updated, &updated_style, nullptr) == AETHER_OK,
            "text-align style update access failed");
        for (int align_index = 0; align_index < 6; ++align_index) {
            if (updated != align_nodes[align_index]) continue;
            aether_computed_style_t copied{};
            require(aether_computed_style_copy(updated_style, &copied) == AETHER_OK,
                "text-align computed-style copy failed");
            resolved_alignments[align_index] = copied.text_align;
            aether_computed_style_free(&copied);
        }
        aether_computed_style_handle_destroy(updated_style);
    }
    aether_style_updates_free(updates, update_count);
    for (int index = 0; index < 6; ++index) {
        require(resolved_alignments[index] == expected_alignments[index],
            "text-align keyword did not preserve its computed semantic value");
        for (int other = 0; other < index; ++other) {
            require(resolved_alignments[index] != resolved_alignments[other],
                "distinct text-align keywords collapsed during cascade");
        }
    }

    require(aether_style_runtime_remove_stylesheet(runtime, sheet) == AETHER_TRUE,
        "stylesheet removal failed");
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);

    printf("OK: smoke test passed\n");
    return 0;
}
