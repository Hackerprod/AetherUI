// Opt-in isolated style-runtime benchmark. Build this identical source against
// exact base and candidate libraries; it does not require private test hooks.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

static size_t allocation_calls = 0;
void* operator new(std::size_t n) {
    ++allocation_calls;
    if (auto p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
// Include aligned/PMR calls in the count as well as ordinary new.
static long fail_after = -1;
static bool fault_delivered = false;
#define AETHER_TEST_ALIGNED_NOTE() (++allocation_calls)
#include "fault_inject_aligned.h"

namespace {
using Clock = std::chrono::steady_clock;
uint64_t checksum = 0;
void require(bool ok) { if (!ok) { std::fprintf(stderr, "runtime benchmark operation failed\n"); std::exit(1); } }
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t rt{};
    Fixture(const std::string& classes, size_t nodes) {
        require(aether_doc_create(&doc) == AETHER_OK);
        std::string html = "<html><body>";
        for (size_t i = 0; i < nodes; ++i) html += "<div id='n" + std::to_string(i) + "' class='" + classes + "'></div>";
        html += "</body></html>";
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK);
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &rt) == AETHER_OK);
    }
    ~Fixture() { aether_style_runtime_destroy(rt); aether_doc_destroy(doc); }
    void add(const std::string& source) {
        int32_t rules{};
        require(aether_style_runtime_add_stylesheet(rt, source.c_str(), 2, "bench", nullptr, &rules) == AETHER_OK);
        checksum += static_cast<uint32_t>(rules);
    }
    void resolve() {
        aether_style_transaction_t tx{}; aether_style_revision_t revision{};
        require(aether_style_runtime_prepare_revision(rt, aether_doc_get_document_element(doc), nullptr, &tx) == AETHER_OK);
        require(aether_style_runtime_commit_revision(rt, tx, &revision) == AETHER_OK);
        checksum += aether_style_revision_generation(revision) + aether_style_revision_style_count(revision) + aether_style_revision_change_count(revision);
        aether_style_revision_destroy(revision); aether_style_transaction_destroy(tx);
    }
};
std::string css(size_t rules, size_t buckets) {
    std::string result;
    for (size_t i = 0; i < rules; ++i)
        result += ".c" + std::to_string(i % buckets) + "{opacity:" + std::to_string(.1 + .001 * (i % 800)) + "}";
    return result;
}
void measure(const char* mode, size_t rules, size_t buckets, size_t nodes) {
    std::string classes;
    if (std::string(mode) == "misses") for (int i = 0; i < 32; ++i) classes += "missing" + std::to_string(i) + " ";
    else if (std::string(mode) == "repeated") for (int i = 0; i < 24; ++i) classes += "c0 ";
    else for (size_t i = 0; i < buckets; ++i) classes += "c" + std::to_string(i) + " ";
    const auto source = css(rules, buckets);
    Fixture fixture(classes, nodes); fixture.add(source); fixture.resolve();
    constexpr size_t samples = 5, iterations = 20;
    std::array<double, samples> resolve_ns{}, build_ns{};
    std::array<size_t, samples> resolve_alloc{}, build_alloc{};
    for (size_t sample = 0; sample < samples; ++sample) {
        auto before = allocation_calls; const auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) fixture.resolve();
        const auto end = Clock::now();
        resolve_ns[sample] = std::chrono::duration<double, std::nano>(end - begin).count() / iterations;
        resolve_alloc[sample] = (allocation_calls - before) / iterations;
        Fixture build(classes, nodes); build.add(".seed{opacity:.5}");
        before = allocation_calls; const auto build_begin = Clock::now(); build.add(source); const auto build_end = Clock::now();
        build_ns[sample] = std::chrono::duration<double, std::nano>(build_end - build_begin).count();
        build_alloc[sample] = allocation_calls - before;
    }
    // Keep individual samples for noise/order analysis; don't infer engine speed
    // from this isolated workload or discard misses/small-index cases.
    for (size_t sample = 0; sample < samples; ++sample)
        std::printf("%s,%zu,%zu,%zu,%zu,%.2f,%zu,%.2f,%zu\n", mode, rules, buckets, nodes, sample,
            resolve_ns[sample], resolve_alloc[sample], build_ns[sample], build_alloc[sample]);
}
} // namespace
int main() {
    std::printf("mode,rules,buckets,nodes,sample,resolve_ns,resolve_allocations,add_rebuild_ns,add_rebuild_allocations\n");
    for (size_t nodes : {size_t(1), size_t(32)}) {
        measure("empty", 0, 1, nodes);
        for (size_t rules : {size_t(1), size_t(256)}) {
            measure("one", rules, 1, nodes);
            measure("misses", rules, 1, nodes);
            measure("repeated", rules, 1, nodes);
        }
        measure("sixteen", 256, 16, nodes);
    }
    std::fprintf(stderr, "runtime_benchmark_checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
