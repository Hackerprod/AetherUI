#include <aether/aether.h>
#include "../src/style_runtime/style_runtime_types.h"
#include "../src/style_runtime/css_variable_tokens.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_computed_style_t style{};
    bool copied = false;
    int var_errors = 0;
    int diagnostics = 0;
    int value_errors = 0;
    std::string error_value;
    std::string text;
    static void diagnostic(const char* code, const char*, const char* value, const char*, void* user) {
        ++static_cast<fixture*>(user)->diagnostics;
        if (std::strcmp(code, "AETHER-CSS-VALUE") == 0) { ++static_cast<fixture*>(user)->value_errors; static_cast<fixture*>(user)->error_value = value ? value : ""; }
        if (std::strcmp(code, "AETHER-CSS-VAR") == 0) ++static_cast<fixture*>(user)->var_errors;
    }
    ~fixture() {
        if (copied) aether_computed_style_free(&style);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void load(const std::string& declaration, bool inlined = false, const std::string& extra = "") {
        const auto html = "<!doctype html><html><body><div id='target'></div></body></html>";
        require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, html) == AETHER_OK, "document");
        if (inlined) require(aether_element_set_attribute(aether_doc_get_element_by_id(doc, "target"), "style", declaration.c_str()) == AETHER_OK, "inline attribute");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, diagnostic, this, &runtime) == AETHER_OK, "runtime");
        const auto css = "body{font-family:'parent-family';--family:resolved-family}#target{" +
            (inlined ? std::string() : declaration) + "}" + extra;
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "variable-strings", &sheet, nullptr) == AETHER_OK, "stylesheet");
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime, reinterpret_cast<aether_node_t>(doc), &updates);
        require(count >= 0, "resolve");
        // Reacquire after generated pseudo creation. get_element_by_id returns the host first.
        const auto target = aether_doc_get_element_by_id(doc, "target");
        std::vector<aether_node_t> generated;
        for (int i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "get_update");
            if (node == target) {
                require(aether_computed_style_copy(handle, &style) == AETHER_OK, "style_copy");
                copied = true;
            }
            if (node != target && aether_node_get_parent(node) == target) generated.push_back(node);
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
        require(target && copied, "target style");
        char* tag = aether_element_get_tag_name(target);
        require(tag && std::strcmp(tag, "div") == 0, "target tag");
        aether_string_free(tag);
        char* id = aether_element_get_attribute(target, "id");
        require(id && std::strcmp(id, "target") == 0, "target ID");
        aether_string_free(id);
        // Public child enumeration hides generated nodes. Fresh update handles
        // expose the before/after nodes in source traversal order.
        std::vector<aether_node_t> pending(generated.rbegin(), generated.rend());
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            char* value = aether_node_get_text_content(current);
            if (value) text += value;
            aether_string_free(value);
            for (int i = aether_node_get_child_count(current); i > 0; --i)
                pending.push_back(aether_node_get_child(current, i - 1));
        }
    }
};
void family(const std::string& css, const char* expected, bool var_error = false, bool inlined = false) {
    fixture f;
    f.load(css, inlined);
    require(std::memchr(f.style.font_family, 0, sizeof(f.style.font_family)), "terminated family");
    require(f.style.font_family == std::string(expected), css + " expected=" + expected + " actual=" + f.style.font_family);
    require((f.var_errors > 0) == var_error, css + " diagnostic");
    if (!var_error) require(f.diagnostics == 0, css + " unexpected diagnostic");
}
std::string chain(int n) {
    std::string css = "--a0:var(--missing,);";
    for (int i = 1; i <= n; ++i) css += "--a" + std::to_string(i) + ":var(--a" + std::to_string(i-1) + ")var(--a" + std::to_string(i-1) + ");";
    return css;
}
void integration() {
    // These go through actual var() substitution, not only serialized calc text.
    struct width_case { const char* declaration; int unit; };
    const width_case widths[] = {
        {"--n:2;width:calc(var(--n) * 1px)", AETHER_CSS_UNIT_LINEAR},
        {"--n:2;width:calc(1px +var(--n)*1px)", AETHER_CSS_UNIT_AUTO},
        {"--n:2;--p:+;width:calc(1px var(--p)var(--n)*1px)", AETHER_CSS_UNIT_AUTO},
        {"--n:2;--p:+;width:calc(1px var(--p) var(--n)*1px)", AETHER_CSS_UNIT_LINEAR},
        {"--f:calc;width:var(--f)(10px)", AETHER_CSS_UNIT_AUTO},
        {"--n:10;width:var(--n)px", AETHER_CSS_UNIT_AUTO},
        {"--n:10;width:var(--n)%", AETHER_CSS_UNIT_AUTO},
        {"--n:1;width:calc(var(--n)px)", AETHER_CSS_UNIT_AUTO},
    };
    for (bool inlined : {false, true}) for (const auto& example : widths) {
        fixture f;
        f.load(std::string("width:73px;") + example.declaration, inlined);
        require(f.style.width.unit == example.unit, std::string("computed width tokens: ") + example.declaration);
    }
    family("font-family:'plain-family'", "plain-family");
    family("font-family:'var(--missing)'", "var(--missing)");
    family("font-family:'var(--family)'", "var(--family)");
    family("font-family:var(--family)", "resolved-family");
    family("font-family:var(--missing,fallback-family)", "fallback-family");
    family("--q:'var(--q)';font-family:var(--q)", "var(--q)");
    family("font-family:var(--missing,'var(--family)')", "var(--family)");
    family("font-family:var(--missing,var(--family))", "resolved-family");
    family("font-family:'var(--missing)',var(--family)", "var(--missing)");
    family("font-family:'/*var(--missing)*/'", "/*var(--missing)*/");
    family("font-family:/*var(--missing) ) , */var(--family)", "resolved-family");
    family("font-family:'var(--missing)'", "var(--missing)", false, true);
    {
        fixture f;
        f.load("--q:'var(--family)'", false, "#target::before{content:'var(--missing)'}#target::after{content:var(--q)}");
        require(f.text == "var(--missing)var(--family)" && f.var_errors == 0, "G7 content actual=" + f.text + " diagnostics=" + std::to_string(f.var_errors));
    }
    {
        fixture f;
        f.load("background:url(\"var(--missing)\")");
        require(f.style.background_layers.count == 1, "G8 URL count");
        const auto& layer = f.style.background_layers.items[0];
        require(layer.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && layer.image_source &&
            std::strcmp(layer.image_source, "var(--missing)") == 0, "G8 URL metadata");
    }
    {
        fixture f;
        f.load("--bg:linear-gradient(90deg,#000,#fff);background:var(--bg)");
        require(f.style.background_layers.count == 1, "G8 gradient count");
        const auto& layer = f.style.background_layers.items[0];
        require(layer.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT && layer.gradient.stop_count == 2, "G8 gradient metadata");
    }
    family("--a:var(--a,inner-family);font-family:var(--a,outer-family)", "outer-family");
    family("--a:var(--b);--b:var(--a);font-family:var(--a,outer-family)", "outer-family");
    family("--bad:var(--bad);--ok:var(--bad,recovered-family);font-family:var(--ok,outer-family)", "recovered-family");
    family("font-family:var(--missing,)fallback-family", "fallback-family");
    family("font-family:var(--missing)", "parent-family", true);
    family(chain(2) + "font-family:var(--a2)small-family", "small-family");
    family(chain(20) + "font-family:var(--a20,outer-family)", "parent-family", true);
}
void string_decoder() {
    struct Case { std::string source, expected; };
    const std::vector<Case> cases = {
        {R"("AB")", "AB"}, {"'AB'", "AB"}, {R"("A\42")", "AB"},
        {R"("A\000042 C")", "ABC"}, {R"("A\42  C")", "AB C"},
        {R"("A\"B")", "A\"B"}, {R"('A\'B')", "A'B"}, {R"("A\\B")", "A\\B"},
        {R"("\5c 42")", "\\42"}, {R"("A\2c B")", "A,B"}, {R"("A\a B")", "A\nB"},
        {R"("\0")", "\xef\xbf\xbd"}, {R"("\d800")", "\xef\xbf\xbd"},
        {R"("\110000")", "\xef\xbf\xbd"}, {R"("\e9")", "\xc3\xa9"},
        {R"("\1f642")", "\xf0\x9f\x99\x82"}, {R"("\000041B")", "AB"},
        {"'  AB  '", "  AB  "}, {"'/*x*/var(--q)'", "/*x*/var(--q)"}, {"''", ""},
        {"\"A'B\"", "A'B"}, {"'A\"B'", "A\"B"}, {"'\xc3\xa9'", "\xc3\xa9"},
        {"'\\\xc3\xa9'", "\xc3\xa9"}, {std::string("'A\0B'", 5), "A\xef\xbf\xbd" "B"},
        {std::string("'A\\\0B'", 6), "A\xef\xbf\xbd" "B"}, {"'A\tB'", "A\tB"}
    };
    auto success = [](const std::string& source, const std::string& expected) {
        std::string out = "sentinel";
        css_variable_budget measured;
        css_variable_lexer lexer(source.data(), source.size(), 0, &measured);
        require(lexer.decode_string(out) && out == expected, "string exact semantic bytes");
        const size_t cost = 1048576 - measured.remaining;
        require(cost > 0, "nonzero measured string cost");
        out = source;
        css_variable_budget exact{cost};
        css_variable_lexer alias(out.data(), out.size(), 0, &exact);
        require(alias.decode_string(out) && out == expected && exact.remaining == 0, "string exact budget and alias");
        out = source;
        css_variable_budget short_budget{cost - 1};
        css_variable_lexer short_alias(out.data(), out.size(), 0, &short_budget);
        require(!short_alias.decode_string(out) && out == source && short_budget.exhausted, "string short budget preserves alias");
    };
    for (const auto& c : cases) success(c.source, c.expected);
    for (const std::string line : {"\n", "\r", "\r\n", "\f"}) {
        success("'A\\" + line + "B'", "AB");
        success("'A\\42" + line + "C'", "ABC");
    }
    for (const std::string bad : {"", "AB", "'AB", "'A\\", "'A'B", "'A\nB'", "'A\rB'", "'A\fB'",
                                 "'\xc0\xaf'", "'\xed\xa0\x80'", "'\xf4\x90\x80\x80'", "'\xe2\x82'", "'\\\xc0\xaf'"}) {
        std::string out = "sentinel";
        css_variable_lexer lexer(bad.data(), bad.size());
        require(!lexer.decode_string(out) && out == "sentinel", "invalid string preserves output");
        out = bad;
        css_variable_lexer alias(out.data(), out.size());
        require(!alias.decode_string(out) && out == bad, "invalid string preserves aliased source");
    }
}
void escaped_families() {
    struct Case { const char* css; const char* expected; };
    const Case cases[] = {
        {R"(font-family:"A\42")", "AB"}, {R"(font-family:'A\000042 C')", "ABC"},
        {R"(font-family:"A\"B")", "A\"B"}, {R"(font-family:'A\\B')", "A\\B"},
        {"font-family:'A\\\nB'", "AB"}, {R"(--f:'A\42';font-family:var(--f))", "AB"},
        {"font-family:'AB'", "AB"}, {R"(font-family:A\42)", "AB"},
        {R"(font-family:var(--missing,'A\42'))", "AB"},
        {R"(--f:'A\42';--g:var(--f);font-family:var(--g))", "AB"},
        {R"(--f:'\5c 42';font-family:var(--f))", "\\42"},
        {R"(--f:'\76 ar(--missing)';font-family:var(--f))", "var(--missing)"},
        {R"(font-family:'A\2c B',Second)", "A,B"}, {"font-family:'A,B',Second", "A,B"},
        {"font-family:'AB',123", "AB"}, {"font-family:'AB',", "AB"},
        {"font-family:Times/**/New", "Times New"}, {"--f:Times;--g:New;font-family:var(--f)var(--g)", "Times New"},
        {"font-family:'initial'", "initial"}, {"font-family:'inherit'", "inherit"},
        {R"(font-family:'\69 nitial')", "initial"}, {"font-family:'serif'", "serif"},
        {"font-family:'A\"B'", "A\"B"}, {"font-family:\"A'B\"", "A'B"},
        {R"(font-family:'\e9\1f642')", "\xc3\xa9\xf0\x9f\x99\x82"},
        {"font-family:'previous';font-family:'A' extra", "previous"},
        {"font-family:'previous';font-family:''", "previous"},
        {"font-family:'previous';font-family:'\\\n'", "previous"},
    };
    for (bool inlined : {false, true}) {
        for (const auto& c : cases) family(c.css, c.expected, false, inlined);
        fixture f;
        f.load("--f:'A' extra;font-family:previous;font-family:var(--f)", inlined);
        require(std::string(f.style.font_family) == "parent-family", "deferred invalid family inherits rather than reviving prior declaration");
        require(f.value_errors == 1 && f.error_value == "'A' extra", "deferred rejection preserves original diagnostic bytes");
        for (size_t n : {size_t(62), size_t(63), size_t(64)}) {
            std::string escaped;
            for (size_t i = 0; i < n; ++i) escaped += "\\41 ";
            fixture length;
            length.load("font-family:'" + escaped + "'", inlined);
            require(sizeof(length.style.font_family) == 64 && length.style.font_family[63] == 0 &&
                std::string(length.style.font_family) == std::string(std::min(size_t(63), n), 'A'), "existing 63 byte ABI cut");
        }
        fixture unicode;
        unicode.load("font-family:'" + std::string(62, 'A') + "\\e9'", inlined);
        require(std::string(unicode.style.font_family) == std::string(62, 'A') + "\xc3" &&
            unicode.style.font_family[63] == 0, "existing ABI cut may split UTF-8");
    }
}

