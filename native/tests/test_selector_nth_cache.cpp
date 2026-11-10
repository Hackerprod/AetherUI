// Scoped nth position cache: independent structural oracle, recursive differential,
// activation gates and allocator fault sweep. Checks intentionally survive NDEBUG.
#include "style_runtime_types.h"
#include "selector_match_context.h"
#include "../src/dom/dom_types.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <type_traits>
#include <vector>

namespace {
bool track = false;
size_t allocations = 0, fail_at = std::numeric_limits<size_t>::max();
size_t live_bytes = 0, peak_bytes = 0, total_bytes = 0;
struct alignas(std::max_align_t) AllocationHeader { size_t bytes; bool counted; };
}
void* operator new(size_t n) {
    if (track && allocations++ == fail_at) throw std::bad_alloc();
    if (n > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader)) throw std::bad_alloc();
    auto* h = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + (n ? n : 1)));
    if (!h) throw std::bad_alloc();
    h->bytes = n; h->counted = track;
    if (track) { live_bytes += n; total_bytes += n; peak_bytes = std::max(peak_bytes, live_bytes); }
    return h + 1;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = static_cast<AllocationHeader*>(p) - 1;
    if (h->counted) live_bytes -= h->bytes;
    std::free(h);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

struct selector_match_context_test_access {
    static inline const selector_match_context_s::parents_s empty;
    static const auto& parents(const selector_match_context_s& c) { return c.parents_ ? *c.parents_ : empty; }
};
namespace {
size_t checks = 0, failures = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value && ++failures <= 30) std::fprintf(stderr, "FAIL: %s\n", label);
}
bool eligible(const aether_node_s* n) {
    return n && n->type == AETHER_NODE_ELEMENT && !n->is_generated && !n->anonymous_flex_owner;
}
size_t oracle(aether_node_s* n) {
    if (!eligible(n)) return 0;
    auto* p = n->parent;
    if (!p) return 1; // Existing disconnected-element semantics, not absence.
    if (p->is_generated || p->anonymous_flex_owner) return 0;
    size_t ordinal = 0;
    for (auto* child : p->children) {
        if (!eligible(child)) continue;
        ++ordinal;
        if (child == n) return ordinal;
    }
    return 0;
}
struct Fixture {
    aether_node_s parent;
    std::vector<std::unique_ptr<aether_node_s>> nodes;
    explicit Fixture(size_t n) {
        parent.type = AETHER_NODE_ELEMENT; parent.tag_name = "section";
        for (size_t i = 0; i < n; ++i) add();
    }
    aether_node_s* add() {
        auto n = std::make_unique<aether_node_s>();
        n->type = AETHER_NODE_ELEMENT; n->tag_name = "div"; n->parent = &parent;
        auto* raw = n.get(); nodes.push_back(std::move(n)); parent.children.push_back(raw);
        return raw;
    }
};
const auto& parents(const selector_match_context_s& c) {
    return selector_match_context_test_access::parents(c);
}
void audit(const selector_match_context_s& c) {
    for (const auto& [p, entry] : parents(c)) {
        for (const auto& [n, pos] : entry.positions) check(pos == oracle(n), "every retained memo exact");
        if (entry.complete) for (auto* n : p->children) if (eligible(n)) {
            auto found = entry.positions.find(n);
            check(found != entry.positions.end() && found->second == oracle(n), "complete means every eligible sibling");
        }
    }
}
void numeric() {
    static_assert(!std::is_copy_constructible_v<selector_match_context_s>);
    static_assert(!std::is_move_constructible_v<selector_match_context_s>);
    const size_t max = std::numeric_limits<size_t>::max();
    check(aether::selector_position::next_ordinal(0) == 1, "ordinal begins at one");
    check(aether::selector_position::next_ordinal(max - 1) == max, "last representable ordinal");
    bool threw = false;
    try { (void)aether::selector_position::next_ordinal(max); } catch (const std::length_error&) { threw = true; }
    check(threw, "ordinal overflow throws instead of wrapping");
    size_t budget = 123;
    check(aether::selector_position::work_budget(max / 16, budget) && budget == (max / 16) * 16, "budget last representable product");
    budget = 123;
    check(!aether::selector_position::work_budget(max / 16 + 1, budget) && budget == 123, "budget overflow refuses without changing output");
    const int coefficients[] = {std::numeric_limits<int>::min(), -2147483647, -17, -1, 0, 1, 2, 17, std::numeric_limits<int>::max()};
    for (int a : coefficients) for (int b : coefficients) {
        nth_expression_s e{a,b};
        check(!e.matches_position(0), "zero always absent");
        for (int p = 1; p <= 513; ++p) {
            const int64_t d = int64_t(p) - b;
            const bool expected = a == 0 ? p == b : d % int64_t(a) == 0 && d / int64_t(a) >= 0;
            check(e.matches_position(size_t(p)) == expected, "int32 coefficients independent signed-wide oracle");
        }
    }
    // At huge positions use quotient/remainder, never an overflowing p + |b|.
    const size_t points[] = {size_t(std::numeric_limits<int>::max()), size_t(std::numeric_limits<int>::max()) + 1, max - 1, max};
    for (size_t p : points) for (int a : coefficients) for (int b : coefficients) {
        bool expected = false;
        if (a == 0) expected = b > 0 && p == size_t(b);
        else if (a < 0) {
            if (b > 0 && p <= size_t(b)) expected = (uint64_t(b) - p) % uint64_t(-int64_t(a)) == 0;
        } else if (b >= 0) {
            expected = p >= size_t(b) && (p - size_t(b)) % size_t(a) == 0;
        } else {
            const uint64_t r = uint64_t(-int64_t(b)) % uint64_t(a);
            expected = p % size_t(a) == (r == 0 ? 0 : uint64_t(a) - r);
        }
        check(nth_expression_s{a,b}.matches_position(p) == expected, "huge ordinal modular oracle");
    }
}
void activation() {
    for (size_t n : {size_t(0), size_t(1), size_t(31), size_t(32), size_t(33), size_t(34), size_t(127), size_t(128), size_t(129)}) {
        Fixture f(n); selector_match_context_s c;
        allocations = 0; track = true;
        for (size_t i = 0; i < std::min(n, size_t(128)); ++i) check(c.position(f.nodes[i].get()) == i + 1, "cheap position");
        track = false;
        check(allocations == 0 && parents(c).empty(), "small and cheap scans allocate nothing");
    }
    Fixture large(65536);
    for (int repeats : {1,2,3,4,8}) {
        selector_match_context_s c; allocations = 0; track = true;
        for (int i = 0; i < repeats; ++i) check(c.position(large.nodes[32].get()) == 33, "child33 huge parent");
        track = false;
        check(allocations == 0 && parents(c).empty(), "child33 never activates");
        for (int i = 0; i < repeats; ++i) check(c.position(large.nodes.back().get()) == 65536, "repeated tail");
        const auto& e = parents(c).at(&large.parent);
        check(!e.complete && e.positions.size() == 1 && e.remaining == 15 * 65536, "tail memo hits do not pay budget or complete");
    }
    selector_match_context_s sparse;
    for (size_t i = 0; i < 32; ++i) {
        check(sparse.position(large.nodes[65535-i].get()) == 65536-i, "sparse late position");
        const auto& e = parents(sparse).at(&large.parent);
        check(!e.complete && e.positions.size() == i+1, "coverage prevents sparse bulk");
    }
    check(parents(sparse).at(&large.parent).remaining == 0, "paid budget saturates");
    Fixture prefix(4097);
    for (size_t i = 0; i < 4096; ++i) prefix.nodes[i]->type = AETHER_NODE_TEXT;
    selector_match_context_s pc;
    check(pc.position(prefix.nodes.back().get()) == 1, "text prefix contributes work but no ordinal");
    const auto& pe = parents(pc).at(&prefix.parent);
    check(pe.positions.size() == 1 && pe.positions.bucket_count() < 128 && !pe.complete, "no raw-size reserve");
    std::vector<std::unique_ptr<Fixture>> many;
    for (size_t i = 0; i < 128; ++i) many.push_back(std::make_unique<Fixture>(256));
    selector_match_context_s mc; allocations = 0; track = true;
    for (auto& f : many) for (int i = 0; i < 2; ++i) check(mc.position(f->nodes[32].get()) == 33, "many cheap parents");
    track = false;
    check(allocations == 0 && parents(mc).empty(), "all cheap parent metadata omitted");
    for (auto& f : many) for (auto& n : f->nodes) check(mc.position(n.get()) == oracle(n.get()), "dense sweep exact");
    check(parents(mc).size() == 128, "all participating parents stored");
    for (const auto& [p,e] : parents(mc)) check(e.complete && e.positions.size() == 256, "dense parents complete");
    audit(mc);
}
std::vector<compiled_selector_s> selectors() {
    const char* sources[] = {
        "*", "div", ":nth-child(odd)", ":nth-child(even)", ":nth-child(0)", ":nth-child(-n+17)", ":nth-child(3n-7)",
        ":nth-child(2147483647n-2147483648)", ":nth-child(-2147483648n+2147483647)",
        ":is(:nth-child(odd),:nth-child(2))", ":where(:not(:nth-child(even)))", ":not(:is(:nth-child(3n),:nth-child(7)))",
        "section > :nth-child(odd)", "section :nth-child(even)", ":nth-child(odd) + :nth-child(even)", ":nth-child(3n) ~ :nth-child(odd)",
        ":is(section > :nth-child(odd))", ":where(section :nth-child(even))", ":not(:nth-child(odd) + :nth-child(even))",
        ":is(:nth-child(3n) ~ :nth-child(odd))", "section:has(:nth-child(even))", "section:has(div > :nth-child(odd))",
        "section:has(:nth-child(odd) + :nth-child(even))", "section:has(:nth-child(3n) ~ :nth-child(odd))",
        "section:has(div > :is(:nth-child(odd)))", "section:has(div :where(:not(:nth-child(even))))",
        ":is(section:has(:nth-child(129)), :nth-child(2))", "div:nth-child(odd)::before"
    };
    std::vector<compiled_selector_s> result;
    for (auto* source : sources) {
        compiled_selector_s s; bool ok = selector_parse(source, s); check(ok, source);
        if (ok) result.push_back(std::move(s));
    }
    return result;
}
void differential() {
    auto ss = selectors(); std::mt19937 rng(0x4e544843);
    for (int fixture = 0; fixture < 60; ++fixture) {
        Fixture f(180 + rng()%100); Fixture nested(5);
        nested.parent.parent = f.nodes[130].get(); f.nodes[130]->children.push_back(&nested.parent);
        for (auto& n : f.nodes) switch (rng()%11) {
            case 0: n->type = AETHER_NODE_TEXT; break;
            case 1: n->type = AETHER_NODE_COMMENT; break;
            case 2: n->is_generated = true; n->style_origin = &f.parent; n->pseudo_element = aether_pseudo_element_t::before; break;
            case 3: n->anonymous_flex_owner = &f.parent; break;
            default: break;
        }
        for (int scope = 0; scope < 4; ++scope) {
            {
            selector_match_context_s c;
            for (auto& s : ss) {
                check(selector_match(s, &f.parent,c) == selector_match(s,&f.parent), "parent recursive differential");
                for (auto& n : f.nodes) check(selector_match(s,n.get(),c) == selector_match(s,n.get()), "mixed recursive differential");
                for (auto& n : nested.nodes) check(selector_match(s,n.get(),c) == selector_match(s,n.get()), "deep recursive differential");
            }
            for (auto& n : f.nodes) check(c.position(n.get()) == oracle(n.get()), "mixed structural oracle");
            audit(c);
            }
            // Mutation occurs after destruction, never inside a cache scope.
            std::shuffle(f.parent.children.begin(), f.parent.children.end(), rng);
            f.parent.type = scope % 2 ? AETHER_NODE_ELEMENT : AETHER_NODE_DOCUMENT;
        }
    }
    // Prove context reaches each recursive path: equality alone would miss a
    // forgotten context argument that silently uses the uncached oracle.
    Fixture f(256); Fixture deep(256);
    deep.parent.parent = &f.parent; f.parent.children.push_back(&deep.parent);
    const char* paths[] = {":is(:nth-child(256))", ":where(:nth-child(256))", ":not(:nth-child(255))", "section > :nth-child(256)", "section :nth-child(256)", ":nth-child(255) + div", ":nth-child(129) ~ div"};
    for (auto* path : paths) {
        compiled_selector_s s; check(selector_parse(path,s), "parse propagation path"); selector_match_context_s c;
        check(selector_match(s,f.nodes.back().get(),c), "propagation expected match");
        check(!parents(c).empty(), "context reaches recursive/combinator route");
    }
    for (auto* path : {"section:has(:nth-child(256))", "section:has(section > :nth-child(256))", "section:has(:nth-child(255) + :nth-child(256))", "section:has(:nth-child(129) ~ :nth-child(256))"}) {
        compiled_selector_s s; check(selector_parse(path,s), "parse has propagation"); selector_match_context_s c;
        check(selector_match(s,&f.parent,c), "has expected match"); check(!parents(c).empty(), "has context propagated");
    }
    // nth occurs only on the ancestor: a context dropped by either ancestor
    // combinator would still match correctly but leave its expensive memo absent.
    aether_node_s leaf;
    leaf.type = AETHER_NODE_ELEMENT; leaf.tag_name = "i";
    leaf.parent = deep.nodes.front().get(); leaf.parent->children.push_back(&leaf);
    struct AncestorPath { const char* source; aether_node_s* subject; };
    const AncestorPath ancestor_paths[] = {
        {"section:nth-child(257) > div", deep.nodes.front().get()},
        {"section:nth-child(257) i", &leaf}
    };
    for (const auto& path : ancestor_paths) {
        compiled_selector_s s;
        check(selector_parse(path.source, s), "parse ancestor-only nth propagation");
        selector_match_context_s context;
        check(selector_match(s, path.subject), "uncached ancestor-only nth matches");
        check(selector_match(s, path.subject, context), "cached ancestor-only nth matches");
        const auto parent = parents(context).find(&f.parent);
        check(parent != parents(context).end(), "ancestor combinator forwards context to ancestor parent");
        if (parent != parents(context).end()) {
            const auto memo = parent->second.positions.find(&deep.parent);
            check(memo != parent->second.positions.end() && memo->second == 257,
                  "ancestor combinator memoizes exact ancestor identity and ordinal");
            check(parent->second.positions.size() == 1,
                  "ancestor-only nth memo contains no subject position");
        }
    }
    leaf.parent->children.pop_back();
    compiled_selector_s plain; check(selector_parse("div",plain), "parse no nth");
    selector_match_context_s c; allocations = 0; track = true;
    for (auto& n : f.nodes) check(selector_match(plain,n.get(),c), "no nth match");
    track = false; check(allocations == 0 && parents(c).empty(), "no nth zero allocations");
}
void membership() {
    Fixture f(256); aether_node_s missing; missing.type = AETHER_NODE_ELEMENT; missing.parent = &f.parent;
    {
    selector_match_context_s c;
    check(c.position(nullptr) == 0, "null absent");
    check(c.position(&missing) == 0 && parents(c).empty(), "missing creates no metadata");
    check(c.position(f.nodes.back().get()) == 256, "partial seed");
    check(c.position(&missing) == 0, "partial miss scans and stays absent");
    check(c.position(f.nodes[199].get()) == 200, "partial miss not false absence");
    for (auto& n : f.nodes) (void)c.position(n.get());
    check(parents(c).at(&f.parent).complete, "membership map complete");
    check(c.position(&missing) == 0, "complete miss absent"); audit(c);
    }
    f.parent.children.insert(f.parent.children.begin()+5, f.nodes[0].get());
    {
    selector_match_context_s duplicate;
    for (auto& n : f.nodes) check(duplicate.position(n.get()) == oracle(n.get()), "duplicate first occurrence preserved");
    check(parents(duplicate).at(&f.parent).complete, "duplicates complete"); audit(duplicate);
    missing.parent = nullptr; check(duplicate.position(&missing) == 1, "detached element existing semantics");
    compiled_selector_s s; check(selector_parse(":nth-child(256)",s), "parse origin");
    aether_node_s pseudo; pseudo.type = AETHER_NODE_ELEMENT; pseudo.is_generated = true;
    pseudo.pseudo_element = aether_pseudo_element_t::before; pseudo.style_origin = f.nodes[254].get();
    check(selector_match(s,&pseudo,duplicate), "generated subject normalizes to host current position");
    check(duplicate.position(&pseudo) == 0, "generated traversal candidate never normalizes");
    pseudo.style_origin = &pseudo; check(!selector_match(s,&pseudo,duplicate), "self origin rejected");
    pseudo.style_origin = nullptr; check(!selector_match(s,&pseudo,duplicate), "null origin rejected");
    pseudo.style_origin = f.nodes[254].get(); pseudo.anonymous_flex_owner = &f.parent;
    check(!selector_match(s,&pseudo,duplicate), "anonymous subject rejected");
    }
    for (int kind = 0; kind < 2; ++kind) {
        f.parent.is_generated = kind == 0; f.parent.anonymous_flex_owner = kind == 1 ? &missing : nullptr;
        selector_match_context_s boundary;
        check(boundary.position(f.nodes.back().get()) == 0, "generated/anonymous parent boundary");
    }
    // Placement-new reproduces exact pointer identity reuse between fresh scopes.
    alignas(aether_node_s) unsigned char storage[sizeof(aether_node_s)];
    f.parent.is_generated = false; f.parent.anonymous_flex_owner = nullptr;
    auto* reused = new (storage) aether_node_s; reused->type = AETHER_NODE_ELEMENT;
    reused->parent = &f.parent; f.parent.children.push_back(reused);
    { selector_match_context_s fresh;
      check(fresh.position(reused) == 258, "first lifetime expensive cached ordinal");
      check(parents(fresh).at(&f.parent).positions.count(reused) == 1, "first lifetime pointer cached"); }
    f.parent.children.pop_back(); reused->~aether_node_s();
    reused = new (storage) aether_node_s;
    reused->type = AETHER_NODE_ELEMENT; reused->parent = &f.parent;
    f.parent.children.insert(f.parent.children.begin(), reused);
    { selector_match_context_s fresh; check(fresh.position(reused) == 1, "fresh scope same pointer new ordinal"); }
    f.parent.children.erase(f.parent.children.begin()); reused->~aether_node_s();
    Fixture other(160); auto* moved = f.nodes.back().get();
    f.parent.children.erase(std::find(f.parent.children.begin(),f.parent.children.end(),moved));
    moved->parent = &other.parent; other.parent.children.push_back(moved);
    { selector_match_context_s fresh; check(fresh.position(moved) == 161, "fresh scope reparent"); }
}
struct Memory { size_t calls, total, peak, retained; };
template<class Work> Memory measure(const char* name, Work work) {
    check(live_bytes == 0, "memory interval begins with no counted allocations");
    allocations = 0; total_bytes = 0; peak_bytes = 0; size_t retained = 0;
    track = true;
    { selector_match_context_s c; work(c); retained = live_bytes; }
    track = false;
    check(live_bytes == 0, "scope releases all counted requested bytes");
    Memory m{allocations,total_bytes,peak_bytes,retained};
    std::printf("memory %s: calls=%zu total=%zu peak=%zu retained=%zu bytes\n", name,m.calls,m.total,m.peak,m.retained);
    return m;
}
void memory_gates() {
    // Includes parent metadata, memo maps and coexisting temporary complete maps.
    // Requested allocator bytes exclude headers, allocator overhead and RSS.
    Fixture big(65536), prefix(4097);
    for (size_t i = 0; i < 4096; ++i) prefix.nodes[i]->type = AETHER_NODE_TEXT;
    auto cheap = measure("child33x8/65536", [&](auto& c) {
        for (int i = 0; i < 8; ++i) check(c.position(big.nodes[32].get()) == 33, "memory cheap result");
    });
    check(cheap.calls == 0 && cheap.total == 0 && cheap.peak == 0, "cheap zero requested bytes");
    auto tail = measure("tailx1/65536", [&](auto& c) { check(c.position(big.nodes.back().get()) == 65536, "memory tail result"); });
    auto repeated = measure("tailx8/65536", [&](auto& c) {
        for (int i = 0; i < 8; ++i) check(c.position(big.nodes.back().get()) == 65536, "memory repeated result");
    });
    check(tail.calls == repeated.calls && tail.total == repeated.total && tail.peak == repeated.peak, "repeats add no allocation work");
    auto text = measure("4096texts+1element", [&](auto& c) { check(c.position(prefix.nodes.back().get()) == 1, "memory text result"); });
    check(text.calls == tail.calls && text.total == tail.total && text.peak == tail.peak, "raw text prefix does not amplify allocation");
    auto sparse = measure("32late/65536", [&](auto& c) {
        for (size_t i = 0; i < 32; ++i) check(c.position(big.nodes[65535-i].get()) == 65536-i, "memory sparse result");
        check(parents(c).at(&big.parent).positions.size() == 32, "memory sparse only queried entries");
    });
    check(sparse.peak < tail.peak * 64, "sparse storage stays proportional to queried positions");
    std::vector<std::unique_ptr<Fixture>> many;
    for (int i = 0; i < 128; ++i) many.push_back(std::make_unique<Fixture>(256));
    auto dense = measure("128parentsx256full", [&](auto& c) {
        for (auto& f : many) for (auto& n : f->nodes) check(c.position(n.get()) == oracle(n.get()), "memory dense result");
        for (const auto& [p,e] : parents(c)) check(e.complete, "memory dense complete");
    });
    check(dense.peak >= dense.retained && dense.retained > 32768 * sizeof(size_t), "all-parent memory includes positions and metadata");
}
void synthetic_traversal() {
    Fixture f(0), hidden(160);
    hidden.parent.is_generated = true;
    hidden.parent.pseudo_element = aether_pseudo_element_t::before;
    hidden.parent.style_origin = &f.parent;
    hidden.parent.parent = &f.parent;
    f.parent.children.push_back(&hidden.parent);
    for (int kind = 0; kind < 2; ++kind) {
        hidden.parent.is_generated = kind == 0;
        hidden.parent.anonymous_flex_owner = kind == 1 ? &f.parent : nullptr;
        selector_match_context_s c;
        for (auto* text : {"section:has(:nth-child(n))", "section:has(div:nth-child(160))", "section:has(section div:nth-child(160))"}) {
            compiled_selector_s s; check(selector_parse(text,s), "parse synthetic traversal");
            check(!selector_match(s,&f.parent,c) && !selector_match(s,&f.parent), "has prunes whole synthetic subtree");
        }
        check(parents(c).empty(), "pruned subtree creates no metadata");
    }
    hidden.parent.is_generated = false; hidden.parent.anonymous_flex_owner = nullptr;
    { selector_match_context_s c;
      compiled_selector_s s; check(selector_parse("section:has(div:nth-child(160))",s), "parse visible descendant");
      check(selector_match(s,&f.parent,c), "fresh scope sees restored DOM subtree");
      check(!parents(c).empty(), "visible descendant nth uses shared scope"); }
}
void oom() {
    Fixture f(256); auto original = f.parent.children;
    compiled_selector_s all; check(selector_parse(":nth-child(n)", all), "parse fault matcher");
    size_t total = 0;
    { selector_match_context_s c; allocations = 0; track = true;
      for (auto& n : f.nodes) (void)c.position(n.get());
      track = false; total = allocations;
      check(parents(c).at(&f.parent).complete, "successful allocation reference completes"); }
    check(total > 256, "fault sweep includes metadata memo and temporary complete map");
    size_t partial_failures = 0;
    for (size_t failure = 0; failure < total; ++failure) {
        selector_match_context_s c; bool threw = false; fail_at = failure; allocations = 0; track = true;
        try { for (auto& n : f.nodes) check(selector_match(all, n.get(), c), "matcher OOM never silent false"); }
        catch (const std::bad_alloc&) { threw = true; }
        track = false; fail_at = std::numeric_limits<size_t>::max();
        check(threw, "every injected allocation failure propagates");
        for (const auto& [p,e] : parents(c)) { check(!e.complete, "failure never publishes partial complete map"); if (!e.positions.empty()) ++partial_failures; }
        audit(c); check(f.parent.children == original, "OOM preserves topology");
        for (auto& n : f.nodes) check(n->parent == &f.parent, "OOM preserves parent identity");
        selector_match_context_s retry;
        for (auto& n : f.nodes) check(retry.position(n.get()) == oracle(n.get()), "fresh retry succeeds after every failure");
        check(parents(retry).at(&f.parent).complete, "fresh retry completes");
    }
    check(partial_failures > 256, "fault sweep reaches partial and bulk phases");
    std::printf("OOM allocation points: %zu\n",total);
}
}
int main() {
    numeric(); activation(); differential(); membership(); synthetic_traversal(); oom(); memory_gates();
    std::printf("nth cache: %zu checks, %zu failures\n",checks,failures);
    return failures ? 1 : 0;
}
