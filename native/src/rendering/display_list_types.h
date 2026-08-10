#pragma once
#include <aether/aether.h>
#include "../animations/transition_runtime.h"
#include "../layout/layout_types.h"
#include <vector>

/* One per-element paint context flowing through the builder. The struct is the
   single source of truth for "what opacity does this box paint at", including
   live transition overrides and the accumulated ancestor opacity. */
struct paint_context_s {
    float effective_opacity = 1.0f;   /* own opacity x inherited (for THIS box's paint) */
    float inherited_opacity = 1.0f;   /* accumulated ancestor opacity (for children) */
    bool has_transform_override = false;
    aether_transform_matrix transform_override{};  /* resolved override or matrix */
    bool has_transform = false;                    /* whether a transform bracket must be pushed */
};

struct aether_display_list_builder_s {
    aether_text_measurer_fn text_measurer = nullptr;
    void*                   text_measurer_data = nullptr;
    aether_image_dimensions_fn image_dimensions = nullptr;
    void*                   image_dimensions_data = nullptr;
    aether_sizef            viewport{};
    float                   root_font_size = 16.f;
    const aether_style_map_s* current_style_map = nullptr;
    // When body supplies the CSS canvas background, its ordinary box must not
    // paint that same background a second time.
    const aether_node_s*    canvas_background_source = nullptr;
    // Live transition overrides. Null disables transitions entirely; the
    // builder does not own this handle.
    aether_transition_runtime_t transition_runtime = nullptr;
    // Borrowed layout result used to resolve the viewport-propagated overflow
    // of the root box (CSS Overflow 3.3). Never owned by the builder.
    const aether_layout_result_s* layout = nullptr;
};

struct aether_display_list_s {
    std::vector<aether_paint_command_t> commands;

    ~aether_display_list_s() {
        for (auto& cmd : commands) {
            aether_paint_command_free(&cmd);
        }
    }
};
