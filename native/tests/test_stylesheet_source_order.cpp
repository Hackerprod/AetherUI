#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

// Public ABI only. Literal cascade oracles and checks remain active in Release.
namespace {
int checks = 0, failures = 0;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void equal(const std::string& name, float actual, float expected) {
    ++checks;
    if (!(std::fabs(actual - expected) < .0001f)) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: got %g, expected %g\n", name.c_str(), actual, expected);
    }
}
void equal(const std::string& name, const std::string& actual, const char* expected) {
    ++checks;
    if (actual != expected) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: got '%s', expected '%s'\n", name.c_str(), actual.c_str(), expected);
    }
}
struct Values {
    float opacity = 0, width = 0, top = 0, right = 0, bottom = 0, left = 0;
    int overflow_x = -1, overflow_y = -1;
};
Values copy(aether_computed_style_handle_t handle) {
    require(handle != nullptr, "missing style handle");
    aether_computed_style_t s{};
    require(aether_computed_style_copy(handle, &s) == AETHER_OK, "copy style");
    Values v{s.opacity, s.width.value, s.margin.top.value, s.margin.right.value,
             s.margin.bottom.value, s.margin.left.value, s.overflow_x, s.overflow_y};
    aether_computed_style_free(&s);
    return v;
}
struct Pseudo { std::string text; float opacity; };
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t rt = nullptr;
    aether_node_t target = nullptr, child = nullptr;
    aether_computed_style_handle_t target_style = nullptr, child_style = nullptr;
    std::vector<Pseudo> pseudos;
    Fixture(const char* inline_style = "") {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        const std::string html = "<body><div id='parent'><div id='target' class='a b' style='" +
            std::string(inline_style) + "'><span id='child'></span></div></div></body>";
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
        target = aether_doc_get_element_by_id(doc, "target");
        child = aether_doc_get_element_by_id(doc, "child");
        aether_style_runtime_options_t opts{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(target && child && aether_style_runtime_create(&opts, &env, nullptr, nullptr, &rt) == AETHER_OK,
                "create runtime");
    }
    ~Fixture() {
        aether_computed_style_handle_destroy(target_style); aether_computed_style_handle_destroy(child_style);
        aether_style_runtime_destroy(rt); aether_doc_destroy(doc);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    int64_t sheet(const std::string& css, int layer = 2, const char* name = "source-order.css", int* count = nullptr) {
        int64_t h = 0; int32_t n = -1;
        require(aether_style_runtime_add_stylesheet(rt, css.c_str(), layer, name, &h, &n) == AETHER_OK,
                "add stylesheet");
        if (count) *count = n;
        return h;
    }
    void remove(int64_t h) { require(aether_style_runtime_remove_stylesheet(rt, h) == AETHER_TRUE, "remove stylesheet"); }
    void consume(aether_style_update_t* updates, int n) {
        require(n >= 0, "resolution failed");
        pseudos.clear();
        for (int i = 0; i < n; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "get update");
            if (node != child && aether_node_get_parent(node) == target) {
                char* text = aether_node_get_text_content(aether_node_get_child(node, 0));
                pseudos.push_back({text ? text : "", copy(handle).opacity});
                aether_string_free(text);
            }
            if (node == target || node == child) {
                auto& slot = node == target ? target_style : child_style;
                aether_computed_style_handle_destroy(slot); slot = handle;
            } else aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, n);
    }
    void resolve(bool incremental = false) {
        aether_style_update_t* updates = nullptr;
        const int n = incremental
            ? aether_style_runtime_resolve_state_changes(rt, aether_doc_get_body(doc), &target, 1, &updates)
            : aether_style_runtime_resolve_subtree(rt, aether_doc_get_body(doc), &updates);
        consume(updates, n);
    }
    void viewport(float width) {
        aether_style_environment_t env{width, 600, 1, 16, 0, 0, 0};
        aether_style_update_t* updates = nullptr;
        const int n = aether_style_runtime_set_environment(rt, &env, aether_doc_get_body(doc), &updates);
        consume(updates, n);
    }
    // The legacy public aether_style_map_get is currently a stub. The private
    // companion test inspects resolved maps; public coverage uses owned update
    // snapshots. A no-change resolution legitimately emits no replacement.
    Values current(aether_node_t node = nullptr) {
        return copy(node == child ? child_style : target_style);
    }
    aether_computed_style_handle_t snapshot() {
        // Force a new public update without borrowing or transferring the
        // fixture's retained handle. Snapshot survives cache and sheet changes.
        aether_style_runtime_clear_cache(rt);
        aether_style_update_t* updates = nullptr;
        int n = aether_style_runtime_resolve_subtree(rt, aether_doc_get_body(doc), &updates);
        aether_computed_style_handle_t result = nullptr;
        for (int i = 0; i < n; ++i) {
            aether_node_t node = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, nullptr, nullptr) == AETHER_OK, "snapshot node");
            if (node == target)
                require(aether_style_runtime_get_update(updates[i], nullptr, &result, nullptr) == AETHER_OK, "snapshot handle");
        }
        consume(updates, n);
        require(result != nullptr, "fresh snapshot missing");
        return result;
    }
    Values run() { resolve(); return current(); }
    void attr(const char* name, const char* value) {
        require(aether_element_set_attribute(target, name, value) == AETHER_OK, "set attribute");
    }
};
void opacity(const char* name, const char* a, const char* b, float expected,
             int la = 2, int lb = 2, const char* in = "") {
    Fixture f(in); f.sheet(a, la); if (b) f.sheet(b, lb);
    equal(name, f.run().opacity, expected);
}
void margins(const std::string& name, const Values& v, float t, float r, float b, float l) {
    equal(name + " top", v.top, t); equal(name + " right", v.right, r);
    equal(name + " bottom", v.bottom, b); equal(name + " left", v.left, l);
}
void margin_case(const char* name, const char* a, const char* b, float t, float r, float bottom, float l) {
    Fixture f; f.sheet(a); f.sheet(b); margins(name, f.run(), t, r, bottom, l);
}
void basic_and_priorities() {
    opacity("same sheet", ".a{opacity:.4}.a{opacity:.8}", nullptr, .8f);
    opacity("one rule each", ".a{opacity:.4}", ".a{opacity:.8}", .8f);
    opacity("global order", ".a{opacity:.4}.a{opacity:.6}", ".a{opacity:.8}", .8f);
    opacity("declaration order", ".a{width:4px;opacity:.4}", ".a{opacity:.8}", .8f);
    opacity("same sheet buckets", ".b{opacity:.4}.a{opacity:.8}", nullptr, .8f);
    for (int n : {0, 1, 23}) {
        std::string noise;
        for (int i = 0; i < n; ++i) noise += ".never" + std::to_string(i) + "{opacity:.1}";
        Fixture f;
        f.sheet(noise + ".a{opacity:.4}" + noise + ".a{opacity:.6}" + noise);
        f.sheet(noise + ".a{opacity:.8}" + noise);
        equal("unmatched rules " + std::to_string(n), f.run().opacity, .8f);
    }
    for (const char* classes : {"a b", "b a", "a a b b", "b\t a\n b"}) {
        Fixture f; f.attr("class", classes); f.sheet(".b{opacity:.4}"); f.sheet(".a{opacity:.8}");
        equal("class token order", f.run().opacity, .8f);
    }
    for (const char* list : {".a,.b", ".b,.a", ".a,.a,.b"}) {
        Fixture f; f.sheet(std::string(list) + "{opacity:.4}"); f.sheet(".a{opacity:.8}");
        equal("selector list across sheets", f.run().opacity, .8f);
        Fixture g; g.sheet(std::string(list) + "{opacity:.4}.a{opacity:.8}");
        equal("selector list within sheet", g.run().opacity, .8f);
    }
    opacity("matching specific list", "#target,.a{opacity:.4}", ".a{opacity:.8}", .4f);
    opacity("nonmatching specific list", "#absent,.a{opacity:.4}", ".a{opacity:.8}", .8f);
    opacity("declaration duplicates", ".a{opacity:.4;opacity:.8}", nullptr, .8f);
    opacity("declaration important", ".a{opacity:.4!important;opacity:.8}", nullptr, .4f);
    opacity("important before normal", ".a{opacity:.4!important}", ".a{opacity:.8}", .4f);
    opacity("important after normal", ".a{opacity:.4}", ".a{opacity:.8!important}", .8f);
    opacity("important tie", ".never{opacity:.1}.a{opacity:.4!important}", ".a{opacity:.8!important}", .8f);
    opacity("higher earlier specificity", "#target{opacity:.4}", ".a{opacity:.8}", .4f);
    opacity("higher later specificity", ".a{opacity:.4}", "#target{opacity:.8}", .8f);
    opacity("higher earlier layer", ".a{opacity:.4}", ".a{opacity:.8}", .4f, 2, 1);
    opacity("higher later layer", ".never{opacity:.1}.a{opacity:.4}", ".a{opacity:.8}", .8f, 1, 2);
    // Engine layers, not CSS @layer: important does not reverse AetherUI layers.
    opacity("important higher earlier engine layer", ".a{opacity:.4!important}", ".a{opacity:.8!important}", .4f, 2, 1);
    opacity("important higher later engine layer", ".a{opacity:.4!important}", ".a{opacity:.8!important}", .8f, 1, 2);
    opacity("inline normal", ".a{opacity:.4}.a{opacity:.6}", ".a{opacity:.8}", .9f, 2, 2, "opacity:.9");
    opacity("sheet important beats inline normal", ".a{opacity:.4!important}", ".a{opacity:.8}", .4f, 2, 2, "opacity:.9");
    opacity("late sheet important beats inline normal", ".a{opacity:.4}", ".a{opacity:.8!important}", .8f, 2, 2, "opacity:.9");
    opacity("inline important", ".a{opacity:.4!important}", ".a{opacity:.8!important}", .9f, 2, 2, "opacity:.9!important");
    for (const char* name : {"source-order.css", "different.css", static_cast<const char*>(nullptr)}) {
        Fixture f; f.sheet(".a{opacity:.4}.a{opacity:.6}"); f.sheet(".a{opacity:.8}", 2, name);
        equal("diagnostic name independent", f.run().opacity, .8f);
    }
}
void consumers() {
    for (bool same_rule : {false, true}) {
        Fixture f; f.sheet(".never{width:1px}.a{--w:4px;width:var(--w)}#child{width:var(--w)}");
        f.sheet(same_rule ? ".a{--w:8px;width:var(--w)}" : ".a{--w:8px}");
        equal("custom property", f.run().width, 8); equal("inherited custom property", f.current(f.child).width, 8);
        f.sheet("#child{--w:12px}"); f.resolve(); equal("local variable override", f.current(f.child).width, 12);
        equal("local override leaves parent", f.current().width, 8);
    }
    margin_case("immediate then longhand", ".a{margin:1px 2px 3px 4px}", ".a{margin-left:8px}", 1,2,3,8);
    margin_case("longhand then immediate", ".a{margin-left:4px}", ".a{margin:1px 2px 3px 8px}", 1,2,3,8);
    margin_case("pending then immediate", ".a{--edges:4px}.a{margin:var(--edges)}", ".a{margin:8px}", 8,8,8,8);
    margin_case("immediate then pending", ".a{margin:4px}", ".a{--edges:1px 2px 3px 8px;margin:var(--edges)}", 1,2,3,8);
    margin_case("pending then longhand", ".a{--edges:1px 2px 3px 4px;margin:var(--edges)}", ".a{margin-left:8px}", 1,2,3,8);
    margin_case("longhand then pending", ".a{margin-left:4px}", ".a{--edges:1px 2px 3px 8px;margin:var(--edges)}", 1,2,3,8);
    margin_case("pending important tie", ".a{--edges:4px;margin:var(--edges)!important}", ".a{margin:8px!important}", 8,8,8,8);
    margin_case("later pending important tie", ".a{margin:4px!important}", ".a{--edges:8px;margin:var(--edges)!important}", 8,8,8,8);
    margin_case("pending important before normal", ".a{--edges:4px;margin:var(--edges)!important}", ".a{margin:8px}", 4,4,4,4);
    margin_case("invalid pending unset", ".a{margin:4px}", ".a{--edges:1px 2px 3px 4px 5px;margin:var(--edges)}", 0,0,0,0);
    margin_case("invalid pending respects important", ".a{margin:4px;margin-left:6px!important}", ".a{--edges:1px 2px 3px 4px 5px;margin:var(--edges)}", 0,0,0,6);
    struct OverflowCase { const char* css; int x, y; };
    for (const auto& c : std::vector<OverflowCase>{{".a{overflow:visible}", AETHER_OVERFLOW_VISIBLE,AETHER_OVERFLOW_VISIBLE},
             {".a{--ov:clip;overflow:var(--ov)}", AETHER_OVERFLOW_CLIP,AETHER_OVERFLOW_CLIP},
             {".a{overflow-y:scroll}", AETHER_OVERFLOW_AUTO,AETHER_OVERFLOW_SCROLL}}) {
        Fixture f; f.sheet(c.y == AETHER_OVERFLOW_SCROLL ? ".a{overflow:visible}.a{overflow:visible}" : ".a{overflow:hidden}.a{overflow:hidden}"); f.sheet(c.css);
        auto v = f.run(); equal("overflow x", v.overflow_x, c.x); equal("overflow y", v.overflow_y, c.y);
    }
    for (const char* pseudo : {"before", "after"}) {
        for (int mode = 0; mode < 4; ++mode) {
            Fixture f; const std::string selector = std::string(".a::") + pseudo;
            f.sheet(".never{opacity:.1}" + selector + "{content:'old'" + (mode == 1 || mode == 3 ? "!important" : "") + ";opacity:.4}");
            auto h = f.sheet(selector + "{--word:'new';content:" + (mode == 2 ? "var(--word)" : "'new'") +
                             (mode == 1 ? "!important" : "") + ";opacity:.8}");
            f.resolve(); equal("pseudo count", static_cast<float>(f.pseudos.size()), 1);
            if (!f.pseudos.empty()) { equal("pseudo content", f.pseudos[0].text, mode == 3 ? "old" : "new"); equal("pseudo style", f.pseudos[0].opacity, .8f); }
            equal("authored child count", aether_node_get_child_count(f.target), 1);
            f.remove(h); f.resolve();
            equal("pseudo count after remove", static_cast<float>(f.pseudos.size()), 1);
            if (!f.pseudos.empty()) equal("pseudo after remove", f.pseudos[0].text, "old");
            equal("authored children after remove", aether_node_get_child_count(f.target), 1);
        }
    }
}
void media_and_lifecycle() {
    {
        Fixture f; f.sheet(".a{opacity:.4}"); f.sheet("@media(min-width:600px){.a{opacity:.8}}"); f.resolve();
        for (float w : {500.f, 800.f, 500.f, 800.f}) { f.viewport(w); equal("media round trip", f.current().opacity, w == 500 ? .4f : .8f); }
    }
    opacity("nested media", ".never{opacity:.1}@media(min-width:600px){.a{opacity:.4}}",
            "@media(min-width:600px){@media(max-width:900px){.a{opacity:.8}}}", .8f);
    {
        Fixture f;
        f.sheet(".a{opacity:.2}@media(min-width:600px){.a{opacity:.4}}.a{opacity:.6}");
        f.sheet(".a{opacity:.7}@media(min-width:600px){@media(max-width:900px){.a{opacity:.8}}}.a{opacity:.9}");
        f.resolve();
        for (float w : {500.f,800.f,1000.f,800.f}) { f.viewport(w); equal("media textual position", f.current().opacity, .9f); }
    }
    {
        Fixture f;
        f.sheet(".a{opacity:.4}@media(min-width:600px){.a{opacity:.6}}");
        f.sheet("@media(min-width:600px){@media(max-width:900px){.a{opacity:.8}}}");
        f.resolve();
        for (float w : {500.f,800.f,1000.f,800.f,500.f}) {
            f.viewport(w); equal("both sheets media eligibility", f.current().opacity, w == 500 ? .4f : w == 1000 ? .6f : .8f);
        }
    }
    {
        Fixture f;
        auto a = f.sheet(".a{opacity:.6}"); f.sheet(".a{opacity:.8}");
        equal("before same text reload", f.run().opacity, .8f);
        f.remove(a); auto readded = f.sheet(".a{opacity:.6}");
        require(readded != a, "readd must use a fresh handle");
        equal("same text reload becomes latest", f.run().opacity, .6f);
    }
    for (bool warm : {false, true}) {
        Fixture f; if (warm) equal("fresh resolved default", f.run().opacity, 1);
        auto a = f.sheet(".a{opacity:.4}.a{opacity:.6}"); equal("lifecycle A", f.run().opacity, .6f);
        auto snapshot = f.snapshot();
        auto b = f.sheet(".a{opacity:.8}"); equal("lifecycle B", f.run().opacity, .8f);
        equal("retained snapshot after add", copy(snapshot).opacity, .6f);
        f.remove(b); equal("remove last B", f.run().opacity, .6f);
        auto c = f.sheet(".a{opacity:.9}"); equal("readd C", f.run().opacity, .9f);
        auto middle = f.sheet(".a{opacity:.7}"); auto last = f.sheet(".a{opacity:.9}"); f.resolve();
        f.remove(middle); equal("remove intermediate", f.run().opacity, .9f);
        f.remove(last); f.remove(a); equal("remove A", f.run().opacity, .9f);
        equal("missing handle", aether_style_runtime_remove_stylesheet(f.rt, -123), AETHER_FALSE);
        aether_style_runtime_clear_cache(f.rt); equal("clear cache", f.run().opacity, .9f);
        for (const char* css : {"", "/* only comment */", "???{opacity:.1}", ".a{not-a-property:9}"}) {
            int n = -1; f.sheet(css, 2, nullptr, &n); equal("empty/recovered rule count", n, 0);
            equal("empty/recovered sheet", f.run().opacity, .9f);
        }
        int64_t sentinel_h = -7; int32_t sentinel_n = -9;
        require(aether_style_runtime_add_stylesheet(f.rt, nullptr, 2, nullptr, &sentinel_h, &sentinel_n) != AETHER_OK, "null CSS must fail");
        equal("failed API keeps handle", static_cast<float>(sentinel_h), -7); equal("failed API keeps count", sentinel_n, -9);
        equal("failed API keeps style", f.run().opacity, .9f);
        f.remove(c); equal("remove final styled sheet", f.run().opacity, 1);
        equal("snapshot after removals", copy(snapshot).opacity, .6f);
        aether_style_runtime_clear_cache(f.rt);
        equal("snapshot after cache destruction", copy(snapshot).opacity, .6f);
        aether_computed_style_handle_destroy(snapshot);
    }
}
void mutations_and_incremental() {
    Fixture f; f.sheet(".a{opacity:.4;--w:4px}.a:hover{opacity:.6}#child{width:var(--w)}");
    f.resolve(); aether_element_set_state(f.target, AETHER_STATE_HOVER);
    f.sheet(".a{--w:8px}.a:hover{opacity:.8}");
    f.resolve(true); equal("incremental invalidated parent fallback", f.current().opacity, .8f);
    equal("fallback inherited variable", f.current(f.child).width, 8);
    f.resolve(); equal("full agrees with fallback", f.current().opacity, .8f);
    for (int state : {AETHER_STATE_NONE, AETHER_STATE_HOVER, AETHER_STATE_NONE, AETHER_STATE_HOVER}) {
        aether_element_set_state(f.target, state); f.resolve(true);
        equal("stable incremental hover", f.current().opacity, state == AETHER_STATE_HOVER ? .8f : .4f);
    }
    aether_element_set_state(f.target, AETHER_STATE_NONE);
    f.sheet(".b{opacity:.7}.a{opacity:.8}"); f.attr("class", "b a a");
    equal("mutate class order", f.run().opacity, .8f);
    f.attr("class", "b"); equal("mutate matching class", f.run().opacity, .7f);
    f.attr("class", "a b"); f.attr("style", "opacity:.9;--w:12px");
    equal("mutate inline", f.run().opacity, .9f); equal("mutate inherited variable", f.current(f.child).width, 12);
    f.attr("style", ""); equal("remove inline", f.run().opacity, .8f);
    equal("restore inherited variable", f.current(f.child).width, 8);
}
} // namespace
int main() {
    try {
        basic_and_priorities(); consumers(); media_and_lifecycle(); mutations_and_incremental();
        std::printf("stylesheet source order: %d checks, %d failures\n", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "HARNESS ERROR: %s\n", e.what()); return 2;
    }
}
