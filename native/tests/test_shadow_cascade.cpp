// Public ABI regression; deliberately no assert(), including under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace {
int checks = 0, failures = 0;
void need(bool ok, const char* s) {
    if (!ok) {
        std::fprintf(stderr, "HARNESS: %s\n", s);
        std::exit(2);
    }
}
void check(bool ok, const std::string& s) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", s.c_str());
    }
}
struct Diagnostic {
    std::string code, property, value, message;
};
void diagnostic(const char* c, const char* p, const char* v, const char* m, void* data) {
    static_cast<std::vector<Diagnostic>*>(data)->push_back(
        {c ? c : "", p ? p : "", v ? v : "", m ? m : ""});
}
std::vector<float> signature(const aether_shadow_list& list) {
    need(list.count >= 0 && (list.count == 0 || list.items), "shadow list storage");
    std::vector<float> out{float(list.count)};
    for (int i = 0; i < list.count; ++i) {
        auto s = list.items[i];
        out.insert(out.end(), {s.offset_x, s.offset_y, s.blur_radius, s.spread_radius,
                               float(s.inset), s.color.r, s.color.g, s.color.b, s.color.a});
    }
    return out;
}
// Expected values use independently valid declarations, never the rejected input.
void run(const std::string& prop, const std::string& rules, const std::string& inlined,
         const std::string& expected, const std::string& label, bool dedicated = false,
         bool root = false, const std::string& diagnostic_value = "",
         const std::string& sibling_expected = "", bool exact_fields = false,
         const std::string& diagnostic_message = "") {
    std::vector<Diagnostic> diagnostics;
    aether_doc_t doc{};
    aether_style_runtime_t rt{};
    need(aether_doc_create(&doc) == AETHER_OK, "create doc");
    const auto html = root
                          ? "<html id='actual' style='" + inlined +
                                "'><body><div id='expected'></div></body></html>"
                          : "<html><body><div id='parent'><div id='actual' class='target' style='" +
                                inlined + "'></div><div id='expected'></div><div id='sibling'></div>"
                                "<div id='sibling-expected'></div></div></body></html>";
    need(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
    aether_style_runtime_options_t opts{};
    opts.max_variable_depth = 32;
    opts.max_variable_expansion_length = 65536;
    aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
    need(aether_style_runtime_create(&opts, &env, diagnostic, &diagnostics, &rt) == AETHER_OK,
         "runtime");
    const auto css =
        "#parent{color:#2468ac;text-shadow:7px 8px currentColor}#actual,#expected{color:#2468ac}" +
        rules + "#expected{" + prop + ":" + expected + "}" +
        (sibling_expected.empty() ? "" : "#sibling-expected{" + prop + ":" + sibling_expected + "}");
    int64_t sheet{};
    need(aether_style_runtime_add_stylesheet(rt, css.c_str(), 2, "shadow-cascade.css", &sheet,
                                             nullptr) == AETHER_OK,
         "sheet");
    aether_style_update_t* updates{};
    int n =
        aether_style_runtime_resolve_subtree(rt, aether_doc_get_document_element(doc), &updates);
    need(n > 0 && updates, "resolve");
    aether_computed_style_t actual{}, want{}, sibling{}, sibling_want{};
    bool found_sibling = false, found_sibling_want = false;
    need(aether_doc_get_element_by_id(doc, "actual") &&
             aether_doc_get_element_by_id(doc, "expected"), "target nodes");
    bool found = false, foundwant = false;
    for (int i = 0; i < n; ++i) {
        aether_node_t node{};
        aether_computed_style_handle_t h{};
        need(aether_style_runtime_get_update(updates[i], &node, &h, nullptr) == AETHER_OK,
             "update");
        if (node == aether_doc_get_element_by_id(doc, "actual")) {
            need(aether_computed_style_copy(h, &actual) == AETHER_OK, "actual copy");
            found = true;
        }
        if (node == aether_doc_get_element_by_id(doc, "expected")) {
            need(aether_computed_style_copy(h, &want) == AETHER_OK, "expected copy");
            foundwant = true;
        }
        if (!sibling_expected.empty()) {
            if (node == aether_doc_get_element_by_id(doc, "sibling")) {
                need(aether_computed_style_copy(h, &sibling) == AETHER_OK, "sibling copy");
                found_sibling = true;
            }
            if (node == aether_doc_get_element_by_id(doc, "sibling-expected")) {
                need(aether_computed_style_copy(h, &sibling_want) == AETHER_OK,
                     "sibling expected copy");
                found_sibling_want = true;
            }
        }
        aether_computed_style_handle_destroy(h);
    }
    need(found && foundwant, "target updates");
    auto a = signature(prop == "box-shadow" ? actual.box_shadows : actual.text_shadows);
    const auto& expected_list = prop == "box-shadow" ? want.box_shadows : want.text_shadows;
    need(expected == "none" ? expected_list.count == 0 : expected_list.count > 0,
         "independent expected list is not vacuous");
    auto b = signature(expected_list);
    bool equal = a.size() == b.size();
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        equal = equal && std::isfinite(a[i]) &&
                (exact_fields ? a[i] == b[i] : std::fabs(a[i] - b[i]) < .0001f);
    check(equal, prop + " / " + label);
    if (!sibling_expected.empty()) {
        need(found_sibling && found_sibling_want, "sibling updates");
        const auto& expected_sibling_list =
            prop == "box-shadow" ? sibling_want.box_shadows : sibling_want.text_shadows;
        need(expected_sibling_list.count > 0, "independent sibling expected list is nonempty");
        const auto sa =
            signature(prop == "box-shadow" ? sibling.box_shadows : sibling.text_shadows);
        const auto sb =
            signature(prop == "box-shadow" ? sibling_want.box_shadows : sibling_want.text_shadows);
        bool same = sa.size() == sb.size();
        for (size_t i = 0; i < sa.size() && i < sb.size(); ++i)
            same = same && std::isfinite(sa[i]) && std::fabs(sa[i] - sb[i]) < .0001f;
        check(same, prop + " / sibling retains inherited variable / " + label);
    }
    aether_computed_style_free(&sibling);
    aether_computed_style_free(&sibling_want);
    aether_style_updates_free(updates, n);
    aether_computed_style_free(&actual);
    aether_computed_style_free(&want);
    aether_style_runtime_destroy(rt);
    aether_doc_destroy(doc);
    // All payloads remain owned and usable after compiler, runtime and doc die.
    if (dedicated) {
        int count = 0, exact = 0;
        for (const auto& d : diagnostics)
            if (d.code ==
                    (prop == "text-shadow" ? "AETHER-CSS-TEXT-SHADOW" : "AETHER-CSS-BOX-SHADOW") &&
                d.property == prop) {
                ++count;
                if (d.value == diagnostic_value &&
                    (diagnostic_message.empty() || d.message == diagnostic_message))
                    ++exact;
            }
        check(count == 1, prop + " / exactly one dedicated owned diagnostic / " + label);
        if (!diagnostic_value.empty())
            check(exact == 1, prop + " / exact owned diagnostic property and value / " + label);
    }
    if (exact_fields)
        check(diagnostics.size() == (dedicated ? 1u : 0u),
              prop + " / exact total diagnostic count / " + label);
}
} // namespace
int main() {
    const std::string lower = "2px 3px 4px #e13c72";
    for (const std::string p : {"box-shadow", "text-shadow"}) {
        const std::string invalid_message = p == "box-shadow"
            ? "Each box shadow requires two to four lengths and an optional color or inset keyword."
            : "Each text shadow requires two or three lengths and an optional color.";
        const std::string tiny_unset = p == "text-shadow" ? "7px 8px #2468ac" : "none";
        // Exact nonempty literal fallback versus computed-value unset. The
        // parent text shadow is deliberately distinct from the previous rule.
        for (const std::string v : {"1px 2px -1e-46px currentColor",
                                    "1px 2px red, 3px 4px -1e-46em currentColor"}) {
            run(p, "#actual{" + p + ":" + lower + ";" + p + ":" + v + "}", "", lower,
                "tiny negative literal retains complete fallback", true, false, v, "", true,
                invalid_message);
            run(p, "#actual{--blur:" + v + ";" + p + ":" + lower + ";" + p +
                ":var(--blur,9px 10px blue)}", "", tiny_unset,
                "tiny negative defined variable computes unset", true, false, v, "", true,
                invalid_message);
        }
        run(p, "#actual{font-size:0px;" + p + ":" + lower + ";" + p +
            ":1px 2px -1e-46em}", "", lower, "zero context cannot hide negative literal",
            true, false, "1px 2px -1e-46em", "", true, invalid_message);
        for (const std::string token : {"-0px", "1e-46px"}) {
            const auto value = "1px 2px " + token + " currentColor";
            run(p, "#actual{" + p + ":" + lower + ";" + p + ":" + value + "}", "",
                "1px 2px 0px #2468ac", "accepted literal blur " + token,
                false, false, "", "", true);
            run(p, "#actual{--blur:" + value + ";" + p + ":" + lower + ";" + p +
                ":var(--blur,9px 10px blue)}", "", "1px 2px 0px #2468ac",
                "accepted defined variable blur " + token, false, false, "", "", true);
        }
        run(p, "#actual{" + p + ":" + lower + ";" + p +
            ":var(--absent,1px 2px 1e-46px currentColor)}", "", "1px 2px 0px #2468ac",
            "absent variable uses valid tiny positive fallback", false, false, "", "", true);
        run(p, "#actual{font-size:23px;" + p + ":1px 2px 2em currentColor}", "",
            "1px 2px 46px #2468ac", "positive contextual blur", false, false, "", "", true);
        run(p, "#actual{" + p + ":1px 2px calc(2px - 5px) currentColor}", "",
            "1px 2px 0px #2468ac", "calc negative blur still clamps", false, false, "", "", true);
        std::vector<std::string> bad = {"1px",
                                        "1px 2px -3px red",
                                        "1px 2px red blue",
                                        "1px 2px red,",
                                        " ,1px 2px red",
                                        "none,1px 2px red",
                                        "1px 2px red,,3px 4px blue",
                                        "1px 2px red,3px",
                                        "1px 2px rgb(10%,20,30%)",
                                        "1px 2px rgba(1,2,3,0x1p0)",
                                        "1px 2px rgb(1px,2,3)",
                                        "1px red 2px",
                                        "1px currentColor 2px",
                                        "1px 2px red 3px"};
        if (p == "text-shadow") {
            bad.push_back("inset 1px 2px red");
            bad.push_back("1px 2px 3px 4px red");
        } else {
            bad.push_back("inset inset 1px 2px red");
            bad.push_back("1px inset 2px");
            bad.push_back("1px 2px inset 3px");
        }
        for (const auto& v : bad) {
            // Diagnostics own the declaration value after outer whitespace trimming.
            const auto diagnostic_value = v == " ,1px 2px red" ? ",1px 2px red" : v;
            const auto lo = p + ":" + lower + ";", hi = p + ":" + v + ";";
            run(p, "#actual{" + lo + hi + "}", "", lower, "duplicate literal: " + v, true, false, diagnostic_value);
            run(p, "#actual{" + lo + "}#actual{" + hi + "}", "", lower, "later rule literal: " + v,
                true, false, diagnostic_value);
            run(p, ".target{" + lo + "}#actual{" + hi + "}", "", lower, "specificity literal: " + v,
                true, false, diagnostic_value);
            run(p, "#actual{" + lo + p + ":" + v + "!important}", "", lower,
                "important literal: " + v, true, false, diagnostic_value);
            run(p, "#actual{" + lo + "}", hi, lower, "inline literal: " + v, true, false, diagnostic_value);
            const auto deferred = "--bad:" + v + ";" + lo + p + ":var(--bad, 9px 10px #00ff00);";
            const auto unset = p == "text-shadow" ? "7px 8px #2468ac" : "none";
            run(p, "#actual{" + deferred + "}", "", unset, "defined invalid ignores fallback: " + v,
                true);
            run(p,
                ".target{" + lo + "}#actual{--bad:" + v + ";" + p + ":var(--bad,9px 10px #00ff00)}",
                "", unset, "higher specificity deferred unset: " + v, true);
            run(p,
                "#actual{" + lo + "--bad:" + v + ";" + p +
                    ":var(--bad,9px 10px #00ff00)!important}",
                lo, unset, "important deferred beats valid inline: " + v, true);
            run(p, "", deferred, unset, "inline deferred unset: " + v, true);
        }
        const auto unset = p == "text-shadow" ? "7px 8px #2468ac" : "none";
        run(p, "#actual{" + p + ":" + lower + ";" + p + ":var(--missing)}", "", unset,
            "missing variable unset");
        run(p, "#actual{--a:var(--b);--b:var(--a);" + p + ":" + lower + ";" + p + ":var(--a)}", "",
            unset, "cycle unset");
        run(p, "#actual{--a:var(--b);--b:var(--a);" + p + ":var(--a,1px 2px red)}", "",
            "1px 2px red", "cycle uses fallback");
        run(p, "#actual{" + p + ":var(--missing,1px 2px rgb(100%,0%,0%),3px 4px blue)}", "",
            "1px 2px red,3px 4px blue", "list fallback");
        run(p, "#actual{" + p + ":" + lower + ";" + p +
                   ":var(--missing,1px red 2px)}", "", unset,
            "missing variable with split fallback computes unset");
        const auto scoped = "#parent{--lengths:3px 4px 5px}#actual,#sibling{color:#2468ac;" + p +
                            ":var(--lengths,9px 10px)}";
        run(p, scoped, "", "3px 4px 5px #2468ac", "inherited complete length group",
            false, false, "", "3px 4px 5px #2468ac");
        run(p, scoped + "#actual{--lengths:1px red 2px}", "", unset,
            "local invalid override ignores var fallback without contaminating sibling",
            false, false, "", "3px 4px 5px #2468ac");
        run(p, "#actual{--c:#e13c72;" + p + ":var(--c) 2px 3px 4px}", "", lower,
            "variable color before length group");
        run(p, "#actual{--c:#e13c72;" + p + ":2px 3px 4px var(--c)}", "", lower,
            "variable color after length group");
        run(p, "#actual{--c:#e13c72;" + p + ":2px var(--c) 3px}", "", unset,
            "variable color cannot split offsets");
        run(p, "#actual{--bad:1px red 2px;" + p + ":var(--bad,9px 10px blue)}", "", "none",
            "root split group computes initial", false, true);
        run(p, "#actual{--r:100%;" + p + ":1px 2px rgb(var(--r),0%,0%)}", "", "1px 2px red",
            "RGB var tokens");
        run(p, "#actual{" + p + ":1px 2px rgba(255,0,0,50%),3px 4px rgb(+0e2,0,255)}", "",
            "1px 2px rgba(255,0,0,.5),3px 4px blue", "RGB list commas");
        run(p, "#actual{" + p + ":" + lower + ";" + p + ":var(--missing)}", "", "none",
            "root computed invalid is initial", false, true);
    }
    run("box-shadow", "#actual{box-shadow:inset 1px 2px 3px -4px red,5px 6px blue}", "",
        "1px 2px 3px -4px red inset,5px 6px blue", "box accepts inset and negative spread");
    std::printf("Shadow cascade: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
