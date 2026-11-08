#include "../src/style_runtime/style_property_registry_internal.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
void builtin_properties_register(style_property_registry_s*);
namespace {
int checks=0, failures=0;
void check(bool ok,const char* label) { ++checks; if(!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",label); } }
}
int run_corner_radius_registry_tests();
int run_opacity_registry_tests();

int main() {
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const auto* origin=registry.try_get("transform-origin");
    check(origin && origin->parse && origin->apply_checked && origin->parse_failure && origin->css_wide && origin->inherit_explicit,"origin callbacks registered");
    if(!origin || !origin->parse || !origin->apply_checked || !origin->parse_failure || !origin->css_wide || !origin->inherit_explicit) return 1;
    check(!origin->inherited && origin->impact==(STYLE_IMPACT_COMPOSITE|STYLE_IMPACT_HITTEST),"origin metadata");
    for(const char* source : {"center left","CENTER RIGHT","calc(20% + 3px) calc(1em + 4px)","c\\65 nter/**/left"}) {
        for(int padding : {0,1,128,4096}) {
            std::string value=std::string(padding,' ')+source+std::string(padding,'\t');
            check(origin->parse(value.c_str(),value),"aliased parse success");
            check(value==source,"aliased parse preserves trimmed source spelling");
            check(origin->parse(value.c_str(),value),"repeated aliased parse");
        }
    }
    for(const char* source : {"", "top 20px", "1", "calc(0)", "revert-layer", "left right", "inherit left"}) {
        std::string value=source;
        check(!origin->parse(value.c_str(),value),"invalid aliased parse");
        check(value==source,"invalid aliased parse leaves output unchanged");
        std::string out="sentinel";
        check(!origin->parse(source,out)&&out=="sentinel","invalid parse preserves separate output");
        const auto error=origin->parse_failure(source);
        check(!error.success && error.diagnostic_code && std::strcmp(error.diagnostic_code,"AETHER-CSS-TRANSFORM-ORIGIN")==0,"dedicated pure diagnostic");
        aether_computed_style_t style{};
        check(origin->apply_checked(&style,"right bottom").success,"seed origin");
        const auto saved=style.transform_origin;
        check(!origin->apply_checked(&style,source).success,"checked applicator rejects");
        check(std::memcmp(&saved,&style.transform_origin,sizeof(saved))==0,"atomic checked applicator");
    }
    check(origin->css_wide("/**/\\69 NHeRiT/**/")==property_css_wide_e::inherit,"decoded whole CSS-wide keyword");
    check(origin->css_wide("/**/\\72 EVERT/**/")==property_css_wide_e::initial,"revert explicitly initial");
    for(const char* source : {"revert-layer","inherit left","in/**/herit","inherit)","initial initial","inherit /*"})
        check(origin->css_wide(source)==property_css_wide_e::none,"CSS-wide classification never accepts mixed tokens");
    std::string out="sentinel"; check(!origin->parse(nullptr,out)&&out=="sentinel","null callback source");
    // The contextual measure cycle explicitly opts these properties in. Keep
    // an exact allowlist so an accidental new hook or a lost hook still fails.
    const std::set<std::string> contextual = {
        "width","height","min-width","min-height","max-width","max-height","flex-basis",
        "top","right","bottom","left","margin-top","margin-right","margin-bottom","margin-left",
        "padding-top","padding-right","padding-bottom","padding-left",
        "border-top-width","border-right-width","border-bottom-width","border-left-width",
        "font-size","line-height","row-gap","column-gap",
        "transform","transform-origin","border-radius","border-top-left-radius","border-top-right-radius",
        "border-bottom-left-radius","border-bottom-right-radius","background-image","background-position","background-size",
        "text-shadow","box-shadow"
    };
    for (const auto& name : contextual) check(registry.try_get(name.c_str())!=nullptr,"contextual allowlist property registered");
    for(const auto& definition : registry.definitions) {
        const bool expected = contextual.count(definition.name) != 0;
        check(static_cast<bool>(definition.css_wide)==expected,"exact CSS-wide hook coverage");
        check(static_cast<bool>(definition.inherit_context)==expected,"exact contextual inheritance coverage");
        check(static_cast<bool>(definition.inherit_explicit)==(definition.name=="transform-origin"),"legacy inheritance hook remains origin-only");
    }
    std::printf("transform-origin dispatch: %d checks, %d failures\n",checks,failures);
    const int corner_failures = run_corner_radius_registry_tests();
    const int opacity_failures = run_opacity_registry_tests();
    return (corner_failures || opacity_failures || failures) ? 1 : 0;
}
