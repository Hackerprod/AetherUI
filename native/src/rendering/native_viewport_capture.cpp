#include "native_viewport_capture.h"
#include "../layout/layout_internal.h"
#include "../css/style_map_types.h"
#include <algorithm>
#include <cmath>
namespace aether::rendering {
namespace {
using E=text::truetype::HtmlTextError;
std::optional<E> bounded(double v) noexcept {
 if(!std::isfinite(v)||v<0||std::floor(v)!=v)return E::unsupported;
 if(v>65536)return E::limit_exceeded;
 return {};
}
bool zero(const aether_css_edges& e) noexcept {
 for(auto p:{e.top,e.right,e.bottom,e.left})if(p.value!=0||p.unit!=AETHER_CSS_UNIT_PX)return false;
 return true;
}
aether_rectf thumb(aether_rectf track,bool horizontal,float range,float offset) noexcept {
 const float length=horizontal?track.width:track.height;
 const float extent=length+range;
 const float size=std::min(length,std::max(std::min(24.f,length),extent>0?length*(length/extent):length));
 const float available=std::max(0.f,length-size);
 const float progress=range>0?offset/range:0;
 const float position=(horizontal?track.x:track.y)+available*progress;
 return horizontal?aether_rectf{position,track.y,size,track.height}:aether_rectf{track.x,position,track.width,size};
}
}
ViewportChromeResult resolve_native_viewport_chrome(std::uint32_t w,std::uint32_t h,double rx,double ry,text::truetype::ViewportOffset offset) noexcept {
 if(!w||!h)return E::invalid_input;
 if(w>65536||h>65536||offset.x>65536||offset.y>65536)return E::limit_exceeded;
 if(auto e=bounded(rx))return *e;
 if(auto e=bounded(ry))return *e;
 if(offset.x>rx||offset.y>ry)return E::invalid_input;
 const bool horizontal=rx>0,vertical=ry>0;
 if((horizontal||vertical)&&(w<8||h<8))return E::unsupported;
 aether_scrollbar_geometry_t out{};if(!horizontal&&!vertical)return out;
 out.horizontal_visible=horizontal?AETHER_TRUE:AETHER_FALSE;out.vertical_visible=vertical?AETHER_TRUE:AETHER_FALSE;
 out.vertical_track={float(w)-6.f,0,4.f,float(h)-(horizontal?6.f:0.f)};
 out.horizontal_track={0,float(h)-6.f,float(w)-(vertical?6.f:0.f),4.f};
 if(vertical)out.vertical_thumb=thumb(out.vertical_track,false,float(ry),float(offset.y));
 if(horizontal)out.horizontal_thumb=thumb(out.horizontal_track,true,float(rx),float(offset.x));
 if(horizontal&&vertical)out.corner={out.vertical_track.x,out.horizontal_track.y,4,4};
 return out;
}
std::variant<NativeViewportAuthority,E> NativeViewportAuthority::create(aether_doc_s* doc,aether_layout_result_s* l,aether_style_map_s* map,std::uint32_t w,std::uint32_t h,text::truetype::ViewportOffset offset) noexcept {
 try {
  if(!doc||!l||!map)return E::invalid_input;
  auto version=aether::style::pin_style_map(map);auto token=doc->identity;
  if(!token||!token->alive||token->owner!=doc||!version||!version->contextual()||version->document_lifetime()!=token||!aether::style::inputs_current(version.get())||version->style_tree_epoch()!=token->style_tree_epoch)return E::unsupported;
  aether::layout::StyleOperationPin styles(l->assets,version,true);
  if(styles.document_lifetime()!=token||!styles.structure_valid()||!styles.inputs_current()||!styles.accepts_override())return E::unsupported;
  auto* root=l->root;if(!root||!root->element||root->element->tag_name!="body"||l->find_primary_box(root->element)!=root||root->owner!=l)return E::unsupported;
  auto record=version->backing()->find_owner(root->element);
  if(!record||record->style()!=root->style.style_owner())return E::unsupported;
  const auto& s=root->style;
  if(!zero(s.margin)||!zero(s.padding)||!zero(s.border_width)||s.width.unit!=AETHER_CSS_UNIT_AUTO||root->border_box.x!=0||root->border_box.y!=0||root->padding_box.x!=0||root->padding_box.y!=0||root->content_box.x!=0||root->content_box.y!=0)return E::unsupported;
  if(l->viewport.width!=w||l->viewport.height!=h||l->viewport_overflow_x!=AETHER_OVERFLOW_AUTO||l->viewport_overflow_y!=AETHER_OVERFLOW_AUTO)return E::unsupported;
  for(const auto& [node,box]:l->primary_boxes){
   if(!node||!box||box->owner!=l||box->element!=node)return E::unsupported;
   if(box!=root&&(layout_scroll_overflow_axis(l,box,true)!=AETHER_OVERFLOW_VISIBLE||layout_scroll_overflow_axis(l,box,false)!=AETHER_OVERFLOW_VISIBLE))return E::unsupported;
  }
  const double rx=root->scroll.scrollable_overflow_x,ry=root->scroll.scrollable_overflow_y;
  auto geometry=resolve_native_viewport_chrome(w,h,rx,ry,offset);if(auto* e=std::get_if<E>(&geometry))return *e;
  if(auto e=bounded(double(w)+rx))return *e;
  if(auto e=bounded(double(h)+ry))return *e;
  if(root->element->style_node_id<=0)return E::unsupported;
  NativeViewportAuthority out;out.document_=token;out.root_identity_=root->element->identity;
  out.structure_=token->structure_epoch;out.mutation_=token->mutation_epoch;out.style_=token->style_tree_epoch;out.nonstate_=token->non_state_epoch;
  out.doc_=doc;out.map_=map;out.layout_=l;out.root_=root;out.version_=version;out.root_style_=record->style();
  out.pin_=std::make_unique<aether::layout::LayoutOperationPin>(l);
  out.viewport_={0,0,float(w),float(h)};out.state_={root->element->style_node_id,double(offset.x),double(offset.y),rx,ry,std::get<aether_scrollbar_geometry_t>(geometry)};
  out.diagnostics_={offset,std::uint32_t(rx),std::uint32_t(ry),std::uint32_t(w+rx),std::uint32_t(h+ry),rx>0||ry>0,out.viewport_,out.state_.chrome};
  return out;
 }catch(const std::bad_alloc&){return E::out_of_memory;}catch(...){return E::unsupported;}
}
bool NativeViewportAuthority::accepts(aether_layout_result_s* l,aether_style_map_s* map) const noexcept {
 if(std::this_thread::get_id()!=thread_)return false;
 if(!document_||!document_->alive||document_->owner!=doc_||!root_identity_||!root_identity_->alive||!root_identity_->attached)return false;
 if(document_->structure_epoch!=structure_||document_->mutation_epoch!=mutation_||document_->style_tree_epoch!=style_||document_->non_state_epoch!=nonstate_)return false;
 if(!pin_||!l||l!=layout_||!map||map!=map_||aether::style::pin_style_map(map)!=version_||!aether::style::inputs_current(version_.get()))return false;
 if(l->root!=root_||root_identity_->owner!=root_->element||root_->style.style_owner()!=root_style_||l->find_primary_box(root_->element)!=root_||root_->owner!=l)return false;
 if(l->viewport.width!=viewport_.width||l->viewport.height!=viewport_.height||l->viewport_overflow_x!=AETHER_OVERFLOW_AUTO||l->viewport_overflow_y!=AETHER_OVERFLOW_AUTO)return false;
 if(root_->scroll.scrollable_overflow_x!=state_.range_x||root_->scroll.scrollable_overflow_y!=state_.range_y||root_->element->style_node_id!=state_.id)return false;
 const auto& s=root_->style;
 return zero(s.margin)&&zero(s.padding)&&zero(s.border_width)&&s.width.unit==AETHER_CSS_UNIT_AUTO&&root_->border_box.x==0&&root_->border_box.y==0&&root_->padding_box.x==0&&root_->padding_box.y==0&&root_->content_box.x==0&&root_->content_box.y==0;
}
}
