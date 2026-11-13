#ifndef AETHER_REUSABLE_PAYLOAD_CAPTURE
#define AETHER_REUSABLE_PAYLOAD_CAPTURE 1
#endif
#include "../src/style_runtime/style_runtime_core.cpp"
#include <cstdio>
#include <cstdlib>
static long fail_after=-1;
void* operator new(size_t n) { if(fail_after>=0 && fail_after--==0) throw std::bad_alloc(); if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void resolve(aether_style_runtime_t r,aether_doc_t d,aether_node_t hint=nullptr){aether_style_update_t* u=nullptr;auto g=r->generation;if(!hint)hint=d;int n=aether_style_runtime_resolve_state_changes(r,d,&hint,1,&u);CHECK(r->generation>=g+1);aether_style_updates_free(u,n);}
struct Reentry {aether_style_runtime_t rt{};aether_doc_t doc{};aether_node_t node{};bool armed=false,done=false;};
static void diagnostic(const char*,const char*,const char*,const char*,void* p){auto& x=*static_cast<Reentry*>(p);if(x.armed){x.armed=false;aether_element_set_state(x.node,AETHER_STATE_HOVER);resolve(x.rt,x.doc);x.done=true;}}
int main(){
 aether_doc_t d{};CHECK(aether_doc_create(&d)==AETHER_OK);CHECK(aether_doc_parse_html(d,"<html><body><div id='a'></div><div id='b'></div></body></html>")==AETHER_OK);
 auto* a=aether_doc_get_element_by_id(d,"a");auto* b=aether_doc_get_element_by_id(d,"b");auto* parent=a->parent;
 auto base=HoverProvenance::capture(d);auto epoch=d->identity->mutation_epoch;
 aether_element_set_state(a,AETHER_STATE_HOVER);aether_element_set_state(b,AETHER_STATE_HOVER);
 CHECK(d->identity->mutation_epoch==epoch+2);CHECK(HoverProvenance::capture(d).only_hover_delta_from(base));
 aether_element_set_state(b,AETHER_STATE_FOCUS);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(base));
 aether_element_set_state(b,0);CHECK(aether_element_set_attribute(a,"class","x")==AETHER_OK);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(base));
 base=HoverProvenance::capture(d);CHECK(aether_node_remove_child(parent,a)==AETHER_OK);CHECK(aether_node_append_child(parent,a)==AETHER_OK);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(base));
 // Failed id update may erase an old index entry before allocation fails.
 base=HoverProvenance::capture(d);fail_after=0;try{aether_element_set_attribute(a,"id","abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");}catch(const std::bad_alloc&){}fail_after=-1;CHECK(!HoverProvenance::capture(d).only_hover_delta_from(base));
 Reentry x{};x.doc=d;x.node=a;CHECK(aether_style_runtime_create(nullptr,nullptr,diagnostic,&x,&x.rt)==AETHER_OK);
 CHECK(aether_style_runtime_add_stylesheet(x.rt,"div{color:red}div:hover{color:blue}",2,"test",nullptr,nullptr)==AETHER_OK);
 resolve(x.rt,d);auto before_incomplete=x.rt->hover_provenance;
 aether_element_set_state(b,AETHER_STATE_FOCUS);aether_element_set_state(a,0);resolve(x.rt,d,a);
 CHECK(!x.rt->hover_provenance.only_hover_delta_from(before_incomplete));
 bool saw_focus=false;for(auto& e:x.rt->hover_provenance.entries)if(e.identity==b->identity){CHECK(e.bits==AETHER_STATE_FOCUS);saw_focus=true;}CHECK(saw_focus);
 aether_element_set_state(b,0);resolve(x.rt,d);
 auto committed=x.rt->hover_provenance;auto generation=x.rt->generation;
#if AETHER_REUSABLE_PAYLOAD_CAPTURE
 auto committed_payload=x.rt->reusable_payload;CHECK(committed_payload);CHECK(committed_payload->published_generation==generation);CHECK(committed_payload->source_style_tree_epoch+1==committed_payload->published_style_tree_epoch);
