// Public ABI tests for symbolic shadow metadata, no-op publication and ownership.
// Checks intentionally remain active when compiled with NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
namespace {
int checks = 0, failures = 0;
void need(bool ok, const char* s) {
    if (!ok) {
        std::fprintf(stderr, "HARNESS: %s\n", s);
        std::exit(2);
    }
}
void check(bool ok, const char* s) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", s);
    }
}
bool color(aether_color_rgba a, aether_color_rgba b) {
    return std::fabs(a.r - b.r) < .0001f && std::fabs(a.g - b.g) < .0001f &&
           std::fabs(a.b - b.b) < .0001f && std::fabs(a.a - b.a) < .0001f;
}
const aether_color_rgba red{1, 0, 0, 1}, blue{0, 0, 1, 1}, lime{0, 1, 0, 1};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t rt{};
    aether_node_t nodes[3]{};
    aether_computed_style_t cached[3]{};
    bool seen[3]{};
    uint32_t impacts[3]{};
    aether_computed_style_handle_t retained{};
    int update_count = 0;
    explicit Fixture(bool direct_child = false) {
        need(aether_doc_create(&doc) == AETHER_OK, "doc");
        const char* html = direct_child
            ? "<html><body><div id='p'><div id='g'></div><div id='m'></div>"
              "<div id='none'></div></div></body></html>"
            : "<html><body><div id='p'><div id='m'><div id='g'></div></div></div></body></html>";
        need(aether_doc_parse_html(doc, html) == AETHER_OK, "HTML");
        nodes[0] = aether_doc_get_element_by_id(doc, "p");
        nodes[1] = aether_doc_get_element_by_id(doc, "m");
        nodes[2] = aether_doc_get_element_by_id(doc, "g");
        aether_style_runtime_options_t opts{};
        opts.max_variable_depth = 32;
        opts.max_variable_expansion_length = 65536;
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        need(aether_style_runtime_create(&opts, &env, nullptr, nullptr, &rt) == AETHER_OK,
             "runtime");
    }
    void sheet(const char* css) {
        int64_t h{};
        need(aether_style_runtime_add_stylesheet(rt, css, 2, "symbols.css", &h, nullptr) ==
                 AETHER_OK,
             "sheet");
    }
    void set(int i, const char* s) {
        need(aether_element_set_attribute(nodes[i], "style", s) == AETHER_OK, "set inline");
    }
    void resolve(bool retain = false, aether_node_t changed = nullptr) {
        for (int i = 0; i < 3; ++i) {
            seen[i] = false;
            impacts[i] = 0;
        }
        aether_style_update_t* u{};
        const auto root = aether_doc_get_document_element(doc);
        const int n = changed
            ? aether_style_runtime_resolve_state_changes(rt, root, &changed, 1, &u)
            : aether_style_runtime_resolve_subtree(rt, root, &u);
        need(n >= 0, "resolve");
        update_count = n;
        for (int j = 0; j < n; ++j) {
            aether_node_t node{};
            aether_computed_style_handle_t h{};
            uint32_t impact{};
            need(aether_style_runtime_get_update(u[j], &node, &h, &impact) == AETHER_OK, "update");
            bool keep = false;
            for (int i = 0; i < 3; ++i)
                if (nodes[i] == node) {
                    seen[i] = true;
                    impacts[i] = impact;
                    aether_computed_style_free(&cached[i]);
                    need(aether_computed_style_copy(h, &cached[i]) == AETHER_OK, "copy");
                    if (retain && i == 0) {
                        need(!retained, "retain once");
                        retained = h;
                        keep = true;
                    }
                }
            if (!keep)
                aether_computed_style_handle_destroy(h);
        }
        aether_style_updates_free(u, n);
    }
    void shadow(int node, int index, aether_color_rgba c, const char* message) {
        const auto& l = cached[node].text_shadows;
        check(l.count > index && l.items && color(l.items[index].color, c), message);
    }
    void painted(int i, const char* message) {
        check(seen[i] && impacts[i] == (AETHER_STYLE_IMPACT_PAINT | AETHER_STYLE_IMPACT_INHERITED),
              message);
    }
    ~Fixture() {
        if (retained)
            aether_computed_style_handle_destroy(retained);
        aether_style_runtime_destroy(rt);
        aether_doc_destroy(doc);
        for (auto& s : cached)
            aether_computed_style_free(&s);
    }
};
void noop_transitions() {
    Fixture f;
    f.sheet("#p{color:red;text-shadow:1px 2px red}#m{color:red}#g{color:blue}");
    f.resolve(true);
    aether_computed_style_t snapshot{};
    need(aether_computed_style_copy(f.retained, &snapshot) == AETHER_OK, "retained copy");
    check(snapshot.text_shadows.items != f.cached[0].text_shadows.items,
          "snapshot is independent allocation");
    f.set(0, "text-shadow:1px 2px currentColor");
    f.resolve();
    f.shadow(0, 0, red, "symbol no-op parent stays red");
    f.shadow(1, 0, red, "same-color middle stays red");
    f.shadow(2, 0, blue, "symbol traverses no-op middle to blue grandchild");
    f.painted(2, "symbol transition grandchild PAINT|INHERITED");
    f.set(2, "color:#00ff00");
    f.resolve();
    f.shadow(2, 0, lime, "published symbol follows later grandchild color");
    f.set(0, "text-shadow:1px 2px red");
    f.resolve();
    f.shadow(2, 0, red, "reverse equal parent transition restores explicit color");
    f.painted(2, "reverse transition grandchild PAINT|INHERITED");
    f.set(2, "color:blue");
    f.resolve();
    f.shadow(2, 0, red, "reverse transition clears stale inherited symbol");
    f.set(0, "text-shadow:1px 2px currentColor");
    f.resolve();
    f.set(0, "--bad:1px 2px red,3px;text-shadow:var(--bad,8px 9px blue)");
    f.resolve();
    for (int i = 0; i < 3; ++i)
        check(f.cached[i].text_shadows.count == 0,
              "valid to computed-invalid mutation clears root inherited list");
    f.set(0, "text-shadow:4px 5px currentColor");
    f.resolve();
    f.shadow(2, 0, blue, "invalid to valid restores symbol");
    aether_computed_style_t old{};
    need(aether_computed_style_copy(f.retained, &old) == AETHER_OK, "old handle after mutations");
    check(old.text_shadows.count == 1 && old.text_shadows.items[0].offset_x == 1 &&
              color(old.text_shadows.items[0].color, red),
          "retained handle unchanged after updates freed");
    check(snapshot.text_shadows.count == 1 && snapshot.text_shadows.items[0].offset_x == 1 &&
              color(snapshot.text_shadows.items[0].color, red),
          "deep copy unchanged after all mutations");
    aether_computed_style_handle_destroy(f.retained);
    f.retained = nullptr;
    aether_computed_style_free(&old);
    check(snapshot.text_shadows.items[0].offset_x == 1,
          "copy survives handle and sibling-copy free");
    aether_computed_style_free(&snapshot);
    f.resolve();
    check(!f.seen[0] && !f.seen[1] && !f.seen[2], "repeat resolution has no spurious changes");
}
// A no-op intermediary recomputes its own symbolic impact and can hide a
// missing inherited flag in the changed-parent branch. Test a direct child too,
// after paint-only updates clear the initial historical inherited impacts.
void direct_changed_parent(bool incremental) {
    Fixture f(true);
    f.sheet("#p{color:red;text-shadow:1px 2px red}#g,#m,#none{color:blue}"
            "#m{text-shadow:1px 2px red}#none{text-shadow:none}");
    f.resolve(true);
    f.shadow(2, 0, red, "direct child initially inherits fixed red");
    f.set(0, "background-color:#111111");
    f.set(2, "background-color:#111111");
    f.resolve();
    check(f.update_count == 2 && f.seen[0] && f.seen[2] && !f.seen[1],
          "paint-only warmup updates exactly parent and direct child");
    // The first warmup can still carry inherited impact from initial creation.
    // A second paint-only change proves that neither node retains those bits.
    f.set(0, "background-color:#222222");
    f.set(2, "background-color:#222222");
    f.resolve();
    check(f.update_count == 2 && f.seen[0] && f.seen[2] && !f.seen[1],
          "second warmup updates exactly parent and direct child");
    check(f.impacts[0] == AETHER_STYLE_IMPACT_PAINT && f.impacts[2] == AETHER_STYLE_IMPACT_PAINT,
          "second warmup proves inherited impact history is clear");

    const auto changed = incremental ? f.nodes[0] : nullptr;
    f.set(0, "text-shadow:1px 2px currentColor;background-color:#123456");
    f.resolve(false, changed);
    check(f.update_count == 2 && f.seen[0] && !f.seen[1],
          "changed parent and direct child are the only updates");
    check(f.impacts[0] == AETHER_STYLE_IMPACT_PAINT, "parent background delta is exactly PAINT");
    f.shadow(0, 0, red, "changed parent keeps concrete red shadow");
    f.shadow(2, 0, blue, "direct child receives changed-parent symbolic color");
    f.painted(2, "direct changed-parent shadow delta is exactly PAINT|INHERITED");
    f.resolve(false, changed);
    check(f.update_count == 0, "repeat changed-parent resolution emits no updates");

    f.set(0, "text-shadow:1px 2px red;background-color:#123456");
    f.resolve(false, changed);
    check(f.update_count == 1 && !f.seen[0] && !f.seen[1],
          "fixed transition updates only direct child, not no-op parent or siblings");
    f.shadow(2, 0, red, "direct child returns to fixed red");
    f.painted(2, "direct fixed transition is exactly PAINT|INHERITED");
    f.resolve(false, changed);
    check(f.update_count == 0, "repeat direct fixed transition emits no updates");

    f.set(0, "text-shadow:1px 2px;background-color:#123456");
    f.resolve(false, changed);
    check(f.update_count == 1 && !f.seen[0] && !f.seen[1],
          "implicit color transition updates only direct child");
    f.shadow(2, 0, blue, "direct child uses implicit currentColor");
    f.painted(2, "direct implicit transition is exactly PAINT|INHERITED");
    f.resolve(false, changed);
    check(f.update_count == 0, "repeat direct implicit transition emits no updates");

    // The retained public handle must outlive all updates and its runtime.
    aether_style_runtime_destroy(f.rt);
    f.rt = nullptr;
    aether_doc_destroy(f.doc);
    f.doc = nullptr;
    aether_computed_style_t old{};
    need(aether_computed_style_copy(f.retained, &old) == AETHER_OK,
         "retained direct-parent handle after runtime destruction");
    check(old.text_shadows.count == 1 && old.text_shadows.items[0].offset_x == 1 &&
              color(old.text_shadows.items[0].color, red),
          "direct parent retained handle preserves original fixed shadow");
    aether_computed_style_free(&old);
}
void mixed_lists() {
    Fixture f;
    f.sheet(
        "#p{color:red;text-shadow:1px 2px blue,3px 4px currentColor,5px 6px;box-shadow:inset 7px "
        "8px 9px -2px blue,10px 11px currentColor,12px 13px}#m{color:red}#g{color:#00ff00}");
    f.resolve();
    for (int n = 0; n < 3; ++n) {
        check(f.cached[n].text_shadows.count == 3, "mixed inherited list keeps all entries");
        f.shadow(n, 0, blue, "explicit entry stays blue");
        f.shadow(n, 1, n == 2 ? lime : red, "currentColor entry resolves per descendant");
        f.shadow(n, 2, n == 2 ? lime : red, "default entry resolves per descendant");
        if (f.cached[n].text_shadows.count == 3)
            for (int j = 0; j < 3; ++j)
                check(f.cached[n].text_shadows.items[j].offset_x == 1 + 2 * j,
                      "mixed text order preserved");
    }
    const auto& b = f.cached[0].box_shadows;
    check(b.count == 3, "mixed box list has three entries");
    if (b.count == 3) {
        check(b.items[0].inset && b.items[0].spread_radius == -2 && color(b.items[0].color, blue),
              "box geometry inset explicit color preserved");
        check(color(b.items[1].color, red) && color(b.items[2].color, red),
              "box currentColor and default resolve");
    }
    check(f.cached[1].box_shadows.count == 0 && f.cached[2].box_shadows.count == 0,
          "box shadows not inherited");
    f.set(0, "color:#00ff00");
    f.resolve();
    const auto& changed = f.cached[0].box_shadows;
    check(changed.count == 3 && color(changed.items[0].color, blue) &&
              color(changed.items[1].color, lime) && color(changed.items[2].color, lime),
          "box explicit and symbolic references survive color mutation");
    f.shadow(1, 1, red, "explicit middle text color resolves inherited symbol locally");
    f.shadow(2, 2, lime, "grandchild default symbol remains local");
}
void deferred_noop() {
    Fixture f;
    f.sheet("#p{color:red;text-shadow:1px 2px currentColor}#m{color:red;text-shadow:1px 2px "
            "red}#g{color:blue}");
    f.resolve();
    f.shadow(2, 0, red, "middle initially masks inherited symbol");
    f.set(1, "--bad:1px 2px red,3px;text-shadow:var(--bad,1px 2px blue)");
    f.resolve();
    f.shadow(1, 0, red, "computed-invalid middle inherits concretely equal parent");
    f.shadow(2, 0, blue, "computed-invalid no-op publishes inherited symbol to grandchild");
    f.painted(2, "deferred no-op descendant PAINT|INHERITED");
    f.set(2, "color:#00ff00");
    f.resolve();
    f.shadow(2, 0, lime, "deferred inherited symbol survives next resolution");
    f.set(1, "text-shadow:1px 2px red");
    f.resolve();
    f.shadow(2, 0, red, "valid replacement clears deferred inherited symbol");
}
} // namespace
int main() {
    noop_transitions();
    direct_changed_parent(false);
    direct_changed_parent(true);
    mixed_lists();
    deferred_noop();
    std::printf("Shadow symbols: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
