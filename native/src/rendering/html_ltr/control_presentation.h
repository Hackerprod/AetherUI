#pragma once
#include "../../css/frozen_node_record.h"
#include "../text/shaped_run.h"
#include <cstdint>
#include <string>
namespace aether::rendering::html_ltr {
enum class ControlKind { value, placeholder, empty };
// Constructed once by the admitted session. All published references are const;
// neither layout nor paint may reread the authored value or regenerate masking.
struct ControlPresentation {
 std::uint32_t profile{};
 std::size_t ordinal{};
 aether::style::FrozenNodeRef record;
 std::uint64_t epoch{},nonstate{};
 std::string type;
 ControlKind kind=ControlKind::empty;
 std::string text;
 aether_color_rgba color{};
 bool shows_placeholder{};
};
struct AtomicControlPlacement {
 std::shared_ptr<const ControlPresentation> presentation;
 text::Placement text;
 std::uint64_t occurrence{};
};
}
