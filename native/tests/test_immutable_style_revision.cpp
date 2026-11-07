#include "css/style_revision.h"
#include "css/style_map_types.h"
#include "style_runtime/frozen_runtime_metadata.h"
#include "dom/dom_types.h"
#include "frozen_backing_fault_support.h"
#include <type_traits>
using namespace aether::style;
static void need(bool b, const char* why) { if (!b) { std::fprintf(stderr,"FAIL: %s\n",why); std::abort(); } }
static void ok(aether_status_t s) { need(s==AETHER_OK,"status"); }
static FrozenNodeRef record(aether_node_s& n, aether_doc_s& d, FrozenDocumentRef document,
    std::shared_ptr<aether_node_s> leaf={}) {
    NodeRecordInput in; in.origin=RecordOrigin::runtime;in.node=&n;
    std::shared_ptr<const FrozenStylePayload> payload(new const FrozenStylePayload({}, {}, {}, {}, {}, {}));
    in.style=std::make_shared<const FrozenStyle>(StyleScalars{},payload);
    in.measures=std::make_shared<const FrozenMeasures>(MeasureScalarValues{},document,ImmutableArray<MeasureBackgroundPosition>{},ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
    ok(freeze_custom_scope({}, {}, in.custom_properties));
    in.color_references=std::make_shared<const FrozenColorMetadata>(std::array<bool,4>{},ImmutableArray<uint8_t>{},ImmutableArray<uint8_t>{});
    in.node_lifetime=n.identity; in.node_identity=aether_node_identity(&n);
    in.document_lifetime=d.identity;in.document_identity=aether_node_identity(&d);in.generated_leaf=leaf;
    FrozenNodeRef out;ok(freeze_node_record(in,out));return out;
}
static MapVersionInput map_input(StyleRevisionRef r, MapMode mode) {
    MapVersionInput in; in.mode=mode;in.revision=r;in.backing=r->backing();in.document_lifetime=r->document_lifetime();
    in.document_identity=r->document_identity();in.generation=r->generation();in.structure_epoch=r->structure_epoch();
    in.input_epoch=r->input_epoch();in.style_tree_epoch=r->style_tree_epoch();return in;
}
int main() {
    static_assert(!std::is_assignable_v<style_revision_s&,style_revision_s>);
    aether_doc_s doc, previous_doc; aether_node_s a,b;
    auto document=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{800,600,16,21,2,aether_node_identity(&doc)});
    auto ra=record(a,doc,document), rb=record(b,doc,document);
    auto dead=std::make_unique<aether_node_s>(); auto previous_document=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{800,600,16,20,1,aether_node_identity(&previous_doc)}); auto rd=record(*dead,previous_doc,previous_document); dead.reset();
    auto leaf=std::make_shared<aether_node_s>();leaf->is_generated=true;leaf->snapshot_owned=true;
    auto rp=record(*leaf,doc,document,leaf);std::weak_ptr<aether_node_s> weak_leaf=leaf;
    std::weak_ptr<const FrozenStyle> weak_style=rp->style();
    BackingBuildInput index;index.entries.emplace(&a,ra);index.entries.emplace(&b,rb);
    StyleRevisionInput in;ok(freeze_backing(index,in.backing));in.document=document;in.document_lifetime=doc.identity;
    in.document_identity=aether_node_identity(&doc);in.generation=2;in.input_epoch=5;in.structure_epoch=7;in.style_tree_epoch=11;
    in.entries={{&b,rb,0},{&a,ra,17}};in.retired={{rp,31},{rd,31}};
    StyleRevisionRef revision;ok(freeze_style_revision(in,revision));
    need(revision->entries().size()==2 && revision->changes().size()==4,"complete changes");
    need(revision->changes()[0].node()==&b && revision->changes()[0].impact()==0 && revision->changes()[1].record()==ra,"NONE and order");
    need(!revision->changes()[2].record() && revision->changes()[2].node()==leaf.get() && revision->retired_nodes().size()==2,"retirement");
    auto reject=[&](StyleRevisionInput bad){auto out=revision;need(freeze_style_revision(bad,out)==AETHER_ERROR_INVALID_STATE && out==revision,"invalid atomic");};
    auto bad=in;bad.entries.pop_back();reject(bad);bad=in;bad.entries[1]=bad.entries[0];reject(bad);
    bad=in;bad.retired.push_back(bad.retired[0]);reject(bad);bad=in;bad.retired.push_back({ra,31});reject(bad);
    bad=in;bad.document=std::make_shared<const FrozenDocumentMeasureContext>(document->values());reject(bad);
    bad=in;bad.generation=3;reject(bad);bad=in;bad.document_identity++;reject(bad);
    bad=in;bad.entries[0].record=ra;reject(bad);
    auto mi=map_input(revision,MapMode::contextual);MapVersionRef version;ok(make_map_version(mi,version));
    auto badmap=[&](MapVersionInput invalid){auto output=version;need(make_map_version(invalid,output)==AETHER_ERROR_INVALID_STATE && output==version,"map coherence atomic");};
    auto bm=mi;bm.generation++;badmap(bm);bm=mi;bm.input_epoch++;badmap(bm);bm=mi;bm.style_tree_epoch++;badmap(bm);bm=mi;bm.structure_epoch++;badmap(bm);
    bm=mi;ok(freeze_backing(index,bm.backing));badmap(bm); // equal contents, distinct authentic backing
    auto legacy=in;legacy.contextual=false;StyleRevisionRef legacyrev;ok(freeze_style_revision(legacy,legacyrev));
    auto lm=map_input(legacyrev,MapMode::legacy_complete);MapVersionRef lv;ok(make_map_version(lm,lv));need(!lv->contextual(),"legacy complete false");
    lm.mode=MapMode::contextual;badmap(lm);
    auto partial=mi;partial.mode=MapMode::legacy_partial;partial.revision.reset();partial.generation=0;MapVersionRef pv;ok(make_map_version(partial,pv));
    need(!pv->revision() && pv->generation()==0 && pv->input_epoch()==5 && pv->structure_epoch()==7,"partial preserves provenance");
    aether_style_map_s wrapper;wrapper.current=version;auto snapshot=aether_style_map_snapshot(&wrapper);wrapper.current=pv;
    need(snapshot->current==version && pin_style_map(&wrapper)==pv,"constant-owner snapshot");
    aether_style_map_s empty;need(!pin_style_map(&empty) && !pin_style_map(nullptr),"caller must retain explicit presence");
    bool terminal=false;
    for(long ordinal=0;ordinal<1000;++ordinal) {
        auto output=revision;fault::arm(fault::Mode::cpp,ordinal);
        auto status=freeze_style_revision(in,output);bool hit=fault::hit;
        if(status==AETHER_OK) { need(!hit,"first terminal did not swallow failure"); output.reset(); need(fault::cpp_alloc==fault::cpp_free,"successful factory allocations balanced");fault::disarm();terminal=true;break; }
        need(hit && status==AETHER_ERROR_OUT_OF_MEMORY && output==revision,"OOM atomic");
        need(fault::cpp_alloc==fault::cpp_free,"failed factory balanced");fault::disarm();
    }
    need(terminal,"bounded OOM terminal");
    // Drop all construction inputs/revisions except the selected revision.
    bad={};legacy={};in.retired.clear();rp.reset();rd.reset();leaf.reset();legacyrev.reset();lm={};lv.reset();
    need(!weak_leaf.expired(),"retired leaf retained");
    need(weak_style.expired(),"retired payload not retained by new revision");
    std::puts("immutable revision/map contract and allocation sweep PASS");
}
