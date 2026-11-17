#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/dom/dom_types.h"
#include "../src/style_runtime/typography_record.h"
#include <cstdio>
#include <cstdlib>
#include "same_stage_assertions.h"
using namespace aether::style;
#define CHECK(x) SS_REQUIRE(x)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
static aether_style_revision_t resolve(aether_style_runtime_t rt,aether_doc_t d) {
    aether_style_transaction_t tx{};aether_style_revision_t r{};
    ok(aether_style_runtime_prepare_revision(rt,d,nullptr,&tx));
    ok(aether_style_runtime_commit_revision(rt,tx,&r));aether_style_transaction_destroy(tx);return r;
}
static void masks(aether_style_revision_t r,uint32_t expected,uint32_t gaps) {
    for(int i=0;i<aether_style_revision_style_count(r);++i) {
        aether_node_t node{};aether_computed_style_handle_t h{};uint32_t impact{};
        ok(aether_style_revision_get_style(r,i,&node,&h,&impact));
        CHECK(impact==expected);CHECK(h->record->declared_impact()==expected);
        CHECK(h->record->declared_inherited_impact()==expected);
        CHECK(h->record->inherited_change_impact()==expected);
        auto t=h->record->typography();CHECK(t&&t->prepared_evidence&&t->inputs);
        SS_CHECK("F-coverage",t->evidence.coverage_gaps==gaps&&!t->evidence.coverage_complete);
        CHECK(t->prepared_evidence->coverage_gaps==1);
        aether_computed_style_handle_destroy(h);
    }
}
static TypographyRecordRef first(aether_style_revision_t r) {
    aether_node_t node{};aether_computed_style_handle_t h{};uint32_t impact{};
    ok(aether_style_revision_get_style(r,0,&node,&h,&impact));
    auto t=h->record->typography();aether_computed_style_handle_destroy(h);return t;
}
static void run(bool reverse) {
    aether_doc_t d{};aether_style_runtime_t rt{};ok(aether_doc_create(&d));
    ok(aether_doc_parse_html(d,reverse ? "<html><body><div id='b'></div><div id='a'></div></body></html>" :
        "<html><body><div id='a'></div><div id='b'></div></body></html>"));
    ok(aether_style_runtime_create(nullptr,nullptr,nullptr,nullptr,&rt));
    auto a=aether_doc_get_element_by_id(d,"a");auto initial=resolve(rt,d);
    auto repeat=resolve(rt,d);masks(repeat,0,1);aether_style_revision_destroy(repeat);
    ok(aether_element_set_attribute(a,"style","unknown-property:1"));
    auto limited=resolve(rt,d);masks(limited,19,5);auto retained=first(limited);
    CHECK(retained->inputs->losses!=0);
    repeat=resolve(rt,d);masks(repeat,0,5);aether_style_revision_destroy(repeat);
    // Non-typographic whitespace changes only accounting, not P/F/validity.
    ok(aether_element_set_attribute(a,"style","  unknown-property:1  "));
    auto whitespace=resolve(rt,d);masks(whitespace,0,5);auto current=first(whitespace);
    CHECK(current->inputs->source_bytes==retained->inputs->source_bytes+4);
    CHECK(current->evidence==retained->evidence);
    CHECK(*current->prepared_evidence==*retained->prepared_evidence);
    // Prepared but abandoned candidate must not replace any published evidence.
    ok(aether_element_set_attribute(a,"style",""));
    aether_style_transaction_t abandoned{};ok(aether_style_runtime_prepare_revision(rt,d,nullptr,&abandoned));
    aether_style_transaction_destroy(abandoned);
    aether_style_revision_t still{};ok(aether_style_runtime_retain_revision(rt,&still));
    CHECK(first(still)==current);aether_style_revision_destroy(still);
    // A stale candidate is not published either.
    aether_style_transaction_t stale{};ok(aether_style_runtime_prepare_revision(rt,d,nullptr,&stale));
    ok(aether_element_set_attribute(a,"data-stale","1"));
    aether_style_revision_t rejected{};
    CHECK(aether_style_runtime_commit_revision(rt,stale,&rejected)!=AETHER_OK&&rejected==nullptr);
    aether_style_transaction_destroy(stale);
    auto cleared=resolve(rt,d);masks(cleared,19,1);
    repeat=resolve(rt,d);masks(repeat,0,1);
    CHECK(aether_style_revision_generation(repeat)>aether_style_revision_generation(cleared));
    aether_style_runtime_destroy(rt);
    CHECK(retained->evidence.coverage_gaps==5&&retained->prepared_evidence->coverage_gaps==1);
    CHECK(retained->inputs->source_bytes+4==current->inputs->source_bytes);
    for(auto r:{initial,limited,whitespace,cleared,repeat})aether_style_revision_destroy(r);
    aether_doc_destroy(d);
}
static void e1(bool transition) {
    aether_doc_t d{};aether_style_runtime_t rt{};ok(aether_doc_create(&d));
    ok(aether_doc_parse_html(d,transition ?
        "<html><body><div id='parent'><div id='box'></div></div></body></html>" :
        "<html><body><div id='p'><div id='c' class='target'><div id='g'></div></div></div></body></html>"));
    aether_style_runtime_options_t options{};aether_style_environment_t env{800,600,1,16,0,0,0};
    ok(aether_style_runtime_create(&options,&env,nullptr,nullptr,&rt));
    ok(aether_style_runtime_add_stylesheet(rt,transition ?
        "#parent { transition: transform 0.8s ease; } #box { transition: opacity 0.7s ease-in 0.2s; transition: ;}" :
        "#p{transform:scale(7)}#c{transform:scale(2)}",transition?2:0,"E1",nullptr,nullptr));
    if(!transition)ok(aether_style_runtime_add_stylesheet(rt,"#c{transform:scale(2 3);}",0,"E1",nullptr,nullptr));
    uint64_t generation=0;TypographyRecordRef previous;
    for(int pass=0;pass<3;++pass) {
        aether_style_update_t* updates{};
        auto count=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(d),&updates);
        if(pass==0)SS_PRECONDITION("initial-updates",count>0);else SS_CHECK("repeat-updates-empty",count==0);
        aether_style_updates_free(updates,count);
        aether_style_revision_t r{};ok(aether_style_runtime_retain_revision(rt,&r));
        CHECK(aether_style_revision_generation(r)>generation);generation=aether_style_revision_generation(r);
        auto current=first(r);CHECK(current->evidence.coverage_gaps==5);
        CHECK(current->inputs->losses==2&&current->inputs->inputs_accounted&&!current->inputs->external_pending);
        if(pass) {
            masks(r,0,5);
            CHECK(current->evidence==previous->evidence&&*current->prepared_evidence==*previous->prepared_evidence);
            CHECK(current->inputs->source_bytes==previous->inputs->source_bytes);
            CHECK(current->inputs->declarations==previous->inputs->declarations);
            CHECK(current->inputs->rules==previous->inputs->rules);
            CHECK(current->inputs->compiled_rules==previous->inputs->compiled_rules);
            CHECK(current->inputs->expanded_declarations==previous->inputs->expanded_declarations);
            CHECK(current->inputs->compiled_bytes==previous->inputs->compiled_bytes);
        }
        previous=current;aether_style_revision_destroy(r);
    }
    aether_style_runtime_destroy(rt);aether_doc_destroy(d);
}
int main(){ss::run("runtime-forward",[]{run(false);});ss::run("runtime-reverse",[]{run(true);});ss::run("e1-transition",[]{e1(true);});ss::run("e1-transform",[]{e1(false);});return ss::finish();}
