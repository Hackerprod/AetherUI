#include "h11_profile.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/style_runtime/style_runtime_types.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include "../../native/src/style_runtime/css_variable_tokens.h"
#include <algorithm>
#include <limits>
namespace h11 {
namespace {
constexpr size_t cap=16000001;
size_t add(size_t a,size_t b){return a>=cap||b>=cap-a?cap:a+b;}
size_t mul(size_t a,size_t b){return a&&b>=cap/a?cap:a*b;}
bool trivia(const css_variable_token& t){return t.kind==css_variable_token_kind::whitespace||t.kind==css_variable_token_kind::comment;}
bool delim(const css_variable_token& t,char c){return t.delimiter==c&&t.serialization!=css_variable_serialization::string;}
// Share the native lexer: braces/commas in strings, escapes and comments cannot
// invent rules. No recovery, nesting >32, or partial result earns an exception.
bool tokens(const std::string& s,std::vector<css_variable_token>& ts){
 if(s.size()>2*1024*1024)return false;
 css_variable_budget budget{s.size()*8+1024,false};
 css_variable_lexer lexer(s.data(),s.size(),0,&budget);
 for(;;){auto t=lexer.next();if(budget.exhausted||t.kind==css_variable_token_kind::error)return false;
  if(t.kind==css_variable_token_kind::end)return true;
  if(!trivia(t)){if(ts.size()==65536)return false;ts.push_back(t);}
 }
}
}
Proof::Proof(aether_node_s* root){
 if(!root)return;
 std::vector<std::pair<aether_node_s*,size_t>> todo{{root,1}};size_t count=0;
 while(!todo.empty()){
  auto [n,d]=todo.back();todo.pop_back();
  if(!n||++count>1024||d>64||n->is_generated||n->anonymous_flex_owner)return;
  depth=std::max(depth,d);siblings=std::max(siblings,n->children.size());
  if(n->children.size()>1024||todo.size()+n->children.size()>1024||n->attributes.size()>64)return;
  // Conservative byte bound for each native string comparison/class scan and
  // attribute lookup. Includes metadata and hidden elements, not just body.
  size_t bytes=n->tag_name.size();for(const auto& [k,v]:n->attributes){bytes=add(bytes,add(k.size(),v.size()));}
  if(bytes>8192)return;
  data=std::max(data,bytes);
  if(n->type==AETHER_NODE_ELEMENT)elements.push_back(n);
  for(auto* child:n->children)todo.push_back({child,d+1});
 }
 usable=true;
}
bool Proof::absent(const std::string& source){
 if(!usable||source.size()>512)return false;
 std::vector<css_variable_token> ts;if(!tokens(source,ts)||ts.size()>128)return false;
 // Bound parser recursion BEFORE invoking it. Nested functional selectors are
 // deliberately excluded, even if a forbidden state is hidden inside :not().
 size_t nesting=0;
 for(const auto& t:ts){
  if(t.kind==css_variable_token_kind::open||t.kind==css_variable_token_kind::function){if(++nesting>1)return false;}
  else if(t.kind==css_variable_token_kind::close){if(!nesting)return false;--nesting;}
 }
 if(nesting)return false;
 std::vector<compiled_selector_s> selectors;
 if(!selector_parse_list_strict_profile(source,selectors)||selectors.empty()||selectors.size()>16)return false;
 size_t all=0;
 for(const auto& s:selectors){
  if(s.pseudo_element!=aether_pseudo_element_t::none||s.steps.empty()||s.steps.size()>4)return false;
  size_t cost=0;
  // Recurrence bounds all possible native matcher branches before executing
  // ANY branch. Descendant/general sibling backtracking is not assumed linear.
  for(size_t i=s.steps.size();i-->0;){const auto& step=s.steps[i];const auto& c=step.compound;
   const size_t atoms=1+c.ids.size()+c.classes.size()+c.attributes.size()+c.pseudos.size();
   if(atoms>8)return false;
   size_t own=mul(8*atoms,add(add(data,source.size()),add(siblings,64)));
   for(const auto& a:c.attributes)if(a.op!=ATTR_EXISTS&&a.op!=ATTR_EQUALS)return false;
   for(const auto& p:c.pseudos){
    if(p.nested)return false;
    switch(p.kind){case PSEUDO_ROOT:case PSEUDO_EMPTY:case PSEUDO_FIRST_CHILD:case PSEUDO_LAST_CHILD:case PSEUDO_ONLY_CHILD:case PSEUDO_NTH_CHILD:break;default:return false;}
   }
   switch(step.relation_to_next){
    case COMB_NONE:if(i+1!=s.steps.size())return false;cost=own;break;
    case COMB_CHILD:cost=add(own,cost);break;
    case COMB_DESCENDANT:cost=add(own,mul(depth,add(cost,1)));break;
    case COMB_ADJACENT_SIBLING:cost=add(own,add(siblings,cost));break;
    case COMB_GENERAL_SIBLING:cost=add(own,mul(siblings,add(cost,1)));break;
    default:return false;
   }
  }
  all=add(all,mul(elements.size(),cost));
 }
 if(all>remaining)return false;
 remaining-=all; // debit worst case, not elapsed time or work observed afterward
 for(const auto& s:selectors)for(auto* n:elements){++queries;if(selector_match(s,n))return false;}
 return true;
}
std::string Proof::retained(const std::string& sheet){
 std::vector<css_variable_token> ts;if(!tokens(sheet,ts))return sheet;
 struct Rule{size_t first,open,last;bool ordinary;};std::vector<Rule> rules;
 // Parse all boundaries before omitting anything. At-rules are opaque and
 // retained byte-for-byte, including inactive bodies and keyframes.
 size_t i=0;
 while(i<ts.size()){
  if(rules.size()>=512)return sheet;
  const size_t first=i;size_t open=ts.size();std::vector<char> stack;
  for(;i<ts.size();++i){const auto& t=ts[i];
   if(stack.empty()&&delim(t,'{')){open=i;break;}
   if(stack.empty()&&(delim(t,';')||delim(t,'}')))return sheet;
   if(t.kind==css_variable_token_kind::function)stack.push_back(')');
   else if(t.kind==css_variable_token_kind::open)stack.push_back(t.delimiter=='['?']':')');
   else if(t.kind==css_variable_token_kind::close){if(stack.empty()||stack.back()!=t.delimiter)return sheet;stack.pop_back();}
   if(stack.size()>32)return sheet;
  }
  if(open==ts.size()||open==first)return sheet;
  stack.push_back('}');++i;
  for(;i<ts.size()&&!stack.empty();++i){const auto& t=ts[i];
   if(t.kind==css_variable_token_kind::function)stack.push_back(')');
   else if(t.kind==css_variable_token_kind::open)stack.push_back(t.delimiter=='{'?'}':t.delimiter=='['?']':')');
   else if(t.kind==css_variable_token_kind::close){if(stack.back()!=t.delimiter)return sheet;stack.pop_back();}
   if(stack.size()>32)return sheet;
  }
  if(!stack.empty())return sheet;
  rules.push_back({first,open,i-1,ts[first].serialization!=css_variable_serialization::at_keyword});
 }
 if(rules.size()>rules_left)return sheet;
 rules_left-=rules.size();
 std::string kept;
 for(const auto& r:rules){
  const size_t b=ts[r.first].begin,e=ts[r.last].end;
  if(r.ordinary&&absent(sheet.substr(b,ts[r.open].begin-b))){++omitted;continue;}
  kept.append(sheet,b,e-b);kept+='\n';
 }
 return kept;
}
}
