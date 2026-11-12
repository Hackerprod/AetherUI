#pragma once
#include <aether/aether.h>
#include <cstdint>
#include <memory>
#include <vector>
namespace aether::rendering {
struct OpacityEvent { std::size_t boundary{}; bool open{}; std::uint64_t id{}, parent{}; float opacity{1}; };
struct NativeScrollOffset { std::int64_t id{}; double x{},y{}; };
using NativeScrollInput = std::vector<NativeScrollOffset>;
struct NativeScrollState { std::int64_t id{}; double x{},y{},range_x{},range_y{}; aether_scrollbar_geometry_t chrome{}; };
class NativePaintList {
 aether_display_list_t list_{};
 friend struct NativeListAccess;
 public:
 std::vector<OpacityEvent> events;
 std::vector<NativeScrollState> scroll;
 NativePaintList()=default;
 ~NativePaintList(){ if(list_) aether_display_list_destroy(list_); }
 NativePaintList(const NativePaintList&)=delete;
 NativePaintList& operator=(const NativePaintList&)=delete;
};
struct NativeListAccess { static aether_display_list_t& list(NativePaintList& n){return n.list_;} static aether_display_list_t list(const NativePaintList& n){return n.list_;} };
// Private C++ only: node is an isolated subtree, never an incremental patch.
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t node,std::unique_ptr<NativePaintList>&) noexcept;
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t,const NativeScrollInput&,std::unique_ptr<NativePaintList>&) noexcept;
}
