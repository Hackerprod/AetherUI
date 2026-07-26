#include "style_runtime_types.h"

// ═══ Cascade priority comparison ═══════════════════════════════
//
// CSS cascade order (highest wins):
//   1. Important flag
//   2. Inline style flag
//   3. StyleSheet layer (engine_defaults < theme < author)
//   4. Specificity (id > class > type counts)
//   5. Source order (earlier stylesheet = lower)
//   6. Declaration order (earlier in stylesheet = lower)

int cascade_priority_compare(const cascade_priority_t* a, const cascade_priority_t* b) {
    if (a->important != b->important) {
        return a->important ? 1 : -1;
    }
    if (a->inline_ != b->inline_) {
        return a->inline_ ? 1 : -1;
    }
    if (a->layer != b->layer) {
        return (a->layer < b->layer) ? -1 : 1;
    }
    int spec = a->specificity.compare_to(b->specificity);
    if (spec != 0) {
        return spec;
    }
    if (a->source_order != b->source_order) {
        return (a->source_order < b->source_order) ? -1 : 1;
    }
    if (a->decl_order != b->decl_order) {
        return (a->decl_order < b->decl_order) ? -1 : 1;
    }
    return 0;
}

// ═══ CascadeEntry — standard property ══════════════════════════

void cascade_entry_s::offer(const cascade_priority_t& p, const compiled_style_value_s& v) {
    if (!is_set || cascade_priority_compare(&p, &priority) >= 0) {
        is_set = true;
        priority = p;
        value = v;
    }
}

// ═══ CascadeEntryCustom — custom property ══════════════════════
//
// Same winning logic, but stores raw source text for lazy resolution
// through the VariableResolver.

void cascade_entry_custom_s::offer(const cascade_priority_t& p, const std::string& s) {
    if (!is_set || cascade_priority_compare(&p, &priority) >= 0) {
        is_set = true;
        priority = p;
        source = s;
    }
}
