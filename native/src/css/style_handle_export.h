#pragma once
#include "style_map_version.h"
namespace aether::style {
// binding must remain pinned. Only an exact record owner in its backing is
// accepted. Flat output owns independent buffers; failure leaves *out intact.
aether_status_t export_style_handle(const MapVersion& binding, const FrozenNodeRef& record,
    aether_computed_style_handle_t* out) noexcept;
}
