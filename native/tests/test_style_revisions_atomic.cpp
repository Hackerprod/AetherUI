#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/measure_state.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/css/style_map_types.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cmath>
#include <string>
static long fail_after = -1;
static bool fault_delivered = false;
static size_t ordinary_test_calls = 0, ordinary_test_failures = 0;
void* operator new(std::size_t n) {
    ++ordinary_test_calls; if (fail_after >= 0 && fail_after-- == 0) { fault_delivered = true; ++ordinary_test_failures; throw std::bad_alloc(); } if (auto p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// Route nothrow and array allocations through the same fault allocator. This
// keeps ASan allocation families matched instead of suppressing its checks.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#include "fault_inject_aligned.h"
#include "fault_inject_pmr_contract.h"
static unsigned checks = 0;
static void require(bool ok, const char* message) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); } }
static bool near(float a,float b) { return std::fabs(a-b)<.001f; }
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_node_t node{}; int64_t sheet{};
    Fixture() {
        require(aether_doc_create(&doc)==AETHER_OK,"create doc");
        require(aether_doc_parse_html(doc,"<html><body><div id='x'></div></body></html>")==AETHER_OK,"parse");
        node=aether_doc_get_element_by_id(doc,"x");
        aether_style_environment_t env{800,600,1,16,0,0,0};
        require(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        require(aether_style_runtime_add_stylesheet(runtime,"*{display:block}#x{width:16px;height:20px}#x::before{content:'old';display:block;width:3px;height:4px}",0,"atomic",&sheet,nullptr)==AETHER_OK,"sheet");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    aether_style_revision_t resolve(const aether_style_environment_t* env=nullptr) {
        aether_style_transaction_t tx{}; require(aether_style_runtime_prepare_revision(runtime,doc,env,&tx)==AETHER_OK,"prepare");
        aether_style_revision_t revision{}; require(aether_style_runtime_commit_revision(runtime,tx,&revision)==AETHER_OK,"commit");
        aether_style_transaction_destroy(tx);return revision;
    }
};
static aether_computed_style_handle_t lookup(aether_style_revision_t revision,aether_node_t target) {
    for(int i=0;i<aether_style_revision_style_count(revision);++i) {
        aether_node_t node{}; aether_computed_style_handle_t style{}; require(aether_style_revision_get_style(revision,i,&node,&style,nullptr)==AETHER_OK,"entry");
        if(node==target)return style; aether_computed_style_handle_destroy(style);
    } require(false,"node missing");return nullptr;
}
static void revisions_and_stale() {
    Fixture f; auto g0=f.resolve(); auto old=lookup(g0,f.node);
    auto* oldpseudo=f.node->children.front(); auto oldtoken=oldpseudo->identity;
    aether_style_map_t map{};require(aether_style_revision_create_map(g0,&map)==AETHER_OK,"map");
    aether_style_transaction_t stale{};require(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&stale)==AETHER_OK,"stale prepare");
    require(aether_element_set_attribute(f.node,"style","width:1rem")==AETHER_OK,"mutate input");
    aether_style_revision_t out=reinterpret_cast<aether_style_revision_t>(1);
    require(aether_style_runtime_commit_revision(f.runtime,stale,&out)==AETHER_ERROR_INVALID_STATE&&out==nullptr,"stale rejected");
    aether_style_transaction_destroy(stale);
    auto g1=f.resolve(); auto metadata=lookup(g1,f.node);
    require(aether_style_revision_generation(g1)>aether_style_revision_generation(g0),"generation advanced");
    require(near(old->style.width.value,16)&&near(metadata->style.width.value,16),"equal numeric intermediate");
    require(metadata->measures->scalars().dependencies&MEASURE_ROOT_FONT,"metadata-only dependency published");
    require(!oldtoken->attached&&oldtoken->alive,"retired pseudo storage retained");
    require(oldpseudo->children.front()->text=="old","retired pseudo content safe");
    require(aether_element_set_attribute(aether_doc_get_document_element(f.doc),"style","font-size:20px")==AETHER_OK,"root change");
    auto g2=f.resolve(); auto latest=lookup(g2,f.node);
    require(near(latest->style.width.value,20)&&near(old->style.width.value,16),"new root old immutable");
    auto mapstyle=aether_style_map_get(map,f.node);require(mapstyle&&near(mapstyle->style.width.value,16),"map pinned G0");
    aether_computed_style_handle_t clone{};require(aether_computed_style_handle_clone(metadata,&clone)==AETHER_OK,"contextual clone");
    require(clone->measures==metadata->measures&&clone->node_identity==metadata->node_identity,"clone retains context identity");
    aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;
    require(near(clone->style.width.value,16)&&clone->revision,"handle survives runtime");
    aether_computed_style_handle_destroy(clone);aether_computed_style_handle_destroy(mapstyle);
    aether_computed_style_handle_destroy(old);aether_computed_style_handle_destroy(metadata);aether_computed_style_handle_destroy(latest);
    aether_style_map_destroy(map);aether_style_revision_destroy(g0);aether_style_revision_destroy(g1);aether_style_revision_destroy(g2);
}
static void fault_prepare() {
    const auto pmr_family = pmr_fault_test::qualify(require);
    Fixture f;auto g0=f.resolve();auto old=lookup(g0,f.node);auto* pseudo=f.node->children.front();
    const auto baseline=aether_style_revision_generation(g0);
    require(aether_element_set_attribute(aether_doc_get_body(f.doc),"style","--radius:calc(1px /*original*/ + 2px);--terminal:var(--radius)")==AETHER_OK,"blur divergent ancestor scope");
    require(aether_element_set_attribute(f.node,"style","width:3rem;box-shadow:1px 2px red;backdrop-filter:blur(var(--terminal)) blur(calc(0px - 1em))")==AETHER_OK,"blur candidate");
    aether_style_environment_t env{1000,700,1,20,0,0,0};
    int failed=0;bool completed=false;
    const pmr_fault_test::Counters sweep_start;
    for(long allocation=0;allocation<20000;++allocation) {
        aether_style_transaction_t tx=reinterpret_cast<aether_style_transaction_t>(1);
        fail_after=allocation;fault_delivered=false;
        const auto status=aether_style_runtime_prepare_revision(f.runtime,f.doc,&env,&tx);
        fail_after=-1;
        if(status==AETHER_OK) {
            require(!fault_delivered,"first successful prepare did not swallow injected failure");
            aether_style_revision_t g1{};require(aether_style_runtime_commit_revision(f.runtime,tx,&g1)==AETHER_OK,"retry commit");
            auto newstyle=lookup(g1,f.node);require(near(newstyle->style.width.value,60),"retry requested context");
            require(newstyle->style.backdrop_filters.count==2&&near(newstyle->style.backdrop_filters.items[0].length_amount.value,3)&&near(newstyle->style.backdrop_filters.items[1].length_amount.value,0),"retry keeps focal custom scope and computed clamp");
            bool legacy_terminal=false;
            for(auto* scope=newstyle->record->custom_properties().get();scope;scope=scope->parent())
                for(const auto& entry:scope->local_entries())if(entry.name()=="--terminal") {
                    require(entry.value()=="calc(1px              + 2px)","published custom scope retains legacy cleaned representation");legacy_terminal=true;
                }
            require(legacy_terminal,"published terminal scope inspected");
            aether_computed_style_handle_destroy(newstyle);aether_style_revision_destroy(g1);aether_style_transaction_destroy(tx);completed=true;break;
        }
        ++failed;require(fault_delivered&&status==AETHER_ERROR_OUT_OF_MEMORY,"prepare reports injected OOM");require(tx==nullptr,"failed prepare clears out handle");
        require(f.node->children.front()==pseudo&&pseudo->identity->attached,"failed prepare preserves pseudo links");
        aether_style_revision_t retained{};require(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK,"retain after fault");
        require(aether_style_revision_generation(retained)==baseline,"failed prepare preserves revision");
        aether_style_revision_destroy(retained);require(near(old->style.width.value,16)&&old->style.backdrop_filters.count==0,"failed prepare preserves old style and filters");
    }
    require(completed&&failed>50,"fault sweep covers preparation allocations");
    pmr_fault_test::require_sweep_coverage(pmr_family, sweep_start, require);
    aether_computed_style_handle_destroy(old);aether_style_revision_destroy(g0);
}
static uint64_t retained_generation(aether_style_runtime_t runtime) {
    aether_style_revision_t revision{};
    require(aether_style_runtime_retain_revision(runtime,&revision)==AETHER_OK,"retain generation");
    auto generation=aether_style_revision_generation(revision);
    aether_style_revision_destroy(revision);return generation;
}

