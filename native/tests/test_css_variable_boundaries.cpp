#include "../src/style_runtime/style_runtime_types.h"
#include "../src/style_runtime/css_variable_tokens.h"
#include "../src/style_runtime/variable_resolver_internal.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
size_t checks = 0;
void require(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
std::string decoded(const std::string& spelling) {
    std::string result;
    css_variable_lexer lexer(spelling.data(), spelling.size());
    require(lexer.decode_identifier([](void* context, const char* bytes, size_t size) {
        static_cast<std::string*>(context)->append(bytes, size);
        return true;
    }, &result), "decode oracle identifier");
    return result;
}
using category = css_variable_serialization;
using sequence = std::vector<std::string>;
void append_tokens(sequence& result, const std::string& source) {
    css_variable_lexer lexer(source.data(), source.size());
    for (;;) {
        const auto t = lexer.next();
        require(t.kind != css_variable_token_kind::error, "oracle lexer error");
        if (t.kind == css_variable_token_kind::end) return;
        if (t.kind == css_variable_token_kind::comment) continue;
        if (t.kind == css_variable_token_kind::whitespace) {
            if (result.empty() || result.back() != "W") result.push_back("W");
            continue;
        }
        std::string value = source.substr(t.begin, t.end - t.begin);
        if (t.serialization == category::ident) value = decoded(value);
        else if (t.serialization == category::function) value = decoded(value.substr(0, value.size() - 1));
        else if (t.serialization == category::dimension) {
            char* end = nullptr;
            (void)std::strtod(value.c_str(), &end);
            const auto split = static_cast<size_t>(end - value.c_str());
            value = value.substr(0, split) + ":" + decoded(value.substr(split));
        }
        result.push_back(std::to_string(static_cast<int>(t.serialization)) + ":" + value);
    }
}
sequence tokens(const std::string& source) { sequence result; append_tokens(result, source); return result; }
std::string resolve(const std::string& source, custom_property_scope_s& scope, int limit = 65536) {
    std::string out = "old output";
    require(variable_resolve(source.c_str(), &scope, 32, limit, out), "resolve: " + source + " left=" + scope.local["--a"] + " right=" + scope.local["--b"]);
    return out;
}
void expect(const std::string& left, const std::string& right, const std::string& expected) {
    custom_property_scope_s scope;
    scope.local = {{"--a", left}, {"--b", right}};
    const auto actual = resolve("var(--a)var(--b)", scope);
    require(actual == expected, "independent bytes: " + left + " | " + right + " -> " + actual);
}
void independent_examples() {
    expect("10", "px", "10/**/px");
    expect("10", "%", "10/**/%");
    expect("a", "zz", "a/**/zz");
    expect("7px", "11px", "7px/**/11px");
    expect("7%", "11px", "7%11px");
    expect("+", "2", "+/**/2");
    expect(".", "2", "./**/2");
    expect("/", "*", "//**/*");
    expect("--", ">", "--/**/>");
    expect("<", "!--", "</**/!--");
    expect("<!", "--", "<!/**/--");
    expect("<!-", "-", "<!-/**/-");
    expect("!", "--x", "!/**/--x");
    expect("!", "--x()", "!/**/--x()");
    expect("!", "-->", "!/**/-->");
    expect("\\61", "\n+", "\\61 \n+");
    expect("\\61\r", "\n+", "\\61 \n+");
    expect("\\61\r\n", "\n+", "\\61\r\n\n+");
    expect("a\\", "b", "a\xef\xbf\xbd/**/b");
    expect(" ", " ", "  ");
    expect(" /*x*/", " ", " /*x*/ ");
    custom_property_scope_s scope;
    scope.local = {{"--x", "<"}, {"--f", "calc"}};
    require(resolve("var(--x)!--", scope) == "</**/!--", "CDO prefix protected before original -- token");
    require(resolve("var(--f)(10px)", scope) == "calc/**/(10px)", "ident/open paren not a function");
    require(tokens("<!--") == sequence{std::to_string(static_cast<int>(category::cdo)) + ":<!--"}, "independent CDO classification");
    require(tokens("-->") == sequence{std::to_string(static_cast<int>(category::cdc)) + ":-->"}, "independent CDC classification");
    require(tokens("\\61\r\n") == tokens("a"), "whole CRLF is escape terminator, not whitespace");
    require(tokens("\\61 \n+") == tokens("a +"), "separate LF remains whitespace");
}
void equivalence() {
    const std::vector<std::string> fragments = {
        "", " ", "\n", "/*comment*/", "a", "é", "--", "--x", "--x()", "#x", "@x",
        "10", "2e3", "7px", "7%", "%", "-", "+", ".", "#", "@", "/", "*", "<", "!", ">",
        "<!", "!-", "!--", "<!--", "-->", "calc(1px)", "url(var--fake)", "'var(--fake)'",
        "\\61", "\\000061", "\\61\r", "\\61\r\n", "a\\", "7p\\78", "\\2d\\2d"
    };
    custom_property_scope_s scope;
    for (const auto& left : fragments) for (const auto& right : fragments) {
        scope.local = {{"--a", left}, {"--b", right}, {"--alias", "var(--a)"}};
        sequence expected;
        append_tokens(expected, left);
        append_tokens(expected, right);
        for (const auto* source : {"var(--a)var(--b)", "var(--alias)var(--b)",
                                   "var(--a)var(--missing,)var(--b)", "var(--missing,var(--a))var(--b)"}) {
            const auto out = resolve(source, scope);
            require(tokens(out) == expected, "token equivalence: " + left + " | " + right + " source=" + source + " result=" + out);
        }
    }
    // Every proper partition of the literal sentinels, including empty pieces.
    for (const std::string sentinel : {"<!--", "-->"}) {
        for (size_t a = 0; a <= sentinel.size(); ++a) for (size_t b = a; b <= sentinel.size(); ++b) {
            const std::string left = sentinel.substr(0, a), middle = sentinel.substr(a, b - a), right = sentinel.substr(b);
            scope.local = {{"--a", left}, {"--b", middle}, {"--c", right}};
            sequence expected;
            append_tokens(expected, left); append_tokens(expected, middle); append_tokens(expected, right);
            require(tokens(resolve("var(--a)var(--b)var(--c)", scope)) == expected, "sentinel partition");
        }
    }
    scope.local = {{"--hex", "\\61\r"}, {"--alias", "var(--hex)"}, {"--é", "10"}};
    for (const auto* source : {"var(--hex)\n+", "var(--alias)\n+", "var(--missing,\\61\r)\n+"})
        require(tokens(resolve(source, scope)) == tokens("a +"), "CR/LF direct, alias and fallback");
    require(resolve("var(--\\e9)px", scope) == "10/**/px", "escaped non-ASCII variable name");
    require(resolve("var(--é)%", scope) == "10/**/%", "non-ASCII variable name");
}
void resources() {
    custom_property_scope_s scope;
    scope.local = {{"--a", "a"}, {"--b", "zz"}, {"--hex", "\\61"}, {"--slash", "a\\"}};
    std::string out;
    require(resolve("var(--a)var(--b)", scope, 7) == "a/**/zz", "exact serialized limit");
    for (int limit : {2, 3, 5, 6}) {
        out = "old output";
        require(!variable_resolve("var(--a)var(--b)", &scope, 32, limit, out) && out.empty(), "limit includes barrier/payload");
    }
    require(resolve("var(--a)var(--b,%)", scope, 2) == "a%", "failed join rolls back before fallback");
    require(resolve("var(--hex)", scope, 4) == "\\61 ", "hex termination counted");
    require(resolve("var(--slash)", scope, 4) == "a\xef\xbf\xbd", "replacement bytes counted");
    for (const auto* source : {"var(--hex)", "var(--slash)"})
        require(!variable_resolve(source, &scope, 32, 3, out) && out.empty(), "stabilization length failure");
    scope.local["--long"] = std::string(300, 'z');
    // Measure instead of fixing a magic lexer cost. Sweep every work limit,
    // covering allocations, barrier writes, cross-block payload and compaction.
    for (const std::string source : {"var(--a)var(--b,%)", "var(--a)var(--long,%)", "var(--slash)var(--hex)"}) {
        css_variable_budget measured;
        require(variable_resolve_with_budget(source.c_str(), &scope, 32, 65536, measured, out), "measure work");
        const auto expected = out;
        const size_t cost = 1048576 - measured.remaining;
        css_variable_budget exact{cost};
        require(variable_resolve_with_budget(source.c_str(), &scope, 32, 65536, exact, out) &&
            out == expected && exact.remaining == 0, "exact work budget");
        for (size_t available = 0; available < cost; ++available) {
            css_variable_budget budget{available};
            out = "old output";
            require(!variable_resolve_with_budget(source.c_str(), &scope, 32, 65536, budget, out) &&
                budget.exhausted && out.empty(), "work exhaustion never publishes or chooses fallback");
        }
    }
    scope.local["--out"] = "var(--a)var(--b)";
    require(variable_resolve(scope.local["--out"].c_str(), &scope, 32, 65536, scope.local["--out"]) &&
        scope.local["--out"] == "a/**/zz", "scope storage alias remains alive until publication");
}
}
int main() {
    try { independent_examples(); equivalence(); resources(); std::printf("PASS variable boundaries: %zu checks\n", checks); return 0; }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL variable boundaries: %s\n", error.what()); return 1; }
}
