// Deliberately linked only with transition_properties.cpp, never with Core.
#include "transition_properties_internal.h"
#include "style_property_registry_internal.h"
#include "builtin_property_parse_utils.h"

#include <array>
#include <bit>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "transition module: %s\n", message);
        std::exit(1);
    }
}

constexpr std::array<const char*, 4> names = {
    "transition-property", "transition-duration", "transition-delay",
    "transition-timing-function"
};
constexpr std::array<const char*, 4> initials = {"none", "0s", "0s", "linear"};

void compose(style_property_registry_s& registry) {
    transition_properties_register_longhands(&registry);
    transition_properties_register_shorthands(&registry);
    require(!registry.sealed, "module must not seal the registry");
}

void metadata(const style_property_registry_s& registry, int offset) {
    for (int i = 0; i < 4; ++i) {
        const auto* property = registry.try_get(names[i]);
        require(property != nullptr, "longhand lookup after seal");
        require(property == &registry.definitions[offset + i], "name/order/ID lookup");
        require(property->id == offset + i, "stable ID");
        require(!property->inherited && property->impact == 2, "non-inherited PAINT only");
        require(property->initial_value == initials[i], "engine initial value");
        require(property->parse && property->apply, "ordinary callbacks");
        require(!property->apply_checked && !property->apply_with_context &&
                !property->apply_with_context_checked, "no checked/context callback");
    }
    const auto* shorthand = registry.try_get_shorthand("transition");
    require(shorthand && shorthand->expand, "shorthand callback");
}

// Compare the entire object representation against the same starting snapshot,
// modified at exactly one scalar field. No public default/free or engine needed.
void parse_apply(const style_property_registry_s& registry, int index, const char* input,
                 const char* expected_text, float expected_number, bool alias = false) {
    const auto& property = *registry.try_get(names[index]);
    std::string output = alias ? input : "unchanged";
    require(property.parse(alias ? output.c_str() : input, output), "accepted parse");
    require(output == expected_text, "normalized parse text");
    aether_computed_style_t style{};
    style.opacity = 0.625f;
    style.transition_properties = 91;
    style.transition_duration = 92;
    style.transition_delay = 93;
    style.transition_timing = 94;
    aether_computed_style_t expected;
    std::memcpy(&expected, &style, sizeof(style));
    if (index == 0) expected.transition_properties = static_cast<int>(expected_number);
    if (index == 1) expected.transition_duration = expected_number;
    if (index == 2) expected.transition_delay = expected_number;
    if (index == 3) expected.transition_timing = static_cast<int>(expected_number);
    property.apply(&style, output);
    require(std::memcmp(&style, &expected, sizeof(style)) == 0,
            "apply changes only its own scalar field");
}

void parsing(const style_property_registry_s& registry) {
    const std::pair<const char*, int> properties[] = {
        {"none", 0}, {"all", 3}, {"opacity", 1}, {"transform", 2},
        {" OPACITY ", 1}, {"", 0}, {" ", 0}, {"width", 0},
        {"opacity, transform", 0}, {"123", 0}, {"initial", 0}, {"var(--prop)", 0}
    };
    for (const auto& [input, number] : properties) {
        const auto text = std::to_string(number);
        for (bool alias : {false, true})
            parse_apply(registry, 0, input, text.c_str(), static_cast<float>(number), alias);
    }
    parse_apply(registry, 0, nullptr, "0", 0);
    const std::pair<const char*, float> times[] = {
        {"0", 0}, {"-0", 0}, {"+0", 0}, {"0s", 0}, {"0ms", 0},
        {"150ms", .15f}, {"0.3s", .3f}, {" 2 MS ", .002f},
        {"1e-3s", .001f}, {"+2s", 2}, {"1 s", 1}, {"0x1p2s", 4},
        {"1.23456789s", 1.234568f}
    };
    for (int index : {1, 2}) {
        for (const auto& [input, number] : times) {
            const auto text = std::to_string(number);
            for (bool alias : {false, true})
                parse_apply(registry, index, input, text.c_str(), number, alias);
        }
        const auto& property = *registry.try_get(names[index]);
        for (const char* input : {static_cast<const char*>(nullptr), "", " ", "-1s", "-1ms", "1", "s", "ms", "1px",
                                 "1ms junk", "1s,2s", "initial", "inherit", "unset", "revert",
                                 "revert-layer", "var(--time)", "calc(1s)", "-infs", "1\fs", "1\vs"}) {
            std::string output = "unchanged";
            require(!property.parse(input, output), "invalid time rejected");
            require(output == "unchanged", "invalid time does not overwrite output");
            // Never apply on failure, including on the deferred alias path.
            if (input) {
                output = input;
                require(!property.parse(output.c_str(), output), "aliased invalid time rejected");
                require(output == input, "aliased invalid time unchanged");
            }
        }
        for (const char* input : {"nans", "infs", "1e40s"}) {
            std::string value = input;
            require(property.parse(value.c_str(), value), "legacy nonfinite accepted");
            aether_computed_style_t style{};
            property.apply(&style, value);
            float number = index == 1 ? style.transition_duration : style.transition_delay;
            require(input[0] == 'n' ? std::isnan(number) : (std::isinf(number) && number > 0),
                    "nonfinite classification, not epsilon comparison");
        }
        std::string negative_zero = "-0s";
        require(property.parse(negative_zero.c_str(), negative_zero), "signed zero accepted");
        aether_computed_style_t style{};
        property.apply(&style, negative_zero);
        require(std::bit_cast<uint32_t>(index == 1 ? style.transition_duration : style.transition_delay)
                    == 0x80000000u, "unit-bearing negative zero preserved");
    }
    const char* timings[] = {"linear", "ease", "ease-in", "ease-out", "ease-in-out", " EASE "};
    for (int i = 0; i < 6; ++i) {
        int number = i == 5 ? 1 : i;
        const auto text = std::to_string(number);
        for (bool alias : {false, true}) parse_apply(registry, 3, timings[i], text.c_str(), number, alias);
    }
    for (const char* input : {static_cast<const char*>(nullptr), "", " ", "step-start", "steps(2)", "cubic-bezier(0,0,1,1)",
                             "linear, ease", "initial", "inherit", "unset", "revert", "var(--timing)"}) {
        std::string output = "unchanged";
        require(!registry.try_get(names[3])->parse(input, output), "invalid timing rejected");
        require(output == "unchanged", "invalid timing preserves output");
    }
}

