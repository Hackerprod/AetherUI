#include "../src/style_runtime/style_property_registry_internal.h"
#include "../src/css/style_freeze.h"
#include "../src/css/border_radii.h"
#include <cstdio>
#include <string>

void builtin_properties_register(style_property_registry_s*);
namespace {
int checks = 0, failures = 0;
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL corner registry: %s\n", label.c_str()); }
}
aether_css_length length(float value, aether_css_unit_t unit = AETHER_CSS_UNIT_PX) {
    aether_css_length result{}; result.value = value; result.unit = unit; return result;
}
bool equal(const aether_css_length& a, const aether_css_length& b) {
    return a.unit == b.unit && a.value == b.value && a.linear_percent == b.linear_percent &&
        a.linear_em == b.linear_em && a.linear_rem == b.linear_rem &&
        a.linear_vw == b.linear_vw && a.linear_vh == b.linear_vh;
}
bool equal(const aether_corner_radius& a, const aether_corner_radius& b) {
    return equal(a.horizontal, b.horizontal) && equal(a.vertical, b.vertical);
}
using Member = aether_corner_radius aether_border_radii::*;
const Member members[] = {&aether_border_radii::top_left, &aether_border_radii::top_right,
    &aether_border_radii::bottom_right, &aether_border_radii::bottom_left};
const char* names[] = {"border-top-left-radius", "border-top-right-radius",
    "border-bottom-right-radius", "border-bottom-left-radius"};
bool equal(const aether_border_radii& a, const aether_border_radii& b) {
    for (auto member : members) if (!equal(a.*member, b.*member)) return false;
    return true;
}
aether_border_radii seed(float base) {
    aether_border_radii result{};
    for (int i = 0; i < 4; ++i) {
        auto& corner = result.*members[i];
        // Nonzero inactive coefficients detect partial writes to any field.
        corner.horizontal = {base + 2*i, AETHER_CSS_UNIT_LINEAR, 2, 3, 4, 5, 6};
        corner.vertical = {base + 2*i + 1, AETHER_CSS_UNIT_LINEAR, 7, 8, 9, 10, 11};
    }
    return result;
}

void parse_case(const style_property_registry_s& registry, const char* name,
                const std::string& source, bool expected, const std::string& spelling = "") {
    const std::string label = std::string(name) + ": " + source;
    const auto* property = registry.try_get(name);
    check(property && property->parse, label + " parse callback exists");
    if (!property || !property->parse) return;
    std::string output = "length sentinel";
    const bool accepted = property->parse(source.c_str(), output);
    check(accepted == expected, label + " acceptance");
    check(output == (expected ? (spelling.empty() ? source : spelling) : "length sentinel"),
          label + " exact spelling or unchanged sentinel");
}

void shorthand_case(const style_property_registry_s& registry, const char* name,
                    const std::string& source, bool expected) {
    const std::string label = std::string(name) + ": " + source;
    const auto* shorthand = registry.try_get_shorthand(name);
    check(shorthand && shorthand->expand, label + " expand callback exists");
    if (!shorthand || !shorthand->expand) return;
    style_shorthand_writer_s writer;
    const bool expanded = shorthand->expand(source.c_str(), writer);
    bool accepted = expanded;
    if (expanded) {
        check(!writer.items.empty() && writer.items.size() == shorthand->target_names.size(),
              label + " complete expansion");
        // Check every emitted destination, even after one fails. Expansion alone
        // need not reject a negative padding/border-width token. This is not a
        // simulation of the compiler's transactional publication step.
        for (const auto& item : writer.items) {
            const auto* property = registry.try_get(item.name.c_str());
            check(property && property->parse, label + " destination " + item.name);
            if (!property || !property->parse) { accepted = false; continue; }
            std::string output = "destination sentinel";
            const bool valid = property->parse(item.value.c_str(), output);
            check(output == (valid ? item.value : "destination sentinel"),
                  label + " destination spelling/sentinel " + item.name);
            if (!valid) accepted = false;
        }
        for (const auto& target : shorthand->target_names) {
            size_t count = 0;
            for (const auto& item : writer.items) if (item.name == target) ++count;
            check(count == 1, label + " exactly one destination " + target);
        }
    }
    check(accepted == expected, label + " expansion plus all destination validation");
}

