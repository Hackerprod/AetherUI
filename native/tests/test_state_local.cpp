#include <aether/aether_state_style.h>
#include <aether/aether_work_counters.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/style_revision.h"
#include "../src/style_runtime/state_semantics.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
using namespace aether::style;
static const char* scenario="initialization";
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL [%s] %d: %s\n",scenario,__LINE__,#x);std::abort();} } while(0)
static void ok(aether_status_t s){CHECK(s==AETHER_OK);}
static aether_style_state_result_t info(aether_style_transaction_t tx){aether_style_state_result_t r{};r.struct_size=sizeof(r);ok(aether_style_transaction_state_result(tx,&r));return r;}

// Only identities differ across the independent documents. All semantic fields
// are copied unchanged. In particular impacts, scopes, currentColor metadata,
// symbolic measures and parent dependencies are NOT normalized away.
static FrozenNodePayloadRef normalize(const FrozenNodePayload& value,const FrozenNodePayload& identity){
    NodePayloadInput in;
    in.parent_dependencies=value.parent_dependencies();in.origin=value.origin();
    in.style=value.style();in.measures=value.measures();
    in.custom_properties=value.custom_properties();in.backdrop_custom_properties=value.backdrop_custom_properties();
    in.color_references=value.color_references();in.declared_impact=value.declared_impact();
    in.declared_inherited_impact=value.declared_inherited_impact();in.inherited_change_impact=value.inherited_change_impact();
    in.node=identity.node();in.node_identity=identity.node_identity();in.node_lifetime=identity.node_lifetime();
    in.document_identity=identity.document_identity();in.document_lifetime=identity.document_lifetime();
    in.generated_leaf=identity.generated_leaf();
    FrozenNodePayloadRef out;ok(freeze_node_payload(in,out));return out;
}
static std::string key(aether_node_t node){
    CHECK(node);
    if(node->is_generated){CHECK(node->style_origin);return key(node->style_origin)+"::"+std::to_string(int(node->pseudo_element));}
    auto id=node->attributes.find("id");
    if(id!=node->attributes.end())return "#"+id->second;
    // Fixture supplies unique IDs to every styled element. This also makes
    // accidental generated-node treatment as a logical element fail loudly.
    CHECK(false);return {};
}
struct Fixture {
    aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};
    explicit Fixture(const char* css){
        ok(aether_doc_create(&doc));
        ok(aether_doc_parse_html(doc,"<html id='root'><body id='body'><div id='a' class='card'><span id='b' class='label'><i id='c'></i></span><span id='d'></span></div><div id='e' class='card'><span id='f'></span></div></body></html>"));
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime));
        ok(aether_style_runtime_add_stylesheet(runtime,css,2,"differential",nullptr,nullptr));
        aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,doc,nullptr,&tx));commit(tx);aether_style_transaction_destroy(tx);
    }
    ~Fixture(){aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    void state(const char* id,int32_t value){auto n=aether_doc_get_element_by_id(doc,id);CHECK(n);aether_element_set_state(n,value);}
    aether_style_transaction_t prepare(bool full){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_state_revision(runtime,doc,full?nullptr:revision,nullptr,0,nullptr,&tx));return tx;}
    void commit(aether_style_transaction_t tx){aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next));aether_style_revision_destroy(revision);revision=next;}
    StyleRevisionRef snapshot(){aether_computed_style_handle_t h{};ok(aether_style_revision_get_style(revision,0,nullptr,&h,nullptr));auto r=h->revision;aether_computed_style_handle_destroy(h);return r;}
};
static void generated_content_equal(aether_node_t a,aether_node_t b){
    CHECK(a->type==b->type&&a->tag_name==b->tag_name&&a->text==b->text);
    CHECK(a->children.size()==b->children.size());
    for(size_t i=0;i<a->children.size();++i)generated_content_equal(a->children[i],b->children[i]);
}
static void equivalent(const StyleRevisionRef& a,const StyleRevisionRef& b){
    CHECK(a->generation()==b->generation());CHECK(a->entries().size()==b->entries().size());
    const auto x=a->document()->values(),y=b->document()->values();
    CHECK(x.viewport_width==y.viewport_width&&x.viewport_height==y.viewport_height);
    CHECK(x.initial_font_size==y.initial_font_size&&x.root_font_size==y.root_font_size&&x.generation==y.generation);
    CHECK(x.identity==a->document_identity()&&y.identity==b->document_identity());
    std::map<std::string,FrozenRevisionEntry> expected;
    for(auto entry:b->entries())CHECK(expected.emplace(key(entry.node()),entry).second);
    for(auto entry:a->entries()){
        const auto it=expected.find(key(entry.node()));CHECK(it!=expected.end());const auto& other=it->second;
        CHECK(entry.node()->is_generated==other.node()->is_generated);
        CHECK(entry.node()->pseudo_element==other.node()->pseudo_element);
        if(entry.node()->is_generated)generated_content_equal(entry.node(),other.node());
        CHECK(entry.node()->tag_name==other.node()->tag_name&&entry.node()->text==other.node()->text);
        CHECK(entry.impact()==other.impact());
        const auto normalized=normalize(*other.payload(),*entry.payload());
        if(!node_semantically_equal(*entry.payload(),*normalized)){
            std::fprintf(stderr,"payload differs at %s\n",key(entry.node()).c_str());CHECK(false);
        }
        const auto* al=a->directory().find(reinterpret_cast<uintptr_t>(entry.node()));
        const auto* bl=b->directory().find(reinterpret_cast<uintptr_t>(other.node()));
        CHECK(al&&bl&&al->committed_state==bl->committed_state&&al->has_committed_state==bl->has_committed_state);
    }
}
static std::map<std::string,uint32_t> deltas(aether_style_transaction_t tx){
    std::map<std::string,uint32_t> out;
    for(int32_t i=0;i<info(tx).semantic_delta_count;++i){
        aether_node_t n{};uint32_t impact{};aether_computed_style_handle_t h{};
        ok(aether_style_transaction_get_state_delta(tx,i,&n,&h,&impact));
        // FULL generated leaves can have both a retired and a new host+slot.
        out[key(n)]|=impact;aether_computed_style_handle_destroy(h);
    }
    return out;
}
struct Pair {
    Fixture local,oracle;
    std::vector<StyleRevisionRef> retained;
    std::vector<std::vector<FrozenNodePayloadRef>> retained_payloads;
    explicit Pair(const char* css):local(css),oracle(css){equivalent(local.snapshot(),oracle.snapshot());}
    void state(const char* id,int32_t value){local.state(id,value);oracle.state(id,value);}
    void step(aether_state_fallback_t fallback=AETHER_STATE_FALLBACK_NONE,int computes=-1,bool zero_visual=false){
        const auto old=local.snapshot();std::vector<FrozenNodePayloadRef> saved;
        for(auto e:old->entries())saved.push_back(e.payload());
        retained.push_back(old);retained_payloads.push_back(saved);
        aether_work_counters_reset();auto lt=local.prepare(false);aether_work_counters_v1_t work{};ok(aether_work_counters_get_v1(&work));
        const auto l=info(lt);
        CHECK(l.incremental==(fallback==AETHER_STATE_FALLBACK_NONE));CHECK(l.fallback==fallback);
        if(computes>=0 && work.compute_nodes!=uint64_t(computes)) {
            std::fprintf(stderr,"compute mismatch generation=%llu actual=%llu expected=%d semantic=%d visual=%d impact=%u fallback=%d\n",(unsigned long long)l.generation,(unsigned long long)work.compute_nodes,computes,l.semantic_delta_count,l.visual_delta_count,l.visual_impact,int(l.fallback));
            for(auto e:old->entries())std::fprintf(stderr,"base %s dependencies=%u declared=%u inherited=%u inherited_change=%u\n",key(e.node()).c_str(),e.payload()->parent_dependencies(),e.payload()->declared_impact(),e.payload()->declared_inherited_impact(),e.payload()->inherited_change_impact());
            for(int32_t j=0;j<l.semantic_delta_count;++j){
                aether_node_t n{};aether_computed_style_handle_t h{};uint32_t visual{};
                ok(aether_style_transaction_get_state_delta(lt,j,&n,&h,&visual));
                const auto* before=old->directory().find(reinterpret_cast<uintptr_t>(n));
                const auto after=h->record->payload_owner();
                std::fprintf(stderr,"delta %s visual=%u propagation=%u background-currentColor=%d->%d border-currentColor=%d%d%d%d->%d%d%d%d\n",key(n).c_str(),visual,compare_node_semantics(*before->payload,*after).propagation,
                    before->payload->color_references()->background(),after->color_references()->background(),
                    before->payload->color_references()->border(0),before->payload->color_references()->border(1),before->payload->color_references()->border(2),before->payload->color_references()->border(3),
                    after->color_references()->border(0),after->color_references()->border(1),after->color_references()->border(2),after->color_references()->border(3));
                aether_computed_style_handle_destroy(h);
            }
        }
        if(computes>=0)CHECK(work.compute_nodes==uint64_t(computes));
        if(zero_visual)CHECK(l.semantic_delta_count>0&&l.visual_delta_count==0&&l.visual_impact==0);
        // Oracle is prepared against its own unchanged previous revision, NOT
        // after publishing the incremental result. Its historical impacts are
        // therefore meaningful and directly comparable.
        auto ft=oracle.prepare(true);const auto f=info(ft);CHECK(!f.incremental);
        CHECK(l.semantic_delta_count==f.semantic_delta_count&&l.visual_delta_count==f.visual_delta_count&&l.visual_impact==f.visual_impact);
        CHECK(deltas(lt)==deltas(ft));
        local.commit(lt);oracle.commit(ft);equivalent(local.snapshot(),oracle.snapshot());
        aether_style_transaction_destroy(lt);aether_style_transaction_destroy(ft);
        for(size_t i=0;i<retained.size();++i){
            CHECK(retained[i]->generation()==i+1);
            CHECK(retained[i]->document()->values().generation==retained[i]->generation());
            for(size_t j=0;j<retained_payloads[i].size();++j)CHECK(retained[i]->entries()[j].payload()==retained_payloads[i][j]);
        }
    }
};
static void local_colors(){
    scenario="background and border local";
    Pair p("#a:hover {background-color:red;border-color:blue} #a:focus {background-color:green;border-color:red}");
    p.state("a",AETHER_STATE_HOVER);p.step(AETHER_STATE_FALLBACK_NONE,1);
    p.state("a",AETHER_STATE_FOCUS);p.step(AETHER_STATE_FALLBACK_NONE,1);
    p.state("a",0);p.step(AETHER_STATE_FALLBACK_NONE,1);
    // A -> B -> A updates neither style nor ledger. Counter checks only assert
    // compute work; they do not claim zero DOM traversal (C2 is O(depth)).
    p.state("a",AETHER_STATE_HOVER);p.state("a",0);p.step(AETHER_STATE_FALLBACK_NONE,0);
}
static void inheritance(){
    const char* cases[]={
        "#a:hover {background-color:red;border-color:blue} #b,#c {background-color:inherit;border-color:inherit}",
        "#a:hover {color:red;font-size:24px} #b {width:2em;height:50%;line-height:1.5} #c {font-size:125%;line-height:150%;padding:1em}",
        "#a {--tone:red} #a:hover {--tone:blue} #b {color:var(--tone)} #c {background-color:currentColor}",
        "#a {color:red;background-color:red} #a:hover {background-color:currentColor} #a:focus {color:blue} #b {background-color:inherit}",
        "#a:hover {color:red} #b:focus {color:blue;background-color:currentColor} #c {border-color:currentColor}"
    };
    for(const auto css:cases){scenario=css;Pair p(css);
        p.state("a",AETHER_STATE_HOVER);p.step();
        p.state("a",AETHER_STATE_HOVER|AETHER_STATE_FOCUS);p.state("b",AETHER_STATE_FOCUS);p.step();
        p.state("a",0);p.state("b",0);p.step();
    }
    scenario="custom property metadata only";Pair p("#a {--unused:red} #a:hover {--unused:blue}");
    p.state("a",AETHER_STATE_HOVER);p.step(AETHER_STATE_FALLBACK_NONE,-1,true);
    p.state("a",0);p.step(AETHER_STATE_FALLBACK_NONE,-1,true);
}
static void logical_selectors(){
    for(const char* css:{"#a:not(:hover) {background-color:red}","#a:is(:hover,:focus) {background-color:red}","#a:where(:hover,:focus) {border-color:blue}","#a:hover:focus {background-color:red}"}){
        scenario=css;Pair p(css);p.state("a",AETHER_STATE_HOVER);p.step();
        p.state("a",AETHER_STATE_FOCUS);p.step();p.state("a",AETHER_STATE_HOVER|AETHER_STATE_FOCUS);p.step();p.state("a",0);p.step();
    }
}
static void fallbacks(){
    for(const char* css:{"#a:hover .label {color:red}","#a:hover > .label {color:red}","#a:hover + .card {color:red}","#a:hover ~ .card {color:red}","body:has(#a:hover) .label {color:red}"}){
        scenario=css;Pair p(css);p.state("a",AETHER_STATE_HOVER);p.step(AETHER_STATE_FALLBACK_SELECTOR);
        p.state("a",0);p.step(AETHER_STATE_FALLBACK_SELECTOR);
    }
    for(const char* css:{"#a::before {content:'before'} #a:hover {background-color:red}","#a::after {content:'after'} #a:hover {color:red}"}){
        scenario=css;Pair p(css);p.state("a",AETHER_STATE_HOVER);p.step(AETHER_STATE_FALLBACK_PSEUDO);
        p.state("a",0);p.step(AETHER_STATE_FALLBACK_PSEUDO);
    }
    scenario="root font FULL";Pair p("#root:hover {font-size:24px} #b {width:2rem}");
    p.state("root",AETHER_STATE_HOVER);p.step(AETHER_STATE_FALLBACK_ROOT_FONT);
    p.state("root",0);p.step(AETHER_STATE_FALLBACK_ROOT_FONT);
}
int main(int argc,char** argv){
    CHECK(aether_work_counters_set_enabled(AETHER_TRUE));
    const std::string selected=argc>1?argv[1]:"all";
    CHECK(selected=="all"||selected=="colors"||selected=="inheritance"||selected=="logical"||selected=="fallbacks");
    if(selected=="all"||selected=="colors")local_colors();
    if(selected=="all"||selected=="inheritance")inheritance();
    if(selected=="all"||selected=="logical")logical_selectors();
    if(selected=="all"||selected=="fallbacks")fallbacks();
    std::puts("local state differential FULL, complete payloads, impacts and retained snapshots passed");
}