static void final_pseudo_removal() {
    Fixture f; auto old=f.resolve(); auto* pseudo=f.node->children.front(); auto token=pseudo->identity;
    aether_style_map_t map{}; require(aether_style_revision_create_map(old,&map)==AETHER_OK,"retained pseudo map");
    aether_layout_engine_t engine{}; aether_layout_result_t frame{};
    require(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK,"retained frame engine");
    require(aether_layout_layout(engine,f.doc,map,800,600,nullptr,nullptr,&frame)==AETHER_OK,"retained pseudo frame");
    auto box=aether_layout_result_find_primary_box(frame,pseudo);
    require(box!=nullptr,"frame contains pseudo");
    const auto bounds=aether_layout_box_get_border_box(box);
    aether_style_transaction_t stale{};
    require(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&stale)==AETHER_OK,"prepare borrowed rules");
    // Keep a normal sheet so removal rebuild really allocates retained buckets.
    int64_t normal{}; require(aether_style_runtime_add_stylesheet(f.runtime,"#x{width:16px}",0,"normal",&normal,nullptr)==AETHER_OK,"normal retained sheet");
    // Prepare again after adding the normal sheet, so only removal stales it.
    aether_style_transaction_destroy(stale);
    require(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&stale)==AETHER_OK,"prepare before removal");
    bool removed=false; unsigned failures=0;
    for(long allocation=0;allocation<20000;++allocation) {
        aether_style_transaction_destroy(stale); stale=nullptr;
        require(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&stale)==AETHER_OK,"fresh pre-removal candidate");
        const auto generation=retained_generation(f.runtime);
        fail_after=allocation;fault_delivered=false;
        bool result=aether_style_runtime_remove_stylesheet(f.runtime,f.sheet); fail_after=-1;
        if(result) {require(!fault_delivered,"remove cannot swallow fault");removed=true;break;}
        ++failures;require(fault_delivered,"remove failure is injected");
        require(retained_generation(f.runtime)==generation,"failed remove preserves generation");
        require(f.node->children.front()==pseudo&&token->attached,"failed removal leaves live pseudo");
        auto retry=f.resolve(); require(f.node->children.front()->children.front()->text=="old","failed rebuild retains pseudo rules");
        aether_style_revision_destroy(retry);
        // Re-resolve replaces leaves; retain the original via old/map/frame.
        pseudo=f.node->children.front(); token=pseudo->identity;
    }
    require(removed&&failures>0,"remove sweep covers actual allocations");
    aether_style_revision_t last{};
    require(aether_style_runtime_retain_revision(f.runtime,&last)==AETHER_OK,"retain exact final live pseudo revision");
    aether_style_revision_t rejected=reinterpret_cast<aether_style_revision_t>(1);
    require(aether_style_runtime_commit_revision(f.runtime,stale,&rejected)==AETHER_ERROR_INVALID_STATE&&rejected==nullptr,"removed-sheet stale commit rejected");
    aether_style_transaction_destroy(stale); // Candidate rules have been destroyed.
    require(!f.node->children.empty()&&token->attached,"removal alone does not mutate live tree");
    bool committed=false; failures=0;
    for(long allocation=0;allocation<20000;++allocation) {
        aether_style_transaction_t tx{}; fail_after=allocation; fault_delivered=false;
        auto status=aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&tx); fail_after=-1;
        if(status==AETHER_OK) {
            require(!fault_delivered,"absent-pseudo prepare cannot swallow fault");
            aether_style_revision_t current{};
            require(aether_style_runtime_commit_revision(f.runtime,tx,&current)==AETHER_OK,"commit final retirement");
            require(f.node->children.empty()&&!token->attached&&token->alive,"last pseudo retired and retained");
            require(pseudo->children.front()->text=="old","exact final leaf readable after retirement");
            bool retired=false;
            for(int i=0;i<aether_style_revision_change_count(current);++i) {
                aether_node_t changed{};aether_computed_style_handle_t style{};
                require(aether_style_revision_get_change(current,i,&changed,&style,nullptr)==AETHER_OK,"retirement change entry");
                if(changed==pseudo) {require(style==nullptr,"retirement has no current style");retired=true;}
                aether_computed_style_handle_destroy(style);
            }
            require(retired,"exact final leaf appears in retired changes");
            aether_style_revision_destroy(current);aether_style_transaction_destroy(tx);committed=true;break;
        }
        ++failures;require(fault_delivered&&status==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr,"absent-pseudo prepare OOM");
        require(f.node->children.front()==pseudo&&token->attached,"failed prepare preserves old links");
    }
    require(committed&&failures>0,"absent-pseudo sweep covers actual allocations");
    auto retained=aether_style_map_get(map,f.node);require(retained&&near(retained->style.width.value,16),"retained map readable after retirement");
    require(near(aether_layout_box_get_border_box(box).width,bounds.width),"retained frame unchanged");
    aether_computed_style_handle_destroy(retained);aether_layout_result_destroy(frame);aether_layout_destroy(engine);
    aether_style_map_destroy(map);aether_style_revision_destroy(old);aether_style_revision_destroy(last);
}