void length_sign_cases(const style_property_registry_s& registry) {
    // One sign triplet per consumer, not a property-by-unit Cartesian product.
    for (const char* name : {"width", "height", "min-width", "min-height", "max-width", "max-height",
            "flex-basis", "padding-top", "padding-right", "padding-bottom", "padding-left",
            "row-gap", "column-gap", "font-size", "line-height", "border-top-width",
            "border-right-width", "border-bottom-width", "border-left-width", "border-radius",
            "border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
            "border-bottom-left-radius"}) {
        parse_case(registry, name, "-1e-46px", false);
        parse_case(registry, name, "-0px", true);
        parse_case(registry, name, "1e-46px", true);
    }
    struct UnitCase { const char* property; const char* unit; };
    const UnitCase units[] = {{"width", "%"}, {"line-height", "%"},
        {"padding-top", "em"}, {"font-size", "rem"}, {"row-gap", "vw"}, {"border-top-width", "vh"}};
    for (const auto& item : units) {
        parse_case(registry, item.property, std::string("-1e-46") + item.unit, false);
        parse_case(registry, item.property, std::string("-0") + item.unit, true);
        parse_case(registry, item.property, std::string("1e-46") + item.unit, true);
    }
    for (const char* text : {"-0%", "1e-46%", "25%"})
        parse_case(registry, "border-top-width", text, false);
    for (const char* name : {"width", "line-height"}) {
        parse_case(registry, name, "-1e-46", false);
        parse_case(registry, name, "-0", true);
        parse_case(registry, name, "calc(2px - 5px)", true);
        for (const char* text : {"1e-46", "2", "calc(0)", "calc(2)", "calc(1 - 1)"})
            parse_case(registry, name, text, std::string(name) == "line-height");
    }
    for (const char* name : {"margin-top", "margin-right", "margin-bottom", "margin-left",
            "top", "right", "bottom", "left"}) {
        parse_case(registry, name, "-2px", true);
        parse_case(registry, name, "-1e-46px", true);
    }
    for (const char* name : {"box-shadow", "text-shadow"}) {
        parse_case(registry, name, "-2px -1e-46px 0px", true);
        parse_case(registry, name, "1px 2px -1e-46px", false);
        parse_case(registry, name, "1px 2px -0px", true);
        parse_case(registry, name, "1px 2px 1e-46px", true);
    }
    parse_case(registry, "box-shadow", "1px 2px 0px -2px", true);
    parse_case(registry, "box-shadow", "1px 2px 0px -1e-46px", true);
    parse_case(registry, "border-radius", "2px / 3px -1e-46px", false);
    parse_case(registry, "border-radius", "2px / 3px -0px", true);
    for (const char* name : {"padding", "border-width", "border", "border-top", "border-right",
            "border-bottom", "border-left", "gap", "flex"}) {
        const std::string prefix = std::string(name) == "flex" ? "1 1 " : "";
        shorthand_case(registry, name, prefix + "-1e-46px", false);
        shorthand_case(registry, name, prefix + "-0px", true);
        shorthand_case(registry, name, prefix + "1e-46px", true);
    }
    for (const char* name : {"padding", "border-width", "gap"}) {
        shorthand_case(registry, name, "2px -1e-46px", false);
        shorthand_case(registry, name, "2px -0px", true);
    }
    for (const char* name : {"margin", "inset"}) {
        shorthand_case(registry, name, "-2px -1e-46px", true);
    }
    for (const char* name : {"width", "line-height", "border-radius"}) {
        const auto* property = registry.try_get(name);
        check(property && property->parse, std::string(name) + " alias callback");
        if (!property || !property->parse) continue;
        for (const char* text : {" -0PX ", " 1e-46PX ", " -1e-46PX "}) {
            const bool expected = std::string(text) != " -1e-46PX ";
            const std::string spelling = std::string(text) == " -0PX " ? "-0PX" : "1e-46PX";
            parse_case(registry, name, text, expected, spelling);
            for (const char* prefix : {"", "prefix:"}) {
                std::string output = std::string(prefix) + text;
                for (int repeat = 0; repeat < 2; ++repeat) {
                    const std::string saved = output;
                    const size_t offset = repeat == 0 || !expected ? std::string(prefix).size() : 0;
                    const bool accepted = property->parse(output.c_str() + offset, output);
                    check(accepted == expected, std::string(name) + " alias acceptance");
                    check(output == (expected ? spelling : saved), std::string(name) + " alias exact spelling/sentinel");
                }
            }
        }
    }
    aether_css_length raw_length{};
    const bool raw_length_ok = aether_css_length_try_parse("-1e-46px", &raw_length) != AETHER_FALSE;
    check(raw_length_ok, "raw length remains permissive despite width rejection");
    if (raw_length_ok) check(equal(raw_length, length(0)), "raw tiny negative length quantizes to zero px");
    aether_border_radii raw{};
    const bool parsed = aether_border_radii_try_parse("-2px / -1e-46px", &raw);
    check(parsed, "raw border-radii remains permissive");
    if (parsed) {
        for (auto member : members)
            check(equal((raw.*member).horizontal, length(-2)) && equal((raw.*member).vertical, length(0)),
                  "raw negative radius preserved, tiny negative quantized");
    }
}

}

