#include "h19_profile.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include "../../native/src/style_runtime/css_variable_tokens.h"
#include "../../native/src/css/style_map_types.h"
#include <algorithm>
namespace h19 {
struct Gate::Impl {std::vector<compiled_selector_s> selectors;size_t bytes=0;};
Gate::Gate():impl(std::make_unique<Impl>()){} Gate::~Gate()=default;
bool Gate::needed() const{return !impl->selectors.empty();}
bool Gate::add(std::string_view source){
 // The global H5 parser already validates every rule. Here bounded source
 // parsing obtains the native host selector, never a textual replacement.
 if(source.size()>4096||source.size()>2*1024*1024-impl->bytes)return false;
 impl->bytes+=source.size();
 css_variable_budget work{32768,false};css_variable_lexer lex(source.data(),source.size(),0,&work);
 size_t tokens=0,depth=0;for(;;){auto t=lex.next();if(work.exhausted||t.kind==css_variable_token_kind::error)return false;if(t.kind==css_variable_token_kind::end)break;
  if(++tokens>512)return false;
  if(t.kind==css_variable_token_kind::function||t.kind==css_variable_token_kind::open){if(++depth>8)return false;}
  if(t.kind==css_variable_token_kind::close){if(!depth)return false;--depth;}
 }
 if(depth)return false;
 std::vector<compiled_selector_s> list;if(!selector_parse_list_strict_profile(source,list))return false;
 for(auto& s:list){if(s.pseudo_element!=aether_pseudo_element_t::first_letter)continue;
  if(impl->selectors.size()>=128||s.steps.empty()||s.steps.size()>4)return false;
  for(const auto& step:s.steps){const auto& c=step.compound;
   if(1+c.ids.size()+c.classes.size()+c.attributes.size()+c.pseudos.size()>8)return false;
   if(step.relation_to_next!=COMB_NONE&&step.relation_to_next!=COMB_CHILD&&step.relation_to_next!=COMB_DESCENDANT)return false;
   for(const auto& a:c.attributes)if(a.op!=ATTR_EXISTS&&a.op!=ATTR_EQUALS)return false;
   for(const auto& p:c.pseudos){if(p.nested)return false;switch(p.kind){
    case PSEUDO_ROOT:case PSEUDO_EMPTY:case PSEUDO_FIRST_CHILD:case PSEUDO_LAST_CHILD:case PSEUDO_ONLY_CHILD:case PSEUDO_NTH_CHILD:case PSEUDO_FIRST_OF_TYPE:break;
    default:return false;}}
  }
  s.pseudo_element=aether_pseudo_element_t::none;impl->selectors.push_back(std::move(s));
 }
 return true;
}
bool Gate::check(aether_node_t root,aether_style_map_t map){
 if(!needed())return true;
 const auto version=aether::style::pin_style_map(map);if(!version||!version->backing())return false;
 std::vector<aether_node_s*> elements;std::vector<std::pair<aether_node_s*,size_t>> todo{{root,1}};
 size_t count=0,depth=0,siblings=0,data=0;
 while(!todo.empty()){auto [n,d]=todo.back();todo.pop_back();if(!n||++count>1024||d>32||n->is_generated||n->anonymous_flex_owner)return false;
  depth=std::max(depth,d);siblings=std::max(siblings,n->children.size());if(n->attributes.size()>64||n->children.size()>1024)return false;
  size_t b=n->tag_name.size();for(const auto& [k,v]:n->attributes){if(k.size()>8192||v.size()>8192||b>8192-k.size()||b+k.size()>8192-v.size())return false;b+=k.size()+v.size();}data=std::max(data,b);
  if(n->type==AETHER_NODE_ELEMENT)elements.push_back(n);
  for(auto* c:n->children)todo.push_back({c,d+1});
 }
 // Worst-case bound accounts for ancestor backtracking and sibling scans in
 // structural pseudos BEFORE any bool-returning matcher is called.
 constexpr size_t cap=16000001;auto add=[](size_t a,size_t b){return a>=cap||b>=cap-a?cap:a+b;};auto mul=[](size_t a,size_t b){return a&&b>=cap/a?cap:a*b;};
 size_t total=0;for(const auto& s:impl->selectors){size_t cost=0;
  for(size_t i=s.steps.size();i-->0;){const auto& step=s.steps[i];const auto& c=step.compound;size_t atoms=1+c.ids.size()+c.classes.size()+c.attributes.size()+c.pseudos.size();
   const auto own=mul(8*atoms,add(add(data,s.source.size()),add(siblings,64)));
   cost=step.relation_to_next==COMB_DESCENDANT?add(own,mul(depth,add(cost,1))):add(own,cost);
  }total=add(total,mul(elements.size(),cost));
 }if(total>=cap)return false;
 for(const auto& s:impl->selectors)for(auto* n:elements)if(selector_match(s,n)){
  // Only ordinary HTML boxes within the resolved body tree earn the proof.
  const auto& tag=n->tag_name;
  if(tag!="div"&&tag!="span"&&tag!="section"&&tag!="article"&&tag!="main"&&tag!="aside"&&tag!="nav"&&tag!="header"&&tag!="footer"&&tag!="figure"&&tag!="figcaption")return false;
  const auto* record=version->backing()->find(n);if(!record)return false;
  auto display=record->style_view().scalars().display;
  if(display!=AETHER_DISPLAY_FLEX)return false;
 }
 return true;
}
}
