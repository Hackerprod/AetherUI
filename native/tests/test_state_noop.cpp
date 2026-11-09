#include <aether/aether_state_style.h>
#include <aether/aether_work_counters.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_revision.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static void ok(aether_status_t status){CHECK(status==AETHER_OK);}
struct Fixture {
    aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t base{};
    aether_node_t child{},card{};
    explicit Fixture(int count) {
        std::string html="<html><body>";
        for(int i=0;i<count;++i)html+="<div class='card'><span id='x"+std::to_string(i)+"'></span></div>";
        html+="</body></html>";ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));
        child=aether_doc_get_element_by_id(doc,"x0");card=child->parent;
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime));
        ok(aether_style_runtime_add_stylesheet(runtime,".card:hover{background-color:red}",2,"test",nullptr,nullptr));
        full();
    }
    ~Fixture(){aether_style_revision_destroy(base);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    void publish(aether_style_transaction_t tx){aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next));aether_style_revision_destroy(base);base=next;}
    void full(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx));publish(tx);aether_style_transaction_destroy(tx);}
    aether_style_transaction_t prepare(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_state_revision(runtime,doc,base,nullptr,0,nullptr,&tx));return tx;}
};
static aether_style_state_result_t result(aether_style_transaction_t tx){aether_style_state_result_t r{};r.struct_size=sizeof(r);ok(aether_style_transaction_state_result(tx,&r));return r;}
static aether_work_counters_v1_t counters(){aether_work_counters_v1_t c{};ok(aether_work_counters_get_v1(&c));return c;}
static void noop_counts(int size) {
    Fixture f(size);
    aether_computed_style_handle_t old{};ok(aether_style_revision_get_style(f.base,0,nullptr,&old,nullptr));
    const auto old_revision=old->revision;const auto old_payload=old->record->payload_owner();
    const auto old_generation=old_revision->generation();
    aether_element_set_state(f.child,AETHER_STATE_HOVER);
    aether_work_counters_set_enabled(AETHER_TRUE);aether_work_counters_reset();
    auto tx=f.prepare();const auto r=result(tx);const auto work=counters();
    CHECK(r.incremental&&r.fallback==AETHER_STATE_FALLBACK_NONE&&r.semantic_delta_count==0&&r.visual_delta_count==0);
    CHECK(work.compute_nodes==0&&work.state_entries_visited==0&&work.revision_entries_created==0);
    f.publish(tx);aether_style_transaction_destroy(tx);
    aether_computed_style_handle_t now{};ok(aether_style_revision_get_style(f.base,0,nullptr,&now,nullptr));
    CHECK(now->record->payload_owner()==old_payload);
    CHECK(now->revision!=old_revision&&now->revision->generation()==old_generation+1);
    CHECK(now->revision->entries().size()==old_revision->entries().size());
    CHECK(old->revision->generation()==old_generation);
    const auto* before=old_revision->directory().find(reinterpret_cast<uintptr_t>(f.child));
    const auto* after=now->revision->directory().find(reinterpret_cast<uintptr_t>(f.child));
    CHECK(before->committed_state==0&&after->committed_state==AETHER_STATE_HOVER&&before->payload==after->payload);
    // Complete audit against actual FULL on the same current inputs. This walk
    // happens in the test, never inside the benchmarked incremental operation.
    const auto incremental=now->revision;f.full();
    for(int32_t i=0;i<aether_style_revision_style_count(f.base);++i) {
        aether_computed_style_handle_t full{};aether_node_t node{};ok(aether_style_revision_get_style(f.base,i,&node,&full,nullptr));
        const auto* prior=incremental->directory().find(reinterpret_cast<uintptr_t>(node));
        CHECK(prior&&prior->payload==full->record->payload_owner());
        aether_computed_style_handle_destroy(full);
    }
    aether_computed_style_handle_destroy(old);aether_computed_style_handle_destroy(now);
    std::printf("no-op %d cards: zero computes/state scans/manifest entries; FULL payload audit passed\n",size);
}
static void inherited_first_full_shadow() {
    Fixture f(3);
    ok(aether_style_runtime_add_stylesheet(f.runtime,
        "body { color:red; font-size:20px; --tone:red } span { background-color:currentColor; width:2em; line-height:1.5 }",
        2,"inherited",nullptr,nullptr));
    f.full(); // Exactly one FULL after a real inheritable change, never a settling FULL.
    aether_element_set_state(f.child,AETHER_STATE_HOVER);
    auto tx=f.prepare();const auto info=result(tx);
    CHECK(info.incremental&&info.semantic_delta_count==0&&info.visual_delta_count==0);
    f.publish(tx);aether_style_transaction_destroy(tx);
    aether_computed_style_handle_t retained=nullptr;ok(aether_style_revision_get_style(f.base,3,nullptr,&retained,nullptr));
    const auto incremental=retained->revision;
    tx=nullptr;ok(aether_style_runtime_prepare_state_revision(f.runtime,f.doc,nullptr,nullptr,0,nullptr,&tx));
    const auto full_info=result(tx);
    CHECK(!full_info.incremental&&full_info.semantic_delta_count==0&&full_info.visual_delta_count==0);
    CHECK(aether_style_transaction_get_state_delta(tx,0,nullptr,nullptr,nullptr)==AETHER_ERROR_INVALID_STATE);
    f.publish(tx);aether_style_transaction_destroy(tx);
    for(int32_t i=0;i<aether_style_revision_style_count(f.base);++i) {
        aether_computed_style_handle_t full=nullptr;aether_node_t node=nullptr;uint32_t impact=~0u;
        ok(aether_style_revision_get_style(f.base,i,&node,&full,&impact));
        const auto* prior=incremental->directory().find(reinterpret_cast<uintptr_t>(node));
        CHECK(prior&&prior->payload==full->record->payload_owner()&&impact==0);
        CHECK(prior->payload->inherited_change_impact()==0);
        aether_computed_style_handle_destroy(full);
    }
    CHECK(retained->revision==incremental&&retained->measures->document().values().generation==incremental->generation());
    aether_computed_style_handle_destroy(retained);
}
static int diagnostics=0;
static void diagnostic(const char*,const char*,const char*,const char*,void*){++diagnostics;}
static void batches_and_fallbacks() {
    Fixture f(10);
    aether_element_set_state(f.card,1);aether_element_set_state(f.card,0);
    auto tx=f.prepare();CHECK(result(tx).incremental);f.publish(tx);aether_style_transaction_destroy(tx);
    aether_element_set_state(f.card,1);tx=f.prepare();CHECK(result(tx).incremental&&result(tx).semantic_delta_count>0&&result(tx).visual_delta_count>0);f.publish(tx);aether_style_transaction_destroy(tx);
    ok(aether_style_runtime_add_stylesheet(f.runtime,".unrelated:has(.other:hover) span {color: red}",2,"relational",nullptr,nullptr));f.full();f.full();
    aether_element_set_state(f.child,1);tx=f.prepare();CHECK(result(tx).incremental);f.publish(tx);aether_style_transaction_destroy(tx);
    ok(aether_style_runtime_add_stylesheet(f.runtime,"span { width:var(--missing) }",2,"diagnostics",nullptr,nullptr));f.full();f.full();
    aether_style_runtime_set_diagnostics_callback(f.runtime,diagnostic,nullptr);diagnostics=0;
    aether_element_set_state(f.child,0);tx=f.prepare();CHECK(result(tx).incremental);f.publish(tx);aether_style_transaction_destroy(tx);CHECK(diagnostics==0);
    f.full();CHECK(diagnostics>0);
    // Staged no-op cannot publish after a new input and must not ACK it.
    aether_element_set_state(f.child,1);tx=f.prepare();CHECK(result(tx).incremental);
    const auto floor=f.doc->mutation_journal.floor_epoch();aether_element_set_state(f.child,0);
    aether_style_revision_t rejected{};CHECK(aether_style_runtime_commit_revision(f.runtime,tx,&rejected)==AETHER_ERROR_INVALID_STATE&&!rejected);
    CHECK(f.doc->mutation_journal.floor_epoch()==floor&&f.doc->mutation_journal.size()==2);aether_style_transaction_destroy(tx);
}
static void retained_history_and_bounded_journal() {
    Fixture f(20);
    std::vector<aether_style_revision_t> history;history.reserve(10000);
    unsigned full_fallbacks=0;
    const auto first_generation=aether_style_revision_generation(f.base)+1;
    for(unsigned i=0;i<10000;++i) {
        aether_element_set_state(f.child,(i&1)?0:AETHER_STATE_HOVER);
        auto tx=f.prepare();const auto info=result(tx);
        if(!info.incremental){CHECK(info.fallback==AETHER_STATE_FALLBACK_JOURNAL_INCOMPLETE);++full_fallbacks;}
        CHECK(info.semantic_delta_count==0&&info.visual_delta_count==0);
        f.publish(tx);aether_style_transaction_destroy(tx);
        aether_style_revision_t retained=nullptr;ok(aether_style_revision_clone(f.base,&retained));history.push_back(retained);
        CHECK(f.doc->mutation_journal.size()<=aether::dom::MutationJournal::capacity);
    }
    CHECK(full_fallbacks>0); // Incremental ACK is intentionally still prohibited.
    for(size_t i=0;i<history.size();++i) {
        CHECK(aether_style_revision_generation(history[i])==first_generation+i);
        aether_computed_style_handle_t style=nullptr;aether_node_t node=nullptr;
        ok(aether_style_revision_get_style(history[i],3,&node,&style,nullptr));CHECK(node==f.child);
        const auto* ledger=style->revision->directory().find(reinterpret_cast<uintptr_t>(f.child));
        CHECK(ledger&&ledger->committed_state==((i&1)?0:AETHER_STATE_HOVER));
        CHECK(style->revision->generation()==first_generation+i);
        aether_computed_style_handle_destroy(style);aether_style_revision_destroy(history[i]);
    }
    std::printf("10000 retained no-op revisions preserve generations/ledgers; %u explicit FULL journal fallbacks\n",full_fallbacks);
}
int main(){noop_counts(100);noop_counts(500);noop_counts(1000);inherited_first_full_shadow();batches_and_fallbacks();retained_history_and_bounded_journal();}
