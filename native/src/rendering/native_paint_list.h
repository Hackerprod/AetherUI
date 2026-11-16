#pragma once
#include <aether/aether.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "text/shaped_run.h"
#include <string>
#include "preparation/include/aether/rendering/preparation/image_resource.hpp"
struct aether_layout_box_s;
namespace aether::rendering {
struct OpacityEvent { std::size_t boundary{}; bool open{}; std::uint64_t id{}, parent{}; float opacity{1}; };
struct NativeScrollOffset { std::int64_t id{}; double x{},y{}; };
using NativeScrollInput = std::vector<NativeScrollOffset>;
struct NativeScrollState { std::int64_t id{}; double x{},y{},range_x{},range_y{}; aether_scrollbar_geometry_t chrome{}; };
struct NativeTextEntry { std::size_t command_index{}; text::Placement placement; const aether_layout_box_s* document_box{}; std::uint64_t document_placement{}; };
struct NativeImageInput { std::string source; preparation::ImageRef resource; };
using NativeImageCatalog = std::vector<NativeImageInput>;
// Evidence captured alongside the original command-owned source allocation.
struct NativeImageEntry {
 std::size_t command_index{}; const char* source_identity{}; std::string source;
 preparation::ImageRef resource; aether_rectf crop{}, destination{}; float opacity{1};
};
struct NativeShadowEntry { std::size_t command_index{};const aether_layout_box_s* box_identity{};std::size_t declaration_index{}; };
enum class NativeDocumentScopeRole {viewport_outer,viewport_content,nested_padding,scroll,chrome};
struct NativeDocumentScopeEntry {std::size_t command_index{};const aether_layout_box_s* box_identity{};NativeDocumentScopeRole role{};};
class NativePaintList {
 aether_display_list_t list_{};
 friend struct NativeListAccess;
 public:
 std::vector<NativeTextEntry> text_runs;
 std::vector<NativeImageEntry> images;
 std::vector<NativeShadowEntry> shadows;
 std::vector<NativeDocumentScopeEntry> document_scopes;
 std::vector<OpacityEvent> events;
 std::vector<NativeScrollState> scroll;
 NativePaintList()=default;
 ~NativePaintList(){ if(list_) aether_display_list_destroy(list_); }
 NativePaintList(const NativePaintList&)=delete;
 NativePaintList& operator=(const NativePaintList&)=delete;
};
struct NativeListAccess { static aether_display_list_t& list(NativePaintList& n){return n.list_;} static aether_display_list_t list(const NativePaintList& n){return n.list_;} };
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t,const NativeScrollInput&,const NativeImageCatalog&,std::unique_ptr<NativePaintList>&) noexcept;
// Image catalog with the legacy implicit zero-scroll policy.
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t,const NativeImageCatalog&,std::unique_ptr<NativePaintList>&) noexcept;
// Private C++ only: node is an isolated subtree, never an incremental patch.
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t node,std::unique_ptr<NativePaintList>&) noexcept;
aether_status_t build_native_opacity(aether_display_list_builder_t,aether_layout_result_t,
 aether_style_map_t,aether_node_t,const NativeScrollInput&,std::unique_ptr<NativePaintList>&) noexcept;
}
