// Public-ABI regression: checks intentionally remain active under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
static int checks = 0, failures = 0, dom_checks = 0;
static const char* ids[] = {"outer", "left", "right", "a", "b", "c"};

void must(bool b, const char* m) {
    if (!b) {
        std::fprintf(stderr, "HARNESS ERROR: %s\n", m);
        std::exit(2);
    }
}
// Fresh pseudo-free fixtures are the metamorphic oracle at every mutation stage.
// Lower-layer defaults preserve the zero specificity of :where().
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t rt = nullptr;
    std::map<std::string, aether_node_t> nodes;
    std::map<aether_node_t, float> styles;
    Fixture(const char* selector, const char* pseudo) {
        must(aether_doc_create(&doc) == AETHER_OK, "doc");
        must(aether_doc_parse_html(doc, "<body><main id='outer'><section id='left'> \n<!--begin--><i id='a' class='item' style='display:none'></i> <b id='b' class='item'></b><!--middle--><i id='c' class='item'></i> \t</section><section id='right'></section></main></body>") == AETHER_OK, "html");
        aether_style_runtime_options_t o{};
        aether_style_environment_t e {
            800, 600, 1, 16, 0, 0, 0
        };
        must(aether_style_runtime_create(&o, &e, nullptr, nullptr, &rt) == AETHER_OK, "runtime");
        for (auto id : ids) {
            nodes[id] = aether_doc_get_element_by_id(doc, id);
            must(nodes[id], "ID lookup");
        }
        int64_t sheet = 0;
        int32_t rules = 0;
        must(aether_style_runtime_add_stylesheet(rt, "#outer,#left,#right,#a,#b,#c{opacity:.2}", 0, "base", &sheet, &rules) == AETHER_OK && rules == 6, "base rules");
        std::string css = std::string(selector) + "{opacity:.8}";
        must(aether_style_runtime_add_stylesheet(rt, css.c_str(), 1, "selector", &sheet, &rules) == AETHER_OK && rules == 1, "selector rule");
        if (*pseudo) must(aether_style_runtime_add_stylesheet(rt, pseudo, 1, "pseudos", &sheet, &rules) == AETHER_OK && rules > 0, "pseudo rules");
    }
    ~Fixture() {
        aether_style_runtime_destroy(rt);
        // Documents own only reachable nodes. Restore the deliberately detached
        // baseline node before destruction (this is cleanup, not a test mutation).
        if (!aether_node_get_parent(nodes["b"]))
            must(aether_node_append_child(nodes["left"], nodes["b"]) == AETHER_OK, "cleanup detached b");
        aether_doc_destroy(doc);
    }
    void mutate(int stage) {
        if (stage == 1) must(aether_node_append_child(nodes["left"], nodes["a"]) == AETHER_OK, "reorder");
        if (stage == 2) must(aether_node_append_child(nodes["right"], nodes["b"]) == AETHER_OK, "move b");
        if (stage == 3) must(aether_node_append_child(nodes["right"], nodes["c"]) == AETHER_OK, "move c");
        if (stage == 4) must(aether_node_append_child(nodes["right"], nodes["b"]) == AETHER_OK, "reorder b");
        if (stage == 5) {
            must(aether_node_remove_child(nodes["right"], nodes["b"]) == AETHER_OK, "remove b");
        }
        if (stage == 6) must(aether_node_append_child(nodes["left"], nodes["b"]) == AETHER_OK, "reinsert b");
        if (stage == 7) must(aether_element_set_attribute(nodes["b"], "class", "item chosen") == AETHER_OK, "class");
        if (stage == 8) must(aether_element_set_attribute(nodes["b"], "data-k", "yes") == AETHER_OK, "attribute");
        if (stage == 9) aether_element_set_state(nodes["b"], AETHER_STATE_HOVER);
        if (stage == 10) aether_element_set_state(nodes["b"], AETHER_STATE_NONE);
    }
    // Updates are deltas: retain scalar snapshots, never generated-node pointers.
    void resolve(bool state_change = false) {
        aether_style_update_t* u = nullptr;
        auto changed = nodes["b"];
        int n = state_change
            ? aether_style_runtime_resolve_state_changes(rt, aether_doc_get_body(doc), &changed, 1, &u)
            : aether_style_runtime_resolve_subtree(rt, aether_doc_get_body(doc), &u);
        must(n >= 0, "resolve");
        for (int i = 0; i < n; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t h = nullptr;
            must(aether_style_runtime_get_update(u[i], &node, &h, nullptr) == AETHER_OK, "update");
            for (auto id : ids) if (node == nodes[id]) {
                aether_computed_style_t s{};
                must(aether_computed_style_copy(h, &s) == AETHER_OK, "copy");
                styles[node] = s.opacity;
                aether_computed_style_free(&s);
            }
            aether_computed_style_handle_destroy(h);
        }
        aether_style_updates_free(u, n);
        for (auto id : ids) if (aether_node_get_parent(nodes[id])) must(styles.count(nodes[id]), "missing real style");
    }
    std::string child_ids(const char* id) {
        std::string result;
        auto node = nodes[id];
        for (int i = 0; i < aether_node_get_child_count(node); ++i) {
            auto child = aether_node_get_child(node, i);
            result += std::to_string(aether_node_get_type(child));
            for (auto name : ids) if (nodes[name] == child) result += name;
            result += ",";
        }
        return result;
    }
};
int main() {
    const char* selectors[] = {
        "#b:first-child",
        "#b:last-child",
        "#b:only-child",
        "#b:nth-child(1)",
        "#b:nth-child(2)",
        "#b:nth-child(3)",
        "#b:nth-child(2n+1)",
        "#b:nth-child(odd)",
        "#b:nth-child(even)",
        "#b:nth-child(-n+2)",
        "#b:nth-child(0)",
        "#a + #b",
        "#a ~ #b",
        "#left + #b",
        "#right ~ #b",
        "#left > #b",
        "#outer #b",
        "#b:is(:first-child)",
        "#b:where(:last-child)",
        "#b:not(:nth-child(2))",
        "#left:has(#b:first-child)",
        "#left:has(#a + #b)",
        "#left:has(#a ~ #b)",
        "#outer:has(#left + #b)",
        "#outer:has(#right ~ #b)",
        "#left:has(#b:nth-child(2))",
        "#right:has(.item:only-child)",
        "#outer:has(#left :is(body #b))",
        "#outer:has(#left:has(#b:nth-child(2)))",
        "#right:empty",
        "#left:empty",
        "#b:empty",
        "#b.chosen:first-child",
        "#b[data-k=yes]:last-child",
        "#b:hover:nth-child(2)",
        "#outer:has(#b:hover:nth-child(2))"
    };
    const char* pseudos[] = {
        "#left::before,#right::before{content:''}",
        "#left::after,#right::after{content:'after'}",
        "#outer::before,#outer::after,#left::before,#left::after,#right::before,#right::after,#a::before,#a::after,#b::before,#b::after,#c::before,#c::after{content:'x'}",
        "#b:hover::before{content:''}#left::after{content:normal}#right::before{content:none}",
        "#b.chosen::before{content:'class'}#b[data-k=yes]::after{content:'attribute'}"
    };
    for (auto selector : selectors) for (int mode = 0; mode < 5; ++mode) {
        Fixture real(selector, pseudos[mode]);
        for (int stage = 0; stage < 11; ++stage) {
            real.mutate(stage);
            Fixture baseline(selector, "");
            for (int s = 0; s <= stage; ++s)baseline.mutate(s);
            baseline.resolve();
            for (int pass = 0; pass < 3; ++pass) {
                real.resolve(stage >= 9 && pass == 0);
                for (auto id : ids) {
                    // A removed node is not in the resolved tree until reinsertion.
                    if (!aether_node_get_parent(real.nodes[id])) continue;
                    ++checks;
                    float actual = real.styles[real.nodes[id]], expected = baseline.styles[baseline.nodes[id]];
                    if (!(std::fabs(actual-expected)<.001)) {
                        ++failures;
                        std::printf("FAIL selector=%s mode=%d stage=%d pass=%d id=%s actual=%.1f expected=%.1f\n", selector, mode, stage, pass, id, actual, expected);
                    }
                    must(aether_doc_get_element_by_id(real.doc, id) == real.nodes[id], "identity stability");
                    ++dom_checks;
                }
                for (auto id: {"outer", "left", "right"}) {
                    must(real.child_ids(id) == baseline.child_ids(id), "public children order/type/identity");
                    ++dom_checks;
                    for (int i = 0; i < aether_node_get_child_count(real.nodes[id]); ++i) {
                        must(aether_node_get_parent(aether_node_get_child(real.nodes[id], i)) == real.nodes[id], "public parent stability");
                    }
                }
            }
        }
    }
    std::printf("SUMMARY: %d failed / %d metamorphic style checks; %d DOM controls passed; selectors=36 pseudo_modes=5 stages=11 passes=3\n", failures, checks, dom_checks);
    return failures?1:0;
}
