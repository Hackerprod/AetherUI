#include "../src/style_runtime/custom_property_scope_internal.h"
#include "../src/style_runtime/css_variable_tokens.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
size_t checks = 0;
void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
using status = custom_property_status;
computed_custom_property_s root(const std::string& name, const custom_property_sources_s& sources,
    const computed_custom_property_scope_s* parent = nullptr, int depth = 32, int length = 65536) {
    css_variable_budget budget;
    return resolve_custom_property_root_with_budget(name, sources, parent, depth, length, budget);
}
std::string value(const std::string& source, const computed_custom_property_scope_s* scope,
    int depth = 32, int length = 65536) {
    std::string out = "stale";
    check(resolve_computed_value(source.c_str(), scope, depth, length, out), "consumer: " + source);
    return out;
}
void fails(const std::string& source, const computed_custom_property_scope_s* scope,
    int depth = 32, int length = 65536) {
    std::string out = "stale";
    check(!resolve_computed_value(source.c_str(), scope, depth, length, out) && out.empty(), "atomic failure: " + source);
}
void budgets() {
    custom_property_sources_s sources{{"--x", {"var(--y,2px)"}}, {"--y", {"1px"}}};
    auto parent = compute_custom_property_scope({{"--z", {"3px"}}}, {}, 32, 65536);
    for (const auto& name : {std::string("--x"), std::string("--y")}) {
        css_variable_budget measured;
        const size_t initial = measured.remaining;
        const auto expected = resolve_custom_property_root_with_budget(name, sources, parent.get(), 32, 65536, measured);
        check(expected.status == status::valid, "root budget baseline");
        const size_t cost = initial - measured.remaining;
        for (size_t available = 0; available <= cost; ++available) {
            css_variable_budget b; b.remaining = available;
            const auto actual = resolve_custom_property_root_with_budget(name, sources, parent.get(), 32, 65536, b);
            if (available == cost) check(actual.status == status::valid && actual.value == expected.value && !b.exhausted && b.remaining == 0, "exact root budget");
            else check(actual.status == status::work_exhausted && actual.value.empty() && b.exhausted, "every smaller root budget");
        }
    }
    auto scope = compute_custom_property_scope(sources, parent, 32, 65536);
    for (const auto& source : {"var(--x)", "var(--missing,var(--z))", "var(--x)var(--z)"}) {
        css_variable_budget measured;
        const size_t initial = measured.remaining;
        std::string expected;
        check(resolve_computed_value_with_budget(source, scope.get(), 32, 65536, measured, expected), "terminal budget baseline");
        const size_t cost = initial - measured.remaining;
        for (size_t available = 0; available <= cost; ++available) {
            css_variable_budget b; b.remaining = available;
            std::string out = "stale";
            const bool ok = resolve_computed_value_with_budget(source, scope.get(), 32, 65536, b, out);
            if (available == cost) check(ok && out == expected && !b.exhausted && b.remaining == 0, "exact terminal budget");
            else check(!ok && out.empty() && b.exhausted, "every smaller terminal budget");
        }
    }
}
void resources() {
    custom_property_sources_s tree{{"--a0", {""}}};
    for (int i = 1; i <= 20; ++i) {
        const auto previous = "var(--a" + std::to_string(i - 1) + ")";
        tree.emplace("--a" + std::to_string(i), custom_property_source_s{previous + previous});
    }
    check(root("--a3", tree).status == status::valid && root("--a3", tree).value.empty(), "small empty expansion");
    check(root("--a20", tree).status == status::work_exhausted, "empty exponential work bounded");
    auto exhausted = compute_custom_property_scope(tree, {}, 32, 65536);
    check(exhausted->local.at("--a20").status == status::work_exhausted, "stored hard failure");
    fails("var(--a20,outer)", exhausted.get());
    auto child = compute_custom_property_scope({{"--alias", {"var(--a20,outer)"}}}, exhausted, 32, 65536);
    check(child->local.at("--alias").status == status::work_exhausted, "inherited sticky failure");
    auto grandchild = compute_custom_property_scope({}, child, 32, 65536);
    fails("var(--alias,outer)", grandchild.get());
    auto recomputed = compute_custom_property_scope({{"--a20", {"fixed"}}}, exhausted, 32, 65536);
    check(value("var(--a20,outer)", recomputed.get()) == "fixed", "new local winner replaces failure");
    fails("var(--a20,outer)", exhausted.get());
    custom_property_sources_s independent;
    for (int i = 0; i < 5000; ++i) independent.emplace("--independent" + std::to_string(i), custom_property_source_s{std::string(32, 'x')});
    auto many = compute_custom_property_scope(independent, {}, 32, 65536);
    check(many->local.size() == independent.size(), "all independent roots published");
    for (const auto& entry : many->local) check(entry.second.status == status::valid && entry.second.value == std::string(32, 'x'), "no global starvation");
}
void limits() {
    custom_property_sources_s sources{{"--a", {"var(--b)"}}, {"--b", {"abc"}}};
    check(root("--b", sources, nullptr, 1).status == status::valid, "literal depth one");
    check(root("--b", sources, nullptr, 0).status == status::invalid, "literal depth zero");
    check(root("--a", sources, nullptr, 2).status == status::valid, "local exact depth");
    check(root("--a", sources, nullptr, 1).status == status::invalid, "local depth minus one");
    custom_property_sources_s recovery{{"--outer", {"var(--middle,ok)"}}, {"--middle", {"var(--leaf)"}}, {"--leaf", {"long"}}};
    check(root("--outer", recovery, nullptr, 3).value == "long", "root chain exact depth without fallback");
    check(root("--outer", recovery, nullptr, 2).value == "ok", "root depth failure can select local fallback");
    check(root("--outer", recovery, nullptr, 1).status == status::invalid, "root fallback cannot bypass depth");
    check(root("--outer", recovery, nullptr, 32, 2).value == "ok", "root length failure can select fallback");
    check(root("--outer", recovery, nullptr, 32, 1).status == status::invalid, "root fallback also obeys length");
    custom_property_sources_s empty{{"--empty", {"var(--missing,)"}}, {"--invalid", {"ignored", true}}};
    const auto valid_empty = root("--empty", empty);
    check(valid_empty.status == status::valid && valid_empty.value.empty(), "valid empty root distinguished from invalid");
    const auto invalid = root("--invalid", empty);
    check(invalid.status == status::invalid && invalid.value.empty(), "initial is explicit invalid");
    auto empty_scope = compute_custom_property_scope(empty, {}, 32, 65536);
    check(value("var(--empty,wrong)", empty_scope.get()).empty(), "empty terminal never selects fallback");
    check(value("var(--invalid,ok)", empty_scope.get()) == "ok", "invalid terminal selects fallback");
    custom_property_sources_s deep{{"--deep0", {"end"}}};
    for (int i = 1; i < 128; ++i)
        deep.emplace("--deep" + std::to_string(i), custom_property_source_s{"var(--deep" + std::to_string(i - 1) + ")"});
    check(root("--deep127", deep, nullptr, 128).value == "end", "frame reallocations retain borrowed source spans");
    check(root("--deep127", deep, nullptr, 127).status == status::invalid, "deep chain exact depth minus one");
    auto parent = compute_custom_property_scope(sources, {}, 32, 65536);
    check(root("--alias", {{"--alias", {"var(--a)"}}}, parent.get(), 2).status == status::valid, "inherited exact depth");
    check(root("--alias", {{"--alias", {"var(--a)"}}}, parent.get(), 1).status == status::invalid, "inherited insufficient depth");
    check(value("var(--a)", parent.get(), 1) == "abc", "consumer terminal single depth");
    fails("var(--a)", parent.get(), 0);
    check(value("var(--missing,var(--a))", parent.get(), 2) == "abc", "nested fallback exact depth");
    fails("var(--missing,var(--a))", parent.get(), 1);
    fails("var(--a)", parent.get(), -1);
    fails("var(--a)", parent.get(), 32, -1);
    for (int length : {2, 3, 4}) {
        const auto r = root("--b", sources, nullptr, 32, length);
        check(r.status == (length < 3 ? status::invalid : status::valid), "root length boundary");
        if (length < 3) fails("var(--a)", parent.get(), 32, length);
        else check(value("var(--a)", parent.get(), 32, length) == "abc", "terminal length boundary");
    }
    auto join = compute_custom_property_scope({{"--a", {"a"}}, {"--b", {"zz"}}}, {}, 32, 65536);
    check(value("var(--a)var(--b,%)", join.get(), 32, 2) == "a%", "failed join rolled back before fallback");
    fails("var(--a)var(--b)", join.get(), 32, 6);
    check(value("var(--a)var(--b)", join.get(), 32, 7) == "a/**/zz", "separator exact length");
    check(value("var(--a)var(--b)", join.get(), 32, 8) == "a/**/zz", "separator length plus one");
}
void generations_and_aliasing() {
    auto boundary = compute_custom_property_scope({{"--number", {"10"}}, {"--ident", {"calc"}}}, {}, 32, 65536);
    check(value("var(--number)px", boundary.get()) == "10/**/px", "terminal number cannot become dimension");
    check(value("var(--number)%", boundary.get()) == "10/**/%", "terminal number cannot become percentage");
    check(value("var(--ident)(10px)", boundary.get()) == "calc/**/(10px)", "terminal ident cannot become function");
    const std::vector<std::pair<std::string, std::string>> fragments = {
        {"10", "px"}, {"10", "%"}, {"calc", "(10px)"}, {"é", "界"}, {"\\61", "\n+"},
        {"\\61\r", "\n+"}, {"\\61\r\n", "\n+"}, {"a\\", "b"}, {"--", ">"}, {"<", "!--"},
        {"/*comment*/", "a"}, {"", "b"}, {"'var(--missing)'", "url(var--missing)"}
    };
    for (const auto& fragment : fragments) {
        auto scope = compute_custom_property_scope({{"--left", {fragment.first}}, {"--right", {fragment.second}}, {"--alias", {"var(--left)var(--right)"}}}, {}, 32, 65536);
        const auto expected = value("var(--alias)", scope.get());
        std::string previous = "--alias";
        for (int generation = 0; generation < 3; ++generation) {
            const auto next = "--copy" + std::to_string(generation);
            scope = compute_custom_property_scope({{"--left", {"overridden"}}, {next, {"var(" + previous + ")"}}}, scope, 32, 65536);
            check(value("var(--alias)", scope.get()) == expected, "stable inherited bytes across generations");
            check(value("var(" + next + ")", scope.get()) == expected, "terminal reinsertion stable across generations");
            previous = next;
        }
        std::string aliased = "var(--alias)";
        check(resolve_computed_value(aliased.c_str(), scope.get(), 32, 65536, aliased) && aliased == expected, "source and output alias");
    }
    auto mutable_scope = std::make_shared<computed_custom_property_scope_s>();
    mutable_scope->local.emplace("--value", computed_custom_property_s{status::valid, "abc"});
    auto& map_value = mutable_scope->local.at("--value").value;
    check(resolve_computed_value("var(--value)", mutable_scope.get(), 32, 65536, map_value) && map_value == "abc", "output aliases terminal map entry");
    map_value = "var(--missing,ok)";
    check(resolve_computed_value(map_value.c_str(), mutable_scope.get(), 32, 65536, map_value) && map_value == "ok", "source and output both map entry");
    for (const auto& sample : std::vector<std::pair<std::string, std::string>>{
            {"\\61", "\\61 "}, {"a\\", "a\xef\xbf\xbd"}}) {
        custom_property_sources_s escape{{"--escape", {sample.first}}};
        const int exact = static_cast<int>(sample.second.size());
        check(root("--escape", escape, nullptr, 32, exact - 1).status == status::invalid, "escape stabilization length minus one");
        check(root("--escape", escape, nullptr, 32, exact).value == sample.second, "escape stabilization exact bytes and length");
        check(root("--escape", escape, nullptr, 32, exact + 1).value == sample.second, "escape stabilization length plus one");
    }
    custom_property_sources_s names{{"--a)b", {"17px"}}, {"--é", {"19px"}}};
    for (const auto& entry : names) check(root(entry.first, names).value == entry.second.source, "decoded root name never reparsed");
}
void teardown() {
    auto leaf = compute_custom_property_scope({{"--base", {"old"}}}, {}, 32, 65536);
    computed_custom_property_scope_ptr retained;
    std::weak_ptr<const computed_custom_property_scope_s> bottom = leaf;
    for (int i = 0; i < 100000; ++i) {
        leaf = compute_custom_property_scope({}, leaf, 32, 65536);
        if (i == 49999) retained = compute_custom_property_scope({}, leaf, 32, 65536);
    }
    std::weak_ptr<const computed_custom_property_scope_s> top = leaf;
    leaf.reset();
    check(top.expired() && !bottom.expired(), "iterative teardown stops at retained sibling");
    check(value("var(--base)", retained.get()) == "old", "retained sibling owns ancestor bytes");
    retained.reset();
    check(bottom.expired(), "last sibling frees remaining chain iteratively");
}
}
int main() {
    try { budgets(); resources(); limits(); generations_and_aliasing(); teardown(); }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL after %zu checks: %s\n", checks, e.what()); return 1; }
    std::printf("computed custom property scope: %zu checks passed\n", checks);
    return 0;
}
