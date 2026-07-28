#pragma once

#include <cstddef>
#include <vector>

// CSS Sizing contributions are deliberately distinct.  A caller supplies the
// measured unbreakable fragments for min-content and the normal child
// contributions for container aggregation; this module contains no DOM or
// renderer dependency.
enum class aether_intrinsic_size_mode_e {
    min_content,
    max_content,
};

enum class aether_inline_fragment_kind_e {
    word,
    whitespace,
    atomic,
    forced_break,
};

struct aether_inline_intrinsic_fragment_s {
    aether_inline_fragment_kind_e kind;
    float width;
};

struct aether_inline_intrinsic_sizes_s {
    float min_content;
    float max_content;
};

float aether_intrinsic_min_from_unbreakable_fragments(const std::vector<float>& fragment_widths);

aether_inline_intrinsic_sizes_s aether_measure_inline_intrinsic_sizes(
    const std::vector<aether_inline_intrinsic_fragment_s>& fragments);

float aether_intrinsic_flex_main_contribution(
    const std::vector<float>& child_contributions,
    bool column_direction,
    bool wraps,
    float gap,
    aether_intrinsic_size_mode_e mode);
