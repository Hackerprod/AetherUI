// Linux link-time malloc interception for the real core objects only.
// --wrap=malloc does not intercept malloc inside shared libc/libstdc++, calloc,
// aligned allocation, or operator new. Those families have separate tests.
#include <aether/aether_state_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/measure_state.h"
#include "../src/css/style_map_types.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
static long failure_index=-1;
static bool armed=false, fault_delivered=false;
static size_t wrapped_calls=0;
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t size) {
    if(armed) {
        ++wrapped_calls;
        if(failure_index>=0 && failure_index--==0){fault_delivered=true;return nullptr;}
    }
    return __real_malloc(size);
}
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

enum class Operation { style, map, patch };
static const char* name(Operation op) {
    return op==Operation::style ? "get_publication_style" : op==Operation::map ? "preview_legacy_map" : "preview_legacy_patch";
}
static int candidate_index(aether_style_transaction_t tx,aether_node_t target) {
    aether_style_revision_t revision{};ok(aether_style_transaction_preview_revision(tx,&revision));int found=-1;
    for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t n{};ok(aether_style_revision_get_style(revision,i,&n,nullptr,nullptr));if(n==target){found=i;break;}}
    aether_style_revision_destroy(revision);CHECK(found>=0);return found;
}

static unsigned sweep(Operation op) {
    unsigned failures=0;bool completed=false;size_t successful_boundaries=0;
    for(long allocation=0;allocation<1000;++allocation) {
        Fixture f;auto baseline=f.resolve();aether_style_map_t base{};
        ok(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&base));auto original=base->current;
        auto old=aether_style_map_get(base,f.node);CHECK(old);
        ok(aether_element_set_attribute(f.node,"style",
            "width:1rem;box-shadow:1px 2px red;text-shadow:2px 3px blue;"
            "background-image:url(asset.png),linear-gradient(red,blue);"
            "transform:translate(1px,2px);backdrop-filter:blur(2px)"));
        auto tx=f.prepare();const auto before=current(f.runtime);const auto expected_token=expected(tx);
        const int index=candidate_index(tx,f.node);
        aether_style_map_t map=reinterpret_cast<aether_style_map_t>(1);
        aether_computed_style_handle_t style=reinterpret_cast<aether_computed_style_handle_t>(1);
        aether_node_t node{};uint32_t impact{};
        auto invoke=[&] {
            if(op==Operation::style)return aether_style_transaction_get_publication_style(tx,base,index,&node,&style,&impact);
            if(op==Operation::map)return aether_style_transaction_preview_legacy_map(tx,&map);
            return aether_style_transaction_preview_legacy_patch(tx,base,&map);
        };
        fault_delivered=false;wrapped_calls=0;failure_index=allocation;armed=true;
        const auto status=invoke();armed=false;failure_index=-1;
        CHECK(base->current==original && same(before,current(f.runtime)) && same(expected_token,expected(tx)));
        CHECK(old->style.width.value==16 && old->style.box_shadows.count==0);
        if(status==AETHER_OK) {CHECK(!fault_delivered);completed=true;successful_boundaries=wrapped_calls;}
        else {
            ++failures;CHECK(fault_delivered && status==AETHER_ERROR_OUT_OF_MEMORY);
            CHECK(op==Operation::style ? !style : !map);
            ok(invoke()); // Explicit retry uses the same transaction after malloc failure.
            CHECK(base->current==original && same(before,current(f.runtime)));
        }
        auto result=op==Operation::style ? style : aether_style_map_get(map,f.node);
        CHECK(result && result->style.width.value==16 && (result->measures->scalars().dependencies & MEASURE_ROOT_FONT));
        CHECK(result->style.box_shadows.count==1 && result->style.text_shadows.count==1);
        CHECK(result->style.transforms.count==1 && result->style.backdrop_filters.count==1);
        CHECK(result->style.background_layers.count==2);
        CHECK(result->style.background_layers.items[0].image_source &&
            std::strcmp(result->style.background_layers.items[0].image_source,"asset.png")==0);
        CHECK(result->style.background_layers.items[1].gradient.stop_count==2);
        aether_style_transaction_destroy(tx);CHECK(base->current==original && same(before,current(f.runtime)));
        CHECK(result->style.width.value==16);aether_computed_style_handle_destroy(result);
        if(op!=Operation::style)aether_style_map_destroy(map);
        aether_computed_style_handle_destroy(old);aether_style_map_destroy(base);aether_style_revision_destroy(baseline);
        if(completed)break;
    }
    CHECK(completed);CHECK(failures==successful_boundaries);
    if(op==Operation::style)CHECK(failures>=4);
    std::printf("%s: %u wrapped malloc failures; %zu boundaries on successful call%s\n",
        name(op),failures,successful_boundaries,successful_boundaries ? "" : " (no malloc reached; no synthetic failures)");
    return failures;
}
int main() {
    const auto failures=sweep(Operation::style)+sweep(Operation::map)+sweep(Operation::patch);
    std::printf("publication preview wrapped malloc: PASS (%u checks, %u malloc failure boundaries)\n",checks,failures);
}
