#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct resolved_styles {
    aether_doc_t document;
    aether_style_update_t* updates;
    int32_t update_count;
};

// Validator/apply consistency: the longhand validator delegates to the real
// background_layers parser, so a value is accepted only when the apply path
// can build a layer from it, and dropped otherwise. Each resolved style below
// asserts what the shared parser produced.
static aether_computed_style_handle_t find_style(const resolved_styles& resolved, const char* id) {
    aether_node_t element = aether_doc_get_element_by_id(resolved.document, id);
    require(element != nullptr, "fixture element lookup failed");
    aether_computed_style_handle_t style_handle = nullptr;
    for (int32_t index = 0; index < resolved.update_count; ++index) {
        aether_node_t updated = nullptr;
        aether_computed_style_handle_t candidate = nullptr;
        require(aether_style_runtime_get_update(resolved.updates[index], &updated, &candidate, nullptr) == AETHER_OK,
            "style update read failed");
        if (updated == element) style_handle = candidate;
        else aether_computed_style_handle_destroy(candidate);
    }
    require(style_handle != nullptr, "fixture element style update missing");
    return style_handle;
}

static void check_resolved_style(const resolved_styles& resolved, const char* id,
                                 bool expected_has_layer, aether_background_layer_kind_t expected_kind) {
    aether_computed_style_handle_t style_handle = find_style(resolved, id);
    aether_computed_style_t copied_style{};
    require(aether_computed_style_copy(style_handle, &copied_style) == AETHER_OK,
        "computed-style copy failed");
    aether_computed_style_handle_destroy(style_handle);
    if (expected_has_layer) {
        require(copied_style.background_layers.count == 1 && copied_style.background_layers.items,
            "expected exactly one background layer");
        require(copied_style.background_layers.items[0].kind == expected_kind,
            "background layer kind mismatch");
    } else {
        require(copied_style.background_layers.count == 0,
            "garbage background-image value must not produce a background layer");
    }
    aether_computed_style_free(&copied_style);
}

int main() {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;

    require(aether_doc_create(&document) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(
        document,
        "<body>"
        "<div id=\"image\" style=\"display:block; background-image: url('asset://image-a')\"></div>"
        "<div id=\"gradient\" style=\"display:block; background-image: linear-gradient(135deg, #667eea, #764ba2)\"></div>"
        "<div id=\"garbage\" style=\"display:block; background-image: banana\"></div>"
        "</body>") == AETHER_OK,
        "document parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");

    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    require(update_count > 0, "style resolution failed");
    resolved_styles resolved{document, updates, update_count};

    check_resolved_style(resolved, "image", true, AETHER_BACKGROUND_LAYER_IMAGE_URL);
    check_resolved_style(resolved, "gradient", true, AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT);
    check_resolved_style(resolved, "garbage", false, AETHER_BACKGROUND_LAYER_IMAGE_URL);

    aether_style_updates_free(updates, update_count);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
