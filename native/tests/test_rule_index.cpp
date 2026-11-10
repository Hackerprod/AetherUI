// Compile the authentic opaque runtime locally; no certificate setters in the ABI.
#include "../src/style_runtime/style_runtime_core.cpp"
#include "../src/primitives/ascii_text.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <type_traits>

static long fail_after = -1;
static bool fault_delivered = false, count_allocations = false;
static size_t allocations = 0;
void* operator new(size_t n) {
    if (count_allocations) ++allocations;
    if (fail_after >= 0 && fail_after-- == 0) { fault_delivered = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#include "fault_inject_aligned.h"

namespace {
using Rules = std::vector<const compiled_rule_s*>;
static size_t checks = 0;
static uint64_t checksum = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL rule-index: %s\n", label); std::exit(1); }
}
static_assert(std::is_same_v<decltype(std::declval<rule_index_s&>().by_class()), const rule_index_s::bucket_map&>);
static_assert(std::is_same_v<decltype(std::declval<rule_index_s&>().universal()), const Rules&>);
static_assert(std::is_nothrow_move_constructible_v<rule_index_s>);
static_assert(std::is_nothrow_move_assignable_v<rule_index_s>);
static_assert(std::is_nothrow_swappable_v<rule_index_s>);

// Frozen copy of the pre-optimization query, with read-only bucket accessors.
void oracle(const rule_index_s& idx, const node_style_info_s& node, Rules& out) {
    std::unordered_set<int32_t> seen;
    const auto append = [&](const Rules& rules) {
        for (const auto* rule : rules) if (seen.insert(rule->rule_id).second) out.push_back(rule);
    };
    if (!node.id.empty()) {
        const auto it = idx.by_id().find(node.id);
        if (it != idx.by_id().end()) append(it->second);
    }
    for (const auto& name : node.classes) {
        const auto it = idx.by_class().find(name);
        if (it != idx.by_class().end()) append(it->second);
    }
    if (!node.type_name.empty()) {
        const auto it = idx.by_type().find(aether::ascii_lowercase(node.type_name));
        if (it != idx.by_type().end()) append(it->second);
    }
    append(idx.universal());
}
struct Fixture {
    aether_style_runtime_s runtime;
    node_style_info_s node;
    compiled_style_sheet_s& sheet() { return *runtime.sheets.at(1); }
    Fixture(size_t n, const std::string& mode) {
        auto owned = std::make_unique<compiled_style_sheet_s>();
        owned->handle = 1; owned->layer = style_sheet_layer::author; owned->rules.resize(n);
        node.id = "subject"; node.type_name = "DiV";
        for (size_t i = 0; i < n; ++i) {
            auto& rule = owned->rules[i]; rule.rule_id = static_cast<int32_t>(i + 1);
            rule.source_order = 5; // Shared source positions must never collapse IDs.
            if (mode == "id") rule.selector.anchor.id = "subject";
            else if (mode == "type") rule.selector.anchor.type_name = i % 2 ? "div" : "DIV";
            else if (mode == "class" || mode == "repeated") rule.selector.anchor.class_name = "item";
            else if (mode == "eight" || mode == "sixteen" || mode == "many")
                rule.selector.anchor.class_name = "c" + std::to_string(i % (mode == "eight" ? 8 : mode == "sixteen" ? 16 : 128));
            else if (mode == "misses") rule.selector.anchor.class_name = "not-present";
        }
        runtime.sheets.emplace(1, std::move(owned));
        if (mode == "class") node.classes = {"item"};
        else if (mode == "repeated") node.classes.assign(24, "item");
        else if (mode == "eight" || mode == "sixteen" || mode == "many")
            for (int i = 0; i < (mode == "eight" ? 8 : mode == "sixteen" ? 16 : 128); ++i)
                node.classes.push_back("c" + std::to_string(i));
        else if (mode == "misses") node.classes.assign(64, "absent");
        rebuild();
    }
    void rebuild() { runtime.index = aether_style_runtime_s::prepare_rule_index(&runtime); }
};
void equal(const rule_index_s& index, const node_style_info_s& node, const compiled_rule_s* prefix = nullptr) {
    Rules expected, actual;
    if (prefix) { expected.push_back(prefix); actual.push_back(prefix); }
    oracle(index, node, expected); rule_index_get_candidates(&index, &node, actual);
    check(actual == expected, "exact order, pointers and caller prefix");
    for (const auto* rule : actual) checksum = checksum * 131u + static_cast<uint32_t>(rule->rule_id);
}
void lifecycle() {
    rule_index_s empty;
    check(!empty.has_disjoint_runtime_rules(), "default conservative");
    Fixture f(16, "sixteen");
    check(f.runtime.index.has_disjoint_runtime_rules(), "owned unique build certified");
    check(!f.runtime.index.may_target_pseudo(aether_pseudo_element_t::before), "runtime pseudo certificate");
    rule_index_s copy(f.runtime.index), assigned; assigned = f.runtime.index;
    for (const auto* generic : {&copy, &assigned}) {
        check(!generic->has_disjoint_runtime_rules(), "generic copies cannot acquire certificate");
        check(generic->may_target_pseudo(aether_pseudo_element_t::before), "generic pseudo conservative");
        equal(*generic, f.node);
    }
    aether_style_runtime_s staged; staged.copy_index_for_preparation(f.runtime);
    check(staged.index.has_disjoint_runtime_rules(), "trusted staged copy retains certificate");
    check(!staged.index.may_target_pseudo(aether_pseudo_element_t::before), "trusted staged pseudo copy");
    auto moved = std::move(staged.index);
    check(moved.has_disjoint_runtime_rules() && !staged.index.has_disjoint_runtime_rules(), "move transfers certificate");
    check(staged.index.by_class().empty(), "moved source empty");
    staged.index = std::move(moved);
    check(staged.index.has_disjoint_runtime_rules() && !moved.has_disjoint_runtime_rules(), "move assignment transfers");
    swap(staged.index, assigned);
    check(assigned.has_disjoint_runtime_rules() && !staged.index.has_disjoint_runtime_rules(), "swap transfers with buckets");
    equal(assigned, f.node);
    rule_index_clear(&assigned);
    check(!assigned.has_disjoint_runtime_rules() && assigned.by_class().empty(), "clear invalidates");
    compiled_rule_s extra; extra.rule_id = 1; extra.selector.anchor.class_name = "c0";
    rule_index_add(&f.runtime.index, &extra);
    check(!f.runtime.index.has_disjoint_runtime_rules(), "add invalidates before borrow");
    equal(f.runtime.index, f.node);
    f.rebuild();
    rule_index_rebuild(&f.runtime.index, {&f.sheet()});
    check(!f.runtime.index.has_disjoint_runtime_rules(), "generic rebuild never certifies");
    f.rebuild();
    // Mutate only after leaving the runtime's certified index. Borrowed generic
    // indexes must observe current IDs, including duplicate, negative and zero.
    rule_index_clear(&f.runtime.index);
    f.sheet().rules[1].rule_id = f.sheet().rules[0].rule_id;
    f.sheet().rules[2].rule_id = 0; f.sheet().rules[3].rule_id = -1;
    equal(copy, f.node);
    rule_index_add(&copy, &f.sheet().rules[0]); equal(copy, f.node);
    f.rebuild(); check(!f.runtime.index.has_disjoint_runtime_rules(), "duplicate IDs not certified");
    equal(f.runtime.index, f.node);
    f.sheet().rules[1].rule_id = 2; f.rebuild();
    check(f.runtime.index.has_disjoint_runtime_rules(), "unique rebuild re-certifies");
    auto repeated = aether_style_runtime_s::prepare_rule_index(&f.runtime, &f.sheet());
    check(!repeated.has_disjoint_runtime_rules(), "repeated sheet insertion not certified");
    equal(repeated, f.node);
    f.runtime.index = f.runtime.index;
    check(!f.runtime.index.has_disjoint_runtime_rules(), "generic self-assignment conservative");
}
void functional() {
    for (const auto* mode : {"id", "type", "class", "universal", "repeated", "eight", "sixteen", "many", "misses"})
        for (size_t n : {0u, 1u, 8u, 9u, 16u, 128u, 512u}) {
            Fixture f(n, mode);
            const auto* prefix = n ? &f.sheet().rules.front() : nullptr;
            equal(f.runtime.index, f.node, prefix);
            const auto classes = f.node.classes;
            for (int repeat = 0; repeat < 3; ++repeat) f.node.classes.insert(f.node.classes.end(), classes.begin(), classes.end());
            std::reverse(f.node.classes.begin(), f.node.classes.end());
            equal(f.runtime.index, f.node, prefix); equal(f.runtime.index, f.node, prefix);
            f.node.classes = {"c8", "missing", "c7", "c8", "c0", "c7"}; equal(f.runtime.index, f.node, prefix);
        }
    std::mt19937 gen(731909u);
    const std::vector<std::string> names{"a", "A", "b", "c", "long-name-01234567890123456789", "", "x-\xc3\x84", "x-\xc3\xa4"};
    for (int pass = 0; pass < 20000; ++pass) {
        Fixture f(gen() % 129, "universal");
        rule_index_clear(&f.runtime.index);
        for (auto& rule : f.sheet().rules) {
            auto& anchor = rule.selector.anchor; const auto kind = gen() % 6;
            if (kind == 0 || kind == 4) anchor.id = names[gen() % names.size()];
            if (kind == 1 || kind == 4 || kind == 5) anchor.class_name = names[gen() % names.size()];
            if (kind == 2 || kind == 5) anchor.type_name = names[gen() % names.size()];
        }
        f.rebuild();
        check(f.runtime.index.has_disjoint_runtime_rules(), "generated owned build certified");
        f.node.id = names[gen() % names.size()]; f.node.type_name = names[gen() % names.size()];
        for (size_t n = gen() % 32; n; --n) f.node.classes.push_back(names[gen() % names.size()]);
        equal(f.runtime.index, f.node, f.sheet().rules.empty() ? nullptr : &f.sheet().rules.front());
        if (f.sheet().rules.size() > 1) {
            rule_index_s generic = f.runtime.index; rule_index_clear(&f.runtime.index);
            f.sheet().rules.back().rule_id = f.sheet().rules.front().rule_id;
            rule_index_add(&generic, &f.sheet().rules.front()); equal(generic, f.node);
        }
    }
}
template<class Action, class Failure> void sweep(Action action, Failure failed, const char* name) {
    size_t failures = 0; bool done = false;
    for (long point = 0; point < 4096; ++point) {
        fault_delivered = false; fail_after = point;
        try {
            action(); fail_after = -1;
            check(!fault_delivered, "successful operation cannot swallow OOM"); done = true; break;
        } catch (const std::bad_alloc&) {
            fail_after = -1; ++failures; check(fault_delivered, "injected OOM propagates"); failed();
        }
    }
    check(done && failures > 0, name);
    std::printf("sweep=%s failed_allocations=%zu\n", name, failures);
}
void faults_and_allocations() {
    for (size_t n : {1u, 8u, 9u, 16u, 128u}) {
        Fixture f(n, "many"); Rules expected; oracle(f.runtime.index, f.node, expected);
        Rules out;
        sweep([&] { Rules{}.swap(out); rule_index_get_candidates(&f.runtime.index, &f.node, out); }, [&] {
            check(out.size() <= expected.size() && std::equal(out.begin(), out.end(), expected.begin()), "OOM valid prefix");
            equal(f.runtime.index, f.node); check(f.runtime.index.has_disjoint_runtime_rules(), "query OOM not poison certificate");
        }, "query growth and overflow");
        check(out == expected, "final query exact");
        out.clear(); out.reserve(n); allocations = 0; count_allocations = true;
        rule_index_get_candidates(&f.runtime.index, &f.node, out);
        count_allocations = false;
        if (n <= 8) check(allocations == 0, "up to eight matching buckets need zero temporary allocations");
        else check(allocations > 0 && allocations < n, "overflow bookkeeping is per-bucket beyond eight");
    }
    Fixture f(16, "sixteen");
    const auto original = f.runtime.index.by_class();
    sweep([&] { auto next = aether_style_runtime_s::prepare_rule_index(&f.runtime); check(next.has_disjoint_runtime_rules(), "build certificate published"); }, [&] {
        check(f.runtime.index.by_class() == original && f.runtime.index.has_disjoint_runtime_rules(), "failed build preserves committed index");
    }, "build and certificate");
    Fixture destination(1, "id");
    const auto old_destination = destination.runtime.index.by_id();
    sweep([&] { destination.runtime.copy_index_for_preparation(f.runtime); }, [&] {
        check(destination.runtime.index.by_id() == old_destination && destination.runtime.index.has_disjoint_runtime_rules(), "failed trusted copy preserves old destination");
    }, "trusted copy");
    check(destination.runtime.index.has_disjoint_runtime_rules(), "trusted copy final certified");
    Fixture generic_destination(1, "id");
    const auto old_generic = generic_destination.runtime.index.by_id();
    sweep([&] { generic_destination.runtime.index = f.runtime.index; }, [&] {
        check(generic_destination.runtime.index.by_id() == old_generic && generic_destination.runtime.index.has_disjoint_runtime_rules(), "generic assignment strongly transactional");
    }, "generic copy");
    check(!generic_destination.runtime.index.has_disjoint_runtime_rules(), "generic successful copy loses certificate");
    compiled_rule_s extra; extra.rule_id = 999; extra.selector.anchor.class_name = "new-class-bucket";
    fault_delivered = false; fail_after = 0;
    try { rule_index_add(&f.runtime.index, &extra); fail_after = -1; check(false, "add allocation expected"); }
    catch (const std::bad_alloc&) { fail_after = -1; }
    check(fault_delivered && !f.runtime.index.has_disjoint_runtime_rules(), "failed add invalidates before allocation");
    f.rebuild(); const std::vector<const compiled_style_sheet_s*> sheets{&f.sheet()};
    fault_delivered = false; fail_after = 0;
    try { rule_index_rebuild(&f.runtime.index, sheets); fail_after = -1; check(false, "rebuild allocation expected"); }
    catch (const std::bad_alloc&) { fail_after = -1; }
    check(fault_delivered && !f.runtime.index.has_disjoint_runtime_rules() && f.runtime.index.by_class().empty(), "failed generic rebuild remains conservative");
    f.rebuild(); equal(f.runtime.index, f.node);
}
using Clock = std::chrono::steady_clock;
double time_query(const rule_index_s& index, const node_style_info_s& node, size_t n, size_t iterations, bool cold) {
    Rules out; out.reserve(n); const auto start = Clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        if (cold) Rules{}.swap(out); else out.clear();
        rule_index_get_candidates(&index, &node, out); checksum += out.size();
        if (!out.empty()) checksum += static_cast<uint32_t>(out.back()->rule_id);
    }
    return std::chrono::duration<double, std::nano>(Clock::now() - start).count() / iterations;
}
void benchmark() {
    std::printf("mode,rules,candidates,generic_allocations,certified_allocations,generic_ns,certified_ns,cold\n");
    for (const auto* mode : {"id", "type", "class", "universal", "repeated", "eight", "sixteen", "misses"})
        for (size_t n : {0u, 1u, 8u, 64u, 512u, 4096u}) {
            Fixture f(n, mode); rule_index_s generic(f.runtime.index); Rules out; out.reserve(n);
            allocations = 0; count_allocations = true; rule_index_get_candidates(&generic, &f.node, out); count_allocations = false;
            const auto old_allocations = allocations; const auto candidates = out.size(); out.clear();
            allocations = 0; count_allocations = true; rule_index_get_candidates(&f.runtime.index, &f.node, out); count_allocations = false;
            const auto new_allocations = allocations;
            for (bool cold : {false, true}) {
                std::array<double, 5> baseline{}, next{};
                const size_t iterations = std::max(size_t(100), size_t(100000) / std::max(size_t(1), n));
                for (int run = 0; run < 5; ++run) {
                    if (run % 2) { next[run] = time_query(f.runtime.index, f.node, n, iterations, cold); baseline[run] = time_query(generic, f.node, n, iterations, cold); }
                    else { baseline[run] = time_query(generic, f.node, n, iterations, cold); next[run] = time_query(f.runtime.index, f.node, n, iterations, cold); }
                }
                std::sort(baseline.begin(), baseline.end()); std::sort(next.begin(), next.end());
                std::printf("%s,%zu,%zu,%zu,%zu,%.2f,%.2f,%d\n", mode, n, candidates, old_allocations, new_allocations, baseline[2], next[2], int(cold));
            }
        }
    std::fprintf(stderr, "benchmark_checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
} // namespace
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0) benchmark();
    else { lifecycle(); functional(); faults_and_allocations(); std::printf("checks=%zu checksum=%llu\n", checks, static_cast<unsigned long long>(checksum)); }
}
