// Private compiler contract and transactional media ownership regression.
// All checks remain active in Release/NDEBUG. Successful graphs are freed once,
// by identity and shallow delete; abort/zero-rule allocations belong to compiler.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include "style_source_limits.h"
#include "media_condition_allocation_scope.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>

static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global() { return &registry; }
static int checks = 0;
static void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL source order compiler: %s\n", label); std::exit(1); }
}
static void gather(media_condition_s* node, std::set<media_condition_s*>& nodes) {
    std::vector<media_condition_s*> pending{node};
    while (!pending.empty()) {
        auto* current = pending.back();
        pending.pop_back();
        if (!current || current == media_condition_always() || current == media_condition_never() ||
            current == media_condition_unknown() || !nodes.insert(current).second) continue;
        pending.push_back(current->left);
        pending.push_back(current->right);
        pending.push_back(current->inner);
    }
}
struct rules_owner {
    std::vector<compiled_rule_s> rules;
    ~rules_owner() {
        std::set<media_condition_s*> nodes;
        for (const auto& rule : rules) gather(rule.condition, nodes);
        for (auto* node : nodes) delete node;
    }
};
static bool matches(media_condition_s* node, float width, float height = 600) {
    aether_style_environment_t env{width, height, 1, 16, 0, 0, 0};
    return media_condition_matches(node, &env);
}
static void diagnostic(const char* code, const char*, const char*, const char*, void* data) {
    if (code && std::strcmp(code, "AETHER-CSS-LIMIT") == 0) ++*static_cast<int*>(data);
}
static aether_status_t compile(const char* css, rules_owner& owner, int32_t& next,
                               int64_t handle = 701, int32_t layer = 1) {
    return style_source_compile(css, handle, layer, owner.rules, next, nullptr, nullptr);
}
static void expect_abort(const char* css, int32_t initial) {
    rules_owner owner;
    int32_t next = initial;
    int diagnostics = 0;
    check(style_source_compile(css, 701, 1, owner.rules, next, diagnostic, &diagnostics) ==
          AETHER_ERROR_INVALID_STATE, "capacity failure status");
    check(owner.rules.empty() && next == initial, "failure publishes no rule or counter");
    check(diagnostics > 0, "capacity failure has limit diagnostic");
}
static void metadata_and_append() {
    rules_owner owner;
    int32_t next = 41;
    check(compile(".a,.b{opacity:.4}@media(min-width:600px){.c{opacity:.6}}.d{opacity:.8}",
                  owner, next) == AETHER_OK, "compile metadata fixture");
    check(owner.rules.size() == 4 && next == 45, "literal count and next ID");
    const int ids[] = {41, 42, 43, 44};
    const int orders[] = {41, 41, 43, 44};
    for (size_t i = 0; i < 4; ++i) {
        check(owner.rules[i].rule_id == ids[i] && owner.rules[i].source_order == orders[i],
              "selector list shares position, not identity");
        check(owner.rules[i].sheet_handle == 701 && owner.rules[i].layer == style_sheet_layer::theme,
              "literal original sheet metadata");
    }
    auto* original_condition = owner.rules[2].condition;
    check(compile(".next{opacity:.9}", owner, next, 902, 2) == AETHER_OK, "append second sheet");
    check(owner.rules.size() == 5 && next == 46 && owner.rules[4].rule_id == 45 &&
          owner.rules[4].source_order == 45 && owner.rules[4].sheet_handle == 902 &&
          owner.rules[4].layer == style_sheet_layer::author, "append retains global identity across layers");
    check(owner.rules[2].condition == original_condition && owner.rules[0].rule_id == 41 &&
          owner.rules[1].source_order == 41, "append preserves original rules");
    check(!matches(original_condition, 500) && matches(original_condition, 800), "append preserves old graph");
}
static void priorities() {
    cascade_priority_t early, late;
    early.source_order = INT32_MAX - 2; early.decl_order = INT32_MAX - 1;
    late.source_order = INT32_MAX - 1; late.decl_order = 0;
    check(cascade_priority_compare(&early, &late) < 0, "source order precedes declaration order near limit");
    for (bool reversed : {false, true}) {
        cascade_entry_custom_s result;
        result.offer(reversed ? late : early, reversed ? "late" : "early");
        result.offer(reversed ? early : late, reversed ? "early" : "late");
        check(result.source == "late", "candidate permutation has literal winner");
    }
    auto higher = early;
    higher.important = true;
    check(cascade_priority_compare(&higher, &late) > 0, "important precedes source");
    late.inline_ = true;
    check(cascade_priority_compare(&higher, &late) > 0, "important precedes inline");
    late.inline_ = false; higher = early; higher.inline_ = true;
    late.layer = style_sheet_layer::author;
    check(cascade_priority_compare(&higher, &late) > 0, "inline precedes layer and source");
    higher = early; higher.layer = style_sheet_layer::author;
    late.layer = style_sheet_layer::theme; late.specificity = {1, 0, 0};
    check(cascade_priority_compare(&higher, &late) > 0, "engine layer precedes specificity");
    higher = early; late = early; late.source_order = INT32_MAX - 1;
    higher.specificity = {1, 0, 0}; late.specificity = {0, 100, 100};
    check(cascade_priority_compare(&higher, &late) > 0, "ID specificity precedes source");
    higher.specificity = {0, 1, 0}; late.specificity = {0, 0, 100};
    check(cascade_priority_compare(&higher, &late) > 0, "class specificity precedes source");
    higher.specificity = {0, 0, 1}; late.specificity = {};
    check(cascade_priority_compare(&higher, &late) > 0, "type specificity precedes source");
    higher = early; late = early; late.decl_order = INT32_MAX;
    check(cascade_priority_compare(&higher, &late) < 0, "declaration order breaks equal source tie");
    check(cascade_priority_compare(&early, &early) == 0, "identical priorities compare equal");
}
static void boundaries() {
    const auto max = static_cast<size_t>(INT32_MAX);
    check(style_source_size_fits(0) && style_source_size_fits(max) &&
          !style_source_size_fits(max + 1) && !style_source_size_fits(std::numeric_limits<size_t>::max()),
          "source size checked before narrowing without large allocation");
    check(style_rule_ids_fit(0, 1) && style_rule_ids_fit(1, 0) &&
          style_rule_ids_fit(INT32_MAX - 1, 1) && style_rule_ids_fit(INT32_MAX, 0), "ID capacity accepted boundaries");
    check(!style_rule_ids_fit(-1, 0) && !style_rule_ids_fit(INT32_MIN, 1) &&
          !style_rule_ids_fit(INT32_MAX - 1, 2) && !style_rule_ids_fit(INT32_MAX, 1) &&
          !style_rule_ids_fit(1, std::numeric_limits<size_t>::max()), "ID capacity rejects overflow and invalid counter");
    check(style_sheet_handle_available(1) && style_sheet_handle_available(INT64_MAX - 1) &&
          !style_sheet_handle_available(INT64_MAX) && !style_sheet_handle_available(-1), "handle guard boundaries");
    rules_owner owner;
    int32_t next = INT32_MAX - 1;
    check(compile(".a{opacity:.8}", owner, next) == AETHER_OK, "last available rule succeeds");
    check(next == INT32_MAX && owner.rules.size() == 1 && owner.rules[0].rule_id == INT32_MAX - 1 &&
          owner.rules[0].source_order == INT32_MAX - 1, "last identity reserves inline marker");
    check(compile(".b{opacity:.9}", owner, next) == AETHER_ERROR_INVALID_STATE &&
          next == INT32_MAX && owner.rules.size() == 1 && owner.rules[0].rule_id == INT32_MAX - 1,
          "exhausted append preserves prior output");
    for (const char* css : {"", "/**/", ".a{not-a-property:1}", "{broken}"})
        check(compile(css, owner, next) == AETHER_OK && next == INT32_MAX && owner.rules.size() == 1,
              "exhausted empty or recovered source consumes no ID");
    expect_abort("@media print{.a{opacity:.8}}", INT32_MAX);
    expect_abort(".a,.b{opacity:.8}", INT32_MAX - 1);
    expect_abort(".a{opacity:.4}.b{opacity:.8}", INT32_MAX - 1);
    expect_abort(".a{opacity:.4}@media(min-width:600px){@media(max-width:900px){.b{opacity:.8}}}", INT32_MAX - 1);
    expect_abort(".a{opacity:.8}", -1);
    expect_abort("", INT32_MIN);
    rules_owner zero;
    int32_t zero_next = 0;
    check(compile(".a{opacity:.8}", zero, zero_next) == AETHER_OK && zero_next == 1 &&
          zero.rules.size() == 1 && zero.rules[0].rule_id == 0 && zero.rules[0].source_order == 0,
          "private zero counter remains supported");
}
static void independent_arenas() {
    media_condition_allocation_scope_s outer;
    auto* survivor = media_condition_parse("(min-width:600px)", nullptr, nullptr, 0, &outer);
    check(outer.allocated.size() == 1, "outer arena registers its single feature");
    {
        media_condition_allocation_scope_s inner;
        auto* temporary = media_condition_parse("not (min-width:600px), (min-height:700px)",
                                                nullptr, nullptr, 0, &inner);
        check(inner.allocated.size() == 4, "NOT OR and both features registered once");
        check(matches(temporary, 500, 600) && !matches(temporary, 800, 600) &&
              matches(temporary, 800, 800), "non-folded NOT OR literal evaluation");
        check(outer.allocated.size() == 1, "independent arena never registers another arena node");
        for (const auto& node : inner.allocated)
            check(node.get() != survivor && node.get() != media_condition_always() &&
                  node.get() != media_condition_never() && node.get() != media_condition_unknown(),
                  "arena excludes constants and unrelated nodes");
    }
    check(!matches(survivor, 500) && matches(survivor, 800), "destroying one arena leaves other graph alive");
}
static void media_ownership() {
    const char* nested = "@media(min-width:600px){.a{opacity:.4}@media(max-width:900px){.b,.c{opacity:.8}}}";
    const char* folded = "@media(min-width:600px){.a{opacity:.4}@media print{.b{opacity:.8}}.c{opacity:.6}}";
    expect_abort(nested, INT32_MAX - 2);
    expect_abort(folded, INT32_MAX - 2);
    {
        rules_owner owner; int32_t next = 41;
        check(compile(nested, owner, next) == AETHER_OK && owner.rules.size() == 3 && next == 44,
              "nested shared graph commits");
        check(owner.rules[1].condition == owner.rules[2].condition, "selector list shares nested condition");
        for (float width : {500.f, 800.f, 1000.f}) {
            check(matches(owner.rules[0].condition, width) == (width != 500.f), "outer graph remains alive");
            check(matches(owner.rules[1].condition, width) == (width == 800.f) &&
                  matches(owner.rules[2].condition, width) == (width == 800.f), "nested graph literal evaluation");
        }
        auto* previous = owner.rules[0].condition;
        int32_t limited = INT32_MAX - 1;
        check(compile(nested, owner, limited) == AETHER_ERROR_INVALID_STATE && limited == INT32_MAX - 1 &&
              owner.rules.size() == 3 && owner.rules[0].condition == previous && owner.rules[0].rule_id == 41,
              "new abort never adopts prior graph or alters output");
        check(!matches(previous, 500) && matches(previous, 800), "prior graph survives abort");
    }
    {
        rules_owner owner; int32_t next = 1;
        check(compile(folded, owner, next) == AETHER_OK && owner.rules.size() == 3, "folded sibling graph commits");
        check(owner.rules[0].condition == owner.rules[2].condition &&
              owner.rules[1].condition == media_condition_never(), "folded never preserves shared outer operand");
        check(matches(owner.rules[0].condition, 800) && !matches(owner.rules[1].condition, 800) &&
              matches(owner.rules[2].condition, 800) && !matches(owner.rules[2].condition, 500), "folded siblings evaluate literally");
    }
    // Hundreds of isolated cycles make ASan/LSan catch unreachable allocations,
    // enum discard double deletes, and nested rollback UAFs without a runtime.
    for (int cycle = 0; cycle < 200; ++cycle) {
        expect_abort("@media all, (min-width:600px){.a,.b{opacity:.8}}", INT32_MAX - 1);
        expect_abort("@media print and (min-width:600px){.a,.b{opacity:.8}}", INT32_MAX - 1);
        expect_abort(nested, INT32_MAX - 2);
        expect_abort(folded, INT32_MAX - 2);
        expect_abort("@media not (min-width:600px), (min-height:700px){.a{opacity:.4}.b{opacity:.8}}", INT32_MAX - 1);
        for (const char* css : {"@media all, (min-width:600px){}",
                               "@media all, (min-width:600px){.a{not-a-property:1}}",
                               "@media(min-width:600px){@media(max-width:900px){}}"}) {
            rules_owner owner; int32_t next = INT32_MAX;
            check(compile(css, owner, next) == AETHER_OK && owner.rules.empty() && next == INT32_MAX,
                  "zero-rule commit destroys orphan nodes");
        }
        for (const char* feature : {"prefers-color-scheme", "pointer", "prefers-reduced-motion"}) {
            const std::string prefix = std::string("@media (") + feature + ":bogus)";
            const std::string abort = prefix + "{.a{opacity:.4}}.b{opacity:.8}";
            expect_abort(abort.c_str(), INT32_MAX - 1);
            for (const std::string& css : {prefix + "{}", std::string("@media all, (") + feature + ":bogus){}"}) {
                rules_owner owner; int32_t next = 1;
                check(compile(css.c_str(), owner, next) == AETHER_OK && owner.rules.empty() && next == 1,
                      "invalid enum zero-rule success destroys discarded allocation");
            }
            rules_owner owner; int32_t next = 1;
            const std::string css = std::string("@media all, (") + feature + ":bogus){.a{opacity:.8}}";
            check(compile(css.c_str(), owner, next) == AETHER_OK && owner.rules.size() == 1 &&
                  owner.rules[0].condition == media_condition_always(), "invalid enum folded list commits always");
        }
        rules_owner owner; int32_t next = 1;
        check(compile("@media all, (min-width:600px){.a{opacity:.8}}", owner, next) == AETHER_OK &&
              owner.rules.size() == 1 && owner.rules[0].condition == media_condition_always(),
              "success releases only reachable nodes after OR folding");
    }
}
int main() {
    registry.register_property("opacity", false, 0, "1",
        [](const char* source, std::string& value) { value = source; return true; },
        [](aether_computed_style_t*, const std::string&) {});
    registry.seal();
    metadata_and_append(); priorities(); boundaries(); independent_arenas(); media_ownership();
    std::printf("stylesheet source order compiler: %d checks passed\n", checks);
    return 0;
}
