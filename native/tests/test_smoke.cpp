#include <aether/aether.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
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
    require(style.has_background_gradient == AETHER_TRUE &&
            style.background_gradient.stop_count == 3,
        "numeric-angle background shorthand gradient was not parsed");
    require(std::fabs(style.background_gradient.direction_x - 0.70710678f) < 0.001f &&
            std::fabs(style.background_gradient.direction_y + 0.70710678f) < 0.001f,
        "numeric-angle gradient direction is incorrect");
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
    aether_computed_style_handle_destroy(style_handle);
    aether_style_updates_free(updates, update_count);

    require(aether_style_runtime_remove_stylesheet(runtime, sheet) == AETHER_TRUE,
        "stylesheet removal failed");
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);

    printf("OK: smoke test passed\n");
    return 0;
}
