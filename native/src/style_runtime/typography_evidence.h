#pragma once
// Private evidence, not a render authorization or font-resolution result.
#include "css_component_values.h"
#include "css_single_identifier.h"
#include <cmath>
#include <cstdint>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace aether::style {
enum class TypographyFieldState { unknown, known, unsupported };
struct TypographyFamily {
    std::string name;
    bool generic = false;
    bool operator==(const TypographyFamily& v) const { return name == v.name && generic == v.generic; }
};
struct TypographyProvenance {
    bool declaration=false, inherited=false, important=false, inline_style=false;
    int32_t layer=0, source_order=0, declaration_order=0;
    std::string accepted_source; // bounded to 64 KiB; never diagnostics
    bool operator==(const TypographyProvenance& v) const {
        return declaration==v.declaration && inherited==v.inherited && important==v.important &&
            inline_style==v.inline_style && layer==v.layer && source_order==v.source_order &&
            declaration_order==v.declaration_order && accepted_source==v.accepted_source;
    }
};
struct TypographyEvidence {
    std::array<TypographyProvenance,3> provenance{};
    TypographyFieldState family_state = TypographyFieldState::unknown;
    std::vector<TypographyFamily> families;
    TypographyFieldState style_state = TypographyFieldState::unknown;
    std::string font_style;
    TypographyFieldState spacing_state = TypographyFieldState::unknown;
    bool spacing_normal = false;
    double spacing_px = 0;
    // These three fields never prove complete typography coverage on their own.
    bool coverage_complete = false;
    // Bit 1: unrepresented typography pipeline/sheets; bit 2: matched discarded
    // declaration. Never treat absence of a tracked property as an initial value.
    uint32_t coverage_gaps = 1;
    bool operator==(const TypographyEvidence& v) const {
        return provenance == v.provenance && family_state == v.family_state && families == v.families &&
            style_state == v.style_state && font_style == v.font_style &&
            spacing_state == v.spacing_state && spacing_normal == v.spacing_normal &&
            spacing_px == v.spacing_px && coverage_complete == v.coverage_complete && coverage_gaps == v.coverage_gaps;
    }
    bool operator!=(const TypographyEvidence& v) const { return !(*this == v); }
};
using TypographyEvidenceRef = std::shared_ptr<const TypographyEvidence>;
inline std::string typography_ascii_lower(std::string v) {
    for (char& c : v) if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
    return v;
}
inline bool typography_css_wide(const std::string& v) {
    return v == "initial" || v == "inherit" || v == "unset" || v == "revert" || v == "revert-layer";
}
inline bool typography_generic(const std::string& v) {
    return v == "serif" || v == "sans-serif" || v == "monospace" || v == "cursive" ||
        v == "fantasy" || v == "system-ui" || v == "ui-serif" || v == "ui-sans-serif" ||
        v == "ui-monospace" || v == "ui-rounded" || v == "emoji" || v == "math" || v == "fangsong";
}
inline void typography_parse_family(const std::string& source, TypographyEvidence& evidence) {
    // Publication is atomic under allocation failure. No truncated list is known.
    std::vector<TypographyFamily> families;
    TypographyFieldState state = TypographyFieldState::unsupported;
    if (source.size() <= 65536) {
        bool valid = false;
        auto parts = css_split_component_delimited(source, ',', &valid);
        if (valid && !parts.empty() && parts.size() <= 256) {
            for (const auto& part : parts) {
                css_component_cursor cursor(part);
                css_component_value component;
                std::string name;
                bool quoted = false;
                size_t identifiers = 0;
                while (valid && cursor.next(component)) {
                    auto spelling = part.substr(component.begin, component.end - component.begin);
                    css_variable_lexer decoder(spelling.data(), spelling.size());
                    if (component.token.serialization == css_variable_serialization::string) {
                        if (quoted || identifiers || !decoder.decode_string(name) || name.empty()) valid = false;
                        quoted = true;
                    } else if (component.token.kind == css_variable_token_kind::ident && !quoted) {
                        if (identifiers++) name += ' ';
                        if (!decoder.decode_identifier([](void* p, const char* bytes, size_t count) {
                            static_cast<std::string*>(p)->append(bytes, count); return true;
                        }, &name)) valid = false;
                    } else valid = false;
                }
                if (!cursor.valid() || name.empty()) valid = false;
                auto lower = typography_ascii_lower(name);
                if (!quoted && (typography_css_wide(lower) || lower == "default")) valid = false;
                const bool generic = !quoted && identifiers == 1 && typography_generic(lower);
                // Generic names are reserved when used as a component of an
                // unquoted multi-identifier family name as well.
                if (!quoted && identifiers > 1) {
                    css_component_cursor words(part); css_component_value word;
                    while (words.next(word)) {
                        std::string decoded;
                        if (!css_single_identifier(part.substr(word.begin, word.end-word.begin), decoded) ||
                            typography_generic(decoded) || typography_css_wide(decoded) || decoded == "default") valid = false;
                    }
                }
                if (!valid) break;
                families.push_back({generic ? lower : name, generic});
            }
            if (valid) state = TypographyFieldState::known;
        }
    }
    if (state != TypographyFieldState::known) families.clear();
    evidence.families.swap(families);
    evidence.family_state = state;
}
inline void typography_parse_style(const std::string& source, TypographyEvidence& evidence) {
    std::string value;
    auto state = TypographyFieldState::unsupported;
    if (source.size() <= 65536 && css_single_identifier(source, value) &&
        (value == "normal" || value == "italic" || value == "oblique")) state = TypographyFieldState::known;
    if (state != TypographyFieldState::known) value.clear();
    evidence.font_style.swap(value); evidence.style_state = state;
}
inline void typography_parse_spacing(const std::string& source, TypographyEvidence& evidence) {
    auto state = TypographyFieldState::unsupported;
    bool normal = false;
    double px = 0;
    if (source.size() <= 65536) {
        std::string ident;
        if (css_single_identifier(source, ident) && ident == "normal") {
            state = TypographyFieldState::known; normal = true;
        } else {
            css_component_cursor cursor(source); css_component_value value, extra;
            if (cursor.next(value) && !cursor.next(extra) && cursor.valid() &&
                (value.token.serialization == css_variable_serialization::number ||
                 value.token.serialization == css_variable_serialization::dimension)) {
                const auto end = value.token.numeric_end;
                if (end > value.begin && end <= value.end) {
                    const auto number = source.substr(value.begin, end-value.begin);
                    char* tail = nullptr;
                    const double n = std::strtod(number.c_str(), &tail);
                    double factor = 0;
                    std::string unit;
                    if (end == value.end) { if (n == 0) factor = 1; }
                    else if (css_single_identifier(source.substr(end, value.end-end), unit)) {
                        if (unit == "px") factor = 1;
                        else if (unit == "in") factor = 96;
                        else if (unit == "cm") factor = 96 / 2.54;
                        else if (unit == "mm") factor = 96 / 25.4;
                        else if (unit == "q") factor = 96 / 101.6;
                        else if (unit == "pt") factor = 96 / 72.0;
                        else if (unit == "pc") factor = 16;
                    }
                    if (tail == number.c_str()+number.size() && factor && std::isfinite(n * factor)) {
                        px = n * factor; state = TypographyFieldState::known;
                    }
                }
            }
        }
    }
    evidence.spacing_state = state; evidence.spacing_normal = normal; evidence.spacing_px = px;
}
} // namespace aether::style
