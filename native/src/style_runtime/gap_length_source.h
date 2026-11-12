#pragma once
#include "css_variable_tokens.h"
#include <string>

// Decode identifiers, not arbitrary CSS text. Preserve whitespace/comments and
// token boundaries so escaped calc/units use the same existing length parser.
inline bool gap_length_source(const std::string& source, std::string& output) {
    if (source.size() > 16384) return false;
    css_variable_budget budget{source.size() * 8 + 1024, false};
    css_variable_lexer lexer(source.data(), source.size(), 0, &budget);
    std::string result;
    auto identifier = [&](size_t begin, size_t end, std::string& id) {
        css_variable_lexer decoder(source.data() + begin, end - begin);
        return decoder.decode_identifier([](void* p, const char* s, size_t n) {
            static_cast<std::string*>(p)->append(s, n);
            return true;
        }, &id);
    };
    for (;;) {
        auto t = lexer.next();
        if (t.kind == css_variable_token_kind::end) break;
        if (t.kind == css_variable_token_kind::error || budget.exhausted) return false;
        if (t.kind == css_variable_token_kind::function) {
            std::string id;
            if (!identifier(t.begin, t.end - 1, id)) return false;
            for (char& c : id) if (c >= 'A' && c <= 'Z') c += 32;
            if (id != "calc") return false;
            result += "calc(";
        } else if (t.serialization == css_variable_serialization::dimension) {
            std::string unit;
            if (!identifier(t.numeric_end, t.end, unit)) return false;
            for (char& c : unit) if (c >= 'A' && c <= 'Z') c += 32;
            // Match the existing length parser's units. Never let an escaped
            // unknown unit become an exponent (1\65 3px must not become 1e3px).
            if (unit != "px" && unit != "em" && unit != "rem" &&
                unit != "vw" && unit != "vh") return false;
            result.append(source, t.begin, t.numeric_end - t.begin);
            result += unit;
        } else {
            result.append(source, t.begin, t.end - t.begin);
        }
    }
    if (budget.exhausted) return false;
    output = std::move(result);
    return true;
}
