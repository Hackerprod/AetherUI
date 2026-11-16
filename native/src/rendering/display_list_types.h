#pragma once
#include <aether/aether.h>
#include "../animations/transition_runtime.h"
#include "../layout/layout_types.h"
#include <vector>
#include <unordered_set>
#include "border_groups.h"
#include "native_paint_list.h"

namespace aether::layout { class StyleOperationPin; }

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

namespace aether::rendering {class NativeViewportAuthority;class DocumentViewportAuthority;class DocumentShadowPlan;class DocumentScopePlan;}
struct aether_display_list_builder_s {
    const aether::rendering::NativeViewportAuthority* native_viewport_capture = nullptr;
    const aether::rendering::DocumentViewportAuthority* native_document_capture = nullptr;
    const aether::rendering::DocumentShadowPlan* native_shadow_plan = nullptr;
    std::vector<aether::rendering::NativeShadowEntry>* native_shadows = nullptr;
    bool native_document_checked = false;
    const aether::rendering::DocumentScopePlan* native_scope_plan = nullptr;
    std::vector<aether::rendering::NativeDocumentScopeEntry>* native_scopes = nullptr;
    std::size_t native_image_source_bytes = 0;
    const aether::rendering::NativeImageCatalog* native_image_catalog = nullptr;
    std::vector<aether::rendering::NativeImageEntry>* native_images = nullptr;
    aether_display_list_s* native_gradient_output = nullptr;
    std::vector<aether::rendering::OpacityEvent>* opacity_events = nullptr;
    std::vector<aether::rendering::NativeTextEntry>* native_text_runs = nullptr;
    std::size_t native_scroll_depth = 0;
    std::vector<std::uint64_t> opacity_parents;
    std::uint64_t opacity_serial = 0;
    std::vector<aether_border_group_range>* border_groups = nullptr;
    aether_text_measurer_contextual_fn contextual_text_measurer = nullptr;
    void* contextual_text_measurer_data = nullptr;
    aether_text_measurer_fn text_measurer = nullptr;
    void*                   text_measurer_data = nullptr;
    aether_image_dimensions_fn image_dimensions = nullptr;
    void*                   image_dimensions_data = nullptr;
    aether_sizef            viewport{};
    float                   root_font_size = 16.f;
    const aether::layout::StyleOperationPin* style_pin = nullptr;
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
    // Identity follows the command-owned allocation, not a shifting vector index.
    std::unordered_set<const aether_gradient_stop*> native_srgb_gradients;
    bool native_gradient_unproven = false;
    std::size_t native_gradient_stop_count = 0;
    std::vector<aether_border_group_range> border_groups;

    ~aether_display_list_s() {
        for (auto& cmd : commands) {
            aether_paint_command_free(&cmd);
        }
    }
};
