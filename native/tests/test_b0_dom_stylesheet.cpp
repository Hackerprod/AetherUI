#include "../src/style_runtime/style_runtime_core.cpp"
#include <cstdio>
#include <cstdlib>
#include <new>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(size_t n){if(fail_after>=0 && fail_after--==0){fault_delivered=true;throw std::bad_alloc();}if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p)noexcept{std::free(p);} void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);} void operator delete[](void* p,size_t)noexcept{std::free(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#include "fault_inject_aligned.h"
static size_t checks=0;
static void check(bool v,const char* msg){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",msg);std::exit(1);}}
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_node_t first{},second{},target{};
 Fixture(const char* a=".x{color:#112233}",const char* b=".x{color:#445566}") {
  check(aether_doc_create(&doc)==AETHER_OK,"doc");
  std::string html="<html><head><style id='a'>";html+=a;html+="</style><style id='b'>";html+=b;html+="</style></head><body><p id='x' class='x'>X</p></body></html>";
  check(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse");first=aether_doc_get_element_by_id(doc,"a");second=aether_doc_get_element_by_id(doc,"b");target=aether_doc_get_element_by_id(doc,"x");
  check(first&&second&&target,"nodes");check(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&rt)==AETHER_OK,"runtime");
 }
 ~Fixture(){aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 int64_t add(aether_node_t n,size_t ordinal){int64_t h=-1;check(aether::style::add_dom_stylesheet(rt,doc,n,ordinal,&h)==AETHER_OK,"associate");return h;}
 style_source_accounting_s accounting(){aether_style_transaction_t tx{};check(aether_style_runtime_prepare_revision(rt,doc,nullptr,&tx)==AETHER_OK,"prepare");auto a=tx->candidate->source_accounting;aether_style_transaction_destroy(tx);return a;}
};
static void exact_and_partial(){
 Fixture f;auto unassociated=f.accounting();check(unassociated.external_pending,"unassociated external");
 int64_t unrelated{};check(aether_style_runtime_add_stylesheet(f.rt,".x{color:#112233}",2,nullptr,&unrelated,nullptr)==AETHER_OK,"unrelated add");
 check(f.accounting().external_pending,"same bytes public not association");
 check(aether_style_runtime_remove_stylesheet(f.rt,unrelated),"remove unrelated");
 const auto a=f.add(f.first,0);check(f.accounting().external_pending,"partial still external");
 const auto b=f.add(f.second,1);check(a!=b,"distinct handles");auto v=f.accounting();
 check(aether::style::b0_source_accounting_accepted(v),"exact association accepted");
 check(v.source_bytes==std::strlen(".x{color:#112233}")+std::strlen(".x{color:#445566}")&&v.rules==2&&v.declarations==2,"single source count");
 check(aether::style::add_dom_stylesheet(f.rt,f.doc,f.second,1,nullptr)==AETHER_ERROR_INVALID_STATE,"duplicate node rejected");
 Fixture other;check(aether::style::add_dom_stylesheet(f.rt,f.doc,other.first,2,nullptr)==AETHER_ERROR_INVALID_ARGUMENT,"foreign node rejected");
 check(aether::style::add_dom_stylesheet(other.rt,f.doc,f.second,1,nullptr)==AETHER_ERROR_INVALID_ARGUMENT,"foreign runtime cannot claim prior ordinal");
}
static void counts_with_ua_inline(){
 Fixture f;
 const char* ua="p{display:block}";const char* in="font-weight:700";
 check(aether_element_set_attribute(f.target,"style",in)==AETHER_OK,"valid inline before association");
 check(aether_style_runtime_add_stylesheet(f.rt,ua,0,nullptr,nullptr,nullptr)==AETHER_OK,"UA source");
 f.add(f.first,0);f.add(f.second,1);const auto a=f.accounting();
 check(aether::style::b0_source_accounting_accepted(a),"UA stylesheet inline complete");
 check(a.source_bytes==std::strlen(ua)+std::strlen(in)+std::strlen(".x{color:#112233}")+std::strlen(".x{color:#445566}")&&a.rules==3&&a.declarations==4,"UA plus DOM plus inline counted once");
}
static void stale(){
 {Fixture f;f.add(f.first,0);f.add(f.second,1);auto parent=f.first->parent;
  check(aether_node_remove_child(parent,f.first)==AETHER_OK,"detach without reinsertion a");
  check(aether_node_remove_child(parent,f.second)==AETHER_OK,"detach without reinsertion b");
  check(f.accounting().external_pending,"absent style associations remain stale");
  aether_destroy_dom_subtree(f.first);aether_destroy_dom_subtree(f.second);
  check(f.accounting().external_pending,"destroyed absent style associations remain stale");}
 {Fixture f;f.add(f.first,0);f.add(f.second,1);aether_doc_t other{};
  check(aether_doc_create(&other)==AETHER_OK,"other doc for prepare");
  check(aether_doc_parse_html(other,"<html><body><p>X</p></body></html>")==AETHER_OK,"other doc no style");
  aether_style_transaction_t tx{};check(aether_style_runtime_prepare_revision(f.rt,other,nullptr,&tx)==AETHER_OK,"prepare unrelated doc without style");
  check(tx->candidate->source_accounting.external_pending,"foreign prepare document never inherits association");aether_style_transaction_destroy(tx);aether_doc_destroy(other);}

 {Fixture f;f.add(f.first,0);f.add(f.second,1);f.first->children[0]->text=".x{color:#aabbcc}";check(f.accounting().external_pending,"exact bytes mismatch without epoch");}
 {Fixture f;f.add(f.first,0);f.add(f.second,1);auto parent=f.first->parent;check(aether_node_remove_child(parent,f.first)==AETHER_OK,"detach");check(aether_node_append_child(parent,f.first)==AETHER_OK,"reinsert");check(f.accounting().external_pending,"detach reinsert invalidates");}
 {Fixture f;const auto h=f.add(f.first,0);f.add(f.second,1);check(aether_style_runtime_remove_stylesheet(f.rt,h),"remove associated");int64_t next{};check(aether_style_runtime_add_stylesheet(f.rt,".x{color:#112233}",2,nullptr,&next,nullptr)==AETHER_OK&&next!=h,"new handle no identity");check(f.accounting().external_pending,"replacement no association");}
 {Fixture f;f.add(f.first,0);f.add(f.second,1);auto token=f.doc->identity;aether_doc_destroy(f.doc);f.doc=nullptr;check(!token->alive,"destroyed doc token dead");aether_doc_t replacement{};check(aether_doc_create(&replacement)==AETHER_OK,"replacement doc");check(aether_doc_parse_html(replacement,"<style>.x{color:#112233}</style><p>X</p>")==AETHER_OK,"replacement parse");f.doc=replacement;check(f.accounting().external_pending,"fresh DOM never inherits stale association");}
}
static void order(){
 {Fixture f(".x{color:#112233}",".x{color:#112233}");f.add(f.first,0);f.add(f.second,1);auto a=f.accounting();check(a.rules==2&&a.declarations==2&&!a.external_pending,"identical styles counted separately");}
 {Fixture f;int64_t h=-55;check(aether::style::add_dom_stylesheet(f.rt,f.doc,f.first,1,&h)==AETHER_ERROR_INVALID_ARGUMENT&&h==-55,"wrong ordinal rejected");f.add(f.first,0);f.add(f.second,1);aether_style_transaction_t tx{};check(aether_style_runtime_prepare_revision(f.rt,f.doc,nullptr,&tx)==AETHER_OK,"order prepare");const auto& c=tx->candidate->states.at(f.target)->style_view().scalars().color;check(c.r>0.26f&&c.r<0.28f,"later DOM style wins");aether_style_transaction_destroy(tx);std::swap(f.first->parent->children[0],f.first->parent->children[1]);check(f.accounting().external_pending,"order mismatch without epoch rejected");}
 {Fixture f;int64_t h{};check(aether_style_runtime_add_stylesheet(f.rt,"p{color:#ffffff}",0,nullptr,&h,nullptr)==AETHER_OK,"UA before");f.add(f.first,0);f.add(f.second,1);check(aether_style_runtime_remove_stylesheet(f.rt,h),"remove predecessor");check(f.accounting().external_pending,"effective predecessor order invalidates");}
}
static void losses(){
 for(const char* bad:{".missing{unknown-property:1}",".missing{width:invalid}","@media(min-width:0px){p{color:#ffffff}}","@import 'x';","p{display:none}","p > b{color:#ffffff}","p{padding:1px}","p{font-size:12.5px}","p{color:#ffffff}/*unterminated","p{color:#ffffff"}) {
  Fixture f(bad);f.add(f.first,0);f.add(f.second,1);auto loss=f.accounting();if(aether::style::b0_source_accounting_accepted(loss))std::fprintf(stderr,"unexpectedly accepted %s\n",bad);check(!aether::style::b0_source_accounting_accepted(loss),"bad associated source rejected");
 }
 {Fixture f;check(aether_element_set_attribute(f.target,"style","unknown-property:1")==AETHER_OK,"inline set before association");f.add(f.first,0);f.add(f.second,1);auto a=f.accounting();check(a.losses&&!aether::style::b0_source_accounting_accepted(a),"inline loss retained");}
}
static void oom(){
 Fixture f;f.add(f.first,0);const auto next=f.rt->next_sheet_handle;const auto rule=f.rt->next_rule_id;const auto epoch=f.rt->sheet_epoch;size_t failures=0;bool success=false;
 for(long n=0;n<2000;++n){int64_t output=-99;fault_delivered=false;fail_after=n;const auto rc=aether::style::add_dom_stylesheet(f.rt,f.doc,f.second,1,&output);fail_after=-1;
  if(rc==AETHER_OK){success=true;check(output==next,"retry same handle");break;}
  check(rc==AETHER_ERROR_OUT_OF_MEMORY&&fault_delivered,"injected OOM returned");++failures;
  check(output==-99&&f.rt->sheets.size()==1&&f.rt->next_sheet_handle==next&&f.rt->next_rule_id==rule&&f.rt->sheet_epoch==epoch,"OOM publication atomic");check(f.accounting().external_pending,"OOM did not publish coverage");
 }
 check(success&&failures>10,"all publication allocations swept");check(aether::style::b0_source_accounting_accepted(f.accounting()),"retry accepted");std::printf("OOM points=%zu\n",failures);
}
int main(){exact_and_partial();counts_with_ua_inline();stale();order();losses();oom();std::printf("B0 provenance checks=%zu PASS\n",checks);}
