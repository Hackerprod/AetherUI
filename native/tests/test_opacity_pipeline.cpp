#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <clocale>
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
                const std::string& variables = "", const std::string& parent = "opacity:.3;",
                const char* compile_locale = nullptr, const char* apply_locale = nullptr) {
    if (compile_locale) require(std::setlocale(LC_NUMERIC, compile_locale), "set compile locale");
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
    if (apply_locale) require(std::setlocale(LC_NUMERIC, apply_locale), "set apply locale");
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
static void lifecycle() {
    aether_doc_t doc{}; aether_style_runtime_t runtime{};
    require(aether_doc_create(&doc) == AETHER_OK, "lifecycle document");
    require(aether_doc_parse_html(doc, "<html><body><div id='x' style='opacity:.25;color:red'></div></body></html>") == AETHER_OK, "lifecycle html");
    const auto node = aether_doc_get_element_by_id(doc, "x");
    aether_style_runtime_options_t options{};
    aether_style_environment_t env{200,120,1,16,0,0,0};
    require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK, "lifecycle runtime");
    auto resolve = [&]() {
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(count >= 0, "lifecycle resolve");
        uint32_t impact = 0;
        for (int i=0;i<count;++i) {
            aether_node_t current{}; aether_computed_style_handle_t handle{}; uint32_t flags{};
            require(aether_style_runtime_get_update(updates[i],&current,&handle,&flags) == AETHER_OK,"lifecycle update");
            if (current == node) impact |= flags;
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates,count);
        return impact;
    };
    resolve();
    aether_style_map_t retained{};
    require(aether_style_runtime_create_resolved_style_map(runtime,doc,&retained) == AETHER_OK,"retained map");
    auto old = aether_style_map_get(retained,node);
    require(old != nullptr,"retained handle");
    aether_computed_style_handle_t clone{};
    require(aether_computed_style_handle_clone(old,&clone) == AETHER_OK,"clone handle");
    aether_computed_style_t copied{};
    require(aether_computed_style_copy(old,&copied) == AETHER_OK,"old copy");
    check(static_cast<float>(resolve()),0,"unchanged repeat impact");
    struct Mutation { const char* declaration; float expected; bool delta; };
    for (const auto& m : {Mutation{"opacity:50%;color:red",.5f,true},
            Mutation{"opacity:.5;color:red",.5f,false},
            Mutation{"opacity:.25;--bad:bogus;opacity:var(--bad);color:red",1,true},
            Mutation{"opacity:1e-7;color:red",1e-7f,true}}) {
        require(aether_element_set_attribute(node,"style",m.declaration) == AETHER_OK,"mutate inline");
        const uint32_t impact=resolve();
        const uint32_t expected=m.delta ? AETHER_STYLE_IMPACT_COMPOSITE | AETHER_STYLE_IMPACT_HITTEST : 0;
        check(static_cast<float>(impact),static_cast<float>(expected),"isolated opacity mutation impact");
        aether_style_map_t current{};
        require(aether_style_runtime_create_resolved_style_map(runtime,doc,&current) == AETHER_OK,"current map");
        auto handle=aether_style_map_get(current,node); require(handle != nullptr,"current handle");
        aether_computed_style_t style{};
        require(aether_computed_style_copy(handle,&style) == AETHER_OK,"current copy");
        check(style.opacity,m.expected,"mutated opacity");
        check(style.color.r,1,"neighbor color red"); check(style.color.g,0,"neighbor color green");
        aether_computed_style_free(&style); aether_computed_style_handle_destroy(handle); aether_style_map_destroy(current);
        check(static_cast<float>(resolve()),0,"stable repeat after mutation");
        for (auto retained_handle : {old,clone}) {
            aether_computed_style_t snapshot{};
            require(aether_computed_style_copy(retained_handle,&snapshot) == AETHER_OK,"retained snapshot copy");
            check(snapshot.opacity,.25f,"retained immutable handle"); aether_computed_style_free(&snapshot);
        }
        auto retained_map_handle=aether_style_map_get(retained,node);
        require(retained_map_handle != nullptr,"retained map lookup");
        aether_computed_style_t snapshot{};
        require(aether_computed_style_copy(retained_map_handle,&snapshot) == AETHER_OK,"retained map copy");
        check(snapshot.opacity,.25f,"old map remains unchanged");
        aether_computed_style_free(&snapshot); aether_computed_style_handle_destroy(retained_map_handle);
        check(copied.opacity,.25f,"exported old copy remains unchanged");
    }
    aether_computed_style_free(&copied); aether_computed_style_handle_destroy(clone);
    aether_computed_style_handle_destroy(old); aether_style_map_destroy(retained);
    aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}
static void locale_cases() {
    const std::string saved=std::setlocale(LC_NUMERIC,nullptr);
    std::string comma;
    for (const char* name : {"de_DE.UTF-8","de_DE.utf8","fr_FR.UTF-8","de_DE","fr_FR"}) {
        if (std::setlocale(LC_NUMERIC,name) && std::string(std::localeconv()->decimal_point)==",") { comma=name; break; }
    }
    if (comma.empty()) std::printf("SKIP opacity comma-decimal public locale: not installed\n");
    else {
        for (bool reverse : {false,true}) for (bool sheet : {false,true}) {
            const char* compile=reverse?comma.c_str():"C";
            const char* apply=reverse?"C":comma.c_str();
            for (const auto& c : {std::pair<const char*,float>{".5",.5f},{"50%",.5f},{"1e-7",1e-7f}}) {
                run(sheet,c.first,c.second,"","",compile,apply);
                run(sheet,"var(--x)",c.second,std::string("--x:")+c.first+";","",compile,apply);
            }
            run(sheet,"inherit",.3f,"","opacity:.3;",compile,apply);
            run(sheet,"var(--missing,inherit)",.3f,"","opacity:.3;",compile,apply);
            run(sheet,"0,5",.25f,"","",compile,apply);
            run(sheet,"var(--x)",1,"--x:0,5;","",compile,apply);
        }
        std::printf("opacity public locale: %s both directions; sheet compilation/resolution separated; inline compilation occurs at resolve\n",comma.c_str());
    }
    require(std::setlocale(LC_NUMERIC,saved.c_str()),"restore numeric locale");
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
            const float expected = std::string(word) == "inherit" ? .3f : 1.f;
            run(sheet, word, expected);
            run(sheet, std::string("var(--missing,") + word + ")", expected);
        }
        run(sheet, "var(--x)", .5f, "--x:inherit;", "opacity:.3;--x:50%;");
        for (const char* keyword : {"INHERIT", "/**/inherit/**/", R"(\69 nherit)"}) {
            run(sheet, keyword, .3f);
            run(sheet, std::string("var(--missing,") + keyword + ")", .3f);
            run(sheet, std::string("var(--missing,var(--nested,") + keyword + "))", .3f);
            run(sheet, "var(--x)", .3f, std::string("--x:var(--missing,") + keyword + ");");
        }
        for (const auto& c : valid) {
            run(sheet, "inherit", c.expected, "", std::string("opacity:") + c.text + ";");
            run(sheet, "var(--missing,inherit)", c.expected, "", std::string("opacity:") + c.text + ";");
        }
        for (const char* invalid_keyword : {"revert-layer", "inherit initial", "in/**/herit"}) {
            run(sheet, invalid_keyword, .25f);
            run(sheet, std::string("var(--missing,") + invalid_keyword + ")", 1);
        }
    }
    lifecycle();
    locale_cases();
    std::printf("opacity pipeline: %d checks, %d failures (active with NDEBUG)\n", checks, failures);
    return failures ? 1 : 0;
}