void escaped_background_variables() {
    const char* declarations[] = {
        R"(--image:url(hero\2e png);background-image:var(--image))",
        R"(--image:url('hero\.png');background:var(--image) center / cover no-repeat)",
        R"(background-image:var(--missing,url('hero\00002e png')))",
        "--image:url('hero\\2e\npng');background-image:var(--image)",
        "--image:url('hero\\2e\r\npng');background:var(--image)",
        "--image:url('he\\\nro.png');background-image:var(--image)",
    };
    for (const auto* declaration : declarations) {
        fixture f;
        f.load(declaration);
        require(f.style.background_layers.count == 1 && f.style.background_layers.items, declaration);
        const auto& layer = f.style.background_layers.items[0];
        require(layer.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && layer.image_source &&
            std::strcmp(layer.image_source, "hero.png") == 0, "variable background uses decoded identity");
        require(f.var_errors == 0, "valid escaped background has no variable error");
    }
    {
        fixture f;
        f.load(R"(--image:url('\5c 2e');background:var(--image))");
        require(f.style.background_layers.count == 1 && f.style.background_layers.items &&
            f.style.background_layers.items[0].image_source &&
            std::strcmp(f.style.background_layers.items[0].image_source, "\\2e") == 0,
            "variable substitution and shorthand must not decode the same URL twice");
    }
    {
        fixture f;
        f.load("--image:url('');background-image:url(previous.png);background-image:var(--image,url(fallback.png))");
        require(f.style.background_layers.count == 0,
            "selected invalid variable URL neither restores previous declaration nor chooses fallback");
    }
}
void string_hex_spans() {
    using kind = css_variable_token_kind;
    using category = css_variable_serialization;
    for (const std::string separator : {" ", "\t", "\n", "\r", "\r\n", "\f"}) {
        const std::string source = "'hero\\2e" + separator + "png'";
        css_variable_lexer lexer(source.data(), source.size());
        const auto token = lexer.next();
        require(token.kind == kind::other && token.serialization == category::string &&
            token.begin == 0 && token.end == source.size() && !token.eof_recoverable &&
            token.escape_tail == css_variable_escape_tail::none && lexer.next().kind == kind::end,
            "hex terminator stays within one unchanged string span");
        custom_property_scope_s scope;
        scope.local["--image"] = source;
        std::string out;
        require(variable_resolve("var(--image)", &scope, 32, 65536, out) && out == source,
            "string serialization preserves original hex and terminator bytes");
        require(variable_resolve("var(--image)'next'", &scope, 32, 65536, out) && out == source + "'next'",
            "string token joining must not insert identifier escape-tail bytes");
    }
    for (const std::string source : {"'hero\\2e", "'hero\\2e\r"}) {
        css_variable_lexer lexer(source.data(), source.size());
        const auto token = lexer.next();
        require(token.kind == kind::error && token.serialization == category::string &&
            token.eof_recoverable && token.begin == 0 && token.end == source.size() &&
            token.escape_tail == css_variable_escape_tail::none && lexer.next().kind == kind::end,
            "incomplete hex string retains recovery, category, span and no escape tail");
        require(validate_pending_declaration_value(source), "generic EOF string syntax remains recoverable");
        custom_property_scope_s scope;
        scope.local["--image"] = source;
        std::string out = "sentinel";
        require(!variable_resolve("var(--image)", &scope, 32, 65536, out) && out.empty(),
            "legacy resolver still rejects incomplete strings without publishing serialized fragments");
    }
    // Do not turn this hex-only scanner fix into stricter general string UTF-8 admission.
    for (const std::string source : {"'\xc0\xaf'", "'\\\xc0\xaf'"}) {
        css_variable_lexer lexer(source.data(), source.size());
        const auto token = lexer.next();
        require(token.kind == kind::other && token.serialization == category::string && token.end == source.size(),
            "general string scanner retains prior byte admission");
    }
}
void lexical() {
    using kind = css_variable_token_kind;
    const char* opaque[] = {"'var(--x)'", "\"var(--x)\"", "\"a\\\"var(--x)\"", "xvar(--x)", "#var(--x)", "@var(--x)", "1var(--x)", "var (--x)", "var/**/(--x)", "url(var(--x))"};
    for (const auto* s : opaque) require(!variable_has_reference(s), std::string("opaque ") + s);
    for (const auto* s : {"\"a\\\\\" var(--family)", "VAR(--family)", "v\\61r(--family)"}) require(variable_has_reference(s), std::string("reference ") + s);
    // Every token span must reconstruct the input exactly.
    const std::string source = "'var(--x)' /* hi */ v\\61r(--family) url(\"var(--x)\")";
    css_variable_lexer lexer(source.data(), source.size());
    std::string restored;
    for (;;) {
        const auto t = lexer.next();
        require(t.kind != kind::error, "span lexer error");
        if (t.kind == kind::end) break;
        require(t.end > t.begin, "monotonic cursor");
        restored.append(source, t.begin, t.end - t.begin);
    }
    require(restored == source, "byte-preserving spans");
    for (const std::string s : {"'var(--x)", "/* var(--x)"}) {
        css_variable_lexer broken(s.data(), s.size());
        require(broken.next().kind == kind::error && !variable_has_reference(s), "truncated opaque token");
    }
}
void private_resolver() {
    custom_property_scope_s scope;
    scope.local = {{"--a", "var(--b)"}, {"--b", "leaf"}, {"--x", "a"}, {"--q", "decoded-name"}};
    std::string out = "sentinel";
    auto check = [&](const char* source, int depth, int length, bool valid, const std::string& expected) {
        require(variable_resolve(source, &scope, depth, length, out) == valid && out == expected, "private depth/length/alias contract");
    };
    check("var(--a)", 1, 65536, false, "");
    check("var(--a)", 2, 65536, true, "leaf");
    check("abc", 2, 3, true, "abc");
    check("abcd", 2, 3, false, "");
    // Substitution preserves ident tokens; the four-byte comment counts.
    check("var(--x)zz", 2, 7, true, "a/**/zz");
    check("var(--x)zz", 2, 6, false, "");
    check("var(--x)zz", 2, 3, false, "");
    check("var(--x)zz", 2, 2, false, "");
    check(nullptr, 2, 3, false, "");
    // Private scopes hold decoded keys; CSS spelling is not key identity.
    check("var(--\\71)", 2, 65536, true, "decoded-name");
    check("var(--q)", 2, 65536, true, "decoded-name");
    check("v\\61r(--x)", 2, 65536, true, "a");
    check("VAR(--x)", 2, 65536, true, "a");
    for (const auto* broken : {"var(--x", "var(--x,[)", "var(--missing,'bad)", "var(--x extra)", "ordinary(["}) check(broken, 32, 65536, false, "");
    out = "var(--x)";
    check(out.c_str(), 2, 65536, true, "a");
    out = "var(--missing)";
    check(out.c_str(), 2, 65536, false, "");
    scope.local["--d130"] = "leaf";
    for (int i = 1; i < 130; ++i) scope.local["--d" + std::to_string(i)] = "var(--d" + std::to_string(i + 1) + ")";
    check("var(--d1)", 130, 65536, true, "leaf");
    check("var(--d1)", 129, 65536, false, "");
    scope.local["--a0"] = "var(--missing,)";
    for (int i = 1; i <= 20; ++i) scope.local["--a" + std::to_string(i)] = "var(--a" + std::to_string(i - 1) + ")var(--a" + std::to_string(i - 1) + ")";
    check("var(--a2)", 32, 65536, true, "");
    check("var(--a20,outer)", 32, 65536, false, "");

    // A key is not expanded value data: a long decoded key can name one byte.
    const std::string long_key = "--" + std::string(300, 'j');
    scope.local[long_key] = "a";
    check(("var(" + long_key + ")").c_str(), 2, 1, true, "a");
    // Exhaustion must abort the entire result, not take the cheap fallback.
    for (const std::string scalar : {"j", "\\00006a", "\\00fffd", "\\01f642"}) {
        std::string huge = "var(--";
        for (int i = 0; i < (scalar == "j" ? 200000 : 45000); ++i) huge += scalar;
        huge += ",outer)";
        check(huge.c_str(), 32, 65536, false, "");
    }
}
void identifier_decoder() {
    auto decode = [](const std::string& source, css_variable_budget& budget, std::string& out) {
        std::string pending;
        css_variable_lexer lexer(source.data(), source.size(), 0, &budget);
        const bool ok = lexer.decode_identifier([](void* data, const char* bytes, size_t size) {
            static_cast<std::string*>(data)->append(bytes, size);
            return true;
        }, &pending);
        out = ok ? std::move(pending) : std::string();
        return ok;
    };
    std::string out;
    for (const auto* input : {"--j", "--\\6A", "--\\6a", "--\\00006a", "\\2d\\2d j"}) {
        css_variable_budget budget;
        require(decode(input, budget, out) && out == "--j", "decoded identifier equivalence");
    }
    // EOF after a name escape is U+FFFD, but cannot close an incomplete var().
    css_variable_budget eof_budget;
    require(decode("--x\\", eof_budget, out) && out == "--x\xef\xbf\xbd", "identifier EOF escape");
    for (const std::string input : std::vector<std::string>{"--x\\\n", "--x extra", "--x" + std::string("\xc0\xaf"),
                                  "--x" + std::string("\xed\xa0\x80"), "--x" + std::string("\xf4\x90\x80\x80")}) {
        css_variable_budget budget;
        require(!decode(input, budget, out) && out.empty(), "invalid identifier must not publish a partial key");
    }
    const std::string source = "\\2d\\2d \\01f642\\00006a\r\nZ";
    css_variable_budget measured;
    require(decode(source, measured, out) && out == "--\xf0\x9f\x99\x82jZ", "UTF-8 and CRLF decoder");
    const size_t cost = 1048576 - measured.remaining;
    require(cost > source.size(), "lookahead and UTF-8 source reads must be charged");
    css_variable_budget exact{cost};
    require(decode(source, exact, out) && exact.remaining == 0, "exact decoder source budget");
    css_variable_budget short_budget{cost - 1};
    require(!decode(source, short_budget, out) && short_budget.exhausted && out.empty(), "decoder budget failure is atomic at caller");
    // Token recognition and later key decoding are separate charged passes.
    css_variable_budget passes;
    css_variable_lexer lexer(source.data(), source.size(), 0, &passes);
    require(lexer.next().kind == css_variable_token_kind::ident, "budgeted token recognition");
    const auto before_decode = passes.remaining;
    require(decode(source, passes, out) && before_decode - passes.remaining == cost, "second pass charges all source reads again");
    for (const std::string input : std::vector<std::string>{source, "--x\\ ", "--x\\3b ;height:2px", "--\\e9", "--\xf0\x9f\x99\x82"}) {
        css_variable_lexer spans(input.data(), input.size());
        std::string restored;
        for (;;) {
            const auto token = spans.next();
            if (token.kind == css_variable_token_kind::end) break;
            require(token.kind != css_variable_token_kind::error && token.end > token.begin, "decoded token span progress");
            restored.append(input, token.begin, token.end - token.begin);
        }
        require(restored == input, "decoded token spans preserve source bytes");
    }
}
}
int main() {
    try { string_decoder(); escaped_families(); lexical(); string_hex_spans(); identifier_decoder(); private_resolver(); integration(); escaped_background_variables(); std::puts("PASS CSS variable strings G1-G12, decoded identifiers and measured5"); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL CSS variable strings: %s\n", e.what()); return 1; }
}
