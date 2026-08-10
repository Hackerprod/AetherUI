// Regression: `background: var(--bg)` (a shorthand whose value references a custom
// property) must resolve per node and expand like any other shorthand. Shorthands
// with var() used to be dropped at compile time, leaving a transparent background.
#include <aether/aether.h>
#include "../src/css/style_map_types.h"

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
    require(aether_doc_parse_html(document,
        "<html><head><style>:root { --bg: linear-gradient(135deg, #667eea 0%, #764ba2 100%); } "
        "body { background: var(--bg); }</style></head><body><div>x</div></body></html>") == AETHER_OK,
        "document parse failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "style runtime creation failed");
    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "html, body { display: block; } body { margin: 8px; }",
        0, "defaults.css", &sheet, nullptr) == AETHER_OK,
        "defaults stylesheet failed");
    require(aether_style_runtime_add_stylesheet(runtime,
        ":root { --bg: linear-gradient(135deg, #667eea 0%, #764ba2 100%); } "
        "body { background: var(--bg); }",
        2, "shorthand-var.css", &sheet, nullptr) == AETHER_OK,
        "stylesheet compilation failed");

    aether_node_t body = aether_doc_get_body(document);
    require(body != nullptr, "body lookup failed");

    // Resolve from the document root so the :root rule's custom property is computed
    // on html and inherited by body — mirroring the host's full-document resolution.
    aether_style_update_t* updates = nullptr;
    require(aether_style_runtime_resolve_subtree(runtime, reinterpret_cast<aether_node_t>(document), &updates) >= 1,
            "resolve failed");
    aether_style_updates_free(updates, 1);

    aether_style_map_t map = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &map) == AETHER_OK,
            "resolved style map failed");
    const auto found = map->styles.find(body);
    require(found != map->styles.end(), "body has no resolved style");
    const auto& style = found->second;

    require(style.background_layers.count == 1, "background: var(--bg) must produce one background layer");
    require(style.background_layers.items != nullptr, "background layer list must be non-null");
    require(style.background_layers.items[0].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT,
            "resolved layer must be a linear gradient");
    require(style.background_layers.items[0].gradient.stop_count == 2,
            "gradient must keep the author's two stops");

    aether_style_map_destroy(map);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);

    std::printf("PASS  shorthand-with-var resolves to gradient\n");
    return 0;
}
