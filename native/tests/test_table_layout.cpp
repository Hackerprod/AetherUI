#include "table_layout.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

int main() {
    const std::vector<aether_table_column_contribution_s> columns = {
        {40.f, 100.f}, {60.f, 140.f}, {20.f, 20.f}
    };
    const auto constrained = aether_resolve_auto_table_columns(columns, 100.f);
    require(constrained.size() == 3 && same(constrained[0], 40.f) && same(constrained[1], 60.f) && same(constrained[2], 20.f),
        "automatic table layout must not shrink below min-content contributions");

    const auto preferred = aether_resolve_auto_table_columns(columns, 260.f);
    require(same(preferred[0], 100.f) && same(preferred[1], 140.f) && same(preferred[2], 20.f),
        "automatic table layout must reach each column's max-content contribution before surplus distribution");

    const auto surplus = aether_resolve_auto_table_columns(columns, 320.f);
    require(same(surplus[0], 120.f) && same(surplus[1], 160.f) && same(surplus[2], 40.f),
        "automatic table layout must distribute post-preferred surplus symmetrically");
}
