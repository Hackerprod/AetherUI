#include "css/style_revision.h"
#include "css/style_map_version.h"
#include "style_runtime/frozen_runtime_metadata.h"
#include "dom/dom_types.h"
#include "frozen_backing_fault_support.h"
#include <array>
#include <limits>
#include <vector>
using namespace aether::style;
static void need(bool value,const char* what) { if(!value){std::fprintf(stderr,"FAIL %s\n",what);std::abort();} }
static void ok(aether_status_t status){need(status==AETHER_OK,"status");}
struct Fixture {
    aether_doc_s doc;
    std::vector<std::unique_ptr<aether_node_s>> nodes;
    std::vector<FrozenNodePayloadRef> payloads;
    FrozenStyleRef style;
    FrozenScopeRef scope;
    FrozenColorRef colors;
    explicit Fixture(size_t count) {
        style=std::make_shared<const FrozenStyle>(StyleScalars{},std::make_shared<const FrozenStylePayload>(
            ImmutableArray<FrozenBackgroundLayer>{},ImmutableArray<aether_backdrop_filter>{},FrozenGradient{},
            ImmutableArray<aether_transform_operation>{},ImmutableArray<aether_shadow>{},ImmutableArray<aether_shadow>{}));
        ok(freeze_custom_scope({}, {}, scope));
        colors=std::make_shared<const FrozenColorMetadata>(std::array<bool,4>{},ImmutableArray<uint8_t>{},ImmutableArray<uint8_t>{},false);
        for(size_t i=0;i<count;++i){nodes.push_back(std::make_unique<aether_node_s>());payloads.push_back(payload(i,16));}
    }
    FrozenNodePayloadRef payload(size_t index,float font,FrozenScopeRef backdrop={}) {
        NodePayloadInput in;in.origin=RecordOrigin::runtime;in.node=nodes[index].get();in.style=style;
        MeasureScalarValues values{};values.self_font_size=font;
        in.measures=std::make_shared<const FrozenMeasurePayload>(values,ImmutableArray<MeasureBackgroundPosition>{},
            ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
        in.custom_properties=scope;in.color_references=colors;in.backdrop_custom_properties=std::move(backdrop);
        in.node_lifetime=in.node->identity;in.node_identity=aether_node_identity(in.node);
        in.document_lifetime=doc.identity;in.document_identity=aether_node_identity(&doc);
        FrozenNodePayloadRef result;ok(freeze_node_payload(in,result));return result;
    }
    FullPayloadRevisionInput input(uint64_t generation) {
        FullPayloadRevisionInput in;in.generation=generation;in.document_identity=aether_node_identity(&doc);
        in.document_lifetime=doc.identity;in.input_epoch=doc.identity->mutation_epoch;
        in.structure_epoch=doc.identity->structure_epoch;in.style_tree_epoch=doc.identity->style_tree_epoch+1;
        in.environment={800,600,1,16,0,0,0};
        in.document=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{800,600,16,16,generation,in.document_identity});
        for(size_t i=0;i<payloads.size();++i)in.entries.push_back({nodes[i].get(),payloads[i],0});
        return in;
    }
};
static StyleRevisionRef freeze(const StyleRevisionRef& base,const FullPayloadRevisionInput& in) {
    CertifiedRevisionDeltaRef certificate;ok(certify_full_payload_revision(base,in,certificate));
    StyleRevisionRef result;ok(freeze_certified_revision(certificate,result));return result;
}
static MapVersionRef map(const StyleRevisionRef& revision) {
    MapVersionInput in;in.mode=MapMode::contextual;in.revision=revision;
    in.document_lifetime=revision->document_lifetime();in.document_identity=revision->document_identity();
    in.generation=revision->generation();in.input_epoch=revision->input_epoch();
    in.structure_epoch=revision->structure_epoch();in.style_tree_epoch=revision->style_tree_epoch();
    MapVersionRef result;ok(make_map_version(in,result));return result;
}
int main() {
    {
        Fixture ledger(3);
        const auto first=freeze({},ledger.input(1));
        ledger.doc.identity->style_tree_epoch=first->style_tree_epoch();
        ledger.nodes[1]->state=AETHER_STATE_HOVER;
        ++ledger.doc.identity->mutation_epoch;
        const auto input=ledger.input(2);
        CertifiedRevisionDeltaRef certificate;ok(certify_full_payload_revision(first,input,certificate));
        need(certificate->changed_payloads()==0,"input-only state does not invent a payload delta");
        StyleRevisionRef second;ok(freeze_certified_revision(certificate,second));
        const auto key=reinterpret_cast<uintptr_t>(ledger.nodes[1].get());
        const auto* old=first->directory().find(key);const auto* current=second->directory().find(key);
        need(old->has_committed_state&&old->committed_state==0,"old committed ledger immutable");
        need(current->has_committed_state&&current->committed_state==AETHER_STATE_HOVER,"new state committed separately");
        need(old->payload==current->payload,"input-only ledger shares semantic payload");
        need(second->entries()[1].impact()==0,"input-only ledger has no visual impact");
    }
    Fixture f(512);
    // Equal raw token addresses with unrelated control blocks are not owners.
    // Rejection is checked while the real DOM and tokens are still alive.
    {
        const auto& p=f.payloads[0];NodePayloadInput in;
        in.origin=p->origin();in.node=p->node();in.style=p->style();in.measures=p->measures();
        in.custom_properties=p->custom_properties();in.color_references=p->color_references();
        in.node_lifetime=p->node_lifetime();in.node_identity=p->node_identity();
        in.document_lifetime=p->document_lifetime();in.document_identity=p->document_identity();
        auto output=p;
        in.node_lifetime=std::shared_ptr<node_identity_s>(p->node_lifetime().get(),[](node_identity_s*){});
        need(freeze_node_payload(in,output)==AETHER_ERROR_INVALID_STATE&&output==p,"forged node token owner rejected");
        in.node_lifetime=p->node_lifetime();
        in.document_lifetime=std::shared_ptr<node_identity_s>(p->document_lifetime().get(),[](node_identity_s*){});
        need(freeze_node_payload(in,output)==AETHER_ERROR_INVALID_STATE&&output==p,"forged document token owner rejected");
    }
    {
        NodeRecordInput in;in.node=f.nodes[0].get();in.style=f.style;
        in.node_lifetime=in.node->identity;in.node_identity=aether_node_identity(in.node);
        in.document_lifetime=f.doc.identity;in.document_identity=aether_node_identity(&f.doc);
        FrozenNodeRef genuine;ok(freeze_node_record(in,genuine));auto output=genuine;
        in.node_lifetime=std::shared_ptr<node_identity_s>(f.nodes[0]->identity.get(),[](node_identity_s*){});
        need(freeze_node_record(in,output)==AETHER_ERROR_INVALID_STATE&&output==genuine,"legacy record rejects forged node token owner");
        in.node_lifetime=f.nodes[0]->identity;
        in.document_lifetime=std::shared_ptr<node_identity_s>(f.doc.identity.get(),[](node_identity_s*){});
        need(freeze_node_record(in,output)==AETHER_ERROR_INVALID_STATE&&output==genuine,"legacy record rejects forged document token owner");
    }
    auto input1=f.input(1);auto first=freeze({},input1);
    need(!first->backing()&&first->entries().size()==512,"payload-only full revision");
    f.doc.identity->style_tree_epoch=first->style_tree_epoch();
    auto input2=f.input(2);input2.entries[7].payload=f.payload(7,21);input2.entries[7].impact=AETHER_STYLE_IMPACT_PAINT;
    CertifiedRevisionDeltaRef certificate;ok(certify_full_payload_revision(first,input2,certificate));
    need(certificate->stable_order()&&certificate->changed_payloads()==1,"one changed measure payload certified");
    StyleRevisionRef second;
    fault::arm(fault::Mode::cpp,LONG_MAX);ok(freeze_certified_revision(certificate,second));
    const long allocations=fault::calls;fault::disarm();
    need(allocations<=long(4*std::numeric_limits<uintptr_t>::digits+16),"delta allocation independent of manifest length");
    need(!second->backing()&&second->entries().size()==512&&second->changes().size()==512,"complete no-wrapper logical manifest");
    auto old_view=BoundNodeView::find(first,f.nodes[7].get());auto new_view=BoundNodeView::find(second,f.nodes[7].get());
    FrozenNodeRef old_record,new_record;ok(old_view.export_record(old_record));ok(new_view.export_record(new_record));
    need(old_record->measures()->document_owner()==first->document()&&new_record->measures()->document_owner()==second->document(),"exports own exact revision documents");
    need(old_record->measures()->document().values().generation==1&&new_record->measures()->document().values().generation==2,"old/new generation preserved");
    FrozenNodeRef shared_old,shared_new;
    ok(BoundNodeView::find(first,f.nodes[1].get()).export_record(shared_old));
    ok(BoundNodeView::find(second,f.nodes[1].get()).export_record(shared_new));
    need(shared_old->payload_owner()==shared_new->payload_owner()&&
        shared_old->measures()->document_owner()!=shared_new->measures()->document_owner()&&
        shared_old->measures()->document().values().generation==1&&shared_new->measures()->document().values().generation==2,
        "same shared payload exports through different authentic document generations");
    auto first_map=map(first);
    fault::arm(fault::Mode::cpp,LONG_MAX);auto second_map=map(second);
    const long map_allocations=fault::calls;fault::disarm();
    need(map_allocations<=2,"contextual map creation allocates no per-node wrappers");
    need(!accepts_override(first_map.get(),second_map.get()),"cross-revision contextual override rejected");
    fault::arm(fault::Mode::cpp,0);
    for(size_t i=0;i<512;++i){
        const auto entry=second->entries()[i];need(entry.node()==f.nodes[i].get()&&!entry.record()&&entry.payload(),"ordered full payload entries");
        need(entry.impact()==(i==7?AETHER_STYLE_IMPACT_PAINT:0),"zero impacts preserved");
        const auto view=second_map->lookup(entry.node());
        need(view&&view.generation()==2&&view.measures_view().inputs().self_font_size==(i==7?21:16),"allocation-free contextual inputs");
        if(i!=7)need(view.payload_owner()==f.payloads[i],"unchanged payload shared");
    }
    need(old_view.generation()==1&&old_view.measures_view().inputs().self_font_size==16,"old borrowed view unchanged");
    need(!fault::hit&&fault::calls==0,"views and complete enumeration allocate zero");fault::disarm();
    // Same-address but independently owned revision handles are not authentic.
    StyleRevisionRef forged(first.get(),[](const style_revision_s*){});
    auto cert_output=certificate;
    need(certify_full_payload_revision(forged,input2,cert_output)==AETHER_ERROR_INVALID_STATE&&cert_output==certificate,"forged base owner rejected atomically");
    CertifiedRevisionDeltaRef forged_certificate(certificate.get(),[](const CertifiedRevisionDelta*){});
    auto preserved=second;
    need(freeze_certified_revision(forged_certificate,preserved)==AETHER_ERROR_INVALID_STATE&&preserved==second,"forged certificate control block rejected");
    auto bad=input2;bad.document=first->document();
    need(certify_full_payload_revision(first,bad,cert_output)==AETHER_ERROR_INVALID_STATE&&cert_output==certificate,"old document cannot acquire new generation");
    Fixture other(1);auto foreign=other.input(2);
    need(certify_full_payload_revision(first,foreign,cert_output)==AETHER_ERROR_INVALID_STATE&&cert_output==certificate,"cross-document base rejected");
    bad=input2;bad.entries[1]=bad.entries[0];
    need(certify_full_payload_revision(first,bad,cert_output)==AETHER_ERROR_INVALID_STATE&&cert_output==certificate,"duplicate manifest rejected");
    // A captured certificate cannot survive a new mutation, even without publication.
    ++f.doc.identity->mutation_epoch;auto unchanged=second;
    need(freeze_certified_revision(certificate,unchanged)==AETHER_ERROR_INVALID_STATE&&unchanged==second,"stale certificate rejected");
    --f.doc.identity->mutation_epoch;
    // Every ordinary allocation in the certified factory is an atomic boundary.
    bool terminal=false;
    for(long ordinal=0;ordinal<512;++ordinal){
        auto output=first;fault::arm(fault::Mode::cpp,ordinal);
        const auto status=freeze_certified_revision(certificate,output);
        if(fault::hit)need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==first,"factory OOM preserves old output");
        else {need(status==AETHER_OK,"factory terminal");terminal=true;}
        output.reset();need(fault::cpp_alloc==fault::cpp_free,"factory failure allocations balanced");fault::disarm();
        if(terminal)break;
    }
    need(terminal,"factory fault sweep terminates");
    bool exported=false;
    for(long ordinal=0;ordinal<64;++ordinal){
        auto output=old_record;fault::arm(fault::Mode::cpp,ordinal);
        const auto status=new_view.export_record(output);
        if(fault::hit)need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==old_record,"export OOM preserves old bound owner");
        else {need(status==AETHER_OK&&output->measures()->document_owner()==second->document(),"export terminal owns requested revision");exported=true;}
        output.reset();need(fault::cpp_alloc==fault::cpp_free,"export allocations balanced");fault::disarm();
        if(exported)break;
    }
    need(exported,"export fault sweep terminates");
    // Metadata-only backdrop changes are not hidden behind identical style/RGBA.
    CustomPropertyInput properties;properties["--signal"].value="red";
    FrozenScopeRef backdrop;ok(freeze_custom_scope(properties,{},backdrop));
    auto metadata=f.input(3);metadata.entries[5].payload=f.payload(5,16,backdrop);
    CertifiedRevisionDeltaRef metadata_certificate;ok(certify_full_payload_revision(first,metadata,metadata_certificate));
    need(metadata_certificate->changed_payloads()==1,"backdrop metadata difference changes payload without visual delta");
    // The exhaustive certification phase has its own failure boundaries.
    Fixture small(4);auto small_input=small.input(1);bool certified=false;
    for(long ordinal=0;ordinal<256;++ordinal){
        auto output=certificate;fault::arm(fault::Mode::cpp,ordinal);
        const auto status=certify_full_payload_revision({},small_input,output);
        if(fault::hit)need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==certificate,"certification OOM preserves output");
        else {need(status==AETHER_OK,"certification terminal");certified=true;}
        output.reset();need(fault::cpp_alloc==fault::cpp_free,"certification allocations balanced");fault::disarm();
        if(certified)break;
    }
    need(certified,"certification fault sweep terminates");
    // Full output can reorder/remove nodes. Public changes still include retirement.
    auto structural=f.input(3);std::swap(structural.entries[0],structural.entries[511]);structural.entries.erase(structural.entries.begin()+9);
    auto reordered=freeze(second,structural);
    need(reordered->entries().size()==511&&reordered->changes().size()==512&&reordered->retired_nodes().size()==1,"structural retirement counts");
    need(reordered->entries()[0].node()==f.nodes[511].get()&&reordered->changes()[511].node()==f.nodes[9].get()&&!reordered->changes()[511].payload(),"ordered structural changes and retirement");
    // The view, not the caller's original shared_ptr variable, owns the revision.
    std::weak_ptr<const style_revision_s> pinned=reordered;auto pinned_view=BoundNodeView::find(reordered,f.nodes[511].get());
    reordered.reset();need(!pinned.expired()&&pinned_view.generation()==3,"view pins actual revision lifetime");
    pinned_view={};need(pinned.expired(),"view releases revision");
    // No revision chain may survive behind an unchanged directory/manifest.
    Fixture history_fixture(16);auto history=freeze({},history_fixture.input(1));
    for(uint64_t generation=2;generation<1002;++generation){
        std::weak_ptr<const style_revision_s> previous=history;
        auto next_input=history_fixture.input(generation);history=freeze(history,next_input);
        need(previous.expired(),"delta retains no historical revision chain");
    }
    std::printf("payload revision delta PASS: bound generations, full order, K-path allocations=%ld, OOM, provenance and history\n",allocations);
}
