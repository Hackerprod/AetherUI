#include <aether/aether.h>
#include "../src/css/style_map_types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// The resolved style map must own immutable storage for every stored style. A state
// recomputation that destroys the style runtime's owned buffers (historically
// any subtree re-resolve, reproduced here with a hover that changes the icon's
// computed style) must not orphan the map's entry for an image background.
int main() {
    aether_style_runtime_options_t options{};
    options.max_variable_depth = 32;
    options.max_variable_expansion_length = 65536;
    aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(
        &options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "style runtime creation failed");

    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(
        doc, "<body><div id=\"host\"><div id=\"icon\" style=\"background-image:url('asset://x')\"></div></div></body>") == AETHER_OK,
        "HTML parsing failed");

    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(
        runtime,
        "#host { width: 200px; } #icon:hover { background-color: #eef2f7; }",
        2, "style-map-lifetime.css", &sheet, &rule_count) == AETHER_OK && rule_count == 2,
        "stylesheet compilation failed");

    aether_style_update_t* updates = nullptr;
    auto update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0 && updates != nullptr, "initial style resolution failed");
    aether_style_updates_free(updates, update_count);

    aether_style_map_t map = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK && map != nullptr,
        "resolved style map creation failed");

    auto* icon = aether_doc_get_element_by_id(doc, "icon");
    require(icon != nullptr, "icon lookup failed");

    auto verify_icon_background = [&](const char* phase) {
        const auto* found = map->current->backing()->find(icon);
        require(found != nullptr, phase);
        const auto layers = found->style()->payload_owner()->background_layers();
        require(layers.size() == 1 &&
                    !layers.empty() &&
                    layers[0].view().image_source.c_str() != nullptr &&
                    std::strcmp(layers[0].view().image_source.c_str(), "asset://x") == 0,
            phase);
    };
    verify_icon_background("map entry missing the image layer before recompute");

    // Force a recomputation that REPLACES the icon's state: the hover rule
    // changes its computed style, so compute_node destroys the previous state
    // and its owned background buffers. The old borrowed-view map read freed
    // memory here; the deep-copied map entry must stay intact.
    aether_element_set_state(icon, AETHER_STATE_HOVER);
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "hover recompute produced no style updates");
    aether_style_updates_free(updates, update_count);

    verify_icon_background("map entry dangled after the icon state was replaced");

    // Update path: synchronize the map entry, then re-resolve so the runtime
    // state is replaced again. The map's owned copy must remain independent.
    require(aether_style_runtime_update_resolved_style_map(runtime, map, &icon, 1) == AETHER_OK,
        "resolved style map update failed");
    verify_icon_background("map entry lost the image layer after update");

    aether_element_set_state(icon, AETHER_STATE_NONE);
    updates = nullptr;
    update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0, "post-update recompute produced no style updates");
    aether_style_updates_free(updates, update_count);

    verify_icon_background("map entry no longer independent of the runtime state");

    // Destroying the map must free every owned copy without crashing after the
    // recomputes above.
    aether_style_map_destroy(map);

    aether_style_runtime_remove_stylesheet(runtime, sheet);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);

    printf("OK: style map lifetime test passed\n");
    return 0;
}
