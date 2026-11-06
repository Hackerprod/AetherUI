// Public ABI only; every action and check remains active with NDEBUG.
#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int cases = 0, checks = 0, failures = 0;
bool canonical_only = false;
void check(bool ok, const std::string& why) {
    ++checks;
    if (!ok) throw std::runtime_error(why);
}
void run(const std::string& label, const std::function<void()>& body) {
    ++cases;
    try { body(); }
    catch (const std::exception& e) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", label.c_str(), e.what());
    }
}
std::string take(char* value) {
    std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}
struct DomSnapshot {
    aether_node_t node, parent;
    int type, state;
    std::string tag, text;
    std::map<std::string, std::string> attributes;
    std::vector<aether_node_t> children;
    explicit DomSnapshot(aether_node_t n) : node(n), parent(aether_node_get_parent(n)),
        type(aether_node_get_type(n)), state(aether_element_get_state(n)),
        tag(take(aether_element_get_tag_name(n))), text(take(aether_node_get_text_content(n))) {
        for (int i = 0; i < aether_element_get_attribute_count(n); ++i)
            attributes.emplace(take(aether_element_get_attribute_name(n, i)),
                               take(aether_element_get_attribute_value(n, i)));
        for (int i = 0; i < aether_node_get_child_count(n); ++i)
            children.push_back(aether_node_get_child(n, i));
    }
    void verify() const {
        const DomSnapshot now(node);
        check(parent == now.parent && type == now.type && state == now.state && tag == now.tag &&
              text == now.text && attributes == now.attributes && children == now.children,
              "authored DOM identity/attributes/relationships unchanged");
    }
};
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    std::set<aether_node_t> authored, emitted;
    std::map<aether_node_t, float> values;
    // Generated handles are never retained: copy only parent identity, text and opacity.
    std::map<aether_node_t, std::vector<std::pair<std::string, float>>> generated;
    explicit Fixture(const std::string& html) {
        try {
            check(aether_doc_create(&doc) == AETHER_OK, "create document");
            check(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
            collect(reinterpret_cast<aether_node_t>(doc));
            aether_style_runtime_options_t options{};
            const aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
            check(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK,
                  "create runtime");
            sheet("*{opacity:.2}", 1);
        } catch (...) { cleanup(); throw; }
    }
    ~Fixture() { cleanup(); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    void cleanup() {
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
        runtime = nullptr; doc = nullptr;
    }
    void collect(aether_node_t n) {
        authored.insert(n);
        for (int i = 0; i < aether_node_get_child_count(n); ++i) collect(aether_node_get_child(n, i));
    }
    aether_node_t node(const char* id) {
        const auto n = aether_doc_get_element_by_id(doc, id);
        check(n != nullptr, std::string("node ") + id); return n;
    }
    void sheet(const std::string& css, int expected) {
        int64_t id = 0; int32_t rules = -1;
        check(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "pseudo-name.css", &id, &rules)
              == AETHER_OK, "add stylesheet");
        check(rules == expected, "rule count expected " + std::to_string(expected) + " got " + std::to_string(rules));
    }
    void resolve(aether_node_t changed = nullptr) {
        aether_style_update_t* updates = nullptr;
        const auto root = reinterpret_cast<aether_node_t>(doc);
        const int count = changed ? aether_style_runtime_resolve_state_changes(runtime, root, &changed, 1, &updates)
                                  : aether_style_runtime_resolve_subtree(runtime, root, &updates);
        check(count >= 0, "resolve status");
        emitted.clear(); generated.clear();
        try {
            for (int i = 0; i < count; ++i) {
                aether_node_t n = nullptr; aether_computed_style_handle_t handle = nullptr;
                const auto status = aether_style_runtime_get_update(updates[i], &n, &handle, nullptr);
                if (status != AETHER_OK || !handle) {
                    if (handle) aether_computed_style_handle_destroy(handle);
                    check(false, "get update");
                }
                aether_computed_style_t style{};
                const auto copied = aether_computed_style_copy(handle, &style);
                aether_computed_style_handle_destroy(handle);
                check(copied == AETHER_OK, "copy style");
                if (authored.count(n)) { values[n] = style.opacity; emitted.insert(n); }
                else {
                    const auto parent = aether_node_get_parent(n);
                    if (authored.count(parent))
                        generated[parent].emplace_back(take(aether_node_get_text_content(aether_node_get_child(n, 0))), style.opacity);
                }
                aether_computed_style_free(&style);
            }
        } catch (...) { aether_style_updates_free(updates, count); throw; }
        aether_style_updates_free(updates, count);
    }
    float expect(aether_node_t n, float expected) {
        check(values.count(n) == 1, "copied subject style exists");
        const float actual = values.at(n);
        check(std::fabs(actual - expected) < .0001f,
              "opacity expected " + std::to_string(expected) + " got " + std::to_string(actual));
        return actual;
    }
    void expect_generated(aether_node_t host, float expected) {
        const auto& list = generated[host];
        check(list.size() == 1, "one newly reported generated pseudo");
        check(list[0].first == "X", "generated content X");
        check(std::fabs(list[0].second - expected) < .0001f, "generated pseudo opacity");
        check(aether_node_get_child_count(host) == 0, "generated pseudo excluded from public children");
    }
};
std::string upper(std::string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}
std::vector<std::string> variants(const std::string& name) {
    if (canonical_only) return {name};
    const auto caps = upper(name);
    std::string mixed = name, escaped, simple = caps;
    for (size_t i = 0; i < mixed.size(); i += 2)
        if (mixed[i] >= 'a' && mixed[i] <= 'z') mixed[i] = static_cast<char>(mixed[i] - 'a' + 'A');
    char low[16], high[16], six[16];
    std::snprintf(low, sizeof(low), "\\%x ", static_cast<unsigned char>(name[0]));
    std::snprintf(high, sizeof(high), "\\%x ", static_cast<unsigned char>(caps[0]));
    std::snprintf(six, sizeof(six), "\\%06x", static_cast<unsigned char>(caps[0]));
    for (size_t i = 0; i < caps.size(); ++i) {
        if (i % 2 == 0 && caps[i] != '-') {
            char hex[16]; std::snprintf(hex, sizeof(hex), "\\%x ", static_cast<unsigned char>(caps[i])); escaped += hex;
        } else escaped += name[i];
    }
    for (size_t i = 0; i < simple.size(); ++i) {
        if (simple[i] >= 'G' && simple[i] <= 'Z') { simple.insert(i, "\\"); break; }
    }
    return {name, caps, mixed, std::string(low) + name.substr(1), std::string(high) + caps.substr(1),
            std::string(six) + caps.substr(1), escaped, simple};
}
std::string insert_name(std::string pattern, const std::string& name) {
    const auto at = pattern.find('@'); check(at != std::string::npos, "test name marker");
    pattern.replace(at, 1, name); return pattern;
}
const char* identity_html = "<html id='root'><body><section><div id='Target' class='Item' data-v='AbC'></div>"
                            "<div id='other' class='other'></div></section></body></html>";
