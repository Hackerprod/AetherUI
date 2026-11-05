#include <aether/aether.h>
#include "../src/style_runtime/style_runtime_types.h"
#include "../src/style_runtime/css_variable_tokens.h"
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
    std::string text;
    static void diagnostic(const char* code, const char*, const char*, const char*, void* user) {
        if (std::strcmp(code, "AETHER-CSS-VAR") == 0) ++static_cast<fixture*>(user)->var_errors;
    }
    ~fixture() {
        if (copied) aether_computed_style_free(&style);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void load(const std::string& declaration, bool inlined = false, const std::string& extra = "") {
        const auto html = "<!doctype html><html><body><div id='target'" +
            (inlined ? " style=\"" + declaration + "\"" : std::string()) + "></div></body></html>";
        require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "document");
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
    require(f.style.font_family == std::string(expected), css + " expected=" + expected + " actual=" + f.style.font_family);
    require((f.var_errors > 0) == var_error, css + " diagnostic");
}
std::string chain(int n) {
    std::string css = "--a0:var(--missing,);";
    for (int i = 1; i <= n; ++i) css += "--a" + std::to_string(i) + ":var(--a" + std::to_string(i-1) + ")var(--a" + std::to_string(i-1) + ");";
    return css;
}
void integration() {
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
    check("var(--x)zz", 2, 3, true, "azz");
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
    try { lexical(); identifier_decoder(); private_resolver(); integration(); std::puts("PASS CSS variable strings G1-G12, decoded identifiers and measured5"); return 0; }
    catch (const std::exception& e) { std::fprintf(stderr, "FAIL CSS variable strings: %s\n", e.what()); return 1; }
}
