// Public ABI regression: structural CSS EOF recovery, not value-grammar repair.
// Checks deliberately remain active with NDEBUG. Expected results are derived
// from CSS Syntax simple blocks, not from a browser or a visual comparison.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool ok, const std::string& label) {
    ++checks;
    if (!ok) throw std::runtime_error(label);
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t target = nullptr;
    float width = -1;
    Fixture() {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, "<body><div id='target' class='a'></div></body>") == AETHER_OK, "parse document");
        target = aether_doc_get_element_by_id(doc, "target");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        require(target && aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    void add(const std::string& css, int expected) {
        int64_t sheet = 0; int32_t count = -1;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "eof.css", &sheet, &count) == AETHER_OK, "add stylesheet");
        require(count == expected, "rule count: got " + std::to_string(count) + ", expected " + std::to_string(expected));
    }
    void consume(aether_style_update_t* updates, int count) {
        require(count >= 0, "resolve status");
        for (int i = 0; i < count; ++i) {
            aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "get update");
            if (node == target) {
                aether_computed_style_t style{};
                require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy style");
                width = style.width.value;
                aether_computed_style_free(&style);
            }
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
    }
    void resolve() {
        aether_style_update_t* updates = nullptr;
        int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
        consume(updates, count);
    }
    void viewport(float w) {
        aether_style_environment_t env{w,600,1,16,0,0,0};
        aether_style_update_t* updates = nullptr;
        int count = aether_style_runtime_set_environment(runtime, &env, aether_doc_get_body(doc), &updates);
        consume(updates, count);
    }
    void expect(float expected, const std::string& label) {
        require(std::fabs(width - expected) < .0001f, label + ": width " + std::to_string(width));
    }
};
void cases() {
    struct Case { const char* label; const char* css; int rules; float width; };
    const Case cases[] = {
        {"closed", ".a{width:42px}", 1, 42},
        {"rule EOF", ".a{width:42px", 1, 42},
        {"rule EOF semicolon", ".a{width:42px;", 1, 42},
        {"media EOF", "@media(min-width:1px){.a{width:42px}", 1, 42},
        {"both EOF", "@media(min-width:1px){.a{width:42px", 1, 42},
        {"nested EOF", "@media(min-width:1px){@media(max-width:900px){.a{width:42px;", 1, 42},
        {"mixed EOF", "@media(min-width:1px){@media(max-width:900px){.a{width:42px}}", 1, 42},
        {"no opening", ".a", 0, 11},
        {"no media opening", "@media(min-width:1px)", 0, 11},
        {"no opening in prelude", ".a[title='{width:73px}'", 0, 11},
        {"unknown EOF", "@unknown{.a{width:73px}", 0, 11},
        {"function false sibling", ".a{width:11px;--x:f(}.a{width:73px}", 1, 11},
        {"bracket false sibling", ".a{width:11px;--x:[}.a{width:73px}", 1, 11},
        {"brace mismatched bracket", ".a{width:11px;--x:{]}.a{width:73px}", 1, 11},
        {"mixed false sibling", ".a{width:11px;--x:f([}]).a{width:73px}", 1, 11},
        {"balanced function", ".a{width:11px;--x:f(})}.a{width:73px}", 2, 73},
        {"balanced bracket", ".a{width:11px;--x:[}]}.a{width:73px}", 2, 73},
        {"balanced braces", ".a{width:11px;--x:{]}}.a{width:73px}", 2, 73},
        {"quoted punctuation", ".a{--x:'};@media{';width:42px", 1, 42},
        {"URL punctuation", ".a{--x:url(https://example.invalid/};@media{);width:42px", 1, 42},
        {"comment punctuation", ".a{/* } ; @media { */width:42px", 1, 42},
        {"escaped closing brace", ".a{--x:\\};width:42px", 1, 42},
        {"escaped opening brace", ".a{--x:\\{;width:42px", 1, 42},
        {"EOF comment", ".a{width:42px;/* }.a{width:73px}", 1, 42},
        {"bad string newline", ".a{width:42px;--x:'bad\n", 1, 42},
    };
    for (const auto& c : cases) {
        try {
            Fixture f; f.add(std::string(".a{width:11px}") + c.css, c.rules + 1);
            f.resolve(); f.expect(c.width, c.label);
        } catch (const std::exception& e) { throw std::runtime_error(std::string(c.label) + ": " + e.what()); }
    }
    Fixture f;
    f.add(".a{width:11px}@media(min-width:900px){.a{width:42px", 2);
    f.resolve(); f.expect(11, "inactive EOF is retained");
    for (float w : {1000.f,800.f,1000.f}) { f.viewport(w); f.expect(w >= 900 ? 42 : 11, "EOF viewport transition"); }
}
void depth(const std::string& mode, int count) {
    std::string css;
    for (int i = 0; i < count; ++i) css += "@media(min-width:1px){";
    css += ".a{width:42px";
    if (mode != "open") css += '}';
    if (mode == "closed") css.append(count, '}');
    else if (mode == "mixed") css.append(count / 2, '}');
    std::printf("ABI depth %d %s: compile begin\n", count, mode.c_str()); std::fflush(stdout);
    {
        Fixture f; f.add(css, 1);
        std::puts("ABI compile passed; resolve begin"); std::fflush(stdout);
        f.resolve(); f.expect(42, "deep real media condition");
        std::puts("ABI resolve passed; cleanup begin"); std::fflush(stdout);
    }
    std::puts("ABI cleanup passed");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc > 1) {
            const std::string mode = argv[1];
            require(mode == "open" || mode == "closed" || mode == "mixed", "expected open, closed or mixed");
            depth(mode, 10000);
        } else cases();
        std::printf("stylesheet EOF ABI: %d checks passed\n", checks); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL stylesheet EOF ABI: %s\n", e.what()); return 1; }
}
