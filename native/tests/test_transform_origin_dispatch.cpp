#include "../src/style_runtime/style_property_registry_internal.h"
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>
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
    struct Accepted {
        std::string spelling;
        aether_css_length x, y;
    };
    const std::vector<Accepted> accepted={
        {"center left",{0,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0},{50,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0}},
        {"CENTER RIGHT",{100,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0},{50,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0}},
        {"calc(20% + 3px) calc(1em + 4px)",{3,AETHER_CSS_UNIT_LINEAR,20,0,0,0,0},{4,AETHER_CSS_UNIT_LINEAR,0,1,0,0,0}},
        {R"(c\65 nter/**/left)",{0,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0},{50,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0}},
        {R"(3p\x)",{3,AETHER_CSS_UNIT_PX,0,0,0,0,0},{50,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0}},
        {R"(10p\78  20\70x)",{10,AETHER_CSS_UNIT_PX,0,0,0,0,0},{20,AETHER_CSS_UNIT_PX,0,0,0,0,0}},
        {R"(c\61 lc(3p\78  + 25% + 2e\m + 4re\m + 5\vw + 6\vh)/**/ca\lc(2p\x + 10%))",
            {3,AETHER_CSS_UNIT_LINEAR,25,2,4,5,6},{2,AETHER_CSS_UNIT_LINEAR,10,0,0,0,0}},
        {std::string(R"(ca\lc(3p\x))") + "/*" + std::string(512,'x') + "*/" + R"(20\70x)",
            {3,AETHER_CSS_UNIT_LINEAR,0,0,0,0,0},{20,AETHER_CSS_UNIT_PX,0,0,0,0,0}}
    };
    const auto same=[](float a,float b) { return std::isfinite(a)&&std::isfinite(b)&&std::fabs(a-b)<.001f; };
    const auto same_length=[&](const aether_css_length& a,const aether_css_length& b) {
        return a.unit==b.unit && same(a.value,b.value) && same(a.linear_percent,b.linear_percent) &&
            same(a.linear_em,b.linear_em) && same(a.linear_rem,b.linear_rem) &&
            same(a.linear_vw,b.linear_vw) && same(a.linear_vh,b.linear_vh);
    };
    const auto seed=[](aether_computed_style_t& style) {
        style.width={17,AETHER_CSS_UNIT_EM,0,0,0,0,0};
        style.height={29,AETHER_CSS_UNIT_PX,0,0,0,0,0};
        style.opacity=.375f; style.color={11,22,33,44};
        style.transform_origin={{3,AETHER_CSS_UNIT_LINEAR,25,2,4,5,6},
                                {-7,AETHER_CSS_UNIT_LINEAR,31,11,13,17,19}};
    };
    for(const auto& item:accepted) {
        for(int padding:{0,1,128,4096}) {
            const std::string padded=std::string(padding,' ')+item.spelling+std::string(padding,'\t');
            std::string separate="sentinel";
            check(origin->parse(padded.c_str(),separate),"independent escaped parse success");
            check(separate==item.spelling,"independent parse retains exact trimmed spelling");
            for(size_t offset:{size_t(0),size_t(1),size_t(129)}) {
                // Offsets exercise true interior aliases; no STL SSO threshold is assumed.
                std::string value=std::string(offset,'!')+padded;
                check(origin->parse(value.c_str()+offset,value),"complete/interior alias success");
                check(value==item.spelling,"alias retains escapes comments and internal spaces exactly");
                check(origin->parse(value.c_str(),value)&&value==item.spelling,"repeated aliased parse stable");
            }
        }
        aether_computed_style_t style{}; seed(style);
        unsigned char before[sizeof(style)]; std::memcpy(before,&style,sizeof(style));
        const auto applied=origin->apply_checked(&style,item.spelling);
        check(applied.success,"escaped checked apply succeeds");
        check(same_length(style.transform_origin.x,item.x)&&same_length(style.transform_origin.y,item.y),"checked apply all symbolic fields");
        // Restore only the allowed destination region, then compare the original object's bytes.
        std::memcpy(&style.transform_origin,before+offsetof(aether_computed_style_t,transform_origin),sizeof(style.transform_origin));
        check(std::memcmp(before,&style,sizeof(style))==0,"successful apply changes no neighboring field");
    }
    const char* rejected[]={"", "top 20px", "1", "calc(0)", "revert-layer", "left right", "inherit left",
        R"(10p\78 20px)",R"(calc(10p\78 + 25%) 20px)",R"(c\61 lc/**/(10px) 20px)",R"(1\65 2px 20px)",
        R"(10p\x left)",R"(10p\x 20p\x !)","10px 20p\\"};
    for(const char* source:rejected) {
        for(int padding:{0,1,128,4096}) {
            const std::string padded=std::string(padding,' ')+source+std::string(padding,'\t');
            std::string out="sentinel";
            check(!origin->parse(padded.c_str(),out)&&out=="sentinel","invalid independent parse preserves output");
            for(size_t offset:{size_t(0),size_t(1),size_t(129)}) {
                std::string value=std::string(offset,'!')+padded;
                const std::string saved=value;
                check(!origin->parse(value.c_str()+offset,value),"invalid complete/interior aliased parse");
                check(value==saved,"invalid alias preserves whole buffer including prefix");
                check(!origin->parse(value.c_str()+offset,value)&&value==saved,"repeated rejected alias unchanged");
            }
        }
        const auto error=origin->parse_failure(source);
        check(!error.success && error.diagnostic_code && std::strcmp(error.diagnostic_code,"AETHER-CSS-TRANSFORM-ORIGIN")==0,"dedicated pure diagnostic");
        aether_computed_style_t style{}; seed(style);
        unsigned char before[sizeof(style)]; std::memcpy(before,&style,sizeof(style));
        const auto result=origin->apply_checked(&style,source);
        check(!result.success && result.diagnostic_code && std::strcmp(result.diagnostic_code,"AETHER-CSS-TRANSFORM-ORIGIN")==0,"checked applicator rejects with origin diagnostic");
        check(std::memcmp(before,&style,sizeof(style))==0,"rejection preserves every destination byte");
    }
    check(origin->css_wide("/**/\\69 NHeRiT/**/")==property_css_wide_e::inherit,"decoded whole CSS-wide keyword");
    check(origin->css_wide("/**/\\72 EVERT/**/")==property_css_wide_e::initial,"revert explicitly initial");
    for(const char* source : {"revert-layer","inherit left","in/**/herit","inherit)","initial initial","inherit /*"})
        check(origin->css_wide(source)==property_css_wide_e::none,"CSS-wide classification never accepts mixed tokens");
    std::string out="sentinel"; check(!origin->parse(nullptr,out)&&out=="sentinel","null callback source");
    // Contextual properties and computed backdrop lists explicitly opt in. Keep
    // an exact allowlist so an accidental new hook or a lost hook still fails.
    const std::set<std::string> contextual = {
        "width","height","min-width","min-height","max-width","max-height","flex-basis",
        "top","right","bottom","left","margin-top","margin-right","margin-bottom","margin-left",
        "padding-top","padding-right","padding-bottom","padding-left",
        "border-top-width","border-right-width","border-bottom-width","border-left-width",
        "font-size","line-height","row-gap","column-gap",
        "transform","transform-origin","border-radius","border-top-left-radius","border-top-right-radius",
        "border-bottom-left-radius","border-bottom-right-radius","background-image","background-position","background-size",
        "text-shadow","box-shadow","background-color"
    };
    const std::set<std::string> computed_lists = {"backdrop-filter", "-webkit-backdrop-filter"};
    for (const auto& name : contextual) check(registry.try_get(name.c_str())!=nullptr,"contextual allowlist property registered");
    for (const auto& name : computed_lists) check(registry.try_get(name.c_str())!=nullptr,"computed list allowlist property registered");
    for(const auto& definition : registry.definitions) {
        const bool expected = contextual.count(definition.name) != 0;
        const bool computed_list = computed_lists.count(definition.name) != 0;
        const bool opacity = definition.name == "opacity";
        const bool flex_alignment = definition.name == "flex-direction" || definition.name == "justify-content";
        check(static_cast<bool>(definition.css_wide)==(expected||computed_list||opacity||flex_alignment),"exact CSS-wide hook coverage");
        check(static_cast<bool>(definition.inherit_context)==expected,"exact contextual inheritance coverage");
        check(static_cast<bool>(definition.inherit_explicit)==(definition.name=="transform-origin"||computed_list||opacity||flex_alignment),"exact explicit inheritance hook coverage");
    }
    std::printf("transform-origin dispatch: %d checks, %d failures\n",checks,failures);
    const int corner_failures = run_corner_radius_registry_tests();
    const int opacity_failures = run_opacity_registry_tests();
    return (corner_failures || opacity_failures || failures) ? 1 : 0;
}
