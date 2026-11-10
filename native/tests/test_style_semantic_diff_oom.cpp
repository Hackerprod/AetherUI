// Focused R2 transactional OOM regression: category-only and metadata-only deltas.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/measure_state.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/css/style_map_types.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cmath>
#include <string>
static long fail_after = -1;
static bool fault_delivered = false;
static size_t ordinary_test_calls = 0, ordinary_test_failures = 0;
void* operator new(std::size_t n) {
    ++ordinary_test_calls; if (fail_after >= 0 && fail_after-- == 0) { fault_delivered = true; ++ordinary_test_failures; throw std::bad_alloc(); } if (auto p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// Route nothrow and array allocations through the same fault allocator. This
// keeps ASan allocation families matched instead of suppressing its checks.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#include "fault_inject_aligned.h"
#include "fault_inject_pmr_contract.h"
#include "frozen_measure_test_support.h"
static void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static aether_computed_style_handle_t lookup(aether_style_revision_t revision, aether_node_t target, uint32_t* impact = nullptr) {
    for (int i = 0; i < aether_style_revision_style_count(revision); ++i) {
        aether_node_t node{}; aether_computed_style_handle_t h{}; uint32_t bits{};
        require(aether_style_revision_get_style(revision, i, &node, &h, &bits) == AETHER_OK, "revision entry");
        if (node == target) { if (impact) *impact = bits; return h; }
        aether_computed_style_handle_destroy(h);
    }
    require(false, "node in revision"); return nullptr;
}
static void sweep(bool percentage_delta) {
    const auto family = pmr_fault_test::qualify(require);
    aether_doc_t doc{}; aether_style_runtime_t runtime{};
    require(aether_doc_create(&doc) == AETHER_OK, "doc");
    require(aether_doc_parse_html(doc, "<html><body><div id='x'></div></body></html>") == AETHER_OK, "HTML");
    auto x = aether_doc_get_element_by_id(doc, "x");
    aether_style_environment_t env{300, 200, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) == AETHER_OK, "runtime");
    int64_t sheet{};
    require(aether_style_runtime_add_stylesheet(runtime, "#x::before{content:'snapshot';width:4px}", 0, "OOM R2", &sheet, nullptr) == AETHER_OK, "pseudo sheet");
    require(aether_element_set_attribute(x, "style", "height:calc(0% + 100px);width:16px;background-position:1px 2px,3px 4px") == AETHER_OK, "initial style");
    aether_style_transaction_t initial{}; aether_style_revision_t old_revision{};
    require(aether_style_runtime_prepare_revision(runtime, doc, &env, &initial) == AETHER_OK, "initial prepare");
    require(aether_style_runtime_commit_revision(runtime, initial, &old_revision) == AETHER_OK, "initial commit");
    aether_style_transaction_destroy(initial);
    auto old = lookup(old_revision, x); auto pseudo = x->children.front();
    const auto generation = aether_style_revision_generation(old_revision);
    aether_style_map_t old_map{};
    require(aether_style_revision_create_map(old_revision, &old_map) == AETHER_OK, "old map");
    require(aether_element_set_attribute(x, "style", percentage_delta
        ? "height:calc(100px);width:1rem;background-position:1px 2px,30px 40px"
        : "height:calc(0% + 100px);width:1rem;background-position:1px 2px,30px 40px") == AETHER_OK, "candidate");
    const pmr_fault_test::Counters before;
    unsigned failures = 0; bool completed = false;
    for (long allocation = 0; allocation < 20000; ++allocation) {
        aether_style_transaction_t tx = reinterpret_cast<aether_style_transaction_t>(1);
        fault_delivered = false; fail_after = allocation;
        const auto status = aether_style_runtime_prepare_revision(runtime, doc, &env, &tx);
        fail_after = -1;
        if (status == AETHER_OK) {
            require(!fault_delivered, "prepare never swallows injected OOM");
            aether_style_revision_t next{};
            const size_t calls = ordinary_test_calls + aligned_test_calls;
            fault_delivered = false; fail_after = 0;
            const auto committed = aether_style_runtime_commit_revision(runtime, tx, &next);
            fail_after = -1;
            require(committed == AETHER_OK && !fault_delivered, "commit does not allocate through operator new");
            require(ordinary_test_calls + aligned_test_calls == calls, "zero new calls during commit");
            uint32_t impact{}; auto current = lookup(next, x, &impact);
            require(impact == (percentage_delta ? 7u : 0u), "retry publishes exact R2 mask");
            require(frozen_measure_test::has_percentage(current->measures.get(), "height", current->style.height) == !percentage_delta, "retry publishes percentage category");
            require((current->measures->dependency("width") & MEASURE_ROOT_FONT) != 0, "retry publishes root dependency");
            require(current->measures->background_positions()[1].x.value == 30, "retry publishes latent list");
            require(aether_style_revision_generation(next) > generation, "successful retry advances revision");
            aether_computed_style_handle_destroy(current); aether_style_revision_destroy(next);
            aether_style_transaction_destroy(tx); completed = true; break;
        }
        ++failures;
        require(status == AETHER_ERROR_OUT_OF_MEMORY && tx == nullptr && fault_delivered, "injected prepare failure is atomic OOM");
        aether_style_revision_t published{};
        require(aether_style_runtime_retain_revision(runtime, &published) == AETHER_OK, "retain after OOM");
        require(aether_style_revision_generation(published) == generation, "OOM preserves published revision");
        auto unchanged = lookup(published, x);
        require(frozen_measure_test::has_percentage(unchanged->measures.get(), "height", unchanged->style.height), "OOM preserves percentage category");
        require((unchanged->measures->dependency("width") & MEASURE_ROOT_FONT) == 0, "OOM preserves absent root dependency");
        require(unchanged->measures->background_positions()[1].x.value == 3, "OOM preserves latent list");
        require(x->children.size() == 1 && x->children.front() == pseudo && pseudo->identity->attached, "OOM preserves generated node identity");
        aether_computed_style_handle_destroy(unchanged); aether_style_revision_destroy(published);
    }
    require(completed && failures > 0, "bounded OOM sweep reaches successful retry");
    pmr_fault_test::require_sweep_coverage(family, before, require);
    auto old_from_map = aether_style_map_get(old_map, x);
    require(old_from_map && frozen_measure_test::has_percentage(old_from_map->measures.get(), "height", old_from_map->style.height), "retained map preserves category after success");
    require((old->measures->dependency("width") & MEASURE_ROOT_FONT) == 0 && old->measures->background_positions()[1].x.value == 3, "retained handle preserves complete sidecars");
    aether_style_runtime_destroy(runtime);
    require(old->style.width.value == 16, "snapshot survives runtime destruction");
    aether_style_map_destroy(old_map); aether_computed_style_handle_destroy(old_from_map);
    aether_style_revision_destroy(old_revision); aether_computed_style_handle_destroy(old); aether_doc_destroy(doc);
    std::printf("R2 %s OOM: %u failed allocations then success\n", percentage_delta ? "percentage" : "metadata-only", failures);
}
int main() { sweep(false); sweep(true); }
