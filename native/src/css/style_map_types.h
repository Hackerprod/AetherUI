#pragma once
#include <aether/aether.h>
#include "style_map_version.h"
#include <memory>

// Mutable external wrapper only. Every operation captures current before any
// callback. A wrapper with null current is EMPTY, not an absent override.
struct aether_style_map_s {
    aether::style::MapVersionRef current;
    aether_style_map_s() = default;
    aether_style_map_s(const aether_style_map_s&) = delete;
    aether_style_map_s& operator=(const aether_style_map_s&) = delete;
    void swap(aether_style_map_s& other) noexcept { current.swap(other.current); }
};
namespace aether::style {
inline MapVersionRef pin_style_map(const aether_style_map_s* map) noexcept {
    return map ? map->current : MapVersionRef{};
}
}
inline std::unique_ptr<aether_style_map_s> aether_style_map_snapshot(const aether_style_map_s* source) {
    auto result = std::make_unique<aether_style_map_s>();
    result->current = aether::style::pin_style_map(source);
    return result;
}
inline bool aether_style_map_structure_valid(const aether_style_map_s* map) noexcept {
    return aether::style::structure_valid(map ? map->current.get() : nullptr);
}
inline bool aether_style_map_inputs_current(const aether_style_map_s* map) noexcept {
    return aether::style::inputs_current(map ? map->current.get() : nullptr);
}
