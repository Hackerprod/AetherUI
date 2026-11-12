#pragma once
#include <cstddef>
// Private native metadata. One range is one emitter invocation, never inferred.
struct aether_border_group_range { std::size_t first{}, count{}; };