void expansion(const style_property_registry_s& registry) {
    const auto& shorthand = *registry.try_get_shorthand("transition");
    struct Case { const char* input; std::array<const char*, 4> values; };
    const Case cases[] = {
        {"all 0.3s ease", {"all", "0.3s", "0s", "ease"}},
        {"opacity 150ms linear 50ms", {"opacity", "0.15s", "0.05s", "linear"}},
        {"0.3s", {"none", "0.3s", "0s", "linear"}},
        {"none", {"none", "0s", "0s", "linear"}},
        {"linear", {"none", "0s", "0s", "linear"}},
        {"all", {"all", "0s", "0s", "linear"}},
        {"ease 50ms transform 20ms", {"transform", "0.05s", "0.02s", "ease"}},
        {"ease all 50ms", {"all", "0.05s", "0s", "ease"}},
        {"50ms ease all", {"all", "0.05s", "0s", "ease"}},
        {"OPACITY 150MS EASE 50MS", {"OPACITY", "0.15s", "0.05s", "EASE"}},
        {"all 1.23456789s", {"all", "1.23457s", "0s", "linear"}}
    };
    for (const auto& test : cases) {
        style_shorthand_writer_s writer;
        writer.set("sentinel", "keep");
        require(shorthand.expand(test.input, writer), "valid shorthand");
        require(writer.items.size() == 5, "adds exactly four items");
        require(writer.items[0].name == "sentinel" && writer.items[0].value == "keep", "keeps prefix");
        aether_computed_style_t style{};
        for (int i = 0; i < 4; ++i) {
            const auto& item = writer.items[i + 1];
            require(item.name == names[i] && item.value == test.values[i], "expansion order and CSS tokens");
            const auto& property = *registry.try_get(item.name.c_str());
            std::string value = item.value;
            require(property.parse(value.c_str(), value), "shorthand CSS token round-trip");
            property.apply(&style, value);
        }
        if (std::string(test.input) == "all 0.3s ease")
            require(style.transition_properties == 3 && style.transition_duration == .3f &&
                    style.transition_delay == 0 && style.transition_timing == 1, "shorthand computed values");
    }
    for (const char* global : {"initial", "inherit", "unset", "revert", " InHeRiT "}) {
        style_shorthand_writer_s writer;
        require(shorthand.expand(global, writer) && writer.items.size() == 4, "global expansion");
        for (int i = 0; i < 4; ++i)
            require(writer.items[i].name == names[i] && writer.items[i].value == str_trim(global),
                    "global left for generic pipeline");
    }
    for (const char* invalid : {"", " ", "all -1s", "all 1s -1s", "all 1s 2s 3s",
                               "opacity transform 1s", "ease linear 1s", "width 1s",
                               "opacity 1s, transform 2s", "opacity,transform 1s", "revert-layer",
                               "var(--transition)", "all var(--time)", "all calc(1s + 2s)",
                               "all \"1 s\"", "all 1\fs", "all 1s junk"}) {
        style_shorthand_writer_s writer;
        writer.set("sentinel", "keep");
        require(!shorthand.expand(invalid, writer), "invalid shorthand rejected");
        require(writer.items.size() == 1 && writer.items[0].name == "sentinel" &&
                writer.items[0].value == "keep", "invalid expansion is atomic");
    }
}

