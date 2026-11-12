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
namespace aether::rendering::preparation {
SnapshotResult snapshot_native_opacity(const aether::rendering::NativePaintList& native,Cancellation* cancel) noexcept {
 try {
  const auto list=NativeListAccess::list(native);
  if(!list)return Error{ErrorCode::INVALID_LIST};
  auto captured=snapshot_native_commands(list,cancel);
  if(auto* e=std::get_if<Error>(&captured))return *e;
  auto& base=std::get<Snapshot>(captured);
  Snapshot out; std::size_t event=0,command=0,range=0;
  for(std::size_t raw=0;raw<=list->commands.size();) {
   if(cancel && cancel->cancelled())return Error{ErrorCode::CANCELLED};
   if(event<native.events.size() && native.events[event].boundary<raw)return Error{ErrorCode::INVALID_PAYLOAD};
   while(event<native.events.size() && native.events[event].boundary==raw) {
    const auto& e=native.events[event++];
    if(e.open)out.commands.push_back(BeginOpacity{e.id,e.parent,e.opacity});
    else out.commands.push_back(EndOpacity{e.id});
   }
   if(raw==list->commands.size())break;
   if(command>=base.commands.size())return Error{ErrorCode::INVALID_PAYLOAD};
   out.commands.push_back(std::move(base.commands[command++]));
   if(range<list->border_groups.size() && list->border_groups[range].first==raw)raw+=list->border_groups[range++].count;
   else ++raw;
  }
  if(event!=native.events.size() || command!=base.commands.size())return Error{ErrorCode::INVALID_PAYLOAD};
  return out;
 }catch(const std::bad_alloc&){return Error{ErrorCode::OUT_OF_MEMORY};}
}
}
