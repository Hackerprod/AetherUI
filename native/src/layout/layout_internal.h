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

aether_layout_box_s* layout_element(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    aether_rectf containing_block,
    float normal_flow_y,
    bool force_available_width,
    float forced_border_width,
    bool has_forced_border_width,
    float forced_border_height,
    bool has_forced_border_height,
    bool is_independent_layout_item,
    aether_layout_result_s* result,
    /* CSS Overflow 3.3: propagated viewport overflow used ONLY for the root
       box's scroll metrics. Null for every non-root element; the root's own
       computed overflow stays untouched (paint keeps the used value). */
    const int32_t* viewport_overflow_override = nullptr);

float layout_flow_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result);

float layout_inline_items(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    const std::vector<inline_item_s>& items,
    float start_x, float start_y, float available_width,
    const aether_computed_style_t& parent_style,
    aether_layout_result_s* result);

float layout_flex_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf content,
    const aether_computed_style_t& style,
    bool has_definite_height,
    aether_layout_result_s* result);

float layout_table_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* table,
    aether_rectf content,
    aether_layout_result_s* result);

void layout_positioned_children(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_layout_box_s* parent_box,
    aether_node_s* parent,
    aether_rectf positioning_block,
    aether_layout_result_s* result);

float measure_intrinsic_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode = aether_intrinsic_size_mode_e::max_content);

float measure_intrinsic_height(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* element,
    const aether_computed_style_t& style,
    aether_thicknessf padding, aether_thicknessf border,
    float available_width);

float measure_child_natural_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* child, float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

float measure_intrinsic_table_width(
    const aether_layout_engine_s* engine,
    aether_style_map_s* styles,
    aether_node_s* table,
    float maximum,
    aether_layout_result_s* result,
    aether_intrinsic_size_mode_e mode);

aether_computed_style_t get_style(aether_style_map_s* styles, aether_node_s* el);

float resolve(const aether_layout_engine_s* e,
              aether_css_length value, float reference,
              const aether_computed_style_t& style);

aether_thicknessf resolve_padding_edges(const aether_layout_engine_s* e,
                                        aether_css_edges edges, float reference,
                                        const aether_computed_style_t& style);

aether_thicknessf resolve_margin_edges(const aether_layout_engine_s* e,
                                       aether_css_edges edges, float reference,
                                       const aether_computed_style_t& style);

aether_thicknessf resolve_border_edges(const aether_layout_engine_s* e,
                                       const aether_computed_style_t& style,
                                       float reference);

void offset_box(aether_layout_box_s* box, float dx, float dy);

bool is_auto_margin(aether_css_length value);

bool is_out_of_flow(const aether_computed_style_t& style);

bool aether_overflow_is_scroll_container(int32_t overflow);
bool aether_overflow_allows_user_scroll(int32_t overflow);
bool aether_overflow_is_clip(int32_t overflow);

void accumulate_scrollable_descendant_extent(const aether_layout_box_s* parent,
                                             bool include_x,
                                             bool include_y,
                                             float& right,
                                             float& bottom);

const std::vector<aether_node_s*>& collect_flex_items(
    aether_style_map_s* styles,
    aether_layout_result_s* result,
    aether_node_s* parent,
    const aether_computed_style_t& parent_style);

void collect_text_items(aether_node_s* owner,
                        const aether_computed_style_t& style,
                        const std::string& text,
                        std::vector<inline_item_s>& output);

void register_box(aether_layout_result_s* result, aether_layout_box_s* box);

int32_t flex_cross_alignment(const aether_computed_style_t& container,
                             const aether_computed_style_t& item);
