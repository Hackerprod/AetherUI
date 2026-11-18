#pragma once
#include <aether/aether.h>
#include "../rendering/html_ltr/owned_budget.h"
namespace aether::dom {
// Private opt-in TEMPORARY storage accounting only. Native DOM allocations
// (nodes, identities, attributes, children and retained strings) remain outside
// this tranche. Same parser/decoder kernels as the legacy C entrypoint.
// Replacement parsing can leave a partial DOM on failure; caller owns cleanup.
aether_status_t parse_html_owned_temporaries(aether_doc_t,const char*,
    rendering::html_ltr::OwnedBudget::Handle);
}
