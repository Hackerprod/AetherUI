#include <aether_visual_compare/paint_snapshot.hpp>
#include <aether_visual_compare/aether_command_source.hpp>
#include <aether_visual_compare/preparation_metrics.hpp>
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <aether/rendering/preparation/aether_command_source.hpp>
#include <aether/rendering/preparation/preparation_metrics.hpp>
#include <type_traits>
namespace native = aether::rendering::preparation;
static_assert(std::is_same_v<avc::preparation::Snapshot,native::Snapshot>);
static_assert(std::is_same_v<avc::preparation::PreparedFrame,native::PreparedFrame>);
static_assert(std::is_same_v<avc::preparation::AetherApi,native::AetherApi>);
static_assert(std::is_same_v<avc::preparation::PreparationMetrics,native::PreparationMetrics>);
static_assert(std::is_same_v<avc::preparation::Error,native::Error>);
static_assert(avc::preparation::snapshot_supported_tags.size()==9);
int main() {}