void matching_matrix() {
    struct Case { const char *name, *pattern, *html, *subject; bool matches; };
    const Case table[] = {
        {"empty", ".Item:@", identity_html, "Target", true},
        {"empty", ".Item:@", "<body><div id='Target' class='Item'> </div></body>", "Target", false},
        {"first-child", ".Item:@", identity_html, "Target", true},
        {"first-child", ".Item:@", "<body><i></i><div id='Target' class='Item'></div></body>", "Target", false},
        {"last-child", ".Item:@", "<body><i></i><div id='Target' class='Item'></div></body>", "Target", true},
        {"last-child", ".Item:@", identity_html, "Target", false},
        {"only-child", ".Item:@", "<body><div id='Target' class='Item'></div></body>", "Target", true},
        {"only-child", ".Item:@", identity_html, "Target", false},
        {"nth-child", ".Item:@(2N+1)", identity_html, "Target", true},
        {"nth-child", ".Item:@(ODD)", identity_html, "Target", true},
        {"nth-child", ".Item:@(2)", identity_html, "Target", false},
        {"root", "html:@", identity_html, "root", true},
        {"root", ".Item:@", identity_html, "Target", false},
        {"is", ".Item:@(:empty,.fallback)", identity_html, "Target", true},
        {"is", ".Item:@(.missing)", identity_html, "Target", false},
        {"where", ".Item:@(#Target)", identity_html, "Target", true},
        {"where", ".Item:@(#target)", identity_html, "Target", false},
        {"not", ".Item:@(.missing)", identity_html, "Target", true},
        {"not", ".Item:@(.Item)", identity_html, "Target", false},
        {"has", ".Parent:@(.Child)", "<body><div id='Target' class='Parent'><span class='Child'></span></div></body>", "Target", true},
        {"has", ".Parent:@(.Child)", "<body><div id='Target' class='Parent'></div><span class='Child'></span></body>", "Target", false},
        {"has", ".Parent:@(.Outside .Child)", "<body class='Outside'><div id='Target' class='Parent'><span class='Child'></span></div></body>", "Target", false},
        {"enabled", ".Item:@", identity_html, "Target", true},
        {"read-write", ".Item:@", identity_html, "Target", true},
        {"disabled", ".Item:@", identity_html, "Target", false},
        {"checked", ".Item:@", identity_html, "Target", false}
    };
    for (const auto& c : table) for (const auto& name : variants(c.name))
        run("matching/" + insert_name(c.pattern, name) + (c.matches ? "/yes" : "/no"), [&] {
            std::vector<float> observations;
            for (const auto& spelling : {std::string(c.name), name}) {
                Fixture f(c.html); const auto subject = f.node(c.subject);
                const DomSnapshot snapshot(subject);
                f.sheet(insert_name(c.pattern, spelling) + "{opacity:.8}", 1); f.resolve();
                observations.push_back(f.expect(subject, c.matches ? .8f : .2f)); snapshot.verify();
            }
            check(observations[0] == observations[1], "canonical/variant matching equality");
        });
    for (const char* name : {"empty", "EMPTY", "\\45 MPTY"}) run("silent-forgiving-branch/" + std::string(name), [&] {
        if (canonical_only && std::string(name) != "empty") return;
        Fixture f(identity_html); f.sheet(std::string(".Item:is(:") + name + ",.fallback){opacity:.8}", 1);
        f.resolve(); f.expect(f.node("Target"), .8f);
    });
}
void cascade_and_arguments() {
    for (const char* spelling : {"is(#Missing:empty,.fallback)", "IS(#Missing:EMPTY,.fallback)", "\\49 S(#Missing:\\45 MPTY,.fallback)"})
        run("specificity/" + std::string(spelling), [&] {
            Fixture f("<body><div id='Target' class='Item fallback'></div></body>");
            f.sheet(std::string(".Item:") + spelling + "{opacity:.8}.Item.fallback{opacity:.4}", 2);
            f.resolve(); f.expect(f.node("Target"), .8f);
        });
    for (const auto& name : variants("where")) run("where-specificity/" + name, [&] {
        Fixture f("<body><div id='Target' class='Item fallback'></div></body>");
        f.sheet(".Item:" + name + "(#Target:HOVER,.fallback){opacity:.8}.Item.fallback{opacity:.4}", 2);
        f.resolve(); f.expect(f.node("Target"), .4f);
    });
    const std::pair<const char*, bool> args[] = {
        {".Item", true}, {".item", false}, {".\\49 tem", true}, {".\\69 tem", false},
        {"#Target", true}, {"#target", false}, {"[data-v='AbC']", true}, {"[data-v='abc']", false},
        {"[data-v='\\41 bC']", true}, {"[data-v='abc' i]", true}, {"[data-v='abc' I]", true},
        {"[data-v='abc' s]", false}, {"[data-v='abc' S]", false}, {"[data-V='AbC']", false},
        {".HOVER", false}, {"#IS", false}, {"[data-v='HOVER']", false}
    };
    for (const char* function : {"is", "IS", "where", "WHERE", "not", "NOT"}) for (const auto& arg : args)
        run("argument/" + std::string(function) + "/" + arg.first, [&] {
            Fixture f(identity_html); const auto subject = f.node("Target");
            const DomSnapshot before(subject);
            f.sheet(std::string(".Item:") + function + "(" + arg.first + "){opacity:.8}", 1); f.resolve();
            const bool negated = std::string(function) == "not" || std::string(function) == "NOT";
            f.expect(subject, arg.second != negated ? .8f : .2f); before.verify();
        });
    for (const char* function : {"has", "HAS", "\\48 AS"}) for (const auto& arg : args)
        run("has-argument/" + std::string(function) + "/" + arg.first, [&] {
            Fixture f("<body><section id='Parent' class='Parent'><div id='Target' class='Item' data-v='AbC'></div></section></body>");
            const DomSnapshot parent(f.node("Parent")), child(f.node("Target"));
            f.sheet(std::string(".Parent:") + function + "(" + arg.first + "){opacity:.8}", 1); f.resolve();
            f.expect(parent.node, arg.second ? .8f : .2f); parent.verify(); child.verify();
        });
    run("custom-property-case-control", [] {
        Fixture f(identity_html);
        f.sheet(".Item{--Brand:.8;--brand:.4}.Item:IS(.Item){opacity:var(--Brand)}", 2);
        f.resolve(); f.expect(f.node("Target"), .8f);
    });
}
std::vector<float> state_sequence(const std::string& html, const std::string& selector,
                                  const char* subject_id, const char* changing_id, int state,
                                  bool inverted = false, const std::string& pseudo = "") {
    Fixture f(html); const auto subject = f.node(subject_id), changing = f.node(changing_id);
    if (!pseudo.empty()) f.sheet(".Item::" + pseudo + "{content:'X';opacity:.2}", 1);
    f.sheet(selector + "{opacity:.8}", 1);
    f.resolve(); // Populate runtime state snapshots once; no subsequent full resolves.
    std::vector<float> result;
    const int states[] = {AETHER_STATE_NONE, state, state, AETHER_STATE_NONE, AETHER_STATE_NONE};
    float previous = inverted ? .8f : .2f;
    for (size_t i = 0; i < std::size(states); ++i) {
        const float expected = (states[i] != 0) != inverted ? .8f : .2f;
        if (i) {
            aether_element_set_state(changing, states[i]);
            f.resolve(changing);
        }
        if (pseudo.empty()) {
            if (expected != previous) check(f.emitted.count(subject) == 1, "changed subject emitted by state delta");
            result.push_back(f.expect(subject, expected));
        } else {
            f.expect(subject, .2f);
            // Generated nodes are recreated and copied from the current update batch.
            f.expect_generated(subject, expected); result.push_back(expected);
        }
        check(aether_element_get_state(changing) == states[i], "public state retained");
        previous = expected;
    }
    return result;
}
void state_changes() {
    struct Case { const char *name; int state; };
    const Case states[] = {{"hover", AETHER_STATE_HOVER}, {"active", AETHER_STATE_ACTIVE},
                           {"focus", AETHER_STATE_FOCUS}, {"invalid", AETHER_STATE_INVALID}};
    for (const auto& state : states) for (const auto& name : variants(state.name)) run("state/" + name, [&] {
        const auto lower = state_sequence(identity_html, std::string(".Item:") + state.name, "Target", "Target", state.state);
        const auto changed = state_sequence(identity_html, ".Item:" + name, "Target", "Target", state.state);
        check(lower == changed, "canonical/variant state sequence");
    });
    struct Nested { const char *lower, *changed, *html, *subject, *changing; bool inverted; };
    const Nested nested[] = {
        {".Item:is(:hover,.fallback)", ".Item:IS(:HOVER,.fallback)", identity_html, "Target", "Target", false},
        {".Item:not(:hover)", ".Item:NOT(:HOVER)", identity_html, "Target", "Target", true},
        {".Item:is(:not(:hover),.fallback)", ".Item:IS(:NOT(:HOVER),.fallback)", identity_html, "Target", "Target", true},
        {".Parent:has(.Child:hover,.never)", ".Parent:HAS(.Child:HOVER,.never)",
         "<body><section id='parent' class='Parent'><div id='child' class='Child'></div></section></body>", "parent", "child", false},
        {".Outer:hover .Item", ".Outer:HOVER .Item",
         "<body><section id='outer' class='Outer'><div id='Target' class='Item'></div></section></body>", "Target", "outer", false},
        {".Toggle:hover + .Item", ".Toggle:HOVER + .Item",
         "<body><i id='toggle' class='Toggle'></i><div id='Target' class='Item'></div></body>", "Target", "toggle", false}
    };
    for (const auto& c : nested) run("nested-state/" + std::string(c.changed), [&] {
        const auto lower = state_sequence(c.html, c.lower, c.subject, c.changing, AETHER_STATE_HOVER, c.inverted);
        const auto changed = state_sequence(c.html, canonical_only ? c.lower : c.changed, c.subject, c.changing, AETHER_STATE_HOVER, c.inverted);
        check(lower == changed, "canonical/variant nested state sequence");
    });
    for (const char* pseudo : {"before", "after"}) for (const auto& name : variants(pseudo)) run("pseudo-state/" + name, [&] {
        const auto lower = state_sequence(identity_html, std::string(".Item:hover::") + pseudo,
                                          "Target", "Target", AETHER_STATE_HOVER, false, pseudo);
        const auto changed = state_sequence(identity_html, std::string(canonical_only ? ".Item:hover::" : ".Item:HOVER::") + name,
                                            "Target", "Target", AETHER_STATE_HOVER, false, pseudo);
        check(lower == changed, "canonical/variant generated state sequence");
    });
    for (const char* selector : {".Item:hover:focus", ".Item:HOVER:FOCUS", ".Item:\\48 OVER:\\46 OCUS"})
        run("combined-state/" + std::string(selector), [&] {
            if (canonical_only && std::string(selector) != ".Item:hover:focus") return;
            Fixture f(identity_html); const auto subject = f.node("Target");
            f.sheet(std::string(selector) + "{opacity:.8}", 1); f.resolve(); f.expect(subject, .2f);
            const int states[] = {AETHER_STATE_HOVER, AETHER_STATE_FOCUS, AETHER_STATE_HOVER | AETHER_STATE_FOCUS,
                                  AETHER_STATE_HOVER | AETHER_STATE_FOCUS, AETHER_STATE_HOVER, AETHER_STATE_NONE};
            float previous = .2f;
            for (const int state : states) {
                aether_element_set_state(subject, state); f.resolve(subject);
                const float expected = state == (AETHER_STATE_HOVER | AETHER_STATE_FOCUS) ? .8f : .2f;
                if (expected != previous) check(f.emitted.count(subject) == 1, "combined state delta emitted");
                f.expect(subject, expected); previous = expected;
            }
        });
}
void invalid_and_recovery() {
    const char* balanced[] = {
        "unknown", "UNKNOWN", "hover()", "HOVER()", "empty(1)", "EMPTY(1)", "is", "IS", "not()", "NOT()",
        "nth-child(bogus)", "NTH-CHILD(bogus)", "nth-child(2n of .Item)", "NTH-CHILD(2n of .Item)",
        "not(.Item,:unknown)", "NOT(.Item,:UNKNOWN)", "has(> .Child)", "HAS(> .Child)",
        "hover-extra", "HOVER-extra", "\\0 HOVER", "\\d800 HOVER", "\\110000 HOVER", "\\3a HOVER", "IS\\28 (.Item)",
        "\xc5\xbf" "ELECTED", "\\17f ELECTED", "\xc4\xb0S(.Item)", "\\130 S(.Item)", "h\xce\xbfver", "h\\3bf ver"
    };
    for (const auto pseudo : balanced) run("balanced-recovery/" + std::string(pseudo), [&] {
        Fixture f("<body><div id='Target' class='Item'></div><div id='next' class='continuation'></div></body>");
        f.sheet(std::string(".Item:") + pseudo + "{opacity:.8}.continuation{opacity:.6}", 1);
        f.resolve(); f.expect(f.node("Target"), .2f); f.expect(f.node("next"), .6f);
    });
    for (const char* pseudo : {"where( )", "WHERE( )", "is(:unknown)", "IS(:UNKNOWN)"})
        run("empty-forgiving-continuation/" + std::string(pseudo), [&] {
            Fixture f("<body><div id='Target' class='Item'></div><div id='next' class='continuation'></div></body>");
            f.sheet(std::string(".Item:") + pseudo + "{opacity:.8}.continuation{opacity:.6}", 2);
            f.resolve(); f.expect(f.node("Target"), .2f); f.expect(f.node("next"), .6f);
        });
    for (const char* spelling : {"is", "IS", "\\49 S"}) run("unclosed-prelude/" + std::string(spelling), [&] {
        Fixture f("<body><div id='Target' class='Item'></div><div id='next' class='continuation'></div></body>");
        // Existing scanner baseline: an unclosed prelude prevents finding either rule block.
        f.sheet(std::string(".Item:") + spelling + "(.Item{opacity:.8}.continuation{opacity:.6}", 0);
        f.resolve(); f.expect(f.node("Target"), .2f); f.expect(f.node("next"), .2f);
    });
    struct ListCase { const char* selector; int rules; bool matches; };
    const ListCase lists[] = {
        {"IS(.Item,:UNKNOWN)", 1, true}, {"WHERE(#Bad:UNKNOWN,.Item)", 1, true},
        {"IS(:NOT(.Item,:UNKNOWN),.fallback)", 1, false}, {"NOT(:IS(.Item,:UNKNOWN))", 1, false},
        {"IS(#Target:EMPTY,.fallback)", 1, true}, {"HAS(> .Child,.Child)", 0, false}
    };
    for (const auto& c : lists) run("list-policy/" + std::string(c.selector), [&] {
        Fixture f(identity_html); f.sheet(std::string(".Item:") + c.selector + "{opacity:.8}", c.rules);
        f.resolve(); f.expect(f.node("Target"), c.matches ? .8f : .2f);
    });
}
} // namespace
int main(int argc, char** argv) {
    canonical_only = argc == 2 && std::string(argv[1]) == "--canonical-only";
    run("ABI version/capability", [] {
        check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR, "ABI major");
        check((aether_abi_capabilities() & AETHER_CAPABILITY_STYLE_RUNTIME) != 0, "style runtime capability");
    });
    matching_matrix(); state_changes();
    if (!canonical_only) { cascade_and_arguments(); invalid_and_recovery(); }
    std::printf("pseudo-name-runtime: %d cases, %d checks, %d failures\n", cases, checks, failures);
    return failures ? 1 : 0;
}
