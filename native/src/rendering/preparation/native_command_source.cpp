#include "native_command_source.h"
#include "../display_list_types.h"
#include <span>
namespace aether::rendering::preparation {
SnapshotResult snapshot_with_border_groups(const AetherApi&,aether_display_list_t,std::span<const aether_border_group_range>,Cancellation*) noexcept;
SnapshotResult snapshot_native_commands(aether_display_list_t list,Cancellation* c) noexcept {
 if(!list)return Error{ErrorCode::INVALID_LIST};
 const AetherApi api{aether_abi_version,aether_abi_capabilities,aether_paint_command_size,aether_display_list_get_count,aether_display_list_get_command,aether_paint_command_free};
 return snapshot_with_border_groups(api,list,list->border_groups,c);
}
}
