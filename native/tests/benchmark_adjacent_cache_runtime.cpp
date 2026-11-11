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
#include <cstring>
#include <cmath>
#include <cerrno>
#include <limits>

// Timing deliberately uses the standard allocator, without allocation counters.
namespace {
using Clock = std::chrono::steady_clock;
uint64_t checksum = 0;
void require(bool ok) { if (!ok) { std::fprintf(stderr, "adjacent runtime benchmark operation failed\n"); std::exit(1); } }
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t rt{};
    size_t expected_hits = 0; float expected_opacity = 1;
    Fixture(const std::string& mode, size_t siblings, size_t parents, size_t repeats) {
        require(aether_doc_create(&doc) == AETHER_OK);
        std::string html = "<html><body>";
        for (size_t p = 0; p < parents; ++p) {
            html += "<section>";
            if (mode == "text-prefix") for (size_t i = 0; i < siblings; ++i) html += "x<!--separator-->";
            for (size_t i = 0; i < (mode == "text-prefix" ? 1 : siblings); ++i) {
                const bool hit = mode == "text-prefix" || mode == "dense" || mode == "many-parents" || mode == "no-adjacent" || mode == "nth-only" || mode == "mixed" ||
                    (mode == "sparse-late32" && i + 32 >= siblings) ||
                    (mode == "late-repeat" && i + 1 == siblings) ||
                    (mode == "cheap33" && i == 32);
                if (hit && (mode == "no-adjacent" || mode == "nth-only" || i > 0)) ++expected_hits;
                html += hit ? "<i class='hit'></i>" : "<i></i>";
            }
            html += "</section>";
        }
        html += "</body></html>";
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK);
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &rt) == AETHER_OK);
        std::string css;
        for (size_t r = 0; r < repeats; ++r) {
            if (mode == "no-adjacent") css += ".hit";
            else if (mode == "nth-only") css += ".hit:nth-child(n)";
            else if (mode == "mixed") css += "i:nth-child(n)+.hit:nth-child(n)";
            else css += "i+.hit";
            css += "{opacity:" + std::to_string(.2 + .01 * r) + "}";
        }
        int32_t rules{};
        require(aether_style_runtime_add_stylesheet(rt, css.c_str(), 2, "nth-bench", nullptr, &rules) == AETHER_OK);
        require(static_cast<size_t>(rules) == repeats);
        checksum += rules;
        expected_opacity = static_cast<float>(.2 + .01 * (repeats - 1));
    }
    ~Fixture() { aether_style_runtime_destroy(rt); aether_doc_destroy(doc); }
    void resolve(bool inspect = false) {
        aether_style_transaction_t tx{}; aether_style_revision_t revision{};
        require(aether_style_runtime_prepare_revision(rt, aether_doc_get_document_element(doc), nullptr, &tx) == AETHER_OK);
        require(aether_style_runtime_commit_revision(rt, tx, &revision) == AETHER_OK);
        checksum += aether_style_revision_generation(revision) + aether_style_revision_style_count(revision) + aether_style_revision_change_count(revision);
        if (inspect) {
            size_t hits = 0;
            for (int32_t i = 0; i < aether_style_revision_style_count(revision); ++i) {
                aether_computed_style_handle_t handle{}; aether_computed_style_t style{};
                require(aether_style_revision_get_style(revision, i, nullptr, &handle, nullptr) == AETHER_OK);
                require(aether_computed_style_copy(handle, &style) == AETHER_OK);
                if (std::fabs(style.opacity - expected_opacity) < .00001f) ++hits;
                aether_computed_style_free(&style); aether_computed_style_handle_destroy(handle);
            }
            require(hits == expected_hits);
            checksum += hits;
        }
        aether_style_revision_destroy(revision); aether_style_transaction_destroy(tx);
    }
};
void measure(const char* mode, size_t siblings, size_t parents, size_t repeats) {
    Fixture f(mode, siblings, parents, repeats); f.resolve(true);
    constexpr size_t samples = 5;
    const size_t total_nodes = siblings * parents;
    const size_t iterations = total_nodes <= 8 ? 1000 : total_nodes <= 128 ? 100 : 3;
    for (size_t sample = 0; sample < samples; ++sample) {
        const auto begin = Clock::now();
        for (size_t i = 0; i < iterations; ++i) f.resolve();
        const auto end = Clock::now();
        const auto ns = std::chrono::duration<double, std::nano>(end - begin).count() / iterations;
        std::printf("%s,%zu,%zu,%zu,%zu,%.2f\n", mode, siblings, parents, repeats, sample, ns);
        std::fflush(stdout);
    }
}
} // namespace
int main(int argc, char** argv) {
    // A mode alone preserves the workload filter. Four arguments select one
    // exact case for fresh-process timing and isolated external RSS measurement.
    const auto known_mode = [](const char* mode) {
        for (const char* known : {"dense", "sparse-late32", "no-adjacent", "late-repeat",
                                  "cheap33", "many-parents", "text-prefix", "nth-only", "mixed"})
            if (std::strcmp(mode, known) == 0) return true;
        return false;
    };
    const auto positive_size = [](const char* source, size_t& result) {
        if (!source || *source < '0' || *source > '9') return false;
        char* end = nullptr; errno = 0;
        const auto value = std::strtoull(source, &end, 10);
        if (errno || !end || *end || !value || value > std::numeric_limits<size_t>::max()) return false;
        result = static_cast<size_t>(value); return true;
    };
    size_t exact_siblings = 0, exact_parents = 0, exact_repeats = 0;
    if ((argc != 1 && argc != 2 && argc != 5) || (argc > 1 && !known_mode(argv[1])) ||
        (argc == 5 && (!positive_size(argv[2], exact_siblings) ||
                       !positive_size(argv[3], exact_parents) ||
                       !positive_size(argv[4], exact_repeats) ||
                       exact_siblings > std::numeric_limits<size_t>::max() / exact_parents))) {
        std::fprintf(stderr, "usage: %s [mode [siblings parents repeats]]\n", argv[0]);
        return 2;
    }
    const auto selected = [&](const char* name) { return argc < 2 || std::strcmp(argv[1], name) == 0; };
    std::printf("mode,siblings,parents,repeats,sample,resolve_ns\n");
    if (argc == 5) {
        measure(argv[1], exact_siblings, exact_parents, exact_repeats);
        std::fprintf(stderr, "adjacent_runtime_checksum=%llu\n", static_cast<unsigned long long>(checksum));
        return 0;
    }
    for (size_t siblings : {size_t(1), size_t(32), size_t(128), size_t(1024), size_t(4096)}) {
        if (selected("dense")) measure("dense", siblings, 1, 1);
        if (selected("sparse-late32")) measure("sparse-late32", siblings, 1, 1);
        if (selected("no-adjacent")) measure("no-adjacent", siblings, 1, 1);
        if (selected("nth-only")) measure("nth-only", siblings, 1, 1);
        if (selected("mixed")) measure("mixed", siblings, 1, 1);
    }
    for (size_t repeats = 1; repeats <= 8; ++repeats) {
        if (selected("late-repeat")) measure("late-repeat", 4096, 1, repeats);
        if (selected("cheap33")) measure("cheap33", 4096, 1, repeats);
    }
    for (size_t parents : {size_t(128), size_t(256)})
        if (selected("many-parents")) measure("many-parents", 32, parents, 1);
    if (selected("many-parents")) measure("many-parents", 256, 128, 1);
    if (selected("text-prefix")) measure("text-prefix", 4096, 1, 1);
    std::fprintf(stderr, "adjacent_runtime_checksum=%llu\n", static_cast<unsigned long long>(checksum));
}
