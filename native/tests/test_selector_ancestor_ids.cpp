// Small ordinary-DOM tests. Allocation tracking is enabled only across the
// admitted no-pseudo matcher calls: DOM construction/parser allocations are out.
#include "style_runtime_types.h"
#include "selector_match_context.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#ifdef AETHER_ANCESTOR_COUNTED
#include "ancestor_id_counted/counters.h"
#endif

namespace {
bool tracking=false;
size_t calls=0, live=0, peak=0, total=0, fail_at=SIZE_MAX;
struct alignas(std::max_align_t) Header { size_t size; bool tracked; };
}
void* operator new(size_t n) {
    if(tracking && calls++==fail_at) throw std::bad_alloc();
    if(n>SIZE_MAX-sizeof(Header)) throw std::bad_alloc();
    auto* h=static_cast<Header*>(std::malloc(sizeof(Header)+(n?n:1)));
    if(!h) throw std::bad_alloc();
    h->size=n; h->tracked=tracking;
    if(tracking) {live+=n;total+=n;peak=std::max(peak,live);}
    return h+1;
}
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {if(p){auto* h=static_cast<Header*>(p)-1;if(h->tracked)live-=h->size;std::free(h);}}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete(void* p,size_t) noexcept {::operator delete(p);}
void operator delete[](void* p,size_t) noexcept {::operator delete(p);}
struct selector_match_context_test_access {
    static int state(const selector_match_context_s& c){return static_cast<int>(c.ancestor_state_);}
    static auto subject(const selector_match_context_s& c){return c.ancestor_subject_;}
    static size_t count(const selector_match_context_s& c){return c.ancestor_ids_?c.ancestor_ids_->size():0;}
    static size_t capacity(const selector_match_context_s& c){return c.ancestor_ids_?c.ancestor_ids_->capacity():0;}
    static size_t strings(const selector_match_context_s& c){size_t n=0;if(c.ancestor_ids_)for(auto& s:*c.ancestor_ids_)n+=s.capacity()+1;return n;}
};
namespace {
using Access=selector_match_context_test_access;
size_t checks=0, failures=0;
void check(bool v,const char* label){++checks;if(!v){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
compiled_selector_s parse(const std::string& text){compiled_selector_s s;check(selector_parse(text.c_str(),s),text.c_str());return s;}
struct Tree {
    aether_node_s subject;
    std::vector<std::unique_ptr<aether_node_s>> parents;
    Tree(size_t links,size_t ids=0,size_t id_bytes=3){
        subject.type=AETHER_NODE_ELEMENT;subject.tag_name="span";subject.attributes["class"]="target";subject.attributes["id"]="self";
        auto* prev=&subject;
        for(size_t i=0;i<links;++i){auto n=std::make_unique<aether_node_s>();n->type=AETHER_NODE_ELEMENT;n->tag_name="div";
            if(i<ids)n->attributes["id"]=std::string(id_bytes,'q');
            prev->parent=n.get();n->children.push_back(prev);prev=n.get();parents.push_back(std::move(n));}
    }
};
void start(){check(!tracking && live==0,"clean ledger start");calls=peak=total=0;fail_at=SIZE_MAX;tracking=true;}
void audit(const selector_match_context_s& c){
    const size_t R=128+64*sizeof(std::string)+8192;
    check(Access::capacity(c)<=64,"vector physical cap");check(Access::strings(c)<=8192,"string physical cap");
    check(live<=R && peak<=2*R,"actual requested scratch retention and transient peak");
}
void finish(const char* label){tracking=false;check(live==0,"scratch released at context destruction");std::printf("ledger %s allocations=%zu total=%zu peak=%zu overhead-per-allocation=%zu\n",label,calls,total,peak,sizeof(Header));}
void basic(){
    Tree t(4,2);t.parents[0]->attributes["id"]="present";t.parents[1]->attributes["id"]="ñ☃";
    const auto miss=parse("#missing .target"),positive=parse("#present .target"),rhs=parse("#missing .target[data-missing]");
    start();{
        selector_match_context_s c(true);
        check(!selector_match(rhs,&t.subject,c),"RHS false");check(!Access::subject(c)&&Access::state(c)==0&&!calls,"RHS preserves untouched state and zero allocations");
        check(selector_match(positive,&t.subject,c),"first positive");check(Access::state(c)==0&&!calls,"positive stays unseen without scratch");
        check(!selector_match(miss,&t.subject,c),"first negative");check(Access::state(c)==1&&Access::count(c)==2,"negative publishes complete inventory");
        const size_t saved=calls;
        for(int i=0;i<8;++i){check(!selector_match(miss,&t.subject,c),"complete negative");check(selector_match(positive,&t.subject,c),"complete positive continues original");}
        check(saved==calls,"reuse never allocates");audit(c);
    }finish("basic");
    {auto self=parse("#self .target");selector_match_context_s c(true);check(!selector_match(self,&t.subject,c),"subject own ID explicitly excluded");}
    for(const char* text:{"#self .target","#Present .target","#present .target","#pr\\65 sent .target","#ñ☃ .target","#present .missing","#present .target[data-missing]"}){
        const auto s=parse(text);selector_match_context_s c(true);
        for(int i=0;i<3;++i)check(selector_match(s,&t.subject,c)==selector_match(s,&t.subject),"ordinary differential");
    }
    for(const char* text:{"#present > .target","#present + .target","#present ~ .target","div#present .target","#present#present .target","#present.x .target","#present[data-x] .target","#present:hover .target","#present .target:hover","#present div .target","#present .target::before",":is(#present .target)",".target"}){
        const auto s=parse(text);selector_match_context_s c(true);
        check(selector_match(s,&t.subject,c)==selector_match(s,&t.subject),"nonadmitted differential");check(!Access::subject(c),"nonadmitted leaves filter untouched");
    }
    auto empty=parse("#present .target");empty.steps[1].compound.ids[0].clear();{selector_match_context_s c(true);check(selector_match(empty,&t.subject,c)==selector_match(empty,&t.subject),"empty compiled ID fallback");check(!Access::subject(c),"empty required ID not admitted");}
    start();{selector_match_context_s c;check(!selector_match(miss,&t.subject,c),"default unchanged");check(!calls&&!Access::subject(c),"default context disabled");}finish("default-disabled");
}
void boundaries(){
    const auto miss=parse("#missing .target"),present=parse("#qqq .target");
    for(size_t links:{0,1,127,128,129})for(size_t ids:{0,1,64,65}){
        if(ids>links)continue;Tree t(links,ids);start();{
            selector_match_context_s c(true);check(!selector_match(miss,&t.subject,c),"boundary negative");
            const bool complete=links<=128&&ids<=64;check(Access::state(c)==(complete?1:2),"exact link and occurrence caps");
            const size_t saved=calls;check(selector_match(present,&t.subject,c)==(ids!=0),"boundary positive fallback");check(!selector_match(miss,&t.subject,c),"boundary repeated negative");
            check(calls==saved,"no cap retry");audit(c);
        }finish("links/ids");
    }
    for(size_t length:{15,16,4095,4096,4097}){
        Tree t(2,1,length);const auto pos=parse("#"+std::string(length,'q')+" .target");start();{
            selector_match_context_s c(true);check(!selector_match(miss,&t.subject,c),"byte cap negative");check(Access::state(c)==(length<=4096?1:2),"exact byte cap");
            const size_t saved=calls;check(selector_match(pos,&t.subject,c),"long ID positive fallback");check(calls==saved,"long ID no retry");if(length==4097)check(!calls,"oversize rejected before copy/allocation");audit(c);
        }finish("bytes");
    }
    for(size_t length:{2048,2049}){
        Tree t(3,2,length);start();{selector_match_context_s c(true);
            check(!selector_match(miss,&t.subject,c),"aggregate byte cap negative");
            check(Access::state(c)==(length==2048?1:2),"byte cap is total not per ID");audit(c);
        }finish("aggregate-bytes");
    }
    for(size_t links:{128,129}){Tree t(links,links);start();{selector_match_context_s c(true);for(int i=0;i<32;++i)check(selector_match(present,&t.subject,c),"deep immediate-parent positive");check(!calls&&Access::state(c)==0&&!Access::count(c),"exclusively positive deep duplicate chain never collects");}finish("deep-positive");}
    Tree t(3,0);t.parents[0]->attributes["id"]="";t.parents.back()->type=AETHER_NODE_DOCUMENT;
    {selector_match_context_s c(true);check(!selector_match(miss,&t.subject,c),"empty ancestry negative");check(Access::state(c)==1&&!Access::count(c),"complete empty not unavailable");}
}
void admission_boundaries(){
    Tree t(3,1);const auto miss=parse("#missing .target"),positive=parse("#qqq .target");
    // Static matcher-only flags prove original-subject admission. No public
    // generated updates, lifetime mutation or ownership paths are exercised.
    aether_node_s synthetic;synthetic.type=AETHER_NODE_ELEMENT;
    synthetic.is_generated=true;synthetic.pseudo_element=aether_pseudo_element_t::before;synthetic.style_origin=&t.subject;
    {selector_match_context_s c(true);check(selector_match(positive,&synthetic,c)==selector_match(positive,&synthetic),"generated entry unchanged normalization");check(!Access::subject(c),"generated original subject never admits");}
    synthetic.is_generated=false;synthetic.anonymous_flex_owner=&t.subject;
    {selector_match_context_s c(true);check(!selector_match(miss,&synthetic,c)&&!Access::subject(c),"anonymous original not admitted");}
    for(int kind=0;kind<2;++kind){
        t.parents[0]->is_generated=kind==0;t.parents[0]->anonymous_flex_owner=kind==1?&t.subject:nullptr;
        selector_match_context_s c(true);check(!selector_match(positive,&t.subject,c),"synthetic parent boundary prevents traversal");check(Access::state(c)==1&&Access::count(c)==0,"boundary complete inventory empty");
    }
    {selector_match_context_s c(true);check(!selector_match(miss,nullptr,c)&&!Access::subject(c),"null entry unchanged");}
}
void changes(){
    const auto miss=parse("#missing .target"),positive=parse("#qqq .target"),rhs=parse("#missing .target[data-missing]");
    Tree a(4,1),b(2,0);start();{
        selector_match_context_s c(true);check(!selector_match(miss,&a.subject,c),"A negative");auto* old=Access::subject(c);size_t old_calls=calls;
        check(!selector_match(rhs,&b.subject,c),"other-subject RHS failure");check(Access::subject(c)==old&&calls==old_calls,"failed RHS preserves prior subject and inventory");
        check(!selector_match(miss,&b.subject,c),"B negative");check(Access::subject(c)==&b.subject&&Access::count(c)==0&&live==0,"new empty subject releases old strings and vector");
        check(!selector_match(miss,&a.subject,c),"A again");check(calls>old_calls&&Access::subject(c)==&a.subject,"A B A reconstructs not historical map");audit(c);
    }finish("subject-changes");
    {selector_match_context_s c(true);check(selector_match(positive,&a.subject,c),"before ID mutation");}
    a.parents[0]->attributes["id"]="changed";
    {selector_match_context_s c(true);check(!selector_match(positive,&a.subject,c),"new context observes ID mutation");}
    a.subject.parent=b.parents[0].get();b.parents[0]->attributes["id"]="qqq";
    {selector_match_context_s c(true);check(selector_match(positive,&a.subject,c),"new context observes ordinary reparent");}
}
void operation_counts(){
#ifdef AETHER_ANCESTOR_COUNTED
    const auto miss=parse("#missing .target"),pos=parse("#qqq .target"),rhs=parse("#missing .target[data-missing]");
    for(size_t links:{4,128,129}) {
        Tree t(links,1);selector_match_context_s c(true);ancestor_counters={};
        check(!selector_match(rhs,&t.subject,c),"counted RHS false");
        check(ancestor_counters.admitted==1&&ancestor_counters.rhs==1&&!ancestor_counters.continuations&&!ancestor_counters.collections,"RHS exactly once no ancestor work");
        ancestor_counters={};check(!selector_match(miss,&t.subject,c),"counted first negative");
        check(ancestor_counters.rhs==1&&ancestor_counters.continuations==1&&ancestor_counters.ancestor_links==links,"exact one original negative continuation");
        check(ancestor_counters.collections==1&&ancestor_counters.collection_links==std::min(links,size_t(128)),"one bounded collection after first negative");
        ancestor_counters={};check(!selector_match(miss,&t.subject,c),"counted second negative");
        check(ancestor_counters.rhs==1&&ancestor_counters.continuations==(links<=128?0u:1u)&&!ancestor_counters.collections,"complete rejects unavailable continues no retry");
        ancestor_counters={};check(selector_match(pos,&t.subject,c),"counted positive membership");
        check(ancestor_counters.rhs==1&&ancestor_counters.continuations==1&&ancestor_counters.ancestor_links==1&&!ancestor_counters.collections,"complete/unavailable positive original continuation only");
    }
    for(size_t links:{128,129}) {Tree t(links,links);selector_match_context_s c(true);ancestor_counters={};
        for(int i=0;i<32;++i)check(selector_match(pos,&t.subject,c),"counted deep positives");
        check(ancestor_counters.rhs==32&&ancestor_counters.continuations==32&&ancestor_counters.ancestor_links==32&&!ancestor_counters.collections&&!ancestor_counters.copies,"deep positives exactly one ancestor each zero collection");
    }
#endif
}
void oom(){
    // Two short fixtures only. Tracking never surrounds DOM/parser/runtime work.
    const auto miss=parse("#missing .target"),positive=parse("#"+std::string(80,'q')+" .target");
    Tree t(3,2,80);size_t sites=0;
    start();{selector_match_context_s c(true);check(!selector_match(miss,&t.subject,c),"OOM successful reference");sites=calls;audit(c);}finish("oom-reference");
    check(sites>0&&sites<20,"bounded scratch allocation sites");
    for(size_t failure=0;failure<sites;++failure){start();fail_at=failure;{
        selector_match_context_s c(true);
#ifdef AETHER_ANCESTOR_COUNTED
        ancestor_counters={};
#endif
        bool threw=false;try{check(!selector_match(miss,&t.subject,c),"OOM keeps already proven false");}catch(const std::bad_alloc&){threw=true;}
        check(!threw&&Access::state(c)==2&&live==0,"scratch OOM releases partial and marks unavailable");
#ifdef AETHER_ANCESTOR_COUNTED
        check(ancestor_counters.rhs==1&&ancestor_counters.continuations==1&&ancestor_counters.collections==1,"OOM first negative has one RHS/search/collection without second continuation");
#endif
        fail_at=SIZE_MAX;size_t saved=calls;check(selector_match(positive,&t.subject,c),"OOM unavailable positive fallback");check(!selector_match(miss,&t.subject,c),"OOM unavailable negative fallback");check(calls==saved,"OOM never retries current subject");audit(c);
    }finish("oom-site");}
}
}
int main(){basic();boundaries();admission_boundaries();changes();operation_counts();oom();
#if defined(__GLIBCXX__) && INTPTR_MAX == INT64_MAX
    check(sizeof(selector_match_context_s)<=128+128,"context growth at most 128 bytes from baseline128");
#endif
    std::printf("ancestor IDs: %zu checks, %zu failures; context=%zu\n",checks,failures,sizeof(selector_match_context_s));return failures?1:0;
}
