#include <aether/aether_state_style.h>
#include "../src/dom/dom_types.h"
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_revision.h"
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
    f.full(); // Stable payload metadata after first-resolution historical impacts.
    aether_element_set_state(f.a,AETHER_STATE_HOVER);
    aether_style_state_change_t declared{f.a,aether_node_identity(f.a),0,AETHER_STATE_HOVER};
    tx=f.prepare(&declared,1);info=result(tx);
    CHECK(info.fallback==AETHER_STATE_FALLBACK_DISABLED&&!info.incremental);
    CHECK(f.doc->mutation_journal.size()==1); // Prepare is observational.
    CHECK(info.semantic_delta_count>=1&&info.visual_delta_count>=1);
    f.commit(tx);aether_style_transaction_destroy(tx);
    // A->B->A consolidated against committed input, with no style update.
    aether_element_set_state(f.a,0);aether_element_set_state(f.a,AETHER_STATE_HOVER);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_DISABLED);
    f.commit(tx);aether_style_transaction_destroy(tx);
    // Caller omits b: journal still supplies b; omission cannot hide input.
    aether_element_set_state(f.b,AETHER_STATE_HOVER);
    tx=f.prepare();CHECK(result(tx).fallback==AETHER_STATE_FALLBACK_DISABLED);
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
    auto tx=f.prepare();const auto generation=result(tx).generation;
    aether_style_revision_t outer=nullptr;ok(aether_style_runtime_commit_revision(f.runtime,tx,&outer));
    CHECK(context.fired&&context.inner_generation>generation);
    CHECK(aether_style_revision_generation(outer)==generation);
    CHECK(f.doc->mutation_journal.size()==1&&f.doc->mutation_journal.end()[-1].subject==f.b->identity);
    CHECK(f.doc->mutation_journal.floor_epoch()+1==f.doc->identity->mutation_epoch);
    aether_style_revision_destroy(outer);aether_style_transaction_destroy(tx);
}
static void oom_and_thread() {
    Fixture f;f.full();aether_element_set_state(f.a,1);
    const auto floor=f.doc->mutation_journal.floor_epoch();const auto size=f.doc->mutation_journal.size();
    const auto generation=aether_style_revision_generation(f.base);
    bool completed=false;
    for(long i=0;i<20000;++i) {
        fail_after=i;fault_delivered=false;aether_style_transaction_t tx=nullptr;
        const auto status=aether_style_runtime_prepare_state_revision(f.runtime,f.doc,f.base,nullptr,0,nullptr,&tx);
        fail_after=-1;
        CHECK(f.doc->mutation_journal.floor_epoch()==floor&&f.doc->mutation_journal.size()==size);
        CHECK(aether_style_revision_generation(f.base)==generation);
        if(status==AETHER_OK){CHECK(!fault_delivered);aether_style_transaction_destroy(tx);completed=true;break;}
        CHECK(status==AETHER_ERROR_OUT_OF_MEMORY&&fault_delivered&&!tx);
    }
    CHECK(completed);
    aether_status_t wrong_thread=AETHER_OK;
    std::thread worker([&]{aether_style_transaction_t tx=nullptr;wrong_thread=aether_style_runtime_prepare_state_revision(f.runtime,f.doc,f.base,nullptr,0,nullptr,&tx);CHECK(!tx);});worker.join();
    CHECK(wrong_thread==AETHER_ERROR_INVALID_STATE);
    auto tx=f.prepare();f.commit(tx);aether_style_transaction_destroy(tx);
}
int main(){contract_and_ledger();failed_commit_keeps_journal();overflow_and_poison();multiple_runtimes();callback_after_ack();oom_and_thread();std::printf("state prepare foundation: %zu checks passed\n",checks);}
