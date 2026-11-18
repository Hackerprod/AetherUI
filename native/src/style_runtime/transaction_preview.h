#pragma once
#include <aether/aether_contextual_style.h>
namespace aether::style {
// Private staging only: never submit this map to layout before successful commit.
aether_status_t transaction_create_pending_map(aether_style_transaction_t, aether_style_map_t*) noexcept;
}