static void fault_pseudo_add() {
    Fixture f; auto old=f.resolve();
    bool completed=false;unsigned failures=0;
    for(long allocation=0;allocation<20000;++allocation) {
        const auto generation=retained_generation(f.runtime);
        auto* live=f.node->children.front();
        int64_t sheet=-1;int32_t count=-1;fail_after=allocation;fault_delivered=false;
        auto status=aether_style_runtime_add_stylesheet(f.runtime,"#x::after{content:'new'}",0,"after",&sheet,&count);fail_after=-1;
        if(status==AETHER_OK) {require(!fault_delivered&&sheet==2&&count==1,"successful add publishes IDs once");completed=true;break;}
        ++failures;require(fault_delivered&&status==AETHER_ERROR_OUT_OF_MEMORY&&sheet==-1&&count==-1,"add OOM preserves outputs");
        require(retained_generation(f.runtime)==generation&&f.node->children.front()==live,"add OOM preserves revision and links");
        auto revision=f.resolve();require(f.node->children.size()==1&&f.node->children.front()->children.front()->text=="old","add OOM preserves rules");aether_style_revision_destroy(revision);
    }
    require(completed&&failures>0,"add sweep covers actual allocations");
    auto current=f.resolve();require(f.node->children.size()==2&&f.node->children.back()->children.front()->text=="new","new kind visible after retry");
    aether_style_revision_destroy(current);aether_style_revision_destroy(old);
}

