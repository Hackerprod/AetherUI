/* ── Layout engine internal API ──────────────────────────────────────
   Shared declarations across the layout_*.cpp translation units.
   PRIVATE: never installed, never part of the public ABI. */

#pragma once

#include "layout_types.h"
#include "scrollbar_geometry.h"
#include "../abi/aether_abi.h"
#include "../dom/dom_types.h"
#include "../css/css_types.h"
#include "../css/border_utils.h"
#include "../css/text_transform.h"
#include "box_model.h"
#include "form_controls.h"
#include "inline_text.h"
#include "flex_resolver.h"
#include "intrinsic_sizing.h"
#include "margin_adjacency.h"
#include "text_justify.h"
#include "table_layout.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

/* Internal to the layout engine. */
class detached_layout_scope_s {
public:
    explicit detached_layout_scope_s(aether_layout_result_s* result)
        : result_(result), previous_(result->records_boxes) {
        result_->records_boxes = false;
    }

    ~detached_layout_scope_s() { result_->records_boxes = previous_; }

    detached_layout_scope_s(const detached_layout_scope_s&) = delete;
    detached_layout_scope_s& operator=(const detached_layout_scope_s&) = delete;

private:
    aether_layout_result_s* result_;
    bool previous_;
};

/* Entry points and cross-file helpers. Definitions live in the concern
   modules; linkage is ordinary external (hidden by the shared-library
   visibility preset). */

// Available space remains numeric; percentage heights preserve initial definiteness.
using percentage_height_s = std::optional<float>;
enum class forced_height_kind_e { none, definite, indefinite };
bool length_depends_on_percentage(aether_css_length value);

// A content measurement changes sizing inputs only for this root and physical
// axis. Never mutate the stored style or inherit the exception into children.
// The same root may itself be a flex row, where height is its cross axis.
enum class intrinsic_axis_e { width, height };
struct intrinsic_measurement_s {
    const aether_node_s* root;
    intrinsic_axis_e axis;

    bool ignores(const aether_node_s* node, intrinsic_axis_e physical_axis) const {
        return root == node && axis == physical_axis;
    }
};

// A row-owned continuation, never an owner or a cross-layout cache. Natural
// in-flow children remain attached; only the completion tail is deferred.
struct element_completion_s {
    aether_layout_box_s* box = nullptr;
    aether_thicknessf margin{}, padding{}, border{};
    aether_rectf containing_block{};
    percentage_height_s containing_height;
};
void layout_complete_element(const aether_layout_engine_s* engine,
    const StyleLookup* styles, const element_completion_s& completion,
    aether_rectf final_border, aether_layout_result_s* result,
    bool table_allocation = false);
void layout_recompute_saved_scroll(aether_layout_box_s* box);

aether_layout_box_s* layout_element(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    aether_rectf containing_block,
    percentage_height_s containing_height,
    float normal_flow_y,
    bool force_available_width,
    float forced_border_width,
    bool has_forced_border_width,
    float forced_border_height,
    forced_height_kind_e forced_height_kind,
    bool is_independent_layout_item,
    aether_layout_result_s* result,
    /* CSS Overflow 3.3: propagated viewport overflow used ONLY for the root
       box's scroll metrics. Null for every non-root element; the root's own
       computed overflow stays untouched (paint keeps the used value). */
    const int32_t* viewport_overflow_override = nullptr,
    element_completion_s* deferred_completion = nullptr,
    const intrinsic_measurement_s* intrinsic_measurement = nullptr);

float layout_flow_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result);

float layout_inline_items(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width,
    float available_height, percentage_height_s containing_height,
    const BoxStyleSnapshot& parent_style,
    aether_layout_result_s* result);

// Shared min/max-height resolution. reference is the containing block's
// semantic percentage height, never a flex container's numeric auto minimum.
float clamp_border_height(const aether_layout_engine_s* engine,
    const BoxStyleSnapshot& style, float height, float reference, float decorations,
    bool semantic_reference, aether_node_s* element);

// Keep the content-box round trip ordered: subtract/add padding and border
// separately. Grouping decorations changes observable float geometry/scroll.
inline float explicit_height_from_forced_border(const BoxStyleSnapshot& style,
    float height, float padding, float border) {
    return style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
        ? std::max(0.f, height - padding - border) : height;
}

inline float border_height_from_explicit(const BoxStyleSnapshot& style,
    float height, float padding, float border) {
    return style.box_sizing == AETHER_BOX_SIZING_CONTENT_BOX
        ? height + padding + border : height;
}

float layout_flex_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    percentage_height_s containing_height,
    const BoxStyleSnapshot& style,
    bool has_definite_height,
    percentage_height_s container_height_reference,
    float height_decorations,
    aether_layout_result_s* result,
    const intrinsic_measurement_s* intrinsic_measurement = nullptr);

float layout_table_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* table,
    aether_rectf content,
    percentage_height_s containing_height,
    aether_layout_result_s* result);

// Positioning block owns sizing/insets; final parent content owns flex static alignment.
void layout_positioned_children(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf positioning_block,
    aether_rectf parent_content_box,
    aether_layout_result_s* result, aether_node_s* only_node = nullptr);
void layout_finalize_fixed_containers(const aether_layout_engine_s* engine,
    const StyleLookup* styles, aether_layout_result_s* result);

