#pragma once
#include <aether/aether.h>
#include "style_source_accounting.h"
#include <cstddef>
namespace aether::style {
// Ordinal counts style elements in document preorder, starting at zero.
// Extracts exact DOM text internally; no caller certificate is accepted.
aether_status_t add_dom_stylesheet(aether_style_runtime_t, aether_doc_t,
    aether_node_t, size_t ordinal, int64_t* out_handle);
bool b0_source_stylesheets_accepted(aether_style_runtime_t,bool b3=false);
aether_status_t b3_source_node_context_status(aether_style_runtime_t,aether_node_t,bool flex);
inline bool b0_source_accounting_accepted(const style_source_accounting_s& a,bool b3=false) noexcept {
    return a.losses==0 && a.inputs_accounted && !a.external_pending &&
        a.attribution.complete && a.attribution_consistent() && (b3?a.b3_subset:a.b0_subset) && a.b0_diagnostics_clean;
}
}
