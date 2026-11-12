#include "../src/style_runtime/style_diff.h"
#include "../src/style_runtime/style_property_registry_internal.h"
#include <cstdio>
#include <cstdlib>
using namespace aether::style;
static void require(bool ok,const char* label){if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);std::abort();}}
int main(){
    style_property_registry_init();
    const auto* registry=style_property_registry_get_global();
    require(registry->definitions.size()==style_dependency_catalog.size(),"dependency catalog covers every registered property exactly once");
    size_t noops=0,aliases=0,percentage=0;
    for(const auto& definition:registry->definitions){
        auto index=style_dependency_index(definition.name);
        if(index==style_dependency_catalog.size())std::fprintf(stderr,"Missing dependency key: %s\n",definition.name.c_str());
        require(index<style_dependency_catalog.size(),"registered key is classified");
        const auto& policy=style_dependency_catalog[index];
        const auto name=policy.name;
        const bool no_op=name=="font-style"||name=="letter-spacing"||name=="align-content"||name.starts_with("animation-");
        const bool alias=name=="-webkit-backdrop-filter"||name=="-webkit-background-clip"||name=="-webkit-text-fill-color"||name=="text-fill-color";
        require((policy.state==PropertyStatePolicy::no_op)==no_op,"explicit no-op policy");
        require((policy.state==PropertyStatePolicy::alias)==alias,"explicit alias policy");
        noops+=no_op;aliases+=alias;
        const bool dimension=name=="width"||name=="height"||name=="min-width"||name=="min-height"||name=="max-width"||name=="max-height";
        const auto expected=dimension?DependencyPercentagePolicy::dimension:name=="flex-basis"?DependencyPercentagePolicy::flex_basis:
            (name=="row-gap"||name=="column-gap")?DependencyPercentagePolicy::gap:DependencyPercentagePolicy::metadata;
        require(policy.percentage==expected,"only current nine percentage consumers have visual policy");
        percentage+=expected!=DependencyPercentagePolicy::metadata;
    }
    for(const auto& shorthand:registry->shorthand_defs)
        for(const auto& name:shorthand.target_names)require(style_dependency_index(name)<style_dependency_catalog.size(),"shorthand target classified");
    require(noops==11&&aliases==4&&percentage==9,"catalog classes explicitly counted");
    std::printf("style diff catalog: %zu registered keys, %zu aliases, %zu no-ops, %zu percentage consumers PASS\n",registry->definitions.size(),aliases,noops,percentage);
    style_property_registry_shutdown();
}
