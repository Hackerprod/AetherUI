#include "frozen_backing_fault_support.h"
#include "css/immutable_style_backing.h"
#include "dom/dom_types.h"
#include <algorithm>
#include <array>
#include <climits>
#include <random>
#include <vector>
using namespace aether::style;
static void need(bool value,const char* why) { if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::abort();} }
static void ok(aether_status_t status) { need(status==AETHER_OK,"status"); }
static FrozenNodeRef make_record(aether_node_s* node, int value) {
    NodeRecordInput input;input.node=node;input.declared_impact=value;
    input.style=std::make_shared<const FrozenStyle>(StyleScalars{},std::make_shared<const FrozenStylePayload>(
        ImmutableArray<FrozenBackgroundLayer>{},ImmutableArray<aether_backdrop_filter>{},FrozenGradient{},
        ImmutableArray<aether_transform_operation>{},ImmutableArray<aether_shadow>{},ImmutableArray<aether_shadow>{}));
    FrozenNodeRef result;ok(freeze_node_record(input,result));return result;
}
static void audit(const BackingRef& backing, const FrozenNodeIndex& oracle) {
    need(backing && backing->size()==oracle.size(),"directory count");
    size_t visits=0;uintptr_t last=0;
    for(const auto& [node,record]:backing->entries()) {
        need(reinterpret_cast<uintptr_t>(node)>last,"iteration unique and ordered");last=reinterpret_cast<uintptr_t>(node);
        auto found=oracle.find(node);need(found!=oracle.end()&&found->second==record,"enumeration matches oracle");
        need(backing->find(node)==record.get()&&backing->find_owner(node)==record,"lookup matches enumeration");++visits;
    }
    need(visits==oracle.size(),"enumeration complete");
}
int main() {
    constexpr size_t count=2048;
    std::vector<std::unique_ptr<aether_node_s>> nodes;
    std::vector<FrozenNodeRef> first,second;
    BackingBuildInput input;
    for(size_t i=0;i<count;++i) {
        nodes.push_back(std::make_unique<aether_node_s>());
        first.push_back(make_record(nodes.back().get(),1));second.push_back(make_record(nodes.back().get(),2));
        input.entries.emplace(nodes.back().get(),first.back());
    }
    BackingRef original;ok(freeze_backing(input,original));audit(original,input.entries);
    auto current=original;auto oracle=input.entries;
    std::mt19937 random(0xA37E);
    // Differential insert/replace/remove plus repeated keys across retained snapshots.
    for(int step=0;step<6000;++step) {
        size_t index=random()%count;
        FrozenNodeRef record=random()%3 ? (random()%2?first[index]:second[index]) : FrozenNodeRef{};
        const std::array<BackingPatchEntry,1> patch={{{nodes[index].get(),record}}};
        BackingRef next;ok(patch_backing(current,patch,next));
        if(record)oracle.insert_or_assign(nodes[index].get(),record);else oracle.erase(nodes[index].get());
        current=std::move(next);
        if(step%100==0) {audit(current,oracle);audit(original,input.entries);}
    }
    audit(current,oracle);audit(original,input.entries);
    // Every unchanged read and full enumeration is allocation-free, with real
    // allocation failure armed, not inferred from timings.
    fault::arm(fault::Mode::cpp,0);
    for(int repeat=0;repeat<20;++repeat) audit(current,oracle);
    fault::disarm();need(!fault::hit&&fault::calls==0,"reads and enumeration allocate zero");
    std::array<BackingPatchEntry,1> replacement={{{nodes[0].get(),second[0]}}};
    bool terminal=false;
    for(long ordinal=0;ordinal<100;++ordinal) {
        auto output=original;
        fault::arm(fault::Mode::cpp,ordinal);
        auto status=patch_backing(original,replacement,output);
        if(fault::hit)need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==original,"patch OOM preserves output");
        else {need(status==AETHER_OK&&output->find_owner(nodes[0].get())==second[0],"patch terminal");terminal=true;}
        output.reset();
        need(fault::cpp_alloc==fault::cpp_free,"patch allocation sweep balanced");fault::disarm();
        audit(original,input.entries);
        if(terminal) {need(ordinal<=long(std::numeric_limits<uintptr_t>::digits+3),"bounded path allocation count");break;}
    }
    need(terminal,"patch fault sweep terminates");
    auto sweep=[&](const char* label,const BackingRef& base,std::span<const BackingPatchEntry> changes) {
        bool completed=false;
        for(long ordinal=0;ordinal<300;++ordinal) {
            auto result=base;
            fault::arm(fault::Mode::cpp,ordinal);
            auto status=patch_backing(base,changes,result);
            if(fault::hit)need(status==AETHER_ERROR_OUT_OF_MEMORY&&result==base,"multi-operation patch OOM atomic");
            else {need(status==AETHER_OK,"multi-operation patch terminal");completed=true;}
            result.reset();need(fault::cpp_alloc==fault::cpp_free,"multi-operation patch balanced");
            fault::disarm();
            if(completed) {std::printf("%s allocation sites=%ld\n",label,ordinal);break;}
        }
        need(completed,"multi-operation sweep terminates");
    };
    std::array<BackingPatchEntry,2> removals={{{nodes[0].get(),{}},{nodes[count/2].get(),{}}}};
    sweep("remove",original,removals);
    BackingRef removed;ok(patch_backing(original,removals,removed));
    std::array<BackingPatchEntry,2> inserts={{{nodes[0].get(),second[0]},{nodes[count/2].get(),second[count/2]}}};
    sweep("insert",removed,inserts);
    std::array<BackingPatchEntry,3> mixed={{{nodes[0].get(),second[0]},{nodes[1].get(),{}},{nodes[count/2].get(),second[count/2]}}};
    sweep("insert-remove-insert",removed,mixed);
    // Validate all input entries before allocating/publishing, including a bad
    // later entry and invalid null keys. Preserve the caller's output on error.
    std::array<BackingPatchEntry,2> invalid={{{nodes[0].get(),second[0]},{nodes[1].get(),second[2]}}};
    auto output=original;
    need(patch_backing(original,invalid,output)==AETHER_ERROR_INVALID_STATE&&output==original,"mismatched key rejected atomically");
    invalid[1]={nullptr,{}};
    need(patch_backing(original,invalid,output)==AETHER_ERROR_INVALID_STATE&&output==original,"null key rejected atomically");
    std::array<BackingPatchEntry,3> repeated={{{nodes[0].get(),second[0]},{nodes[0].get(),{}},{nodes[0].get(),first[0]}}};
    ok(patch_backing(original,repeated,output));audit(output,input.entries);
    // No-op patches reuse the exact authentic backing without allocations.
    replacement[0]={nodes[0].get(),first[0]};
    fault::arm(fault::Mode::cpp,0);ok(patch_backing(original,replacement,output));fault::disarm();
    need(output==original&&!fault::hit&&fault::calls==0,"same-record no-op reuses backing");
    // A patch must not retain previous backings or replaced records. Holding
    // the final snapshot alone cannot keep a chain of historical revisions.
    aether_node_s isolated;
    auto old=make_record(&isolated,3);std::weak_ptr<const FrozenNodeRecord> weak_record=old;
    BackingRef history;std::array<BackingPatchEntry,1> entry={{{&isolated,old}}};
    ok(patch_backing({},entry,history));old.reset();entry[0].record.reset();
    for(int i=0;i<2000;++i) {
        std::weak_ptr<const ImmutableStyleBacking> weak_backing=history;
        entry[0].record=make_record(&isolated,i+4);
        BackingRef next;ok(patch_backing(history,entry,next));history=std::move(next);
        need(weak_backing.expired(),"new snapshot retains no previous backing");
    }
    need(weak_record.expired(),"replaced historical record released");
    entry[0].record.reset();ok(patch_backing(history,entry,output));
    need(output->size()==0&&output->entries().empty()&&!output->find(&isolated),"last erase yields empty directory");
    std::puts("PASS persistent backing: differential, snapshots, bounded paths, no-allocation reads, fault sweep, history release");
}