// A call-local pair, never retained in a result or style snapshot. Only requested
// members are valid. Request both when a wrapping maximum needs the independent
// natural minimum; descendants and text tokens are then measured in one walk.
struct intrinsic_width_request_s {
    bool minimum;
    bool maximum;
};
struct intrinsic_widths_s {
    float min_content = 0.f;
    float max_content = 0.f;
};
intrinsic_widths_s measure_intrinsic_widths(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* element, const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border, float maximum,
    aether_layout_result_s* result, intrinsic_width_request_s request);

float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode = aether_intrinsic_size_mode_e::max_content);

float measure_intrinsic_height(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_layout_result_s* result,
    aether_node_s* element,
    const BoxStyleSnapshot& style,
    aether_thicknessf padding, aether_thicknessf border,
    float available_width);

// Only the measured child's own percentage edges use this context. Recursing
// into its intrinsic content always measures new intrinsic contributions.
enum class child_width_context_e { intrinsic_contribution, inline_height };

bool flex_automatic_minimum_is_zero_for_overflow(int32_t overflow);

float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode,
    child_width_context_e context);

intrinsic_widths_s measure_child_natural_widths(
    const aether_layout_engine_s* engine, const StyleLookup* styles,
    aether_node_s* child, float maximum, aether_layout_result_s* result,
    intrinsic_width_request_s request, child_width_context_e context);

intrinsic_widths_s measure_intrinsic_table_widths(
    const aether_layout_engine_s* engine,
    const StyleLookup* styles,
    aether_node_s* table,
    float maximum,
    aether_layout_result_s* result);

BoxStyleSnapshot get_style(const StyleLookup* styles, aether_node_s* el);

float resolve(const aether_layout_engine_s* e,
              aether_css_length value, float reference,
              const BoxStyleSnapshot& style);

aether_thicknessf resolve_padding_edges(const aether_layout_engine_s* e,
                                        aether_css_edges edges, float reference,
                                        const BoxStyleSnapshot& style);

aether_thicknessf resolve_margin_edges(const aether_layout_engine_s* e,
                                       aether_css_edges edges, float reference,
                                       const BoxStyleSnapshot& style);

aether_thicknessf resolve_border_edges(const aether_layout_engine_s* e,
                                       const BoxStyleSnapshot& style,
                                       float reference);

// Copies in the current parent coordinate space, excluding only the box's own inset.
aether_rectf normal_flow_border_box(const aether_layout_box_s* box);
aether_rectf normal_flow_margin_box(const aether_layout_box_s* box);

// Logical placement always moves the requested root. Prepared results filter
// definite axes at descendant ELEMENT fixed barriers outside a fixed container
// local to this moved subtree. Auto axes retain their static-position delta.
// Legacy results retain full propagation; relative visual offsets are separate.
void offset_box(aether_layout_box_s* box, float dx, float dy);

bool is_auto_margin(aether_css_length value);

bool is_out_of_flow(const BoxStyleSnapshot& style);

bool aether_overflow_is_scroll_container(int32_t overflow);
bool aether_overflow_allows_user_scroll(int32_t overflow);
bool aether_overflow_is_clip(int32_t overflow);

void accumulate_scrollable_descendant_extent(const aether_layout_box_s* parent,
                                             bool include_x,
                                             bool include_y,
                                             float& right,
                                             float& bottom);

const std::vector<aether_node_s*>& collect_flex_items(
    const StyleLookup* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const BoxStyleSnapshot& parent_style);

void register_box(aether_layout_result_s* result, aether_layout_box_s* box);

int32_t flex_cross_alignment(const BoxStyleSnapshot& container,
                             const BoxStyleSnapshot& item);

inline std::optional<aether::style::MeasuresReadView> layout_measures(const StyleLookup* styles, aether_node_s* element) {
    return styles ? styles->lookup_measures(element) : std::nullopt;
}
inline bool layout_measure_has_percentage(const std::optional<aether::style::MeasuresReadView>& measures,
    const std::string& property, aether_css_length value) {
    return aether::style::measure_has_percentage(measures ? &*measures : nullptr, property, value);
}
inline float layout_used_line_height(const aether_layout_engine_s* engine, aether_node_s* element,
                                    const BoxStyleSnapshot& style) {
    const auto measures = layout_measures(engine ? engine->measure_styles : nullptr, element);
    const float used = aether::style::measure_used_line_height(measures ? &*measures : nullptr, style.scalars());
    if (!std::isfinite(used)) throw std::overflow_error("Non-finite used line height");
    return used;
}
inline float layout_used_gap(const StyleLookup* styles, aether_node_s* element,
    const BoxStyleSnapshot& style, bool row, float basis, bool definite) {
    const auto measures = layout_measures(styles, element);
    if (!measures) return std::max(0.f, style.gap);
    const auto value = row ? measures->row_gap() : measures->column_gap();
    auto result = measure_resolve_used_length(value, basis, definite,
        layout_measure_has_percentage(measures, row ? "row-gap" : "column-gap", value));
    if (result.kind == measure_used_kind_e::deferred_percentage)
        result = measure_resolve_used_length(value, 0.f, true);
    if (result.kind == measure_used_kind_e::invalid) throw std::overflow_error("Invalid used gap");
    return result.kind == measure_used_kind_e::resolved ? std::max(0.f, result.px) : 0.f;
}
