#include "../src/style_runtime/css_scope_proof.h"
#include "../src/dom/dom_types.h"
#include <aether/aether.h>
#include <cassert>
#include <thread>
#include <iostream>
#include <cstdlib>
#include <new>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
using namespace aether::style::css_scope;
struct Doc {aether_doc_t d{};Doc(){assert(aether_doc_create(&d)==0);assert(aether_doc_parse_html(d,"<html><body><aside></aside><div id='target'><span></span></div></body></html>")==0);}~Doc(){if(d)aether_doc_destroy(d);}aether_node_t t(){return aether_doc_get_element_by_id(d,"target");}};
int main(){unsigned n=0;for(size_t count:{511,512,513,1023,1024,1025}){Doc d;std::string css;for(size_t i=0;i<count;++i)css+="missing{color:red}";Proof selected(d.d,d.t());auto e=selected.inspect(css);assert(e.boundary_complete==(count<=1024));assert(e.rules.size()==(count<=1024?count:0));Proof legacy(d.d);auto kept=legacy.retained(css);assert((count<=512?kept.empty():kept==css));++n;}
 {Doc d;Proof p(d.d,d.t());auto e=p.inspect("missing{color:red}html{color:red}aside+div{color:red}missing,span{color:red}#missing:hover{color:red}@media print{missing{color:red}}");assert(e.boundary_complete&&e.rules.size()==6);assert(e.rules[0].scope==Scope::proven_disjoint);for(int i:{1,2,3})assert(e.rules[i].scope==Scope::may_affect_scope);for(int i:{4,5})assert(e.rules[i].scope==Scope::unknown);++n;}
 {Doc d;Proof p(d.d,d.t());p.remaining=0;auto e=p.inspect("missing{color:red}");assert(e.boundary_complete&&e.rules[0].scope==Scope::unknown&&e.rules[0].reason==Reason::budget&&p.queries==0);++n;}
 {Doc d;Proof p(d.d,d.t());auto e=p.inspect("missing{color:red");assert(!e.boundary_complete&&e.rules.empty()&&e.reason==Reason::syntax);++n;}
 {Doc d;Proof p(d.d,d.t());std::thread t([&]{auto e=p.inspect("missing{color:red}");assert(!e.boundary_complete&&e.rules.empty());});t.join();++n;}
 {Doc d;Proof p(d.d,d.t());aether_element_set_attribute(d.t(),"style","color:red");auto e=p.inspect("missing{color:red}");assert(!e.boundary_complete);++n;}
 {Doc d;Proof p(d.d,d.t());aether_doc_destroy(d.d);d.d=nullptr;assert(!p.inspect("missing{color:red}").boundary_complete);++n;}
 for(size_t limit:{16384,16385}){aether_doc_t d{};aether_doc_create(&d);auto*t=new aether_node_s();t->type=AETHER_NODE_ELEMENT;t->tag_name="div";t->parent=d;d->children.push_back(t);for(size_t i=2;i<limit;++i){auto*x=new aether_node_s();x->type=AETHER_NODE_TEXT;x->parent=d;d->children.push_back(x);}Proof p(d,t);auto e=p.inspect("missing{color:red}");assert(e.boundary_complete==(limit<=16384));aether_doc_destroy(d);++n;}
 {Doc d,other;Proof p(d.d,other.t());assert(!p.inspect("missing{color:red}").boundary_complete);++n;}
 for(size_t count:{128,129}){Doc d;auto*t=d.t();for(size_t i=2;i<count;++i){auto*x=new aether_node_s();x->type=AETHER_NODE_TEXT;x->parent=t;t->children.push_back(x);}Proof p(d.d,t);assert(p.inspect("missing{color:red}").boundary_complete==(count<=128));++n;}
 {Doc d;bool succeeded=false;size_t failures=0;for(long budget=0;budget<1000&&!succeeded;++budget){SheetEvidence output;try{allocation_budget=budget;Proof proof(d.d,d.t());auto staged=proof.inspect("missing{color:red}html{color:blue}");allocation_budget=-1;output=std::move(staged);assert(output.boundary_complete&&output.rules.size()==2);succeeded=true;}catch(const std::bad_alloc&){allocation_budget=-1;assert(!output.boundary_complete&&output.rules.empty());++failures;}}assert(succeeded&&failures>0);std::cout<<"Selected OOM positions="<<failures<<"\n";++n;}
 std::cout<<"Selected evidence checks="<<n<<"\n";
}
