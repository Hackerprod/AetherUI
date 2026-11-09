#include <aether/aether_state_style.h>
#include "../src/dom/dom_types.h"
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_revision.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(std::size_t n) {
    if(fail_after>=0&&fail_after--==0){fault_delivered=true;throw std::bad_alloc();}
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{std::free(p);}
#include "fault_inject_aligned.h"
static size_t checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
struct Fixture {
    aether_doc_t doc=nullptr;
    aether_style_runtime_t runtime=nullptr;
    aether_style_revision_t base=nullptr;
    aether_node_t a=nullptr,b=nullptr;
    aether_style_environment_t env{800,600,1,16,0,0,0};
    int64_t sheet=0;
    explicit Fixture(const char* css=".card:hover { background-color:red }") {
        ok(aether_doc_create(&doc));
        ok(aether_doc_parse_html(doc,"<html><body><div id='a' class='card'><span></span></div><div id='b' class='card'></div></body></html>"));
        a=aether_doc_get_element_by_id(doc,"a");b=aether_doc_get_element_by_id(doc,"b");
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime));
        if(css)ok(aether_style_runtime_add_stylesheet(runtime,css,2,"test",&sheet,nullptr));
    }
    ~Fixture(){aether_style_revision_destroy(base);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    aether_style_transaction_t prepare(const aether_style_state_change_t* c=nullptr,int32_t n=0) {
        aether_style_transaction_t tx=nullptr;
        ok(aether_style_runtime_prepare_state_revision(runtime,doc,base,c,n,nullptr,&tx));return tx;
    }
    void commit(aether_style_transaction_t tx) {
        aether_style_revision_t next=nullptr;ok(aether_style_runtime_commit_revision(runtime,tx,&next));
        aether_style_revision_destroy(base);base=next;
    }
    void full(){aether_style_transaction_t tx=nullptr;ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx));commit(tx);aether_style_transaction_destroy(tx);}
};
static aether_style_state_result_t result(aether_style_transaction_t tx) {
    aether_style_state_result_t out{};out.struct_size=sizeof(out);ok(aether_style_transaction_state_result(tx,&out));return out;
}
static void contract_and_ledger() {
    Fixture f;
    CHECK(aether_contextual_style_version()==1&&aether_state_style_version()==1);
    auto tx=f.prepare();auto info=result(tx);
    CHECK(!info.incremental&&info.fallback==AETHER_STATE_FALLBACK_FIRST_REVISION);
    CHECK(info.semantic_delta_count==5&&info.visual_delta_count==5);
    const auto epoch=f.doc->identity->mutation_epoch;
    CHECK(f.doc->mutation_journal.floor_epoch()==0&&f.doc->mutation_journal.size()==1);
    aether_computed_style_handle_t preview=nullptr;aether_node_t node=nullptr;
    ok(aether_style_transaction_get_state_delta(tx,0,&node,&preview,nullptr));
    CHECK(preview->revision->generation()==info.generation);
    f.commit(tx);CHECK(f.doc->mutation_journal.floor_epoch()==epoch&&f.doc->mutation_journal.size()==0);
    CHECK(preview->revision->generation()==info.generation);aether_computed_style_handle_destroy(preview);
    aether_style_revision_t invalid=nullptr;
    CHECK(aether_style_runtime_commit_revision(f.runtime,tx,&invalid)==AETHER_ERROR_INVALID_STATE&&!invalid);
    aether_style_transaction_destroy(tx);
    aether_element_set_state(f.a,AETHER_STATE_HOVER);
    aether_style_state_change_t declared{f.a,aether_node_identity(f.a),0,AETHER_STATE_HOVER};
    tx=f.prepare(&declared,1);info=result(tx);
    CHECK((info.fallback==AETHER_STATE_FALLBACK_DISABLED||info.fallback==AETHER_STATE_FALLBACK_SELECTOR)&&!info.incremental);
    CHECK(f.doc->mutation_journal.size()==1); // Prepare is observational.
    CHECK(info.semantic_delta_count>=1&&info.visual_delta_count>=1);
    f.commit(tx);aether_style_transaction_destroy(tx);
    // A->B->A consolidated against committed input, with no style update.
    aether_element_set_state(f.a,0);aether_element_set_state(f.a,AETHER_STATE_HOVER);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_DISABLED||result(tx).fallback==AETHER_STATE_FALLBACK_SELECTOR||result(tx).incremental);
    f.commit(tx);aether_style_transaction_destroy(tx);
    // Caller omits b: journal still supplies b; omission cannot hide input.
    aether_element_set_state(f.b,AETHER_STATE_HOVER);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_DISABLED||result(tx).fallback==AETHER_STATE_FALLBACK_SELECTOR||result(tx).incremental);
    f.commit(tx);aether_style_transaction_destroy(tx);
    // Forged pointer and token are never dereferenced during reconciliation.
    aether_style_state_change_t forged{reinterpret_cast<aether_node_t>(uintptr_t(1)),1,0,1};
    tx=f.prepare(&forged,1);CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_IDENTITY);aether_style_transaction_destroy(tx);
    declared.old_state=0;declared.new_state=0;
    tx=f.prepare(&declared,1);CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_CALLER_BATCH);aether_style_transaction_destroy(tx);
    aether_style_state_result_t short_result{};
    tx=f.prepare();CHECK(aether_style_transaction_state_result(tx,&short_result)==AETHER_ERROR_INVALID_STATE);
    aether_style_transaction_destroy(tx);
}
// The compact semantic API must not report previous-operation bookkeeping.
// Retain all three public snapshot forms across a real inherited change and a
// forced FULL no-op. No pseudo regeneration or structural mutation is involved.
static void inherited_noop_contract() {
    Fixture f("span { background-color:currentColor; width:2em; line-height:1.5 }");
    f.full();
    const int32_t count=aether_style_revision_style_count(f.base);
    aether_style_revision_t old_revision=nullptr;
    ok(aether_style_revision_clone(f.base,&old_revision));
    aether_style_map_t old_map=nullptr;ok(aether_style_revision_create_map(f.base,&old_map));
    const auto child=f.a->children.front();
    auto old_handle=aether_style_map_get(old_map,child);CHECK(old_handle);
    const auto old_record=old_handle->record;const auto old_measures=old_handle->measures;
    const auto old_generation=aether_style_revision_generation(old_revision);
    const auto old_color=old_handle->style.color;const auto old_font=old_handle->style.font_size;
    ok(aether_style_runtime_add_stylesheet(f.runtime,"body { color:red; font-size:20px; --tone:red }",2,"inherited",nullptr,nullptr));
    auto tx=f.prepare();const auto changed=result(tx);
    CHECK(!changed.incremental&&changed.semantic_delta_count>0&&changed.visual_delta_count>0&&changed.visual_impact);
    bool child_delta=false;
    for(int32_t i=0;i<changed.semantic_delta_count;++i) {
        aether_node_t node=nullptr;aether_computed_style_handle_t handle=nullptr;uint32_t impact=0;
        ok(aether_style_transaction_get_state_delta(tx,i,&node,&handle,&impact));
        if(node==child){CHECK(handle&&impact);child_delta=true;}
        aether_computed_style_handle_destroy(handle);
    }
    CHECK(child_delta);f.commit(tx);aether_style_transaction_destroy(tx);
    aether_style_revision_t changed_revision=nullptr;ok(aether_style_revision_clone(f.base,&changed_revision));
    aether_style_map_t changed_map=nullptr;ok(aether_style_revision_create_map(f.base,&changed_map));
    auto changed_handle=aether_style_map_get(changed_map,child);CHECK(changed_handle);
    const auto changed_record=changed_handle->record;const auto changed_measures=changed_handle->measures;
    CHECK(changed_handle->style.font_size==20&&changed_handle->style.color.r==1&&changed_handle->style.color.g==0);
    CHECK(changed_record->color_references()->background());
    const auto changed_generation=aether_style_revision_generation(changed_revision);
    std::vector<aether_node_t> membership;
    for(int32_t i=0;i<count;++i){aether_node_t node=nullptr;ok(aether_style_revision_get_style(f.base,i,&node,nullptr,nullptr));membership.push_back(node);}
    // A null caller base forces authentic FULL without changing the environment.
    tx=nullptr;ok(aether_style_runtime_prepare_state_revision(f.runtime,f.doc,nullptr,nullptr,0,nullptr,&tx));
    const auto unchanged=result(tx);
    CHECK(!unchanged.incremental&&unchanged.fallback==AETHER_STATE_FALLBACK_BASE_REVISION);
    CHECK(unchanged.semantic_delta_count==0&&unchanged.visual_delta_count==0&&unchanged.visual_impact==0);
    aether_node_t absent=nullptr;aether_computed_style_handle_t absent_style=nullptr;
    CHECK(aether_style_transaction_get_state_delta(tx,0,&absent,&absent_style,nullptr)==AETHER_ERROR_INVALID_STATE);
    CHECK(!absent&&!absent_style);
    f.commit(tx);aether_style_transaction_destroy(tx);
    CHECK(aether_style_revision_style_count(f.base)==count&&aether_style_revision_change_count(f.base)==count);
    for(int32_t i=0;i<count;++i) {
        aether_node_t style_node=nullptr,change_node=nullptr;uint32_t style_impact=~0u,change_impact=~0u;
        ok(aether_style_revision_get_style(f.base,i,&style_node,nullptr,&style_impact));
        ok(aether_style_revision_get_change(f.base,i,&change_node,nullptr,&change_impact));
        CHECK(style_node==membership[i]&&change_node==membership[i]&&!style_impact&&!change_impact);
    }
    CHECK(old_handle->record==old_record&&old_handle->measures==old_measures);
    CHECK(old_handle->style.color.r==old_color.r&&old_handle->style.color.g==old_color.g&&old_handle->style.font_size==old_font);
    CHECK(aether_style_revision_generation(old_revision)==old_generation&&old_handle->revision->generation()==old_generation);
    CHECK(old_measures->document().values().generation==old_generation);
    CHECK(changed_handle->record==changed_record&&changed_handle->measures==changed_measures);
    CHECK(changed_handle->style.font_size==20&&changed_handle->style.color.r==1&&changed_handle->style.color.g==0);
    CHECK(aether_style_revision_generation(changed_revision)==changed_generation&&changed_handle->revision->generation()==changed_generation);
    CHECK(changed_measures->document().values().generation==changed_generation);
    aether_style_map_t current_map=nullptr;ok(aether_style_revision_create_map(f.base,&current_map));
    auto current_handle=aether_style_map_get(current_map,child);CHECK(current_handle);
    CHECK(current_handle->record->payload_owner()==changed_record->payload_owner());
    CHECK(current_handle->measures->document().values().generation==changed_generation+1);
    aether_computed_style_handle_destroy(current_handle);aether_style_map_destroy(current_map);
    aether_computed_style_handle_destroy(changed_handle);aether_style_map_destroy(changed_map);aether_style_revision_destroy(changed_revision);
    aether_computed_style_handle_destroy(old_handle);aether_style_map_destroy(old_map);aether_style_revision_destroy(old_revision);
}
static void failed_commit_keeps_journal() {
    Fixture f;f.full();
    aether_element_set_state(f.a,1);auto tx=f.prepare();
    const auto floor=f.doc->mutation_journal.floor_epoch();
    aether_element_set_state(f.b,1);aether_style_revision_t next=nullptr;
    CHECK(aether_style_runtime_commit_revision(f.runtime,tx,&next)==AETHER_ERROR_INVALID_STATE&&!next);
    CHECK(f.doc->mutation_journal.floor_epoch()==floor&&f.doc->mutation_journal.size()==2);
    aether_style_transaction_destroy(tx);f.full();
    tx=f.prepare();CHECK(aether_style_runtime_remove_stylesheet(f.runtime,f.sheet));
    CHECK(aether_style_runtime_commit_revision(f.runtime,tx,&next)==AETHER_ERROR_INVALID_STATE&&!next);
    aether_style_transaction_destroy(tx);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_SHEET);aether_style_transaction_destroy(tx);
}
static void overflow_and_poison() {
    Fixture f;f.full();
    for(int i=0;i<130;++i)aether_element_set_state(f.a,i+1);
    CHECK(f.doc->mutation_journal.loss()==aether::dom::JournalLoss::overflow);
    auto tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE);
    const auto old_floor=f.doc->mutation_journal.floor_epoch();
    aether_style_transaction_destroy(tx);CHECK(f.doc->mutation_journal.floor_epoch()==old_floor);
    f.full();CHECK(!f.doc->mutation_journal.coverage_lost(f.doc->identity->mutation_epoch));
    CHECK(f.doc->mutation_journal.size()==0&&f.doc->mutation_journal.floor_epoch()>old_floor);
    // An earlier overflow must not hide a later wrap/discontinuity.
    for(int i=0;i<130;++i)aether_element_set_state(f.a,1000+i);
    f.doc->identity->mutation_epoch=UINT64_MAX;
    aether_element_set_state(f.a,0);
    CHECK(f.doc->mutation_journal.monotonicity_lost());
    CHECK(f.doc->mutation_journal.loss()==aether::dom::JournalLoss::overflow);
    const auto floor=f.doc->mutation_journal.floor_epoch();
    f.full();f.full();
    CHECK(f.doc->mutation_journal.monotonicity_lost()&&f.doc->mutation_journal.floor_epoch()==floor);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE);aether_style_transaction_destroy(tx);
    Fixture forward;forward.full();forward.doc->identity->mutation_epoch+=7;
    aether_element_set_state(forward.a,1);CHECK(forward.doc->mutation_journal.monotonicity_lost());
    forward.full();CHECK(forward.doc->mutation_journal.monotonicity_lost());
}
static void multiple_runtimes() {
    Fixture f;f.full();
    aether_style_runtime_t other=nullptr;ok(aether_style_runtime_create(nullptr,&f.env,nullptr,nullptr,&other));
    aether_style_transaction_t tx=nullptr;ok(aether_style_runtime_prepare_revision(other,f.doc,nullptr,&tx));
    aether_style_revision_t other_base=nullptr;ok(aether_style_runtime_commit_revision(other,tx,&other_base));
    aether_style_transaction_destroy(tx);
    aether_element_set_state(f.a,1);f.full();
    const auto floor=f.doc->mutation_journal.floor_epoch();
    ok(aether_style_runtime_prepare_state_revision(other,f.doc,other_base,nullptr,0,nullptr,&tx));
    CHECK(!result(tx).incremental&&result(tx).fallback!=AETHER_STATE_FALLBACK_DISABLED);
    CHECK(f.doc->mutation_journal.floor_epoch()==floor);aether_style_transaction_destroy(tx);
    aether_style_revision_destroy(other_base);aether_style_runtime_destroy(other);
}
struct CallbackState{Fixture* f;bool fired=false;uint64_t outer_floor=0,inner_generation=0;};
static void reentrant(const char*,const char*,const char*,const char*,void* context) {
    auto& c=*static_cast<CallbackState*>(context);if(c.fired)return;c.fired=true;
    c.outer_floor=c.f->doc->mutation_journal.floor_epoch();
    CHECK(c.outer_floor==c.f->doc->identity->mutation_epoch);
    aether_element_set_state(c.f->a,1);
    c.f->full();c.inner_generation=aether_style_revision_generation(c.f->base);
    aether_element_set_state(c.f->b,1);
}
static void callback_after_ack() {
    Fixture f(".card { width: var(--missing) }");f.full();
    CallbackState context{&f};aether_style_runtime_set_diagnostics_callback(f.runtime,reentrant,&context);
    aether_style_transaction_t tx=nullptr;ok(aether_style_runtime_prepare_revision(f.runtime,f.doc,nullptr,&tx));
    const auto generation=aether_style_revision_generation(f.base)+1;
    aether_style_revision_t outer=nullptr;ok(aether_style_runtime_commit_revision(f.runtime,tx,&outer));
    CHECK(context.fired&&context.inner_generation>generation);
    CHECK(aether_style_revision_generation(outer)==generation);
    CHECK(f.doc->mutation_journal.size()==1&&f.doc->mutation_journal.end()[-1].subject==f.b->identity);
    CHECK(f.doc->mutation_journal.floor_epoch()+1==f.doc->identity->mutation_epoch);
    aether_style_revision_destroy(outer);aether_style_transaction_destroy(tx);
}
static void oom_and_thread(bool noop=false) {
    Fixture f(noop?nullptr:".card:hover { background-color:red }");f.full();aether_element_set_state(f.a,1);
    const auto floor=f.doc->mutation_journal.floor_epoch();const auto size=f.doc->mutation_journal.size();
    const auto generation=aether_style_revision_generation(f.base);
    bool completed=false;
    for(long i=0;i<20000;++i) {
        fail_after=i;fault_delivered=false;aether_style_transaction_t tx=nullptr;
        const auto status=aether_style_runtime_prepare_state_revision(f.runtime,f.doc,f.base,nullptr,0,nullptr,&tx);
        fail_after=-1;
        CHECK(f.doc->mutation_journal.floor_epoch()==floor&&f.doc->mutation_journal.size()==size);
        CHECK(aether_style_revision_generation(f.base)==generation);
        if(status==AETHER_OK){CHECK(!fault_delivered);
            const auto info=result(tx);
            if(noop)CHECK(info.incremental||info.fallback==AETHER_STATE_FALLBACK_DISABLED);
            std::printf("%s preparation allocation sweep: %ld failure points checked\n",noop?"no-op":"FULL",i);
            aether_style_transaction_destroy(tx);completed=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&fault_delivered&&!tx);
    }
    CHECK(completed);
    aether_status_t wrong_thread=AETHER_OK;
    std::thread worker([&]{aether_style_transaction_t tx=nullptr;wrong_thread=aether_style_runtime_prepare_state_revision(f.runtime,f.doc,f.base,nullptr,0,nullptr,&tx);CHECK(!tx);});worker.join();
    CHECK(wrong_thread==AETHER_ERROR_INVALID_STATE);
    auto tx=f.prepare();f.commit(tx);aether_style_transaction_destroy(tx);
}
int main(){contract_and_ledger();inherited_noop_contract();failed_commit_keeps_journal();overflow_and_poison();multiple_runtimes();callback_after_ack();oom_and_thread();oom_and_thread(true);std::printf("state prepare foundation: %zu checks passed\n",checks);}
