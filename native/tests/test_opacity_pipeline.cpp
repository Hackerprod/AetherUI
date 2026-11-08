#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

// Public API regression. Checks remain active in optimized NDEBUG builds.
static int checks = 0, failures = 0;
static void require(bool ok, const char* operation) {
    if (!ok) { std::fprintf(stderr, "FIXTURE ERROR: %s\n", operation); std::exit(2); }
}
static void check(float actual, float expected, const std::string& label) {
    ++checks;
    // Exact native float equality intentionally prevents tiny values passing as zero.
    const bool ok = std::isfinite(actual) && actual == expected &&
                    (expected != 0 || !std::signbit(actual));
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s expected=%.9g actual=%.9g sign=%d\n",
                     label.c_str(), expected, actual, std::signbit(actual));
    }
}
static void run(bool sheet, const std::string& value, float expected,
                const std::string& variables = "", const std::string& parent = "opacity:.3;") {
    aether_doc_t doc{};
    require(aether_doc_create(&doc) == AETHER_OK, "create document");
    const std::string declaration = variables + "opacity:.25;opacity:" + value + ";";
    const std::string html = "<html><body style=\"" + parent +
        "\"><div id='x' style=\"" + (sheet ? std::string() : declaration) + "\"></div></body></html>";
    require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse html");
    aether_style_runtime_t runtime{};
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200, 120, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "create style runtime");
    if (sheet) {
        int64_t id{}; int32_t count{};
        require(aether_style_runtime_add_stylesheet(runtime, ("#x{" + declaration + "}").c_str(),
                0, "opacity.css", &id, &count) == AETHER_OK, "add stylesheet");
    }
    aether_style_update_t* updates{};
    const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
    require(count > 0, "resolve root");
    bool found = false;
    for (int i = 0; i < count; ++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{};
        require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "read update");
        if (node == aether_doc_get_element_by_id(doc, "x")) {
            found = true;
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy computed style");
            check(style.opacity, expected, std::string(sheet ? "sheet " : "inline ") + variables + value);
            aether_computed_style_free(&style);
        }
        aether_computed_style_handle_destroy(handle);
    }
    require(found, "find target update");
    aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
int main() {
#if defined(__unix__) || defined(__APPLE__)
    Dl_info info{};
    require(dladdr(reinterpret_cast<void*>(aether_abi_version), &info) != 0, "identify loaded DSO");
    std::printf("loaded=%s\n", info.dli_fname);
#endif
    struct Case { const char* text; float expected; };
    const Case valid[] = {
        {".5", .5f}, {"+0.5", .5f}, {"00.50", .5f}, {"5e-1", .5f},
        {"5E+1%", .5f}, {"50%", .5f}, {"2", 1}, {"150%", 1},
        {"1e100", 1}, {"1e100%", 1}, {"-.5", 0}, {"-20%", 0}, {"-1e100", 0},
        {"-0", 0}, {"-0%", 0}, {"1e-7", 1e-7f}, {"1e-5%", 1e-7f},
        {"1e-40", 1e-40f}, {"1e-400", 0}, {"-1e-400", 0},
        {"00000000000000000000000000000000000000000000000050%", .5f}
    };
    // These inputs form balanced outer declarations, so deferred winners are admitted.
    // Unbalanced enclosing CSS must be tested separately from invalid-at-computed-value behavior.
    const char* invalid[] = {
        "bogus", "nan", "NaN", "nan(1)", "inf", "Infinity", "-inf", "0x1p-1",
        ".", "+", "1.", "1e", "1e+", "1e-", "1px", "50 %", "50/**/%",
        "1/**/e-1", "+/**/.5", "1 0", ".5,.6", ".5/1", "calc(.5)",
        "1e400", "-1e400", "1e400%"
    };
    for (bool sheet : {false, true}) {
        for (const auto& c : valid) {
            run(sheet, c.text, c.expected);
            run(sheet, "var(--x)", c.expected, std::string("--x:") + c.text + ";");
        }
        for (const char* text : invalid) {
            run(sheet, text, .25f);
            run(sheet, "var(--x)", 1, std::string("--x:") + text + ";");
        }
        run(sheet, "var(--missing,50%)", .5f);
        run(sheet, "var(--bad,50%)", 1, "--bad:bogus;");
        run(sheet, "var(--empty,50%)", 1, "--empty:;");
        run(sheet, "var(--missing)", 1);
        run(sheet, "var(--missing,var(--also-missing,50%))", .5f);
        for (const char* word : {"initial", "unset", "inherit", "revert"}) {
            // Opacity has no native explicit-inheritance hook: even parent .3 gives 1.
            run(sheet, word, 1);
            run(sheet, std::string("var(--missing,") + word + ")", 1);
        }
        run(sheet, "var(--x)", .5f, "--x:inherit;", "opacity:.3;--x:50%;");
    }
    std::printf("opacity pipeline: %d checks, %d failures (active with NDEBUG)\n", checks, failures);
    return failures ? 1 : 0;
}
