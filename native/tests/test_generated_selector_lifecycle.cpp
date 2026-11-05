// Public-ABI regression: checks intentionally remain active under NDEBUG.
#include <aether/aether.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <initializer_list>
void need(bool b, const char* m) {
    if (!b) {
        std::fprintf(stderr, "HARNESS ERROR %s\n", m);
        std::exit(2);
    }
}
int main() {
    aether_doc_t doc = nullptr;
    need(aether_doc_create(&doc) == AETHER_OK, "doc");
    need(aether_doc_parse_html(doc, "<body><main id='outer'><section id='host'><div id='target'></div></section></main></body>") == AETHER_OK, "HTML");
    aether_style_runtime_t rt = nullptr;
    aether_style_runtime_options_t o{};
    aether_style_environment_t e {
        800, 600, 1, 16, 0, 0, 0
    };
    need(aether_style_runtime_create(&o, &e, nullptr, nullptr, &rt) == AETHER_OK, "runtime");
    int64_t base = 0, pseudo = 0;
    int32_t rules = 0;
    need(aether_style_runtime_add_stylesheet(rt, "#outer{opacity:.2;width:10px}#outer:has(#target:first-child){opacity:.8}#outer:has(#host + #target){width:90px}", 1, "probe", &base, &rules) == AETHER_OK && rules == 3, "base");
    auto outer = aether_doc_get_element_by_id(doc, "outer");
    float opacity = -1, width = -1;
    int failures = 0, checks = 0;
    auto resolve = [&](const char* stage) {
        aether_style_update_t* u = nullptr;
        int n = aether_style_runtime_resolve_subtree(rt, aether_doc_get_body(doc), &u);
        need(n >= 0, "resolve");
        for (int i = 0; i < n; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t h = nullptr;
            need(aether_style_runtime_get_update(u[i], &node, &h, nullptr) == AETHER_OK, "update");
            if (node == outer) {
                aether_computed_style_t s{};
                need(aether_computed_style_copy(h, &s) == AETHER_OK, "copy");
                opacity = s.opacity;
                width = s.width.value;
                aether_computed_style_free(&s);
            }
            aether_computed_style_handle_destroy(h);
        }
        aether_style_updates_free(u, n);
        checks += 2;
        failures += !(std::fabs(opacity-.8f)<.001f);
        failures += !(std::fabs(width-10)<.001f);
        std::printf("%s opacity=%.1f width=%.1f expected_opacity=.8 expected_width=10 pass=%s\n", stage, opacity, width, std::fabs(opacity-.8f)<.001f && std::fabs(width-10)<.001f?"yes":"NO");
    };
    resolve("baseline");
    need(aether_style_runtime_add_stylesheet(rt, "#host::before{content:''}", 1, "pseudo", &pseudo, &rules) == AETHER_OK && rules == 1, "pseudo");
    resolve("with-pseudo-pass-1");
    resolve("with-pseudo-pass-2");
    resolve("with-pseudo-pass-3");
    need(aether_style_runtime_remove_stylesheet(rt, pseudo), "remove");
    resolve("remove-pass-1");
    resolve("remove-pass-2");
    // Each add/remove and content transition is checked on its FIRST resolution.
    for (const char* content :  {
        "normal", "none", "''", "'text'", "none", "''"
    }) {
        std::string css = std::string("#host::before,#host::after{content:")+content+"}";
        need(aether_style_runtime_add_stylesheet(rt, css.c_str(), 1, "transition", &pseudo, &rules) == AETHER_OK && rules == 2, "transition sheet");
        resolve(content);
        resolve(content);
        resolve(content);
        need(aether_style_runtime_remove_stylesheet(rt, pseudo), "transition remove");
        resolve("removed-first-pass");
    }
    aether_style_runtime_destroy(rt);
    aether_doc_destroy(doc);
    std::printf("SUMMARY: %d of %d functional checks failed\n", failures, checks);
    return failures ?  1 : 0;
}
