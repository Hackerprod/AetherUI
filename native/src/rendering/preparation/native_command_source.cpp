#include "native_command_source.h"
#include "../display_list_types.h"
#include <span>
#include <cmath>
#include <algorithm>
namespace aether::rendering::preparation {
SnapshotResult snapshot_with_border_groups(const AetherApi&,aether_display_list_t,std::span<const aether_border_group_range>,Cancellation*,std::span<const NativeScrollState>,bool,std::span<const NativeTextEntry>,bool,bool) noexcept;
SnapshotResult snapshot_native_commands(aether_display_list_t list,Cancellation* c) noexcept {
 if(!list)return Error{ErrorCode::INVALID_LIST};
 const AetherApi api{aether_abi_version,aether_abi_capabilities,aether_paint_command_size,aether_display_list_get_count,aether_display_list_get_command,aether_paint_command_free};
 return snapshot_with_border_groups(api,list,list->border_groups,c,{},false,{},false,true);
}
}
namespace aether::rendering::preparation {
SnapshotResult snapshot_native_opacity(const aether::rendering::NativePaintList& native,Cancellation* cancel) noexcept {
 try {
  const auto list=NativeListAccess::list(native);
  if(!list)return Error{ErrorCode::INVALID_LIST};
  if(native.events.size()>max_commands || native.text_runs.size()>max_commands || native.scroll.size()>max_commands)return Error{ErrorCode::LIMIT_EXCEEDED};
  std::vector<std::int64_t> ids;
  for(const auto& cmd:list->commands) {
   if(cmd.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET)ids.push_back(cmd.data.push_scroll_offset.scroll_container_id);
   if(cmd.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME)ids.push_back(cmd.data.draw_scrollbar_chrome.scroll_container_id);
  }
  std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
  if(ids.size()!=native.scroll.size())return Error{ErrorCode::INVALID_PAYLOAD};
  std::size_t metadata_index=0;
  std::int64_t last=0;
  for(const auto& s:native.scroll) {
   if(s.id!=ids[metadata_index++] || s.id<=last || !std::isfinite(s.x)||!std::isfinite(s.y)||!std::isfinite(s.range_x)||!std::isfinite(s.range_y)||s.x<0||s.y<0||s.x>s.range_x||s.y>s.range_y||s.range_x>65536||s.range_y>65536)return Error{ErrorCode::INVALID_PAYLOAD};
   last=s.id;
  }
  const AetherApi api{aether_abi_version,aether_abi_capabilities,aether_paint_command_size,aether_display_list_get_count,aether_display_list_get_command,aether_paint_command_free};
  auto captured=snapshot_with_border_groups(api,list,list->border_groups,cancel,native.scroll,true,native.text_runs,true,true);
  if(auto* e=std::get_if<Error>(&captured))return *e;
  auto& base=std::get<Snapshot>(captured);
  Snapshot out; out.native_text_required=base.native_text_required; std::size_t event=0,command=0,range=0;
  for(std::size_t raw=0;raw<=list->commands.size();) {
   if(cancel && cancel->cancelled())return Error{ErrorCode::CANCELLED};
   if(event<native.events.size() && native.events[event].boundary<raw)return Error{ErrorCode::INVALID_PAYLOAD};
   while(event<native.events.size() && native.events[event].boundary==raw) {
    if(cancel && cancel->cancelled())return Error{ErrorCode::CANCELLED};
    if(out.commands.size()>=max_commands)return Error{ErrorCode::COMMAND_COUNT};
    const auto& e=native.events[event++];
    if(e.open)out.commands.push_back(BeginOpacity{e.id,e.parent,e.opacity});
    else out.commands.push_back(EndOpacity{e.id});
   }
   if(raw==list->commands.size())break;
   if(command>=base.commands.size())return Error{ErrorCode::INVALID_PAYLOAD};
   if(out.commands.size()>=max_commands)return Error{ErrorCode::COMMAND_COUNT};
   out.commands.push_back(std::move(base.commands[command++]));
   if(range<list->border_groups.size() && list->border_groups[range].first==raw)raw+=list->border_groups[range++].count;
   else ++raw;
  }
  if(event!=native.events.size() || command!=base.commands.size())return Error{ErrorCode::INVALID_PAYLOAD};
  if(out.commands.size()>max_commands)return Error{ErrorCode::COMMAND_COUNT};
  if(cancel && cancel->cancelled())return Error{ErrorCode::CANCELLED};
  return out;
 }catch(const std::bad_alloc&){return Error{ErrorCode::OUT_OF_MEMORY};}
}
}

namespace aether::rendering::preparation {
SnapshotResult snapshot_native_geometry(const aether::rendering::NativePaintList& list,ValidationDomain domain,Cancellation* cancel) noexcept {
 if(domain!=ValidationDomain::ResolvedGeometryOnly)return Error{ErrorCode::UnsupportedValidationDomain};
 return snapshot_native_opacity(list,cancel);
}
}