// Included in the existing transform-origin dispatcher executable so its private
// registry support is reused rather than compiling another private core copy.
int run_corner_radius_registry_tests() {
    style_property_registry_s registry;
    builtin_properties_register(&registry); registry.seal();
    length_sign_cases(registry);
    const auto* aggregate = registry.try_get("border-radius");
    const auto* after = registry.try_get("box-shadow");
    check(aggregate && after, "registration neighbors exist");
    if (!aggregate || !after) return 1;
    check(after->id == aggregate->id + 5, "four corners remain between aggregate and box-shadow");
    for (int index = 0; index < 4; ++index) {
        const std::string label = names[index];
        const auto* definition = registry.try_get(names[index]);
        check(definition != nullptr, label + " exists");
        if (!definition) continue;
        check(definition->name == names[index] && definition->id == aggregate->id + index + 1,
              label + " identity and physical registration order");
        check(!definition->inherited && definition->initial_value == "0" &&
              definition->impact == (STYLE_IMPACT_PAINT | STYLE_IMPACT_HITTEST), label + " metadata");
        check(definition->parse && definition->apply && definition->css_wide && definition->inherit_context,
              label + " existing callback routes");
        check(!definition->apply_checked && !definition->apply_with_context &&
              !definition->apply_with_context_checked && !definition->parse_failure && !definition->inherit_explicit,
              label + " scalar-only applicator contract");
        if (!definition->parse || !definition->apply || !definition->css_wide || !definition->inherit_context) continue;

        for (const char* text : {"10px", "10px 20px", "25% 50%", "7px/**/11px",
                "calc(10px + 2px) 20px", "calc(20px / 2) 20px", "10px -0PX", "10px 1e-46PX"}) {
            std::string output = "sentinel";
            check(definition->parse(text, output) && output == text, label + " separate parse " + text);
            for (int padding : {0, 1, 128, 4096}) {
                std::string alias = std::string(padding, ' ') + text + std::string(padding, '\t');
                check(definition->parse(alias.c_str(), alias) && alias == text, label + " same-string alias");
                check(definition->parse(alias.c_str(), alias) && alias == text, label + " repeat alias");
                const std::string prefix = "prefix-not-a-radius:";
                alias = prefix + std::string(padding, ' ') + text + std::string(padding, '\t');
                const char* interior = alias.c_str() + prefix.size();
                check(definition->parse(interior, alias) && alias == text, label + " interior-pointer alias");
            }
        }
        for (const char* text : {"", "  ", "10px / 20px", "10px 20px 30px", "10px 20px 30px 40px",
                "10px 20px)", "calc(1px + 2%) 20px]", "10px -1px", "10px -1e-46PX", "10px auto", "10px calc(0)",
                "initial 1px", "inherit 2px", "10px,20px", "10/**/px", "calc/**/(10px)"}) {
            std::string output = "sentinel";
            check(!definition->parse(text, output) && output == "sentinel", label + " separate failure " + text);
            std::string alias = text;
            check(!definition->parse(alias.c_str(), alias) && alias == text, label + " aliased failure");
            alias = std::string("prefix:") + text;
            const std::string saved_alias = alias;
            check(!definition->parse(alias.c_str() + 7, alias) && alias == saved_alias,
                  label + " interior aliased failure");
            aether_computed_style_t style{};
            style.border_radii = seed(31);
            const auto saved = style.border_radii;
            definition->apply(&style, text);
            check(equal(style.border_radii, saved), label + " invalid apply leaves all eight axes and coefficients");
        }
        std::string output = "sentinel";
        check(!definition->parse(nullptr, output) && output == "sentinel", label + " null leaves output unchanged");
        struct Valid { const char* text; aether_css_length horizontal, vertical; };
        const Valid valid[] = {
            {"10px", length(10), length(10)}, {"10px 20px", length(10), length(20)},
            {"25% 50%", length(25, AETHER_CSS_UNIT_PERCENT), length(50, AETHER_CSS_UNIT_PERCENT)},
            {"0 20px", length(0), length(20)}, {"2em 3rem", length(2, AETHER_CSS_UNIT_EM), length(3, AETHER_CSS_UNIT_REM)}
        };
        for (const auto& value : valid) {
            aether_computed_style_t style{};
            style.border_radii = seed(31);
            auto expected = style.border_radii;
            expected.*members[index] = {value.horizontal, value.vertical};
            definition->apply(&style, value.text);
            check(equal(style.border_radii, expected), label + " valid apply updates selected pair only " + value.text);
        }
        check(definition->css_wide("initial") == property_css_wide_e::initial, label + " initial classification");
        check(definition->css_wide("revert") == property_css_wide_e::initial, label + " native revert classification");
        check(definition->css_wide("unset") == property_css_wide_e::unset, label + " unset classification");
        check(definition->css_wide("/**/\\69 NHeRiT/**/") == property_css_wide_e::inherit, label + " decoded inherit");
        for (const char* text : {"initial 1px", "inherit 2px", "revert-layer", "inherit)", "in/**/herit"})
            check(definition->css_wide(text) == property_css_wide_e::none, label + " mixed CSS-wide rejected");

        aether_computed_style_t parent{}, child{};
        parent.border_radii = seed(101); parent.font_size = 40;
        child.border_radii = seed(31); child.font_size = 9;
        auto expected = child.border_radii;
        expected.*members[index] = parent.border_radii.*members[index];
        aether::style::FrozenStyleRef frozen;
        const auto status = aether::style::freeze_style(parent, frozen);
        check(status == AETHER_OK && bool(frozen), label + " frozen inheritance fixture");
        if (status == AETHER_OK && frozen) {
            // The inherited pair is scalar, but this contextual callback also
            // records MEASURE_PARENT through its mandatory measure destination.
            measure_state_s measures;
            measure_apply_state_s selected(measures, definition->name);
            property_apply_context_s context{}; context.style = &child; context.measures = &selected;
            definition->inherit_context(context, frozen->view(), nullptr);
            check(selected.property_dependencies() == MEASURE_PARENT, label + " inheritance dependency recorded");
            check(equal(child.border_radii, expected) && child.font_size == 9,
                  label + " inherit copies selected computed pair only, without contextual recomputation");
        }
    }
    std::printf("corner-radius registry: %d checks, %d failures (active with NDEBUG)\n", checks, failures);
    return failures ? 1 : 0;
}
