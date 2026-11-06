#include "style_runtime_types.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

// Contextual pseudo-element rejection and recovery. Checks stay active under NDEBUG.
namespace {
int checks = 0, failures = 0;
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", label.c_str()); }
}
bool has_argument_invariant(const compiled_selector_s& selector, bool in_has = false) {
    if (in_has && selector.pseudo_element != aether_pseudo_element_t::none) return false;
    for (const auto& step : selector.steps) for (const auto& pseudo : step.compound.pseudos)
        if (pseudo.nested) for (const auto& nested : *pseudo.nested)
            if (!has_argument_invariant(nested, in_has || pseudo.kind == PSEUDO_HAS)) return false;
    return true;
}
bool parse(const std::string& source, compiled_selector_s& out) {
    out = compiled_selector_s{};
    const bool ok = selector_parse(source.c_str(), out);
    check(ok, "accept " + source);
    if (ok) {
        check(out.source == source, "source bytes " + source);
        check(has_argument_invariant(out), "no pseudo-element in HAS argument " + source);
    }
    return ok;
}
void same(const compiled_selector_s& a, const compiled_selector_s& b, const std::string& path) {
#define FIELD(x) check(a.x == b.x, path + "." #x)
    FIELD(specificity); FIELD(dependencies); FIELD(pseudo_element);
    FIELD(anchor.id); FIELD(anchor.class_name); FIELD(anchor.type_name);
    FIELD(steps.size());
    for (size_t i = 0; i < std::min(a.steps.size(), b.steps.size()); ++i) {
        const auto& x = a.steps[i]; const auto& y = b.steps[i];
        const auto p = path + ".steps[" + std::to_string(i) + "]";
        check(x.relation_to_next == y.relation_to_next, p + ".combinator");
        const auto& c = x.compound; const auto& d = y.compound;
        check(c.type_name == d.type_name, p + ".type");
        check(c.ids == d.ids, p + ".ids");
        check(c.classes == d.classes, p + ".classes/order");
        check(c.attributes.size() == d.attributes.size(), p + ".attributes.size");
        for (size_t j = 0; j < std::min(c.attributes.size(), d.attributes.size()); ++j) {
            const auto& u = c.attributes[j]; const auto& v = d.attributes[j];
            const auto q = p + ".attributes[" + std::to_string(j) + "]";
            check(u.name == v.name, q + ".name"); check(u.op == v.op, q + ".op");
            check(u.value == v.value, q + ".value"); check(u.has_value == v.has_value, q + ".has_value");
            check(u.case_mode == v.case_mode && u.html_default_ascii_insensitive == v.html_default_ascii_insensitive, q + ".case_policy");
        }
        check(c.pseudos.size() == d.pseudos.size(), p + ".pseudos.size");
        for (size_t j = 0; j < std::min(c.pseudos.size(), d.pseudos.size()); ++j) {
            const auto& u = c.pseudos[j]; const auto& v = d.pseudos[j];
            const auto q = p + ".pseudos[" + std::to_string(j) + "]";
            check(u.kind == v.kind, q + ".kind"); check(u.state == v.state, q + ".state");
            check(u.nth.step == v.nth.step, q + ".nth.step");
            check(u.nth.offset == v.nth.offset, q + ".nth.offset");
            check(bool(u.nested) == bool(v.nested), q + ".nested presence");
            if (u.nested && v.nested) {
                check(u.nested->size() == v.nested->size(), q + ".nested.size");
                for (size_t k = 0; k < std::min(u.nested->size(), v.nested->size()); ++k)
                    same((*u.nested)[k], (*v.nested)[k], q + ".nested[" + std::to_string(k) + "]");
            }
        }
    }
#undef FIELD
}
void equivalent(const std::string& canonical, const std::string& variant) {
    compiled_selector_s a, b;
    const bool ok_a = parse(canonical, a), ok_b = parse(variant, b);
    if (ok_a && ok_b) same(a, b, variant);
}
void invalid(const std::string& source) {
    compiled_selector_s out;
    check(!selector_parse(source.c_str(), out), "reject " + source);
}
void metadata(const std::string& source, specificity_s spec, unsigned deps) {
    compiled_selector_s s;
    if (parse(source,s)) {
        check(s.specificity == spec, source + " specificity");
        check(s.dependencies == deps, source + " dependencies");
    }
}
std::string put(const std::string& pattern, const std::string& leaf) {
    const auto at = pattern.find('@');
    return pattern.substr(0, at) + leaf + pattern.substr(at + 1);
}
void syntax_and_strict_boundaries() {
    const std::vector<std::string> has_names = {"has", "HAS", R"(\68 as)", R"(\000048AS)"};
    const std::vector<std::string> pseudos = {"::before", "::after", "::BeFoRe",
        "::AFTER", R"(::b\65 fore)", R"(::\000061fter)", ":/**/:/**/before"};
    const std::vector<std::string> prefixes = {"", "*", "span", "#bad", ".bad", "[data-x]",
        ":hover", ":first-child", "#bad[data-x]:hover:first-child", ".parent > .leaf",
        ".pre + .leaf", ".pre ~ .leaf", ".ready/**/"};
    for (const auto& name : has_names) for (const auto& pseudo : pseudos) for (const auto& prefix : prefixes) {
        const auto branch = prefix + pseudo;
        const auto subject = ".card:" + name;
        invalid(subject + "(" + branch + ")");
        for (const std::string position : {"@,.ready,.other", ".ready,@,.other", ".ready,.other,@"})
            invalid(subject + "(" + put(position, branch) + ")");
        for (const std::string logical : {"is", "where"}) {
            const auto expected = ".card:has(:" + logical + "(.ready,.other))";
            for (const std::string position : {"@,.ready,.other", ".ready,@,.other", ".ready,.other,@"})
                equivalent(expected, subject + "(:" + logical + "(" + put(position, branch) + "))");
            equivalent(".card:has(:" + logical + "())", subject + "(:" + logical + "(" + branch + "))");
        }
    }
    for (const auto& pseudo : pseudos) {
        for (const std::string pattern : {".card:has(:not(@))", ".card:has(:not(.ready,@))",
                ".card:not(:has(@))", ".card:has(:has(.ready)@)", ".card:has(:has(@))"})
            invalid(put(pattern, pseudo));
        for (const auto& pair : std::vector<std::pair<std::string, std::string>>{
                {".card:has(:not(:is(@)))", ".card:has(:not(:is()))"},
                {".card:has(:not(:where(@)))", ".card:has(:not(:where()))"},
                {".card:has(:is(:not(@),.ready))", ".card:has(:is(.ready))"},
                {".card:has(:is(@),.ready)", ".card:has(:is(),.ready)"},
                {".card:is(:has(@),.safe)", ".card:is(.safe)"},
                {".card:where(:has(@),.safe)", ".card:where(.safe)"},
                {".card:is(:has(@))", ".card:is()"},
                {".card:is(:has(@),::after)", ".card:is(::after)"},
                {".card:has(:is(:has(@),.ready))", ".card:has(:is(.ready))"},
                {".card:has(:is(#never:is(@),.ready))", ".card:has(:is(#never:is(),.ready))"},
                {".card:has(:where(#never:is(@),.ready))", ".card:has(:where(#never:is(),.ready))"},
                {".card:has(:where(#never:hover[data-x]:first-child:is(@),.ready))",
                 ".card:has(:where(#never:hover[data-x]:first-child:is(),.ready))"},
                {".card:has(:has(:is(@)))", ".card:has(:has(:is()))"},
                {".card:has(:is(:has(.ready)@,.safe))", ".card:has(:is(.safe))"},
                {".card:has(:is(:has(@))):is(::after)", ".card:has(:is()):is(::after)"}})
            equivalent(pair.second, put(pair.first, pseudo));
    }
    equivalent(":has(:is())", ":has(:is(:has(::before),::after))");
    equivalent(":has(:where())", ":has(:where(:has(::before),::after))");
}
void metadata_and_protected_data() {
    const unsigned cls = SELECTOR_DEP_SELF_CLASS, id = SELECTOR_DEP_SELF_ID, desc = SELECTOR_DEP_DESCENDANT;
    metadata(".card:has(:is(#never::before,.ready))", {0,2,0}, cls | desc);
    metadata(".card:has(:where(#never::before,.ready))", {0,1,0}, cls | desc);
    metadata(".card:is(:has(#never:hover::before),.safe)", {0,2,0}, cls);
    metadata(".card:where(:has(#never:hover::before),.safe)", {0,1,0}, cls);
    metadata(".card:has(:is(#never:hover::before))", {0,1,0}, cls | desc);
    metadata(".card:has(:is(#never:is(::before),.ready))", {1,1,0}, cls | id | desc);
    metadata(".card:has(:where(#never:is(::before),.ready))", {0,1,0}, cls | id | desc);
    metadata(".card:has(:where(#never:hover[data-x]:first-child:is(::before),.ready))", {0,1,0},
             cls | id | desc | SELECTOR_DEP_SELF_STATE | SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_STRUCTURE);
    for (const std::string source : {R"(.card:has([data-x="::before"]))", R"(.card:has(.literal\:\:before))",
            R"css(.card:has([data-x="a,::before)"]))css", ".card:has(.ready/* ::before , ) */)"}) {
        compiled_selector_s s; parse(source, s);
    }
    for (const std::string logical : {"is", "where"}) {
        for (const std::string data : {R"css([data-x="a,::before)"])css", R"(.literal\:\:before)",
                R"(.comma\,before)", ".ready/* ::after , ) */"})
            equivalent(".card:has(:" + logical + "(" + data + "))",
                       ".card:has(:" + logical + "(::before," + data + ",::after))");
    }
}
void context_isolation_and_transactions() {
    for (const auto& pair : std::vector<std::pair<std::string,aether_pseudo_element_t>>{
            {"::before", aether_pseudo_element_t::before}, {"::after", aether_pseudo_element_t::after}}) {
        for (int repeat = 0; repeat < 8; ++repeat) {
            invalid(".card:has(" + pair.first + ")");
            compiled_selector_s outer;
            if (parse(".card:has(.ready)" + pair.first, outer))
                check(outer.pseudo_element == pair.second, "pseudo belongs to outer subject");
            for (const std::string logical : {"is", "where", "not"}) {
                compiled_selector_s sibling, standalone;
                parse(".card:has(.ready):" + logical + "(" + pair.first + ")", sibling);
                if (parse(".card:" + logical + "(" + pair.first + ")", standalone)) {
                    const auto& p = standalone.steps[0].compound.pseudos[0];
                    check(p.nested && p.nested->size() == 1, "outside HAS retains pseudo branch");
                    if (p.nested && p.nested->size() == 1)
                        check(p.nested->front().pseudo_element == pair.second, "outside HAS retains discriminator");
                }
            }
        }
    }
    compiled_selector_s nested;
    parse(".card:has(.mid:has(.ready))", nested); // Existing AetherUI extension, not Chromium parity.
    compiled_selector_s seed, copy, safe, outer;
    const std::string sentinel = "#sentinel:hover > .anchor[data-live]:nth-child(2n+1)";
    if (!parse(sentinel, seed) || !parse(sentinel, copy)) return;
    std::vector<compiled_selector_s> list;
    list.push_back(std::move(seed));
    for (const char* input : {".card:has(::before)", ".card:has(::after),.card:not(:has(::before))", "", ",,"}) {
        check(!selector_parse_list(input, list), "invalid list does not append");
        check(list.size() == 1, "prefix size preserved");
        if (!list.empty()) { same(list[0], copy, "unchanged prefix"); check(list[0].source == sentinel, "prefix source preserved"); }
    }
    check(!selector_parse_list(nullptr, list), "null list does not append");
    check(selector_parse_list(".card:has(::before),.safe,.card:has(.ready)::after,.card:has(::after)", list),
          "top-level list still forgiving");
    check(list.size() == 3, "only valid branches published");
    if (list.size() == 3 && parse(".safe", safe) && parse(".card:has(.ready)::after", outer)) {
        same(list[0], copy, "prefix after success"); same(list[1], safe, "first survivor"); same(list[2], outer, "second survivor");
        check(list[1].source.empty() && list[2].source.empty(), "list branch source contract");
    }
    for (int depth : {1,4,16,32}) {
        std::string rejected = "::before", recovered = ":is(::after)", expected = ":is()";
        for (int n = 0; n < depth; ++n) {
            rejected = ":has(" + rejected + ")";
            const std::string fn = n % 4 == 0 ? "has" : n % 4 == 1 ? "is" : n % 4 == 2 ? "where" : "not";
            recovered = ":" + fn + "(" + recovered + ")"; expected = ":" + fn + "(" + expected + ")";
        }
        for (int repeat = 0; repeat < 5; ++repeat) {
            invalid(rejected); equivalent(expected, recovered);
            compiled_selector_s s; parse(".card:has(.ready)::after", s);
        }
    }
    // Only independent parser outputs are shared across threads, not runtime state.
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < 4; ++worker) threads.emplace_back([&] {
        for (int n = 0; n < 100; ++n) {
            compiled_selector_s invalid_output, external, recovered;
            const bool rejected = !selector_parse(".card:has(:not(::before))", invalid_output);
            const bool outside = selector_parse(".card:has(.ready)::after", external);
            const bool inside = selector_parse(".card:has(:is(::before,.ready))", recovered);
            if (!rejected || !outside || external.pseudo_element != aether_pseudo_element_t::after ||
                !inside || !has_argument_invariant(recovered) || recovered.specificity != specificity_s{0,2,0})
                ++errors;
        }
    });
    for (auto& thread : threads) thread.join();
    check(errors == 0, "400 parallel parser-only context-isolation cycles");
}
} // namespace
int main() {
    syntax_and_strict_boundaries(); metadata_and_protected_data(); context_isolation_and_transactions();
    std::printf("has pseudo-element parser: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