void sentinel(style_property_registry_s& registry, const std::string& name) {
    registry.register_property(name.c_str(), false, 0, "sentinel",
        [](const char* source, std::string& out) { out = source; return true; },
        [](aether_computed_style_t*, const std::string&) {});
    registry.register_shorthand((name + "-short").c_str(),
        [](const char*, style_shorthand_writer_s&) { return true; });
}

void growth_and_lifetime() {
    style_property_registry_s registry;
    sentinel(registry, "before");
    compose(registry);
    const auto property_capacity = registry.definitions.capacity();
    const auto shorthand_capacity = registry.shorthand_defs.capacity();
    for (int i = 0; i < 256; ++i) sentinel(registry, "after-" + std::to_string(i));
    require(registry.definitions.capacity() > property_capacity &&
            registry.shorthand_defs.capacity() > shorthand_capacity, "both vectors actually grew");
    const auto properties = registry.definitions.size(), shorthands = registry.shorthand_defs.size();
    compose(registry); // Duplicates before seal must be harmless, even after reallocation.
    require(registry.definitions.size() == properties && registry.shorthand_defs.size() == shorthands,
            "unsealed repeat is a no-op");
    registry.seal(); // Exactly once, only after all vector growth.
    metadata(registry, 1);
    for (size_t i = 0; i < registry.definitions.size(); ++i)
        require(registry.try_get(registry.definitions[i].name.c_str()) == &registry.definitions[i] &&
                registry.definitions[i].id == static_cast<int>(i), "all name/ID mappings rebuilt");
    for (const auto& shorthand : registry.shorthand_defs)
        require(registry.try_get_shorthand(shorthand.name.c_str()) == &shorthand, "shorthand mapping rebuilt");
    parsing(registry);
    expansion(registry);
    transition_properties_register_longhands(&registry);
    transition_properties_register_shorthands(&registry);
    sentinel(registry, "sealed-reject");
    require(registry.definitions.size() == properties && registry.shorthand_defs.size() == shorthands,
            "sealed registration is a no-op");
    require(!registry.try_get("sealed-reject") && !registry.try_get_shorthand("sealed-reject-short"),
            "sealed registry remains unchanged");
    style_property_registry_s sealed_empty;
    sealed_empty.seal();
    transition_properties_register_longhands(&sealed_empty);
    transition_properties_register_shorthands(&sealed_empty);
    require(sealed_empty.definitions.empty() && sealed_empty.shorthand_defs.empty(), "sealed empty is a no-op");
}

void shared_helpers() {
    require(bi_split_ws(" a\t b\n c\r d ") == std::vector<std::string>({"a", "b", "c", "d"}), "four whitespace chars");
    require(bi_split_ws("a\fb") == std::vector<std::string>({"a", "b"}), "form feed is CSS whitespace");
    require(bi_split_ws("calc(1px + 2px) [a {b c}] \"a\\\" b\"") ==
            std::vector<std::string>({"calc(1px + 2px)", "[a {b c}]", "\"a\\\" b\""}),
            "nested delimiters and quoted escape");
    require(bi_split_ws("calc(1px + 2px) ) x").empty(), "unmatched close rejected");
    require(bi_split_ws("a(b [c d]) 5px") == std::vector<std::string>({"a(b [c d])", "5px"}), "combined depth");
    require(bi_split_ws("a\\ b") == std::vector<std::string>({"a\\ b"}), "escaped space remains in identifier");
    require(bi_split_ws("7px/**/11px") == std::vector<std::string>({"7px", "11px"}), "comment-separated components");
    require(bi_split_ws("7%11px") == std::vector<std::string>({"7%", "11px"}), "adjacent components");
    require(bi_is_global_keyword("InHeRiT") && !bi_is_global_keyword("revert-layer"), "global keyword scope");
}
} // namespace

int main() {
    require(std::setlocale(LC_ALL, "C") != nullptr, "C locale");
    // Independent registries remain usable after every registration function returns.
    style_property_registry_s first, second;
    compose(first);
    compose(second);
    require(first.definitions.size() == 4 && first.shorthand_defs.size() == 1, "empty registry counts");
    require(first.shorthand_defs[0].name == "transition", "empty shorthand order");
    first.seal();
    second.seal();
    metadata(first, 0);
    metadata(second, 0);
    parsing(first);
    expansion(first);
    parsing(second);
    expansion(second);
    growth_and_lifetime();
    shared_helpers();
    std::puts("transition property module passed");
}
