#include "inline_text.h"

#include <cctype>

namespace {

bool is_line_break(const char value) {
    return value == '\n' || value == '\r';
}

bool is_horizontal_space(const char value) {
    return std::isspace(static_cast<unsigned char>(value)) && !is_line_break(value);
}

void append_token(std::vector<aether_inline_text_token_s>& output,
                  const aether_inline_text_token_kind_e kind, std::string value) {
    if (value.empty() && kind != aether_inline_text_token_kind_e::forced_break) return;
    if (!output.empty() && output.back().kind == kind &&
        kind != aether_inline_text_token_kind_e::forced_break) {
        // `normal`, `nowrap`, and `pre-line` collapse every consecutive CSS
        // whitespace code point to one rendered space.  Keep that invariant in
        // the producer so layout, intrinsic sizing, and painting cannot each
        // interpret the run differently.
        if (kind != aether_inline_text_token_kind_e::collapsible_space) {
            output.back().text += value;
        }
        return;
    }
    output.push_back({kind, std::move(value)});
}

bool preserves_line_breaks(const int32_t white_space) {
    return white_space == AETHER_WHITE_SPACE_PRE ||
        white_space == AETHER_WHITE_SPACE_PRE_WRAP ||
        white_space == AETHER_WHITE_SPACE_PRE_LINE;
}

} // namespace

bool aether_white_space_allows_soft_wrap(const int32_t white_space) {
    return white_space != AETHER_WHITE_SPACE_NOWRAP &&
        white_space != AETHER_WHITE_SPACE_PRE;
}

bool aether_white_space_preserves_spaces(const int32_t white_space) {
    return white_space == AETHER_WHITE_SPACE_PRE ||
        white_space == AETHER_WHITE_SPACE_PRE_WRAP;
}

std::vector<aether_inline_text_token_s> aether_tokenize_inline_text(
    const std::string& text, const int32_t white_space) {
    std::vector<aether_inline_text_token_s> output;
    const bool preserve_spaces = aether_white_space_preserves_spaces(white_space);
    const bool preserve_breaks = preserves_line_breaks(white_space);
    std::string run;

    const auto flush_word = [&]() {
        append_token(output, aether_inline_text_token_kind_e::word, std::move(run));
        run.clear();
    };

    for (size_t index = 0; index < text.size(); ++index) {
        const char value = text[index];
        if (is_line_break(value)) {
            flush_word();
            if (value == '\r' && index + 1 < text.size() && text[index + 1] == '\n') ++index;
            if (preserve_breaks) {
                append_token(output, aether_inline_text_token_kind_e::forced_break, {});
            } else {
                append_token(output, aether_inline_text_token_kind_e::collapsible_space, " ");
            }
            continue;
        }
        if (is_horizontal_space(value)) {
            flush_word();
            if (preserve_spaces) {
                append_token(output, aether_inline_text_token_kind_e::preserved_space, std::string(1, value));
            } else {
                append_token(output, aether_inline_text_token_kind_e::collapsible_space, " ");
            }
            continue;
        }
        run += value;
    }
    flush_word();
    return output;
}

bool aether_inline_text_has_visible_content(const std::string& text, const int32_t white_space) {
    for (const auto& token : aether_tokenize_inline_text(text, white_space)) {
        if (token.kind == aether_inline_text_token_kind_e::word ||
            token.kind == aether_inline_text_token_kind_e::preserved_space ||
            token.kind == aether_inline_text_token_kind_e::forced_break) return true;
    }
    return false;
}
