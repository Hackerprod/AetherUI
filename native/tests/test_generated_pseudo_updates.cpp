#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    aether_doc_t document = nullptr;
    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(document, "<body><div id='hud'></div></body>") == AETHER_OK,
            "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{960, 720, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "runtime creation failed");

    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(
                runtime,
                "#hud::after { content: \"HUD\"; "
                "background: linear-gradient(90deg, #00f2ff, #ff0055), linear-gradient(0deg, #000, #111); "
                "transform: translate(-50%, -50%) rotate(45deg); "
                "box-shadow: 0 0 10px #00f2ff; text-shadow: 1px 1px 2px currentColor; }",
                2, "generated-pseudo.css", &sheet, nullptr) == AETHER_OK,
            "stylesheet compilation failed");

    auto* body = aether_doc_get_body(document);
    require(body != nullptr, "body lookup failed");
    for (int pass = 0; pass < 8; ++pass) {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, body, &updates);
        require(count >= (pass == 0 ? 2 : 1) && updates != nullptr,
                "pseudo-element updates were not emitted");

        bool found_dynamic_pseudo_style = false;
        for (int32_t index = 0; index < count; ++index) {
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[index], nullptr, &handle, nullptr) == AETHER_OK,
                    "style update access failed");
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK,
                    "style update snapshot could not be copied");
            if (style.transforms.count > 0) {
                found_dynamic_pseudo_style = true;
                require(style.background_layers.count == 2, "pseudo background layer list was not preserved");
                require(style.box_shadows.count == 1, "pseudo box shadow list was not preserved");
                require(style.text_shadows.count == 1, "pseudo text shadow list was not preserved");
            }
            aether_computed_style_free(&style);
            aether_computed_style_handle_destroy(handle);
        }
        require(found_dynamic_pseudo_style, "generated pseudo style was missing from the update list");
        aether_style_updates_free(updates, count);
    }

    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
