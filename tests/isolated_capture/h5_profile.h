#pragma once
// One contextual analysis shared by inventory and admission. This is a bounded
// geometry profile, never a claim of complete CSS coverage.
#include "h4_profile.h"
#include "h7_profile.h"
#include "h8_profile.h"
#include "h14_profile.h"
#include "../../native/src/style_runtime/h5_parser_profile.h"
#include "../../native/src/style_runtime/h5_color_profile.h"
#include "../../native/src/style_runtime/supports_condition.h"
namespace h5 {
// Shared traversal returns original declaration spans; never executable flattened CSS.
struct DeclarationRange { size_t begin,end; bool keyframe; unsigned depth; size_t selector_begin=SIZE_MAX,selector_end=SIZE_MAX; };
struct WorkBudget { size_t bytes=2*1024*1024, tokens=524288, rules=65536, steps=32*1024*1024; };
struct Analysis { std::vector<DeclarationRange> declarations; std::set<size_t> functions, at_rules; bool valid=true; bool variables=false; size_t failure_begin=SIZE_MAX,failure_end=SIZE_MAX,failure_selector_begin=SIZE_MAX,failure_selector_end=SIZE_MAX; bool limit_failure=false,lexical_failure=false; };
struct Scanner {
 explicit Scanner(const std::string& source):s(source){}
 const std::string& s; std::vector<css_variable_token> ts; Analysis out;
 size_t current_begin=0,current_end=0,current_selector_begin=SIZE_MAX,current_selector_end=SIZE_MAX;
 void invalid(){if(out.valid){out.failure_begin=current_begin;out.failure_end=current_end;out.failure_selector_begin=current_selector_begin;out.failure_selector_end=current_selector_end;}out.valid=false;}
 size_t supports_budget=65536; WorkBudget local; WorkBudget* budget=nullptr;
 bool spend(size_t& counter,size_t amount=1){if(amount>counter){invalid();out.limit_failure=true;return false;}counter-=amount;return true;}
 bool step(){return spend(budget->steps);}
 bool d(size_t i,char c)const{return i<ts.size()&&ts[i].delimiter==c&&ts[i].serialization!=css_variable_serialization::string;}
 std::string ident(size_t i)const{return h4::identifier(s,ts[i].begin,ts[i].end);}
 bool pair(size_t begin,size_t end,size_t& close){
  std::vector<char> stack;for(size_t i=begin;i<end;++i){if(!step())return false;auto t=ts[i];
   if(t.kind==css_variable_token_kind::function)stack.push_back(')');
   else if(t.kind==css_variable_token_kind::open)stack.push_back(t.delimiter=='('?')':t.delimiter=='['?']':'}');
   else if(t.kind==css_variable_token_kind::close){if(stack.empty()||stack.back()!=t.delimiter)return false;stack.pop_back();if(stack.empty()){close=i;return true;}}
   if(stack.size()>64)return false;
  }return false;
 }
 void declarations(size_t a,size_t z,unsigned depth=0,bool keyframe=false,size_t selector_begin=SIZE_MAX,size_t selector_end=SIZE_MAX){
  if(a==z)return;
  current_selector_begin=selector_begin;current_selector_end=selector_end;
  size_t b=ts[a].begin,e=ts[z-1].end;
  if(!spend(budget->steps,e-b))return;
  out.declarations.push_back({b,e,keyframe,depth,selector_begin,selector_end});
  auto math=h4::admitted_functions(s.substr(b,e-b),false);for(auto p:math)out.functions.insert(b+p);
  size_t i=a;
  while(i<z){if(!step())return;if(d(i,';')){++i;continue;}current_begin=ts[i].begin;current_end=e;if(ts[i].kind!=css_variable_token_kind::ident){invalid();return;}auto property=ident(i++);
   if(!d(i,':')){invalid();return;}++i;size_t start=i;
   while(i<z&&!d(i,';')){if(!step())return;if(d(i,'{')||d(i,'}')){invalid();return;}
    if(ts[i].kind==css_variable_token_kind::function||ts[i].kind==css_variable_token_kind::open){size_t close;if(!pair(i,z,close)){invalid();return;}i=close+1;}
    else if(ts[i].kind==css_variable_token_kind::close){invalid();return;}else ++i;}
   current_end=i<z?ts[i].begin:e;size_t end=i;if(end>=start+2&&d(end-2,'!')&&ts[end-1].kind==css_variable_token_kind::ident&&ident(end-1)=="important")end-=2;
   if(start==end){if(property.starts_with("--"))continue;invalid();return;}
   auto value=s.substr(ts[start].begin,ts[end-1].end-ts[start].begin);
   if(h14::variable_source(property,value)){out.variables=true;for(size_t j=start;j<end;++j)if(ts[j].kind==css_variable_token_kind::function)out.functions.insert(ts[j].begin);}
   // H18 validates display:contents using resolved native styles before layout.
   if(property=="perspective" || property=="transform-style" || property=="rotate" || property=="scale" || property=="translate") {
    const auto keyword=ts[start].kind==css_variable_token_kind::ident&&end==start+1?ident(start):std::string{};
    if(keyword!=(property=="transform-style"?"flat":"none") && keyword!="initial" && keyword!="unset")invalid();
   }
   if(!h7::declaration(property,value)||!h8::declaration(property,value))invalid();
   if(h5_rgba_declaration_supported(property,value))for(size_t j=start;j<end;++j)if(ts[j].kind==css_variable_token_kind::function)out.functions.insert(ts[j].begin);
  }
 }
 void rules(size_t a,size_t z,unsigned depth,bool frames=false){
  if(depth>32){invalid();return;}
  size_t i=a;while(i<z){if(!spend(budget->rules)||!step())return;size_t start=i,open=i;current_begin=ts[i].begin;current_end=ts[z-1].end;current_selector_begin=SIZE_MAX;current_selector_end=SIZE_MAX;
   // Prelude only: balanced parentheses/brackets cannot consume a rule block.
   while(open<z&&!d(open,'{')){if(!step())return;if(d(open,';')||d(open,'}')){invalid();return;}
    if(ts[open].kind==css_variable_token_kind::function||d(open,'(')||d(open,'[')){size_t close;if(!pair(open,z,close)){invalid();return;}open=close+1;}else ++open;}
   if(open==z||open==start){invalid();return;}size_t close;if(!pair(open,z,close)){invalid();return;}
   if(frames){
    // H7 owns frame selectors and allowed animated properties. They are not DOM selectors.
    if(ts[start].serialization==css_variable_serialization::at_keyword){invalid();return;}
    declarations(open+1,close,depth,true,ts[start].begin,ts[open].begin);
   }else if(ts[start].serialization==css_variable_serialization::at_keyword){
    std::string name=h4::identifier(s,ts[start].begin+1,ts[start].end);
    if(name=="media"&&depth<32){auto b=ts[start].end;auto ev=media_condition_profile(std::string_view(s).substr(b,ts[open].begin-b));
     if(ev.full_consumption&&ev.profile_supported&&!ev.saw_unknown_or_discarded)out.at_rules.insert(ts[start].begin);else invalid();
     // Always analyze descendants, even screen-false print/media branches.
     rules(open+1,close,depth+1);
    }else if(name=="supports"&&depth<32){
     auto b=ts[start].end;auto condition=s.substr(b,ts[open].begin-b);
     auto ev=supports_condition_probe(condition,supports_budget);
     if(ev.syntax_valid&&ev.profile_supported&&!ev.budget_exhausted&&ev.consumed==condition.size())out.at_rules.insert(ts[start].begin);else invalid();
     // Truth does not erase evidence: inspect inactive bodies as well.
     rules(open+1,close,depth+1);
    }else if(name=="keyframes"){
     const auto name_source=s.substr(ts[start].end,ts[open].begin-ts[start].end);
     const auto body=s.substr(ts[open].end,ts[close].begin-ts[open].end);
     if(!spend(budget->steps,body.size()))return;
     if(h7::keyframes(name_source,body))out.at_rules.insert(ts[start].begin);else invalid();
     rules(open+1,close,depth+1,true);
    }else invalid();
   }else{
    current_selector_begin=ts[start].begin;current_selector_end=ts[open].begin;
    auto source=std::string_view(s).substr(ts[start].begin,ts[open].begin-ts[start].begin);
    if(!selector_profile_supported(source))invalid();
    else for(size_t j=start;j<open;++j)if(ts[j].kind==css_variable_token_kind::function&&h4::identifier(s,ts[j].begin,ts[j].end-1)=="nth-child")out.functions.insert(ts[j].begin);
    declarations(open+1,close,depth,false,ts[start].begin,ts[open].begin);
   }i=close+1;
  }
 }
 Analysis run(bool sheet){
  current_begin=0;current_end=s.size();if(!budget)budget=&local;
  if(!spend(budget->bytes,s.size()))return out;
  css_variable_budget budget{s.size()*8+1024,false};css_variable_lexer lexer(s.data(),s.size(),0,&budget);
  for(;;){auto t=lexer.next();if(t.kind==css_variable_token_kind::end)break;if(t.kind==css_variable_token_kind::error||budget.exhausted){current_begin=t.begin;current_end=t.end;invalid();out.lexical_failure=t.kind==css_variable_token_kind::error;out.limit_failure=budget.exhausted;return out;}
   if(!spend(this->budget->tokens))return out;
   if(t.kind!=css_variable_token_kind::comment&&t.kind!=css_variable_token_kind::whitespace)ts.push_back(t);}
  if(sheet)rules(0,ts.size(),0);else declarations(0,ts.size());return out;
 }
};
inline Analysis analyze(const std::string& s,bool sheet,WorkBudget* budget=nullptr){Scanner scanner{s};scanner.budget=budget;return scanner.run(sheet);}
}
