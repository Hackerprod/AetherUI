// Internal compiler contract, with an intentionally adversarial private registry.
// Production builtin grammars are covered separately through the public ABI.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include "css_component_values.h"
#include <cstdio>
#include <cstdlib>

static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global() { return &registry; }
static size_t checks = 0;
static void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL compiler: %s\n", label); std::exit(1); }
}
static void parse(const std::string& css, std::vector<compiled_property_declaration_s>& properties,
                  std::vector<compiled_shorthand_declaration_s>& pending) {
    std::vector<custom_property_declaration_s> custom;
    check(style_source_compile_inline(css.c_str(), properties, custom, pending, nullptr, nullptr) == AETHER_OK,
          "invalid declarations do not fail the block");
}
int main() {
    for (const auto* name : {"top", "right", "bottom", "left"})
        registry.register_property(name, false, 0, "0",
            [](const char* source, std::string& parsed) {
                parsed = source;
                return parsed == "1" || parsed == "2" || parsed == "3" || parsed == "4";
            }, [](aether_computed_style_t*, const std::string&) {});
    const std::vector<std::string> names{"top", "right", "bottom", "left"};
    registry.register_shorthand("box", [names](const char* source, style_shorthand_writer_s& writer) {
        bool valid = false;
        auto values = css_split_components(source, &valid);
        if (!valid) return false;
        if (values.size() == 1) values.resize(4, values[0]);
        if (values.size() != 4) return false;
        for (size_t i = 0; i < names.size(); ++i) writer.set(names[i].c_str(), values[i].c_str());
        return true;
    }, names);
    for (const auto* name : {"partial", "duplicate", "unknown", "failure"}) {
        registry.register_shorthand(name, [name](const char*, style_shorthand_writer_s& writer) {
            writer.set("top", "1"); writer.set("right", "2"); writer.set("bottom", "3");
            if (std::string(name) == "duplicate") writer.set("top", "4");
            if (std::string(name) == "unknown") writer.set("other", "4");
            if (std::string(name) == "failure") { writer.set("left", "4"); return false; }
            return true;
        }, names);
    }
    registry.register_shorthand("bad-metadata", [](const char*, style_shorthand_writer_s&) { return true; }, {"top", "top"});
    registry.register_shorthand("missing-metadata", [](const char*, style_shorthand_writer_s&) { return true; }, {"other"});
    registry.seal();
    check(registry.try_get_shorthand("bad-metadata")->target_ids.empty(), "duplicate metadata invalidated at seal");
    check(registry.try_get_shorthand("missing-metadata")->target_ids.empty(), "unknown metadata invalidated at seal");
    std::vector<compiled_property_declaration_s> properties;
    std::vector<compiled_shorthand_declaration_s> pending;
    for (size_t invalid = 0; invalid < 4; ++invalid) {
        std::string value;
        for (size_t i = 0; i < 4; ++i) value += (i ? " " : "") + std::string(i == invalid ? "bogus" : "2");
        parse("box:1;box:" + value + " !important;left:4", properties, pending);
        check(properties.size() == 5 && pending.empty(), "invalid sibling publishes zero declarations");
        for (size_t i = 0; i < 4; ++i) {
            check(properties[i].property_id == static_cast<int>(i), "destination identity");
            check(properties[i].value.typed_value == "1" && !properties[i].important, "earlier candidate untouched");
            check(properties[i].declaration_order == 0, "one logical authored order");
        }
        check(properties.back().value.typed_value == "4" && properties.back().declaration_order == 2,
              "later declaration order retained despite failed group");
    }
    for (const auto* name : {"partial", "duplicate", "unknown", "failure"}) {
        parse("top:1;" + std::string(name) + ":2;left:4", properties, pending);
        check(properties.size() == 2 && properties[0].property_id == 0 && properties[1].property_id == 3,
              "malformed expander never partially publishes");
    }
    for (const auto* bad : {"var()", "var(color)", "var(--x, var())", "var(--x) !bogus", "var(--x) !important junk", "var(--x)]", "var(--x) {x}"}) {
        parse("box:1;box:" + std::string(bad), properties, pending);
        check(properties.size() == 4 && pending.empty(), "invalid pending rejected before cascade");
    }
    for (const auto* good : {"var(--missing)", "var(--x,)", "bogus var(--x)", "var(--x) !important"}) {
        parse("box:1;box:" + std::string(good), properties, pending);
        check(properties.size() == 4 && pending.size() == 1, "valid pending preserved separately");
        check(pending[0].declaration_order == 1, "pending order matches authored order");
    }
    parse("box:var(--x) !important;left:4", properties, pending);
    check(pending.size() == 1 && pending[0].important && pending[0].source == "var(--x)", "importance stripped once");
    check(properties.size() == 1 && properties[0].declaration_order == 1, "inline preserves pending and ordinary ordering");
    for (const auto& item : std::vector<std::pair<const char*, compiled_value_mode>>{
             {"initial", compiled_value_mode::initial}, {"inherit", compiled_value_mode::inherit},
             {"unset", compiled_value_mode::unset}, {"revert", compiled_value_mode::initial}}) {
        parse("box:" + std::string(item.first), properties, pending);
        check(properties.size() == 4, "global compiles whole group");
        for (const auto& property : properties) check(property.value.mode == item.second, "global modes coherent");
    }
    std::vector<custom_property_declaration_s> custom;
    style_source_compile_inline("--empty:;box:var(--empty, 2) 1", properties, custom, pending, nullptr, nullptr);
    check(custom.size() == 1 && custom[0].source.empty(), "empty custom property survives compilation");
    check(pending.size() == 1, "empty custom property does not drop pending group");
    std::printf("PASS %zu shorthand compiler checks\n", checks);
}
