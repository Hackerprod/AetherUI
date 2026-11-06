#include "../src/style_runtime/css_variable_tokens.h"
#include <cstdio>
#include <string>

int main() {
    const char* valid[] = {
        "", " /* comment */ ", "8px", "bogus var(--p)",
        "var(--p)", "VAR(--p)", "v\\61 r(--p)", "var(\\2d \\2d p)",
        "var(/*x*/--p/**/)", "var(--missing,)", "var(--missing, ) 8px",
        "8px var(--missing,)", "var(--p, var(--q, 8px))", "var(--p, ,)",
        "var(--p, {} extra)", "var(--p, {} {})", // unrestricted fallback
        "var(--p, f(!;))", "f(!; var(--p))", "[var(--p); !]", "{var(--p)}",
        "f({} var(--p))", "var(--p) f({})", "var(--p) \\! \\;",
        "var(--p) '!;var()'", "var(--p) \"a\\\"!\"", "var(--p) 'a\\\nb'",
        "var(--p) url(a!;b)", "var(--p) url( a\\(b\\)c )",
        "var(--p) url('a(var()!;)')", "var(--p) url(a\\20 b)",
        "var(--p) #var(--not-a-call)", "var(--p) @var(--not-a-call)",
        "var(--p) xvar(bogus)", "var(--p) 'var()'",
        "var(--p", "var(--p,", "var(--p, var(--q", "f([var(--p)",
        "{var(--p)} /* unterminated", "var(--x/*comment", "var(--x,/*comment",
        "var(--x,8px/*comment", "var(--x,var(--y/*comment", "/*comment",
        "var(--p) /* unterminated", "var(--p) 'unterminated",
        "var(--p) 'continued\\\n", "var(--p) url(unterminated",
        "var(--p) url( trailing  ", "var(--p) url('unterminated"
    };
    const char* invalid[] = {
        "var()", "var( )", "var(color)", "var(--)", "var(,8px)",
        "var(--p --q)", "var(--p 8px)", "var(--p())", "var(--p f())",
        "var(--p, var())", "var(--p, var(color))", "f(var(--p, var(--)))",
        "var(--p, !important)", "var(--p, ;)", "var(--p, 8px !bogus)",
        "var(--p) !bogus", "var(--p) !important", "var(--p) !important junk",
        "var(--p);", "var(--p))", "var(--p)]", "var(--p)}", "var(--p]",
        "var(--p) ([)]", "{} var(--p)", "var(--p) {}", "{var(--p)} extra",
        "var(--p) 'bad\nstring'", "var(--p) \"bad\n", "var(--p) url(a b)",
        "var(--p) url(a\"b)", "var(--p) url(a'b)", "var(--p) url(a(b)",
        "var(--p) url(a\\\nb)", "var(--p) url(a\x01 b)",
        "var(--p) url(a b", "var(--p, var(", "var(", "var(/*comment",
        "var(--p, var(--q, !))", "var(--p, var(--q, ;))"
    };
    int count = 0;
    for (const char* value : valid) {
        ++count;
        if (!validate_pending_declaration_value(value)) {
            std::fprintf(stderr, "Expected valid: %s\n", value);
            return 1;
        }
    }
    for (const char* value : invalid) {
        ++count;
        if (validate_pending_declaration_value(value)) {
            std::fprintf(stderr, "Expected invalid: %s\n", value);
            return 1;
        }
    }
    // Deep values must not recurse on the C++ stack.
    const std::string deep = std::string(20000, '(') + "var(--p)" + std::string(20000, ')');
    if (!validate_pending_declaration_value(deep)) return 1;
    const std::string no_close = "'unterminated";
    const auto legacy = css_variable_lexer(no_close.data(), no_close.size()).next();
    if (legacy.kind != css_variable_token_kind::error || !legacy.eof_recoverable ||
        legacy.serialization != css_variable_serialization::string) return 1;
    const std::string comment = "/*unterminated";
    const auto comment_token = css_variable_lexer(comment.data(), comment.size()).next();
    if (comment_token.kind != css_variable_token_kind::error || !comment_token.eof_recoverable ||
        comment_token.serialization != css_variable_serialization::comment) return 1;
    css_variable_budget budget;
    budget.remaining = 3;
    const auto exhausted = css_variable_lexer(comment.data(), comment.size(), 0, &budget).next();
    if (exhausted.kind != css_variable_token_kind::error || exhausted.eof_recoverable ||
        exhausted.serialization != css_variable_serialization::error) return 1;
    std::printf("PASS pending declaration syntax: %d cases, deep nesting and legacy EOF contract\n", count);
    return 0;
}
