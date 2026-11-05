// Public-ABI regression: checks intentionally remain active under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
void need(bool ok, const char* why) {
    if (!ok) {
        std::fprintf(stderr, "HARNESS ERROR %s\n", why);
        std::exit(2);
    }
}
int main() {
    struct C {
        const char* s;
        bool preceding;
        bool matches = true;
    };
    C cases[] = {
        {
            "#b:first-child::before", false
        }, {
            "#b:last-child::before", false
        }, {
            "#b:only-child::before", false
        }, {
            "#b:nth-child(1)::before", false
        }, {
            "#a + #b::before", true
        }, {
            "#left > #b::before", false
        }, {
            "#b:is(:first-child)::before", false
        }, {
            "#b:not(:nth-child(2))::before", false
        }, {
            "#b:where(:last-child)::before", false
        }, {"#b:nth-child(0)::before", false, false},
        {"#left + #b::before", false, false},
        {"#left ~ #b::before", false, false}
    };
    int checks = 0, failures = 0, controls = 0;
    for (auto c : cases) for (int after = 0; after < 2; ++after) for (int mode = 0; mode < 2; ++mode) {
        aether_doc_t doc = nullptr;
        need(aether_doc_create(&doc) == AETHER_OK, "doc");
        std::string html = "<body><section id='left'>";
        if (c.preceding) html += "<i id='a'></i>";
        html += "<div id='b'></div></section></body>";
        need(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "html");
        auto b = aether_doc_get_element_by_id(doc, "b");
        aether_style_runtime_t rt = nullptr;
        aether_style_runtime_options_t o{};
        aether_style_environment_t e {
            800, 600, 1, 16, 0, 0, 0
        };
        need(aether_style_runtime_create(&o, &e, nullptr, nullptr, &rt) == AETHER_OK, "runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        std::string base = std::string("#b{font-size:23px}#b::") + (after ? "after" : "before") + "{content:'X';opacity:.2}";
        need(aether_style_runtime_add_stylesheet(rt, base.c_str(), 0, "base", &sheet, &rules) == AETHER_OK && rules == 2, "base");
        std::string selector = c.s;
        if (after) selector.replace(selector.find("::before"), 8, "::after");
        std::string css = selector + "{opacity:.8}";
        need(aether_style_runtime_add_stylesheet(rt, css.c_str(), 1, "test", &sheet, &rules) == AETHER_OK && rules == 1, "selector");
        if (mode) need(aether_style_runtime_add_stylesheet(rt, "#left::before,#left::after{content:''}", 1, "parent", &sheet, &rules) == AETHER_OK && rules == 2, "parent");
        for (int pass = 0; pass < 3; ++pass) {
            aether_style_update_t* u = nullptr;
            int n = aether_style_runtime_resolve_subtree(rt, aether_doc_get_body(doc), &u);
            need(n >= 0, "resolve");
            int found = 0;
            for (int i = 0; i < n; ++i) {
                aether_node_t node = nullptr;
                aether_computed_style_handle_t h = nullptr;
                need(aether_style_runtime_get_update(u[i], &node, &h, nullptr) == AETHER_OK, "update");
                if (aether_node_get_parent(node) == b) {
                    ++found;
                    aether_computed_style_t st{};
                    need(aether_computed_style_copy(h, &st) == AETHER_OK, "style copy");
                    ++checks;
                    const float expected = c.matches ?  .8f : .2f;
                    if (!(std::fabs(st.opacity-expected)<.001)) {
                        ++failures;
                        std::printf("FAIL selector=%s mode=%d pass=%d opacity=%.1f expected=%.1f\n", selector.c_str(), mode, pass, st.opacity, expected);
                    }
                    need(std::fabs(st.font_size-23)<.001, "font inheritance");
                    ++controls;
                    need(aether_node_get_child_count(node) == 1, "pseudo text child");
                    ++controls;
                    char* text = aether_node_get_text_content(aether_node_get_child(node, 0));
                    need(text && std::string(text) == "X", "pseudo text X");
                    ++controls;
                    aether_string_free(text);
                    aether_computed_style_free(&st);
                }
                aether_computed_style_handle_destroy(h);
            }
            aether_style_updates_free(u, n);
            need(found == 1, "one target pseudo every pass");
            ++controls;
            need(aether_node_get_child_count(b) == 0, "target DOM remains empty");
            ++controls;
        }
        aether_style_runtime_destroy(rt);
        aether_doc_destroy(doc);
    }
    std::printf("SUMMARY: %d failed / %d pseudo-origin style checks; %d existence/text/inheritance/DOM controls passed\n", failures, checks, controls);
    return failures?1:0;
}
