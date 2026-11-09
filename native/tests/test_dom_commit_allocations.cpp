// Linux link-unit allocation oracle. No observer or allocation API is added to
// the product. The executable owns the TLS ledger and every allocator wrapper.
#include <cstdio>
#if defined(AETHER_DOM_ALLOCATIONS_UNAVAILABLE)
int main() { std::puts("UNAVAILABLE DOM commit allocation coverage: unsupported toolchain/runtime/linkage"); return 77; }
#else
#include <aether/aether_dom_interaction.h>
#include <aether/aether_transition_sync.h>
#include "../src/animations/transition_runtime.h"
#include "../src/dom/dom_types.h"
#include <cstdlib>
#include <new>
#include <limits>
#include <cmath>
#include <thread>

namespace ledger {
enum Origin : unsigned { c, scalar, array, scalar_nothrow, array_nothrow, scalar_aligned, array_aligned, scalar_aligned_nothrow, array_aligned_nothrow, origin_count };
struct Counts { uint64_t requests=0, successes=0, origins[origin_count]{}; bool invalid=false; };
thread_local Counts counts{};
thread_local unsigned depth=0, wrapper_depth=0;
thread_local Origin origin=c;
void increment(uint64_t& value) noexcept { if(value==UINT64_MAX) counts.invalid=true; else ++value; }
void reset() noexcept { if(depth||wrapper_depth){counts.invalid=true;return;} counts={}; }
void begin() noexcept { if(depth==UINT32_MAX){counts.invalid=true;return;} ++depth; }
void end() noexcept { if(!depth)counts.invalid=true;else --depth; }
struct Scope { Scope() noexcept {begin();} ~Scope(){end();} };
struct Wrapper {
    bool active;
    Wrapper() noexcept :active(depth!=0) {
        if(wrapper_depth)counts.invalid=true;
        ++wrapper_depth;
        if(active){increment(counts.requests);increment(counts.origins[origin]);}
    }
    void result(bool success) noexcept {if(active&&success)increment(counts.successes);}
    ~Wrapper(){--wrapper_depth;}
};
struct Tag { Origin old; explicit Tag(Origin value):old(origin){origin=value;} ~Tag(){origin=old;} };
}
extern "C" {
void* __real_malloc(size_t); void* __real_calloc(size_t,size_t); void* __real_realloc(void*,size_t);
void* __real_aligned_alloc(size_t,size_t); int __real_posix_memalign(void**,size_t,size_t);
void* __wrap_malloc(size_t n){ledger::Wrapper w;auto* p=__real_malloc(n);w.result(p!=nullptr);return p;}
void* __wrap_calloc(size_t n,size_t s){ledger::Wrapper w;auto* p=__real_calloc(n,s);w.result(p!=nullptr);return p;}
void* __wrap_realloc(void* p,size_t n){ledger::Wrapper w;auto* q=__real_realloc(p,n);w.result(q!=nullptr);return q;}
void* __wrap_aligned_alloc(size_t a,size_t n){ledger::Wrapper w;auto* p=__real_aligned_alloc(a,n);w.result(p!=nullptr);return p;}
int __wrap_posix_memalign(void** p,size_t a,size_t n){ledger::Wrapper w;int status=__real_posix_memalign(p,a,n);w.result(status==0);return status;}
}
static void* allocate(size_t n,ledger::Origin origin,size_t alignment=0) {
    ledger::Tag tag(origin);
    for(;;){
        void* p=nullptr;
        if(alignment) {if(__wrap_posix_memalign(&p,std::max(alignment,sizeof(void*)),n?n:1)!=0)p=nullptr;}
        else p=__wrap_malloc(n?n:1);
        if(p)return p;
        // Preserve replacement-new behavior. A failed allocator is never a
        // successful zero-allocation sample; no handler is installed by this test.
        auto handler=std::get_new_handler();if(!handler)throw std::bad_alloc();handler();
    }
}
void* operator new(size_t n){return allocate(n,ledger::scalar);}
void* operator new[](size_t n){return allocate(n,ledger::array);}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return allocate(n,ledger::scalar_nothrow);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&)noexcept{try{return allocate(n,ledger::array_nothrow);}catch(...){return nullptr;}}
void* operator new(size_t n,std::align_val_t a){return allocate(n,ledger::scalar_aligned,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){return allocate(n,ledger::array_aligned,size_t(a));}
void* operator new(size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return allocate(n,ledger::scalar_aligned_nothrow,size_t(a));}catch(...){return nullptr;}}
void* operator new[](size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return allocate(n,ledger::array_aligned_nothrow,size_t(a));}catch(...){return nullptr;}}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}void operator delete[](void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,size_t,std::align_val_t)noexcept{std::free(p);}void operator delete[](void* p,size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}void operator delete[](void* p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL DOM commit allocations %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
static void zero(const char* label){CHECK(!ledger::counts.invalid&&!ledger::depth&&!ledger::wrapper_depth);if(ledger::counts.requests){std::fprintf(stderr,"%s: allocation requests=%llu\n",label,(unsigned long long)ledger::counts.requests);std::abort();}}
static aether_dom_string_v1_t str(const char* s){return {s,std::strlen(s)};}
static aether_dom_token_v1_t token(aether_node_t n){aether_dom_token_v1_t t{};ok(aether_dom_read_token_v1(n,&t));return t;}
// Explicit calls in a non-inlined function, with escaped output, prevent the
// compiler from eliding a new/delete-expression pair in positive controls.
static void* volatile escaped=nullptr;
__attribute__((noinline)) static void* positive(unsigned family,size_t n) {
    switch(family){
    case 0:return std::malloc(n);case 1:return std::calloc(1,n);
    case 2:return std::realloc(nullptr,n);case 3:return std::aligned_alloc(64,((n+63)/64)*64);
    case 4:{void* p=nullptr;if(::posix_memalign(&p,64,n))return nullptr;return p;}
    case 5:return ::operator new(n);case 6:return ::operator new[](n);
    case 7:return ::operator new(n,std::nothrow);case 8:return ::operator new[](n,std::nothrow);
    case 9:return ::operator new(n,std::align_val_t(64));case 10:return ::operator new[](n,std::align_val_t(64));
    case 11:return ::operator new(n,std::align_val_t(64),std::nothrow);
    default:return ::operator new[](n,std::align_val_t(64),std::nothrow);
    }
}
static void controls(size_t size){
    for(unsigned family=0;family<13;++family){ledger::reset();{ledger::Scope scope;escaped=positive(family,size);}CHECK(escaped);CHECK(!ledger::counts.invalid&&ledger::counts.requests==1&&ledger::counts.successes==1);CHECK(ledger::counts.origins[family<5?0:family-4]==1);std::free(escaped);escaped=nullptr;}
    ledger::reset();escaped=positive(0,size);CHECK(ledger::counts.requests==0);std::free(escaped);
    ledger::reset();{ledger::Scope a;{ledger::Scope b;escaped=positive(0,size);}}CHECK(ledger::counts.requests==1&&!ledger::depth);std::free(escaped);
    ledger::reset();{ledger::Scope scope;ledger::wrapper_depth=1;escaped=positive(0,size);ledger::wrapper_depth=0;}CHECK(ledger::counts.invalid);std::free(escaped);
    ledger::reset();{ledger::Scope scope;ledger::counts.requests=UINT64_MAX;escaped=positive(0,size);}CHECK(ledger::counts.invalid);std::free(escaped);
    ledger::reset();ledger::end();CHECK(ledger::counts.invalid);ledger::reset();
    std::thread worker([size]{CHECK(!ledger::depth&&ledger::counts.requests==0);ledger::reset();{ledger::Scope scope;void* p=positive(0,size);std::free(p);}CHECK(ledger::counts.requests==1);});worker.join();CHECK(ledger::counts.requests==0);
    ledger::reset();{ledger::Scope scope;}zero("empty scope");
}
// This must run before Fixture's compatibility create route: its first commit
// resolves the exact initial publication path, with all work prepared beforehand.
static void initial_binding_case(){
    aether_doc_t doc=nullptr;ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<body><style>#x{opacity:.2}</style><input id='x'></body>"));
    aether_style_runtime_t runtime=nullptr;ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));
    aether_dom_style_create_t stage=nullptr;ok(aether_dom_style_binding_prepare_create_v1(doc,runtime,&stage));
    aether_dom_style_binding_t binding=nullptr;aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_dom_style_binding_validate_create_v1(stage);if(status==AETHER_OK)status=aether_dom_style_binding_commit_create_v1(stage,&binding);}zero("cold initial style binding");ok(status);CHECK(binding);
    int64_t handle=0;int32_t present=0;uint64_t epoch=0;ok(aether_dom_style_binding_read_slot_v1(binding,&handle,&present,&epoch));CHECK(present);
    aether_dom_style_binding_t repeated=nullptr;
    ledger::reset();{ledger::Scope scope;status=aether_dom_style_binding_commit_create_v1(stage,&repeated);}zero("repeated initial style binding");CHECK(status==AETHER_ERROR_INVALID_STATE&&!repeated);
    ledger::reset();{ledger::Scope scope;aether_dom_style_binding_create_destroy_v1(stage);}CHECK(!ledger::counts.invalid);
    std::printf("DOM cold initial style binding validate+commit: requests=0; teardown requests=%llu (separate)\n",(unsigned long long)ledger::counts.requests);
    aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
}
struct Fixture {
    aether_doc_t doc=nullptr;aether_style_runtime_t runtime=nullptr;aether_dom_style_binding_t binding=nullptr;aether_dom_interaction_owner_t owner=nullptr;
    aether_node_t node=nullptr,other=nullptr,style=nullptr;
    Fixture(){ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<body><style id='s'>#x{opacity:.2}</style><input id='x' value='base' onblur='route'><div id='other'><span>old</span></div></body>"));node=aether_doc_get_element_by_id(doc,"x");other=aether_doc_get_element_by_id(doc,"other");style=aether_doc_get_element_by_id(doc,"s");ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&runtime));ok(aether_dom_style_binding_create_v1(doc,runtime,&binding));ok(aether_dom_interaction_owner_create_v1(doc,binding,1,&owner));}
    ~Fixture(){ok(aether_dom_interaction_owner_dispose_v1(owner));aether_dom_interaction_owner_release_v1(owner);aether_dom_style_binding_destroy_v1(binding);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static aether_dom_edit_t prepare(aether_dom_edit_spec_v1_t spec){auto t=token(spec.target);aether_dom_edit_t edit=nullptr;ok(aether_dom_prepare_edit_v1(&t,&spec,&edit));return edit;}
static unsigned commits=0;
static void commit(aether_dom_edit_t edit,const char* name){
    aether_dom_token_v1_t out{};aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_dom_edit_commit_v1(edit,&out);}zero(name);ok(status);++commits;
    // Rejected repeated commit is also read-only and allocation-free.
    ledger::reset();{ledger::Scope scope;status=aether_dom_edit_commit_v1(edit,&out);}zero("repeated commit");CHECK(status==AETHER_ERROR_INVALID_STATE);
    ledger::reset();{ledger::Scope scope;aether_dom_edit_destroy_v1(edit);}CHECK(!ledger::counts.invalid);
    std::printf("DOM commit %s: requests=0; teardown requests=%llu (separate)\n",name,(unsigned long long)ledger::counts.requests);
}
static void scalar_cases(){
    Fixture f;aether_dom_edit_spec_v1_t s{};s.target=f.node;s.kind=AETHER_DOM_SET_STATE;s.state=1;
    commit(prepare(s),"cold first typed commit/state");CHECK(f.node->state==1);
    commit(prepare(s),"same state");
    for(int kind:{AETHER_DOM_SET_ATTRIBUTE,AETHER_DOM_SET_LEGACY_ATTRIBUTE}){s.kind=kind;s.name=str("title");s.value=str("long attribute payload proving allocation during preparation, never commit");ledger::reset();aether_dom_edit_t e;{ledger::Scope scope;e=prepare(s);}CHECK(!ledger::counts.invalid&&ledger::counts.requests>0);commit(e,"attribute add/replace");CHECK(f.node->attributes.at("title")==std::string(s.value.data,s.value.length));}
    s.kind=AETHER_DOM_REMOVE_ATTRIBUTE;commit(prepare(s),"attribute remove");CHECK(!f.node->attributes.contains("title"));commit(prepare(s),"attribute absent");
    s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value=str("override runtime value");s.value_present=1;commit(prepare(s),"runtime override");CHECK(f.node->runtime_value_present);
    s.value_present=0;s.value={};commit(prepare(s),"runtime clear");CHECK(!f.node->runtime_value_present&&f.node->attributes.at("value")=="base");
    int64_t foreign=0;ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{height:19px}",2,"foreign",&foreign,nullptr));
    s.kind=AETHER_DOM_SET_TEXT;s.target=f.style->children.front();s.value=str("#x{opacity:.4;width:calc(1px + 1deg)}");commit(prepare(s),"style text and source slot");CHECK(s.target->text==std::string(s.value.data,s.value.length));CHECK(aether_style_runtime_remove_stylesheet(f.runtime,foreign));
    s.target=f.node;s.kind=AETHER_DOM_SET_STATE;s.state=2;auto stale=prepare(s);aether_element_set_state(f.node,4);auto before=token(f.node);aether_dom_token_v1_t out{};aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_dom_edit_commit_v1(stale,&out);}zero("stale precondition");CHECK(status==AETHER_ERROR_INVALID_STATE&&f.node->state==4&&token(f.node).observation_serial==before.observation_serial);aether_dom_edit_destroy_v1(stale);
}
static void structural_cases(){
    Fixture f;Fixture dest;aether_dom_edit_spec_v1_t s{};s.kind=AETHER_DOM_INSERT_CHILD;s.target=f.other;s.child=f.node;s.index=-1;
    commit(prepare(s),"same-owner reparent");CHECK(f.node->parent==f.other);
    s.target=dest.other;commit(prepare(s),"cross-owner adoption");CHECK(f.node->parent==dest.other&&f.node->document_owner.lock()==dest.doc->owner_control);
    s.kind=AETHER_DOM_REMOVE_CHILD;commit(prepare(s),"remove");CHECK(!f.node->parent&&!f.node->identity->attached);
    aether_dom_node_spec_v1_t nodes[2]{};nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("section");nodes[0].parent_index=-1;nodes[1].type=AETHER_NODE_TEXT;nodes[1].text=str("new text payload");nodes[1].parent_index=0;
    s={};s.kind=AETHER_DOM_INSERT_SUBTREE;s.target=dest.other;s.index=-1;s.nodes=nodes;s.node_count=2;commit(prepare(s),"insert subtree");CHECK(dest.other->children.back()->children.front()->text=="new text payload");
    s.kind=AETHER_DOM_REPLACE_CHILDREN;commit(prepare(s),"replace children");CHECK(dest.other->children.size()==1);
    s.kind=AETHER_DOM_CLEAR_CHILDREN;commit(prepare(s),"clear children");CHECK(dest.other->children.empty());
    nodes[1]={};nodes[1].existing_node=f.node;nodes[1].parent_index=0;s={};s.kind=AETHER_DOM_MATERIALIZE_FOREST;s.target=f.node;s.nodes=nodes;s.node_count=2;auto forest=prepare(s);aether_node_t parent=nullptr;ok(aether_dom_edit_created_node_v1(forest,0,&parent));commit(forest,"materialize forest");CHECK(f.node->parent==parent&&!parent->parent);
    aether_doc_t empty=nullptr;ok(aether_doc_create(&empty));nodes[1].existing_node=parent;s.kind=AETHER_DOM_MATERIALIZE_DOCUMENT;s.target=empty;commit(prepare(s),"materialize document");CHECK(empty->children.size()==1&&parent->parent==empty->children.front());aether_doc_destroy(empty);
}
static void interaction_cases(){
    Fixture f;auto t=token(f.node);aether_dom_interaction_edit_t edit=nullptr;ok(aether_dom_interaction_prepare_acquire_v1(f.owner,&t,f.node,AETHER_STATE_FOCUS,77,&edit));aether_dom_interaction_claim_t claim=nullptr;aether_dom_token_v1_t out{};aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_dom_interaction_edit_commit_v1(edit,&claim,&out);}zero("interaction acquire");ok(status);CHECK(claim);aether_dom_interaction_edit_release_v1(edit);
    aether_dom_edit_spec_v1_t s{};s.kind=AETHER_DOM_SET_ATTRIBUTE;s.target=f.node;s.name=str("onblur");s.value=str("replacement focus route prepared before loss");commit(prepare(s),"active claim route update");
    s={};s.kind=AETHER_DOM_REMOVE_CHILD;s.target=f.node->parent;s.child=f.node;commit(prepare(s),"disconnect with active claim");
    aether_dom_interaction_loss_v1_t loss{};int32_t present=0;ok(aether_dom_interaction_claim_read_loss_v1(claim,&present,&loss));CHECK(present&&std::string(loss.handler.data,loss.handler.length)=="replacement focus route prepared before loss");
    ok(aether_dom_interaction_prepare_cleanup_v1(claim,loss.claim.generation,loss.record_id,loss.record_serial,&edit));aether_dom_interaction_claim_t none=nullptr;
    ledger::reset();{ledger::Scope scope;status=aether_dom_interaction_edit_commit_v1(edit,&none,&out);}zero("interaction cleanup");ok(status);CHECK(!none&&!(f.node->state&AETHER_STATE_FOCUS));aether_dom_interaction_edit_release_v1(edit);aether_dom_interaction_claim_release_v1(claim);
}
static void rebind_case(){
    Fixture f;aether_doc_t next=nullptr;ok(aether_doc_create(&next));ok(aether_doc_parse_html(next,"<body><style>#x{opacity:.7}</style><div id='x'></div></body>"));
    int64_t foreign=0;ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{height:37px}",2,"foreign",&foreign,nullptr));
    aether_dom_style_rebind_t edit=nullptr;ok(aether_dom_style_binding_prepare_rebind_v1(f.binding,next,&edit));
    aether_dom_style_binding_t replacement=nullptr;aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_dom_style_binding_validate_rebind_v1(edit);if(status==AETHER_OK)status=aether_dom_style_binding_commit_rebind_v1(edit,&replacement);}zero("cold style binding rebind");ok(status);CHECK(replacement);
    int64_t handle=0;int32_t present=0;uint64_t epoch=0;ok(aether_dom_style_binding_read_slot_v1(replacement,&handle,&present,&epoch));CHECK(present&&aether_style_runtime_remove_stylesheet(f.runtime,foreign));
    aether_dom_style_binding_rebind_destroy_v1(edit);aether_dom_style_binding_destroy_v1(replacement);aether_doc_destroy(next);
    std::puts("DOM style binding replacement validate+commit: requests=0 (separate from initial create)");
}
static aether_transition_token_t transition_token(aether_transition_runtime_t runtime){aether_transition_token_t t{};ok(aether_transition_runtime_read_token(runtime,&t));return t;}
static void transition_commit(aether_transition_candidate_t candidate,const char* label){
    aether_transition_token_t out{};aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_transition_candidate_validate(candidate);if(status==AETHER_OK)status=aether_transition_candidate_commit(candidate,&out);}zero(label);ok(status);
    ledger::reset();{ledger::Scope scope;status=aether_transition_candidate_commit(candidate,&out);}zero("repeated transition commit");CHECK(status==AETHER_ERROR_INVALID_STATE);
    ledger::reset();{ledger::Scope scope;aether_transition_candidate_destroy(candidate);}CHECK(!ledger::counts.invalid);
    std::printf("Transition %s validate+swap: requests=0; teardown requests=%llu (separate)\n",label,(unsigned long long)ledger::counts.requests);
}
static void transition_cases(){
    Fixture f;aether_transition_runtime_t runtime=nullptr;ok(aether_transition_runtime_create(&runtime));
    auto before=aether_computed_style_default();auto after=before;after.opacity=0;after.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;after.transition_duration=1;after.transition_timing=AETHER_TRANSITION_TIMING_LINEAR;
    aether_transition_observation_t observation{f.node,&before,&after};aether_transition_candidate_t candidate=nullptr;
    ok(aether_transition_runtime_prepare(runtime,&observation,1,nullptr,0,100,&candidate));transition_commit(candidate,"cold nonempty");
    aether_bool_t active=0;ok(aether_transition_runtime_try_tick(runtime,600,&active));CHECK(active);float opacity=-1;CHECK(aether_transition_runtime_opacity_override(runtime,f.node,&opacity)&&std::fabs(opacity-.5f)<.001f);
    const auto serial=transition_token(runtime).mutation_serial;
    ok(aether_transition_runtime_prepare(runtime,nullptr,0,nullptr,0,600,&candidate));transition_commit(candidate,"active no-op");CHECK(transition_token(runtime).mutation_serial==serial);
    ok(aether_transition_runtime_try_tick(runtime,1200,&active));CHECK(!active);ok(aether_transition_runtime_prepare(runtime,nullptr,0,nullptr,0,1200,&candidate));transition_commit(candidate,"finished no-op");
    after.opacity=.7f;ok(aether_transition_runtime_prepare(runtime,&observation,1,nullptr,0,1200,&candidate));transition_commit(candidate,"retarget");
    ok(aether_transition_runtime_prepare(runtime,nullptr,0,nullptr,0,1200,&candidate));ok(aether_transition_runtime_try_tick(runtime,1400,&active));const auto current=transition_token(runtime);aether_transition_token_t out{};aether_status_t status;
    ledger::reset();{ledger::Scope scope;status=aether_transition_candidate_commit(candidate,&out);}zero("stale transition candidate");CHECK(status==AETHER_ERROR_INVALID_STATE&&transition_token(runtime).mutation_serial==current.mutation_serial);aether_transition_candidate_destroy(candidate);
    const aether_node_t retired[]={f.node};ok(aether_transition_runtime_prepare(runtime,nullptr,0,retired,1,1400,&candidate));transition_commit(candidate,"retire");CHECK(!aether_transition_runtime_has_active(runtime));
    ok(aether_transition_runtime_prepare(runtime,nullptr,0,nullptr,0,1400,&candidate));aether_transition_runtime_destroy(runtime);
    ledger::reset();{ledger::Scope scope;status=aether_transition_candidate_commit(candidate,&out);}zero("disposed transition candidate");CHECK(status==AETHER_ERROR_DISPOSED);aether_transition_candidate_destroy(candidate);
}
int main(int argc,char**){
    // Before any calibration/warmup of the commit entry point. Cold allocations
    // fail identically to warm allocations; only ledger TLS itself is initialized.
    ledger::reset();initial_binding_case();scalar_cases();
    controls(257u+unsigned(argc));structural_cases();interaction_cases();rebind_case();transition_cases();
    std::printf("PASS DOM commit allocation oracle: %u typed commits plus interaction, Linux product link-unit + static C++ runtime; libc-private/CLR/Windows/macOS/whole host publication UNMEASURED\n",commits);
}
#endif
