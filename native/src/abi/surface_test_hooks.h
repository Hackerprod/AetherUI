#pragma once
// Private harness seam. Never installed or compiled into the production DSO.
#ifdef AETHER_SURFACE_TESTING
#include <aether/aether.h>
#include <cstdint>
namespace aether::surface_test {
enum class Stage { create, snapshot, prepare, candidate, copy, allocation, unexpected };
void stage(Stage);
void set_counter(aether_surface_t, bool sequence, std::uint64_t);
}
#endif