static void fault_map() {
    Fixture f;auto g0=f.resolve();aether_style_map_t map{};require(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&map)==AETHER_OK,"legacy map");
    aether_element_set_attribute(f.node,"style","width:72px");auto g1=f.resolve();
    aether_node_t nodes[]={aether_doc_get_document_element(f.doc),f.node};bool completed=false;unsigned failed=0;
    for(long allocation=0;allocation<2000;++allocation) {
        fail_after=allocation;fault_delivered=false;auto status=aether_style_runtime_update_resolved_style_map(f.runtime,map,nodes,2);fail_after=-1;
        auto h=aether_style_map_get(map,f.node);require(h!=nullptr,"map readable");
        if(status==AETHER_OK){require(!fault_delivered,"first successful map update did not swallow injected failure");require(near(h->style.width.value,72),"whole map update");completed=true;aether_computed_style_handle_destroy(h);break;}
        ++failed;require(fault_delivered&&status==AETHER_ERROR_OUT_OF_MEMORY,"map update reports injected OOM");require(near(h->style.width.value,16),"failed map update whole backing unchanged");aether_computed_style_handle_destroy(h);
    }require(completed&&failed>5,"map fault coverage");aether_style_map_destroy(map);aether_style_revision_destroy(g0);aether_style_revision_destroy(g1);
}
struct Reentrant { Fixture* fixture; bool enabled=false,called=false; aether_style_revision_t inner{}; };
static void diagnostic(const char*,const char*,const char*,const char*,void* data) {
    auto& r=*static_cast<Reentrant*>(data);if(!r.enabled||r.called)return;r.called=true;
    aether_element_set_attribute(r.fixture->node,"style","width:99px");r.inner=r.fixture->resolve();
}
static void reentrant_diagnostics() {
    Fixture f;auto g0=f.resolve();Reentrant r{&f};aether_style_runtime_set_diagnostics_callback(f.runtime,diagnostic,&r);
    aether_element_set_attribute(f.node,"style","width:42px;transform:var(--undefined)");r.enabled=true;
    auto outer=f.resolve();require(r.called&&r.inner,"diagnostic can resolve reentrantly after commit");
    auto a=lookup(outer,f.node);auto b=lookup(r.inner,f.node);
    require(near(a->style.width.value,42)&&near(b->style.width.value,99),"outer outputs belong to own operation");
    require(aether_style_revision_generation(r.inner)>aether_style_revision_generation(outer),"inner is later generation");
    aether_computed_style_handle_destroy(a);aether_computed_style_handle_destroy(b);aether_style_revision_destroy(g0);aether_style_revision_destroy(outer);aether_style_revision_destroy(r.inner);
}
static void legacy_scope_is_not_contextual() {
    Fixture f;
    require(aether_element_set_attribute(aether_doc_get_body(f.doc),"style","--base:20px")==AETHER_OK,"parent custom scope");
    require(aether_element_set_attribute(f.node,"style","width:var(--base,7px)")==AETHER_OK,"child custom consumer");
    auto original=f.resolve(); auto old=lookup(original,f.node);
    require(near(old->style.width.value,20),"contextual initial inheritance");
    aether_style_update_t* updates{};
    const int count=aether_style_runtime_resolve_subtree(f.runtime,f.node,&updates);
    require(count>0,"isolated legacy update"); aether_style_updates_free(updates,count);
    aether_style_map_t legacy{};
    require(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&legacy)==AETHER_OK,"legacy projection remains available");
    auto isolated=aether_style_map_get(legacy,f.node);
    require(isolated&&near(isolated->style.width.value,7),"legacy subtree preserves null-parent scope");
    aether_style_revision_t unavailable=reinterpret_cast<aether_style_revision_t>(1);
    require(aether_style_runtime_retain_revision(f.runtime,&unavailable)==AETHER_ERROR_INVALID_STATE&&unavailable==nullptr,
        "isolated legacy revision cannot claim contextual completeness");
    aether_style_transaction_t tx{};
    require(aether_style_runtime_prepare_revision(f.runtime,f.node,nullptr,&tx)==AETHER_OK,"contextual prepare from child");
    aether_style_revision_t restored{};
    require(aether_style_runtime_commit_revision(f.runtime,tx,&restored)==AETHER_OK,"contextual commit restores full scope");
    aether_style_transaction_destroy(tx); auto current=lookup(restored,f.node);
    require(near(current->style.width.value,20)&&near(old->style.width.value,20)&&near(isolated->style.width.value,7),
        "contextual retry and retained legacy snapshots stay independent");
    aether_computed_style_handle_destroy(current);aether_computed_style_handle_destroy(isolated);aether_computed_style_handle_destroy(old);
    aether_style_map_destroy(legacy);aether_style_revision_destroy(original);aether_style_revision_destroy(restored);
}
int main(){final_pseudo_removal();fault_pseudo_add();legacy_scope_is_not_contextual();revisions_and_stale();fault_prepare();fault_map();reentrant_diagnostics();
    std::printf("style revision atomic: %u checks passed, %zu aligned failures\n",checks,aligned_test_failures);}
