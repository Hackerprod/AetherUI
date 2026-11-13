#pragma once
// Deliberately shallow geometry proof, not vertical block/text support.
namespace h21 {
inline void before(aether_style_map_t map) {
 const auto version=aether::style::pin_style_map(map);
 require(bool(version)&&bool(version->backing()),"WRITING_STYLE_MISSING");
 size_t count=0;
 for(const auto& entry:version->backing()->entries()) {
  const auto& r=*entry.second;auto* n=r.node();const auto v=r.style_view();const auto& s=v.scalars();
  if(n->type!=AETHER_NODE_ELEMENT||s.display==AETHER_DISPLAY_NONE)continue;
  require(++count<=2048&&bool(r.measures()),"WRITING_STYLE_LIMIT");
  const aether::style::MeasuresReadView m(*r.measures());
  const auto mode=m.writing_mode();
  require(mode>=0&&mode<=2,"WRITING_MODE_UNSUPPORTED");
  // An explicit horizontal declaration still opts into the strict authored
  // contract, but horizontal geometry keeps its established H20 proof.
  if(!mode)continue;
  require(n->tag_name!="html"&&n->tag_name!="body","WRITING_ROOT_UNSUPPORTED");
  require(h18::html_wrapper(n->tag_name)&&!n->is_generated&&n->pseudo_element==aether_pseudo_element_t::none,"WRITING_ELEMENT_UNSUPPORTED");
  require(s.display==AETHER_DISPLAY_BLOCK||s.display==AETHER_DISPLAY_FLEX,"WRITING_DISPLAY_UNSUPPORTED");
  require(s.position==AETHER_POSITION_STATIC&&s.overflow_x==AETHER_OVERFLOW_VISIBLE&&s.overflow_y==AETHER_OVERFLOW_VISIBLE&&v.transforms().empty()&&v.backdrop_filters().empty()&&s.opacity==1.f,"WRITING_EFFECT_UNSUPPORTED");
  require(h3::px(s.width)&&h3::px(s.height),"WRITING_SIZE_UNSUPPORTED");
  for(const auto& edge:{s.margin.left,s.margin.right,s.margin.top,s.margin.bottom})require(h3::px(edge),"WRITING_MARGIN_UNSUPPORTED");
  for(const auto* key:{"width","height","min-width","min-height","max-width","max-height","flex-basis"})
   require(m.intrinsic_keyword(key)==aether::css::intrinsic_size_keyword::none&&!m.contextual_tree(key),"WRITING_INTRINSIC_UNSUPPORTED");
  if(s.display==AETHER_DISPLAY_FLEX) {
   require(!s.flex_wrap,"WRITING_WRAP_UNSUPPORTED");
   // Vertical flex roots may live in horizontal block flow, not another flex
   // formatting context whose orthogonal intrinsic sizing is unproved.
   auto* pr=version->backing()->find(n->parent);
   if(pr)require(pr->style_view().scalars().display!=AETHER_DISPLAY_FLEX,"WRITING_NESTED_UNSUPPORTED");
   for(auto* c:n->children) {
    if(c->type==AETHER_NODE_COMMENT)continue;
    if(c->type==AETHER_NODE_TEXT){require(c->text.find_first_not_of(" \t\r\n\f")==std::string::npos,"WRITING_TEXT_UNSUPPORTED");continue;}
    const auto* cr=version->backing()->find(c);require(cr&&cr->measures(),"WRITING_STYLE_MISSING");
    require(aether::style::MeasuresReadView(*cr->measures()).writing_mode()==mode,"WRITING_ORTHOGONAL_UNSUPPORTED");
    require(cr->style_view().scalars().display==AETHER_DISPLAY_BLOCK,"WRITING_CHILD_DISPLAY_UNSUPPORTED");
    require(c->children.empty(),"WRITING_SUBTREE_UNSUPPORTED");
   }
  }else {
   const auto* pr=version->backing()->find(n->parent);
   require(pr&&pr->measures()&&pr->style_view().scalars().display==AETHER_DISPLAY_FLEX&&aether::style::MeasuresReadView(*pr->measures()).writing_mode()==mode,"WRITING_BLOCK_UNSUPPORTED");
   require(n->children.empty(),"WRITING_SUBTREE_UNSUPPORTED");
  }
 }
}
}
