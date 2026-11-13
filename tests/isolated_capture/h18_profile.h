#pragma once
// H18 is a capture profile, not a second box-tree builder. It only admits the
// HTML/flex subset tested by the native display:contents implementation.
namespace h18 {
inline bool html_wrapper(std::string_view tag) {
 return tag=="div"||tag=="span"||tag=="section"||tag=="article"||tag=="main"||
        tag=="aside"||tag=="nav"||tag=="header"||tag=="footer"||tag=="figure"||tag=="figcaption";
}
inline void before(aether_style_map_t map) {
 const auto version=aether::style::pin_style_map(map);
 require(bool(version)&&bool(version->backing()),"CONTENTS_STYLE_MISSING");
 const auto style=[&](aether_node_s* n) {
  const auto* record=version->backing()->find(n);
  require(record!=nullptr,"CONTENTS_STYLE_MISSING");return record->style_view();
 };
 size_t wrappers=0,total=0;
 for(const auto& entry:version->backing()->entries()) {
  const auto* record=entry.second.get();auto* n=record->node();const auto view=record->style_view();
  const auto& st=view.scalars();
  if(st.display!=AETHER_DISPLAY_CONTENTS)continue;
  require(++wrappers<=512,"CONTENTS_WRAPPER_LIMIT");
  require(n->type==AETHER_NODE_ELEMENT&&!n->is_generated&&html_wrapper(n->tag_name),"CONTENTS_ELEMENT_UNSUPPORTED");
  const auto supported=[&](aether_node_s* x) {
   const auto v=style(x);const auto& s=v.scalars();
   require(s.position==AETHER_POSITION_STATIC&&v.transforms().empty()&&s.opacity==1.f&&
     s.overflow_x==AETHER_OVERFLOW_VISIBLE&&s.overflow_y==AETHER_OVERFLOW_VISIBLE&&
     v.backdrop_filters().empty(),"CONTENTS_EFFECT_UNSUPPORTED");
  };
  supported(n);
  auto* parent=n->parent;size_t depth=0;
  while(parent&&parent->type==AETHER_NODE_ELEMENT&&style(parent).scalars().display==AETHER_DISPLAY_CONTENTS){
   require(++depth<=32,"CONTENTS_DEPTH_LIMIT");parent=parent->parent;
  }
  require(parent&&parent->type==AETHER_NODE_ELEMENT&&style(parent).scalars().display==AETHER_DISPLAY_FLEX,"CONTENTS_PARENT_UNSUPPORTED");
  // This increment does not claim complex clipping/positioning interaction
  // across promoted descendants. No inherited transform context is admitted.
  for(auto* a=parent;a&&a->type==AETHER_NODE_ELEMENT;a=a->parent)supported(a);
  struct Item {aether_node_s* node;size_t depth;};
  std::vector<Item> todo;for(auto* c:n->children)todo.push_back({c,1});
  while(!todo.empty()) {
   const auto [c,d]=todo.back();todo.pop_back();require(++total<=50000&&d<=32,"CONTENTS_EXPANSION_LIMIT");
   require(!c->is_generated&&c->pseudo_element==aether_pseudo_element_t::none,"CONTENTS_GENERATED_UNSUPPORTED");
   if(c->type==AETHER_NODE_COMMENT)continue;
   if(c->type==AETHER_NODE_TEXT){require(c->text.find_first_not_of(" \t\r\n\f")==std::string::npos,"CONTENTS_TEXT_UNSUPPORTED");continue;}
   require(c->type==AETHER_NODE_ELEMENT,"CONTENTS_ELEMENT_UNSUPPORTED");
   const auto cv=style(c);const auto display=cv.scalars().display;
   if(display==AETHER_DISPLAY_NONE)continue;
   require(display==AETHER_DISPLAY_BLOCK||display==AETHER_DISPLAY_FLEX||display==AETHER_DISPLAY_CONTENTS,"CONTENTS_CHILD_DISPLAY_UNSUPPORTED");
   require(html_wrapper(c->tag_name),"CONTENTS_REPLACED_UNSUPPORTED");
   supported(c);
   if(display==AETHER_DISPLAY_CONTENTS)for(auto* gc:c->children)todo.push_back({gc,d+1});
  }
 }
}
}
