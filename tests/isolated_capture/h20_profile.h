#pragma once
#include "../../native/src/css/frozen_measure_read.h"
// Geometry-only admission. This is not a bidi/text or general RTL block profile.
namespace h20 {
inline void before(aether_style_map_t map) {
 const auto version=aether::style::pin_style_map(map);
 require(bool(version)&&bool(version->backing()),"DIRECTION_STYLE_MISSING");
 bool rtl=false;
 for(const auto& entry:version->backing()->entries()) {
  const auto& r=*entry.second;require(bool(r.measures()),"DIRECTION_STYLE_MISSING");
  rtl|=aether::style::MeasuresReadView(*r.measures()).direction_rtl();
 }
 if(!rtl)return;
 size_t count=0;
 for(const auto& entry:version->backing()->entries()) {
  const auto& r=*entry.second;const auto v=r.style_view();const auto& s=v.scalars();auto* n=r.node();
  if(n->type!=AETHER_NODE_ELEMENT||s.display==AETHER_DISPLAY_NONE)continue;
  require(++count<=2048,"DIRECTION_NODE_LIMIT");
  const aether::style::MeasuresReadView m(*r.measures());
  require(s.display==AETHER_DISPLAY_BLOCK||s.display==AETHER_DISPLAY_FLEX,"DIRECTION_DISPLAY_UNSUPPORTED");
  require(s.position==AETHER_POSITION_STATIC&&s.overflow_x==AETHER_OVERFLOW_VISIBLE&&s.overflow_y==AETHER_OVERFLOW_VISIBLE&&v.transforms().empty()&&v.backdrop_filters().empty()&&s.opacity==1.f,"DIRECTION_EFFECT_UNSUPPORTED");
  for(const auto* key:{"width","height","min-width","min-height","max-width","max-height","flex-basis"})
   require(m.intrinsic_keyword(key)==aether::css::intrinsic_size_keyword::none&&!m.contextual_tree(key),"DIRECTION_INTRINSIC_UNSUPPORTED");
  if(n->tag_name=="html"||n->tag_name=="body") {
   require(!m.direction_rtl(),"DIRECTION_ROOT_UNSUPPORTED");continue;
  }
  require(h18::html_wrapper(n->tag_name)&&!n->is_generated,"DIRECTION_ELEMENT_UNSUPPORTED");
  if(m.direction_rtl()) {
   require(h3::px(s.width)&&h3::px(s.height),"DIRECTION_SIZE_UNSUPPORTED");
   if(s.display!=AETHER_DISPLAY_FLEX)for(auto* c:n->children)
    require(c->type!=AETHER_NODE_ELEMENT,"DIRECTION_BLOCK_SUBTREE_UNSUPPORTED");
  }
 }
}
}
