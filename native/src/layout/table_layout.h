#pragma once

#include <vector>

struct aether_table_column_contribution_s {
    float min_content = 0.f;
    float max_content = 0.f;
};

// Resolves the phase-1 automatic table-layout column widths. Every input is a
// single unspanned column contribution; colspan/rowspan are intentionally not
// represented until their own table-layout phase.
std::vector<float> aether_resolve_auto_table_columns(
    const std::vector<aether_table_column_contribution_s>& columns,
    float available_width);
