#pragma once
#include <aether/rendering/preparation/aether_command_source.hpp>
namespace aether::rendering::preparation {
// Private native entry point. Must receive a real native list, not an opaque external implementation.
SnapshotResult snapshot_native_commands(aether_display_list_t, Cancellation* = nullptr) noexcept;
}
namespace aether::rendering { class NativePaintList; }
namespace aether::rendering::preparation {
SnapshotResult snapshot_native_opacity(const aether::rendering::NativePaintList&,Cancellation* = nullptr) noexcept;
}
