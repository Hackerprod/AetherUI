#pragma once

#include <aether/aether.h>

#include <cmath>
#include <cstddef>
#include <vector>

/* Pure Flexbox main-axis resolution. The layout tree supplies intrinsic and
   specified sizes; this module owns the §9.7 arithmetic and never accesses
   DOM, layout boxes, or styles directly. Bases, constraints and targets use
   border-box sizes; decoration and signed margins are separate inputs. The
   inner base used for shrink weights excludes decoration and may be negative
   before the initial freeze. NaN represents an unavailable/indefinite input
   where noted. This is a private contract, not part of the public C ABI. */

struct aether_flex_basis_resolution_input_s {
    int32_t kind = AETHER_FLEX_BASIS_AUTO;
    float specified_main_size = NAN;
    float content_main_size = 0.f;
    float resolved_length = NAN;
    bool length_is_definite = false;
};

/* Resolves auto -> specified size or content, content -> content, and a
   definite length -> that length. A percentage length with an indefinite
   containing main size is represented by length_is_definite=false and falls
   back to content, as required by Flexbox §7.2.3. */
float aether_flex_resolve_base_size(const aether_flex_basis_resolution_input_s& input);

struct aether_flex_automatic_minimum_input_s {
    bool is_scroll_container = false;
    float content_size_suggestion = 0.f;
    float specified_size_suggestion = NAN;
    float transferred_size_suggestion = NAN;
    float max_main_size = NAN;
};

/* Resolves Flexbox §4.5's automatic minimum. Scroll containers have a zero
   automatic minimum; otherwise content is capped by any definite specified,
   transferred, and maximum-size suggestions. */
float aether_flex_resolve_automatic_minimum(
    const aether_flex_automatic_minimum_input_s& input);

struct aether_flex_item_input_s {
    aether_flex_basis_resolution_input_s basis{};
    /* A finite value is an explicit used minimum. NaN means min-main-size:auto
       and uses automatic_minimum instead. */
    float min_main_size = NAN;
    aether_flex_automatic_minimum_input_s automatic_minimum{};
    float max_main_size = NAN;
    float outer_main_size = 0.f; // Signed non-auto main-axis margins.
    float flex_grow = 0.f;
    float flex_shrink = 1.f;
    float main_decoration_size = 0.f; // Non-flexible main-axis padding + border.
};

// Shared border-box hypothetical-size clamp. Validates sizes before applying
// min/max or decoration floors; NaN remains the absent-constraint sentinel.
float aether_flex_resolve_hypothetical_size(float base, const aether_flex_item_input_s& item);

struct aether_flex_item_result_s {
    float flex_base_size = 0.f;
    float hypothetical_main_size = 0.f;
    float target_main_size = 0.f;
    bool frozen = false;
    float inner_flex_base_size = 0.f; // Raw border-box base minus decoration.
};

struct aether_flex_line_s {
    std::vector<size_t> item_indices;
    float hypothetical_outer_main_size = 0.f;
};

struct aether_flex_resolve_input_s {
    const std::vector<aether_flex_item_input_s>* items = nullptr;
    float available_main_size = NAN;
    float gap = 0.f;
    bool wrap = false;
};

struct aether_flex_resolve_result_s {
    std::vector<aether_flex_item_result_s> items;
    std::vector<aether_flex_line_s> lines;
};

/* Forms lines from outer hypothetical main sizes before distributing free
   space, then resolves flexible lengths independently for every line. */
aether_flex_resolve_result_s aether_resolve_flexible_lengths(
    const aether_flex_resolve_input_s& input);
