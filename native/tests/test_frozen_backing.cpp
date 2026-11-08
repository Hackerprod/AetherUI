#include "css/frozen_c_string.h"
#include "css/style_map_version.h"
#include "style_runtime/frozen_runtime_metadata.h"
#include "dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
using namespace aether::style;
static void need(bool yes,const char* message) { if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::abort();} }
static void ok(aether_status_t s) { need(s==AETHER_OK,"status"); }
static FrozenStyleRef style() {
    std::shared_ptr<const FrozenStylePayload> p(new const FrozenStylePayload({}, {}, {}, {}, {}, {}));
    return std::make_shared<const FrozenStyle>(StyleScalars{},p);
}
static NodeRecordInput input(aether_node_s& n,aether_doc_s& d) {
    NodeRecordInput in; in.node=&n; in.style=style(); in.node_lifetime=n.identity; in.document_lifetime=d.identity;
    in.node_identity=aether_node_identity(&n); in.document_identity=aether_node_identity(&d);
    in.declared_impact=17;in.declared_inherited_impact=29;in.inherited_change_impact=43;return in;
}
int main() {
    static_assert(!std::is_assignable_v<FrozenCString&,FrozenCString>);
    static_assert(std::is_same_v<decltype(std::declval<const FrozenCString&>().c_str()),const char*>);
    FrozenCString absent, empty("");need(!absent.present()&&!absent.c_str(),"absent");need(empty.present()&&empty.c_str()&&empty.c_str()[0]==0,"present empty");
    std::string source("ab\0cd",5);FrozenCString frozen(source);const char* pinned=frozen.c_str();source.assign(500,'x');
    need(frozen.size()==5 && frozen.view()==std::string_view("ab\0cd",5)&&pinned[5]==0,"exact bytes independent");
    FrozenCString shared(frozen);need(shared.c_str()==pinned,"copy shares immutable bytes");
    for(int i=0;i<10000;++i)need(frozen.c_str()==pinned,"zero-copy read");
    aether_doc_s doc; aether_node_s node,other; auto in=input(node,doc);FrozenNodeRef record;ok(freeze_node_record(in,record));
    need(record->node()==&node&&record->style()==in.style&&record->node_lifetime()==node.identity&&record->document_lifetime()==doc.identity,"owners");
    need(record->node_identity()==in.node_identity&&record->document_identity()==in.document_identity&&record->declared_impact()==17&&record->declared_inherited_impact()==29&&record->inherited_change_impact()==43,"all binding scalars");
    need(record->origin()==RecordOrigin::legacy&&!record->measures()&&!record->custom_properties()&&!record->color_references()&&!record->generated_leaf(),"legacy nullable");
    auto old=record;in.node_identity++;need(freeze_node_record(in,record)==AETHER_ERROR_INVALID_STATE&&record==old,"bad token atomic");in.node_identity--;
    in.origin=RecordOrigin::runtime;need(freeze_node_record(in,record)==AETHER_ERROR_INVALID_STATE&&record==old,"runtime requires metadata");
    FrozenScopeRef scope;ok(freeze_custom_scope({}, {},scope));
    in.custom_properties=scope;in.color_references=std::make_shared<const FrozenColorMetadata>(std::array<bool,4>{true,false,true,false},ImmutableArray<uint8_t>{},ImmutableArray<uint8_t>{},false);
    auto document=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{1280,720,16,20,9,in.document_identity});
    in.measures=std::make_shared<const FrozenMeasures>(MeasureScalarValues{},document,ImmutableArray<MeasureBackgroundPosition>{},ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
    ok(freeze_node_record(in,record));need(record->measures()==in.measures&&record->custom_properties()==scope&&record->color_references()==in.color_references,"runtime metadata owners exact");
    const auto runtime=record;
    auto reject_record=[&](NodeRecordInput invalid) { auto output=runtime; need(freeze_node_record(invalid,output)==AETHER_ERROR_INVALID_STATE && output==runtime,"invalid record preserves output"); };
    auto invalid=in;invalid.style.reset();reject_record(invalid);invalid=in;invalid.node=nullptr;reject_record(invalid);
    invalid=in;invalid.measures.reset();reject_record(invalid);invalid=in;invalid.custom_properties.reset();reject_record(invalid);invalid=in;invalid.color_references.reset();reject_record(invalid);
    invalid=in;invalid.node_lifetime=other.identity;reject_record(invalid);invalid=in;invalid.document_identity++;reject_record(invalid);
    invalid=in;invalid.document_lifetime=std::make_shared<node_identity_s>();invalid.document_lifetime->owner=&doc;invalid.document_identity=reinterpret_cast<uintptr_t>(invalid.document_lifetime.get());reject_record(invalid);
    // A new record can share the exact old style yet carry a different document.
    auto newer=in;auto nextdoc=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{1440,900,17,21,10,in.document_identity});
    newer.measures=std::make_shared<const FrozenMeasures>(MeasureScalarValues{},nextdoc,ImmutableArray<MeasureBackgroundPosition>{},ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
    newer.inherited_change_impact=91;FrozenNodeRef next;ok(freeze_node_record(newer,next));
    need(next->style()==runtime->style() && runtime->measures()->document().values().generation==9 && next->measures()->document().values().generation==10 && runtime->inherited_change_impact()==43 && next->inherited_change_impact()==91,"same-style generations remain distinct");
    BackingBuildInput build; build.entries.emplace(&node,record);auto it=build.entries.begin();auto* slot=&it->second;
    BackingRef backing;ok(freeze_backing(std::move(build),backing));*slot=old;build.entries.clear();
    need(backing->find(&node)==record.get()&&backing->find_owner(&node)==record&&!backing->find(&other)&&backing->size()==1,"adversarial retained iterator isolated");
    auto retained_backing=backing;BackingBuildInput badindex;badindex.entries.emplace(&other,record);need(freeze_backing(badindex,backing)==AETHER_ERROR_INVALID_STATE&&backing==retained_backing,"invalid index preserves output");
    badindex.entries.clear();badindex.entries.emplace(&node,FrozenNodeRef{});need(freeze_backing(badindex,backing)==AETHER_ERROR_INVALID_STATE&&backing==retained_backing,"null index record");
    MapVersionInput mv;mv.backing=backing;mv.document_lifetime=doc.identity;mv.document_identity=aether_node_identity(&doc);mv.structure_epoch=11;mv.input_epoch=23;mv.style_tree_epoch=37;
    MapVersionRef version;ok(make_map_version(mv,version));need(version->backing()==backing&&version->mode()==MapMode::legacy_partial&&!version->revision()&&!version->contextual()&&version->generation()==0,"partial mode");
    need(version->document_lifetime()==doc.identity&&version->document_identity()==mv.document_identity&&version->structure_epoch()==11&&version->input_epoch()==23&&version->style_tree_epoch()==37,"all version fields");
    need(!structure_valid(version.get())&&inputs_current(version.get()),"partial guards preserve epochs");doc.identity->structure_epoch=11;need(structure_valid(version.get()),"current structure");doc.identity->alive=false;need(!structure_valid(version.get()),"dead document");doc.identity->alive=true;
    auto immutable_version=version;auto badversion=mv;badversion.generation=1;need(make_map_version(badversion,version)==AETHER_ERROR_INVALID_STATE&&version==immutable_version,"partial generation zero");
    badversion=mv;badversion.backing.reset();need(make_map_version(badversion,version)==AETHER_ERROR_INVALID_STATE&&version==immutable_version,"backing required");
    badversion=mv;badversion.document_lifetime=std::make_shared<node_identity_s>();badversion.document_lifetime->owner=&doc;badversion.document_identity=reinterpret_cast<uintptr_t>(badversion.document_lifetime.get());need(make_map_version(badversion,version)==AETHER_ERROR_INVALID_STATE&&version==immutable_version,"live authentic document token");
    // A dead legitimate document control can still be retained by partial snapshots.
    std::shared_ptr<node_identity_s> dead;{auto temporary=std::make_unique<aether_doc_s>();dead=temporary->identity;}
    auto deadinput=mv;deadinput.document_lifetime=dead;deadinput.document_identity=reinterpret_cast<uintptr_t>(dead.get());MapVersionRef deadversion;ok(make_map_version(deadinput,deadversion));need(!structure_valid(deadversion.get()),"dead owner never dereferenced");
    auto previous=version;mv.mode=MapMode::contextual;need(make_map_version(mv,version)==AETHER_ERROR_INVALID_STATE&&version==previous,"no fake revision");
    mv.mode=MapMode::legacy_complete;need(make_map_version(mv,version)==AETHER_ERROR_INVALID_STATE&&version==previous,"complete requires revision");
    BackingRef emptyback;ok(freeze_backing({},emptyback));MapVersionInput emptyin;emptyin.backing=emptyback;ok(make_map_version(emptyin,version));need(structure_valid(version.get())&&inputs_current(version.get())&&accepts_override(version.get(),previous.get()),"empty legacy");
    // Authentic leaf, not a borrowed stack handle or synthetic revision owner.
    auto leaf=std::make_shared<aether_node_s>();leaf->is_generated=true;leaf->snapshot_owned=true;
    auto pseudo=input(*leaf,doc);pseudo.generated_leaf=leaf;std::weak_ptr<aether_node_s> weak=leaf;FrozenNodeRef retained;ok(freeze_node_record(pseudo,retained));
    pseudo={};leaf.reset();need(!weak.expired(),"record pins real pseudo");retained.reset();need(weak.expired(),"no runtime/revision/frame cycle");
    std::puts("frozen backing functional PASS; opaque revision coherence/integration remains gated");
}
