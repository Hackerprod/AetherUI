#include "../src/style_runtime/style_property_registry_internal.h"
#include <cstdio>
#include <cstring>
void builtin_properties_register(style_property_registry_s*);
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); } }
}
int main() {
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    const auto* transform = registry.try_get("transform");
    check(transform && transform->parse && transform->apply_checked && transform->parse_failure, "transform callbacks registered");
    if (!transform || !transform->parse || !transform->apply_checked || !transform->parse_failure) return 1;
    check(!transform->inherited && transform->impact == (STYLE_IMPACT_COMPOSITE | STYLE_IMPACT_HITTEST), "transform metadata unchanged");
    check(!transform->css_wide && !transform->inherit_explicit, "no new CSS-wide or inheritance hooks");
    for (const char* source : {"none", "/**/n\\6f ne/**/", "r\\6f tate(20deg)", "scale(2,3)", "translateX(c\\61 lc(1px))", "rotate(90d\\65 g)", "translateX(10p\\78)", "translate(calc(20% + 3px),-2em) rotate(0.0)"}) {
        for (int padding : {0, 1, 128, 4096}) {
            std::string value = std::string(padding, ' ') + source + std::string(padding, '\t');
            for (int round = 0; round < 3; ++round) {
                check(transform->parse(value.c_str(), value), "aliased parse success");
                check(value == source, "aliased output preserves trimmed spelling");
            }
            aether_computed_style_t style{};
            check(transform->apply_checked(&style, value).success, "validated value applies");
            aether_computed_style_free(&style);
        }
    }
    for (const char* source : {"", "scale(2 3)", "translateX(12)", "scale(0x1p2)", "rotate(1e38turn)", "rotate(nope)", "scale(2)bogus(3)", "scale(2)translateX(calc(0))", "translateZ(1px)", "none scale(2)", "var(--a)", "revert-layer", "translateX(c\\61 lc/**/(1px))","translateX(calc\\(1px\\))", "translateX(1\\65 2px)", "translateX(calc(1p\\78 + 2%))"}) {
        std::string value = source;
        check(!transform->parse(value.c_str(), value) && value == source, "failed aliased parse preserves output");
        std::string out = "sentinel";
        check(!transform->parse(source, out) && out == "sentinel", "failed separate parse preserves output");
        const auto error = transform->parse_failure(source);
        check(!error.success && error.diagnostic_code && std::strcmp(error.diagnostic_code, "AETHER-CSS-TRANSFORM") == 0, "dedicated pure diagnostic");
        aether_computed_style_t style{};
        check(transform->apply_checked(&style, "translate(1px,2%) scale(2)").success, "seed nonempty list");
        aether_transform_list saved{};
        check(aether_transform_list_copy(&style.transforms, &saved), "independent saved list");
        auto* original = style.transforms.items;
        const auto applied = transform->apply_checked(&style, source);
        check(!applied.success && applied.diagnostic_code && std::strcmp(applied.diagnostic_code, "AETHER-CSS-TRANSFORM") == 0, "checked apply rejects consistently");
        check(style.transforms.items == original && aether_transform_list_equal(&style.transforms, &saved), "invalid apply never mutates original list");
        check(transform->apply_checked(&style, "scale(3)").success && style.transforms.count == 1 && style.transforms.items[0].scalar_x == 3, "successful apply replaces list");
        check(saved.count == 2 && saved.items[1].scalar_x == 2, "snapshot unaffected by replacement");
        check(transform->apply_checked(&style, "/**/none/**/").success && !style.transforms.items && style.transforms.count == 0, "none clears owned list");
        aether_transform_list_free(&saved);
        aether_computed_style_free(&style);
    }
    std::string out = "sentinel";
    check(!transform->parse(nullptr, out) && out == "sentinel", "null source safe");
    std::printf("transform dispatch: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
