#include "frozen_backing_fault_support.h"
#include "css/frozen_c_string.h"
#include "css/style_map_version.h"
#include "dom/dom_types.h"
#include <string>
using namespace aether::style;
using namespace std::literals;
static void need(bool p,const char* s){if(!p){std::fprintf(stderr,"FAIL %s\n",s);std::abort();}}
template<class F> void sweep(const char* label,F action) {
    for(auto mode:{fault::Mode::cpp,fault::Mode::aligned,fault::Mode::c}) {
        bool completed=false;
        for(long index=0;index<512;++index) {
            fault::arm(mode,index);action();fault::disarm();
            need(fault::cpp_alloc==fault::cpp_free&&fault::c_alloc==fault::c_free,"balanced allocations");
            if(!fault::hit){need(index==fault::calls,"clean terminal");std::printf("%s family=%d sites=%ld\n",label,int(mode),index);completed=true;break;}
        }
        need(completed,"sweep fail closed bound");
    }
}
int main(){
    FrozenCString baseline("baseline");
    sweep("cstring",[&]{try{FrozenCString s("exact embedded\0bytes"sv);need(!fault::hit,"no swallowed OOM");need(s.size()==20,"full embedded byte length");}catch(const std::bad_alloc&){need(fault::hit,"injected only");}need(baseline.view()=="baseline","old value stable");});
    aether_node_s node;NodeRecordInput in;in.node=&node;
    in.style=std::make_shared<const FrozenStyle>(StyleScalars{},std::shared_ptr<const FrozenStylePayload>(new const FrozenStylePayload({}, {}, {}, {}, {}, {})));
    FrozenNodeRef previous;need(freeze_node_record(in,previous)==AETHER_OK,"record seed");
    sweep("record",[&]{auto result=previous;auto s=freeze_node_record(in,result);if(fault::hit)need(s==AETHER_ERROR_OUT_OF_MEMORY&&result==previous,"record atomic");else need(s==AETHER_OK,"record success");});
    BackingBuildInput input;input.entries.emplace(&node,previous);BackingRef old;need(freeze_backing(input,old)==AETHER_OK,"backing seed");
    sweep("index",[&]{auto result=old;auto s=freeze_backing(input,result);if(fault::hit)need(s==AETHER_ERROR_OUT_OF_MEMORY&&result==old,"backing atomic");else need(s==AETHER_OK&&result->find(&node)==previous.get(),"backing success");});
    MapVersionInput vi;vi.backing=old;MapVersionRef prior;need(make_map_version(vi,prior)==AETHER_OK,"version seed");
    sweep("version",[&]{auto result=prior;auto s=make_map_version(vi,result);if(fault::hit)need(s==AETHER_ERROR_OUT_OF_MEMORY&&result==prior,"version atomic");else need(s==AETHER_OK,"version success");});
    fault::arm(fault::Mode::cpp,0);for(int i=0;i<10000;++i){need(baseline.c_str()[0]=='b',"cstring read");need(old->find(&node)==previous.get(),"backing read");}fault::disarm();need(!fault::hit&&fault::calls==0,"reads allocate zero");
    std::puts("frozen backing OOM PASS (zero sites means unreachable, not injected)");
}