#endif
 // Every allocation in a complete preparation is faulted in turn. Publication
 // evidence must remain at the prior committed snapshot until a successful commit.
 int failed=0;for(int i=0;i<5000;++i){aether_style_transaction_t t=nullptr;fail_after=i;auto status=aether_style_runtime_prepare_revision(x.rt,d,nullptr,&t);fail_after=-1;CHECK(x.rt->generation==generation);
#if AETHER_REUSABLE_PAYLOAD_CAPTURE
CHECK(x.rt->reusable_payload==committed_payload);
#endif
CHECK(x.rt->hover_provenance.entries.size()==committed.entries.size());CHECK(x.rt->hover_provenance.non_state_epoch==committed.non_state_epoch);for(size_t j=0;j<committed.entries.size();++j){CHECK(x.rt->hover_provenance.entries[j].identity==committed.entries[j].identity);CHECK(x.rt->hover_provenance.entries[j].bits==committed.entries[j].bits);}if(status==AETHER_OK){aether_style_transaction_destroy(t);break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY);CHECK(!t);++failed;}CHECK(failed>10&&failed<4999);
 aether_style_transaction_t t=nullptr;CHECK(aether_style_runtime_prepare_revision(x.rt,d,nullptr,&t)==AETHER_OK);CHECK(aether_element_set_attribute(a,"class","changed")==AETHER_OK);aether_style_revision_t rev=nullptr;CHECK(aether_style_runtime_commit_revision(x.rt,t,&rev)==AETHER_ERROR_INVALID_STATE);CHECK(!rev);aether_style_transaction_destroy(t);CHECK(x.rt->generation==generation);
 CHECK(aether_element_set_attribute(a,"style","color:red;unknown-property:1; width:calc(1px +)")==AETHER_OK);aether_element_set_state(a,0);x.armed=true;resolve(x.rt,d);CHECK(x.done);
#if AETHER_REUSABLE_PAYLOAD_CAPTURE
CHECK(x.rt->reusable_payload->published_generation==x.rt->generation);
#endif
CHECK(x.rt->hover_provenance.only_hover_delta_from(HoverProvenance::capture(d)));
 bool found=false;for(auto& e:x.rt->hover_provenance.entries)if(e.identity==a->identity){CHECK(e.bits==AETHER_STATE_HOVER);found=true;}CHECK(found);
 // Cross-document reparent invalidates both roots and retains node identity.
 aether_doc_t other{};CHECK(aether_doc_create(&other)==AETHER_OK);CHECK(aether_doc_parse_html(other,"<main></main>")==AETHER_OK);
 auto old_d=HoverProvenance::capture(d),old_other=HoverProvenance::capture(other);auto same_b=b->identity;
 CHECK(aether_node_append_child(aether_doc_get_document_element(other),b)==AETHER_OK);
 CHECK(b->identity==same_b);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(old_d));CHECK(!HoverProvenance::capture(other).only_hover_delta_from(old_other));
 CHECK(aether_node_append_child(parent,b)==AETHER_OK);aether_doc_destroy(other);
 d->identity->non_state_epoch=UINT64_MAX-1;aether_node_touch(d);CHECK(d->identity->non_state_exhausted);base=HoverProvenance::capture(d);resolve(x.rt,d);CHECK(!x.rt->hover_provenance.only_hover_delta_from(base));
#if AETHER_REUSABLE_PAYLOAD_CAPTURE
CHECK(x.rt->reusable_payload->non_state_exhausted);
#endif
aether_node_touch(d);CHECK(d->identity->non_state_epoch==UINT64_MAX);CHECK(d->identity->non_state_exhausted);
 // Once saturated, failed attribute mutation cannot change the extra epoch.
 // Legacy transaction semantics may still commit; reuse MUST remain forbidden.
 t=nullptr;CHECK(aether_style_runtime_prepare_revision(x.rt,d,nullptr,&t)==AETHER_OK);
 fail_after=0;try{aether_element_set_attribute(a,"class","abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");}catch(const std::bad_alloc&){}fail_after=-1;
 rev=nullptr;auto saturated_status=aether_style_runtime_commit_revision(x.rt,t,&rev);
 CHECK(saturated_status==AETHER_OK||saturated_status==AETHER_ERROR_INVALID_STATE);
 CHECK(!x.rt->hover_provenance.only_hover_delta_from(base));
 if(rev)aether_style_revision_destroy(rev);aether_style_transaction_destroy(t);
 auto retained=a->identity;CHECK(aether_doc_parse_html(d,"<p>replacement</p>")==AETHER_OK);CHECK(!retained->alive);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(base));
 // Parser allocation failure after destructive replacement must invalidate provenance.
 auto partial_base=HoverProvenance::capture(d);fail_after=0;bool parser_failed=false;
 try { aether_doc_parse_html(d,"<section>partial</section>"); } catch(const std::bad_alloc&) { parser_failed=true; }
 fail_after=-1;CHECK(parser_failed);CHECK(!HoverProvenance::capture(d).only_hover_delta_from(partial_base));
 aether_style_runtime_destroy(x.rt);aether_doc_destroy(d);std::printf("PASS hover provenance; OOM points=%d; incomplete-list legacy, mixed, partial mutation, replacement, reentry, saturation\n",failed);
}
