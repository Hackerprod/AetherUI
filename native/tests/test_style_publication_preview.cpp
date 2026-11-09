#include <aether/aether_state_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_map_types.h"
#include "../src/dom/dom_types.h"
#include "../src/style_runtime/style_property_registry_internal.h"
#include <cstdio>
#include <cstdlib>
#include <thread>

static unsigned checks=0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
static void ok(aether_status_t s) { CHECK(s==AETHER_OK); }
using Token=aether_style_publication_token_t;
static Token current(aether_style_runtime_t runtime) {
    Token t{};t.struct_size=sizeof(t);ok(aether_style_runtime_read_publication_token(runtime,&t));return t;
}
static Token expected(aether_style_transaction_t tx) {
    Token t{};t.struct_size=sizeof(t);ok(aether_style_transaction_read_publication_token(tx,&t));return t;
}
static bool same(const Token& a,const Token& b) {
    return a.struct_size==b.struct_size && a.inputs_current==b.inputs_current &&
        a.revision_identity==b.revision_identity && a.generation==b.generation &&
        a.document_identity==b.document_identity && a.input_epoch==b.input_epoch &&
        a.structure_epoch==b.structure_epoch && a.style_tree_epoch==b.style_tree_epoch &&
        a.sheet_epoch==b.sheet_epoch && a.registry_identity==b.registry_identity;
}
struct Fixture {
    aether_doc_t doc{};aether_node_t node{};aether_style_runtime_t runtime{};
    Fixture() {
        ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,"<html><body><div id='x'></div></body></html>"));
        node=aether_doc_get_element_by_id(doc,"x");CHECK(node);
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime));
        ok(aether_style_runtime_add_stylesheet(runtime,"*{display:block}#x{width:16px;height:20px}#x::before{content:'old'}",0,"preview",nullptr,nullptr));
    }
    ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    aether_style_transaction_t prepare(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx));return tx;}
    aether_style_revision_t commit(aether_style_transaction_t tx){aether_style_revision_t r{};ok(aether_style_runtime_commit_revision(runtime,tx,&r));return r;}
    aether_style_revision_t resolve(){auto tx=prepare();auto r=commit(tx);aether_style_transaction_destroy(tx);return r;}
};
static aether_computed_style_handle_t lookup(aether_style_revision_t r,aether_node_t target) {
    for(int i=0;i<aether_style_revision_style_count(r);++i) {
        aether_node_t n{};aether_computed_style_handle_t s{};ok(aether_style_revision_get_style(r,i,&n,&s,nullptr));
        if(n==target)return s;aether_computed_style_handle_destroy(s);
    }
    CHECK(false);return nullptr;
}
static void preview_and_retention() {
    Fixture f;auto base=f.resolve();auto before=current(f.runtime);CHECK(before.inputs_current);
    auto* pseudo=f.node->children.front();
    ok(aether_element_set_attribute(f.node,"style","width:42px"));
    auto dirty=current(f.runtime);CHECK(!dirty.inputs_current);
    auto tx=f.prepare();auto token=expected(tx);CHECK(token.inputs_current);CHECK(token.generation>before.generation);
    aether_style_revision_t preview{};aether_style_map_t map{};
    ok(aether_style_transaction_preview_revision(tx,&preview));ok(aether_style_transaction_preview_legacy_map(tx,&map));
    CHECK(same(dirty,current(f.runtime)));CHECK(f.node->children.front()==pseudo);
    auto style=lookup(preview,f.node);CHECK(style->style.width.value==42);
    auto mapped=aether_style_map_get(map,f.node);CHECK(mapped && mapped->style.width.value==42);
    CHECK(map->current->mode()==aether::style::MapMode::legacy_complete);
    CHECK(mapped->document_identity==style->document_identity && mapped->node_identity==style->node_identity);
    CHECK(style->revision->authentic_owner(style->revision));
    CHECK(mapped->record->payload_owner()==style->record->payload_owner());
    auto committed=f.commit(tx);CHECK(same(token,current(f.runtime)));
    CHECK(aether_style_revision_generation(preview)==aether_style_revision_generation(committed));
    aether_style_transaction_destroy(tx);
    ok(aether_element_set_attribute(f.node,"style","width:99px"));auto next=f.resolve();
    CHECK(mapped->style.width.value==42 && style->style.width.value==42);
    auto retained=aether_style_map_get(map,f.node);CHECK(retained && retained->style.width.value==42);
    // Complete preview artifacts also survive an aborted transaction and runtime destruction.
    auto abort=f.prepare();aether_style_revision_t aborted{};aether_style_map_t aborted_map{};
    ok(aether_style_transaction_preview_revision(abort,&aborted));ok(aether_style_transaction_preview_legacy_map(abort,&aborted_map));
    auto latest=current(f.runtime);aether_style_transaction_destroy(abort);CHECK(same(latest,current(f.runtime)));
    aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;
    auto aborted_style=lookup(aborted,f.node);CHECK(aborted_style->style.width.value==99);
    auto aborted_mapped=aether_style_map_get(aborted_map,f.node);CHECK(aborted_mapped && aborted_mapped->style.width.value==99);
    aether_computed_style_handle_destroy(aborted_mapped);aether_computed_style_handle_destroy(aborted_style);
    aether_style_revision_destroy(aborted);aether_style_map_destroy(aborted_map);
    aether_computed_style_handle_destroy(style);aether_computed_style_handle_destroy(mapped);aether_computed_style_handle_destroy(retained);
    aether_style_map_destroy(map);aether_style_revision_destroy(base);aether_style_revision_destroy(preview);
    aether_style_revision_destroy(committed);aether_style_revision_destroy(next);
}
struct Reentry {Fixture* f;bool called=false;Token inner{};aether_style_revision_t revision{};};
static void diagnostic(const char*,const char*,const char*,const char*,void* data) {
    auto& r=*static_cast<Reentry*>(data);if(r.called)return;r.called=true;
    ok(aether_element_set_attribute(r.f->node,"style","width:99px"));r.revision=r.f->resolve();r.inner=current(r.f->runtime);
}
static void reentrant_commit() {
    Fixture f;auto base=f.resolve();Reentry r{&f};aether_style_runtime_set_diagnostics_callback(f.runtime,diagnostic,&r);
    ok(aether_element_set_attribute(f.node,"style","width:42px;transform:var(--undefined)"));
    auto tx=f.prepare();CHECK(!r.called);auto outer_token=expected(tx);auto outer=f.commit(tx);
    CHECK(r.called && r.revision);CHECK(!same(outer_token,current(f.runtime)));CHECK(same(r.inner,current(f.runtime)));
    CHECK(outer_token.generation<r.inner.generation);auto s=lookup(outer,f.node);CHECK(s->style.width.value==42);
    aether_computed_style_handle_destroy(s);aether_style_transaction_destroy(tx);
    aether_style_revision_destroy(base);aether_style_revision_destroy(outer);aether_style_revision_destroy(r.revision);
}
static void invalid_inputs() {
    Fixture f;auto base=f.resolve();CHECK(current(f.runtime).inputs_current);
    ok(aether_element_set_attribute(f.node,"class","changed"));CHECK(!current(f.runtime).inputs_current);
    auto r=f.resolve();CHECK(current(f.runtime).inputs_current);aether_style_revision_destroy(r);
    ok(aether_style_runtime_add_stylesheet(f.runtime,"#x{height:30px}",0,"new",nullptr,nullptr));CHECK(!current(f.runtime).inputs_current);
    r=f.resolve();CHECK(current(f.runtime).inputs_current);aether_style_revision_destroy(r);
    style_property_registry_shutdown();style_property_registry_init();CHECK(!current(f.runtime).inputs_current);
    r=f.resolve();CHECK(current(f.runtime).inputs_current);aether_style_revision_destroy(r);
    aether_doc_destroy(f.doc);f.doc=nullptr;CHECK(!current(f.runtime).inputs_current);
    aether_style_revision_destroy(base);
}
static void guards() {
    Fixture f;Token token{};token.struct_size=sizeof(token);
    CHECK(aether_style_runtime_read_publication_token(f.runtime,&token)==AETHER_ERROR_INVALID_STATE);CHECK(!token.revision_identity);
    auto r=f.resolve();auto tx=f.prepare();
    CHECK(aether_style_runtime_read_publication_token(f.runtime,nullptr)==AETHER_ERROR_NULL_ARG);
    CHECK(aether_style_transaction_read_publication_token(tx,nullptr)==AETHER_ERROR_NULL_ARG);
    token=current(f.runtime);token.struct_size=1;
    CHECK(aether_style_runtime_read_publication_token(f.runtime,&token)==AETHER_ERROR_INVALID_STATE);CHECK(!token.struct_size && !token.revision_identity);
    token=current(f.runtime);token.struct_size=1;
    CHECK(aether_style_transaction_read_publication_token(tx,&token)==AETHER_ERROR_INVALID_STATE);CHECK(!token.struct_size && !token.revision_identity);
    token=current(f.runtime);CHECK(aether_style_runtime_read_publication_token(nullptr,&token)==AETHER_ERROR_NULL_ARG);CHECK(!token.revision_identity);
    token=current(f.runtime);CHECK(aether_style_transaction_read_publication_token(nullptr,&token)==AETHER_ERROR_NULL_ARG);CHECK(!token.revision_identity);
    aether_style_revision_t preview=reinterpret_cast<aether_style_revision_t>(1);
    aether_style_map_t map=reinterpret_cast<aether_style_map_t>(1);
    CHECK(aether_style_transaction_preview_revision(nullptr,&preview)==AETHER_ERROR_NULL_ARG && !preview);
    CHECK(aether_style_transaction_preview_legacy_map(nullptr,&map)==AETHER_ERROR_NULL_ARG && !map);
    CHECK(aether_style_transaction_preview_revision(tx,nullptr)==AETHER_ERROR_NULL_ARG);
    CHECK(aether_style_transaction_preview_legacy_map(tx,nullptr)==AETHER_ERROR_NULL_ARG);
    // The owner and transaction remain alive until this thread joins.
    std::thread other([&] {
        Token t{};t.struct_size=sizeof(t);CHECK(aether_style_runtime_read_publication_token(f.runtime,&t)==AETHER_ERROR_INVALID_STATE);CHECK(!t.revision_identity);
        t.struct_size=sizeof(t);CHECK(aether_style_transaction_read_publication_token(tx,&t)==AETHER_ERROR_INVALID_STATE);CHECK(!t.revision_identity);
        auto p=reinterpret_cast<aether_style_revision_t>(1);auto m=reinterpret_cast<aether_style_map_t>(1);
        CHECK(aether_style_transaction_preview_revision(tx,&p)==AETHER_ERROR_INVALID_STATE && !p);
        CHECK(aether_style_transaction_preview_legacy_map(tx,&m)==AETHER_ERROR_INVALID_STATE && !m);
    });other.join();
    CHECK(current(f.runtime).inputs_current);aether_style_transaction_destroy(tx);aether_style_revision_destroy(r);
}
int main(){preview_and_retention();reentrant_commit();invalid_inputs();guards();std::printf("style publication preview: PASS (%u checks)\n",checks);}
