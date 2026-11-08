#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "inline_text.h"

namespace {

void require(const bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

using token_kind = aether_inline_text_token_kind_e;

void require_tokens(const std::vector<aether_inline_text_token_s>& actual,
                    const std::vector<std::pair<token_kind, std::string>>& expected,
                    const char* message) {
    require(actual.size() == expected.size(), message);
    for (size_t index = 0; index < expected.size(); ++index) {
        require(actual[index].kind == expected[index].first && actual[index].text == expected[index].second,
                message);
    }
}

} // namespace

int main() {
    require_tokens(
        aether_tokenize_inline_text("one \t two\nthree", AETHER_WHITE_SPACE_NORMAL),
        {{token_kind::word, "one"}, {token_kind::collapsible_space, " "},
         {token_kind::word, "two"}, {token_kind::collapsible_space, " "},
         {token_kind::word, "three"}},
        "normal must collapse horizontal whitespace and line breaks into one space run");

    require_tokens(
        aether_tokenize_inline_text("one  two\nthree", AETHER_WHITE_SPACE_NOWRAP),
        {{token_kind::word, "one"}, {token_kind::collapsible_space, " "},
         {token_kind::word, "two"}, {token_kind::collapsible_space, " "},
         {token_kind::word, "three"}},
        "nowrap must retain normal's collapsing while changing only soft-wrap eligibility");
    require(!aether_white_space_allows_soft_wrap(AETHER_WHITE_SPACE_NOWRAP),
            "nowrap must forbid soft wrapping");

    require_tokens(
        aether_tokenize_inline_text("one  two\r\nthree", AETHER_WHITE_SPACE_PRE),
        {{token_kind::word, "one"}, {token_kind::preserved_space, "  "},
         {token_kind::word, "two"}, {token_kind::forced_break, ""},
         {token_kind::word, "three"}},
        "pre must preserve spaces and normalize CRLF to one forced break");
    require(!aether_white_space_allows_soft_wrap(AETHER_WHITE_SPACE_PRE),
            "pre must forbid soft wrapping");

    require_tokens(
        aether_tokenize_inline_text("one  two\nthree", AETHER_WHITE_SPACE_PRE_WRAP),
        {{token_kind::word, "one"}, {token_kind::preserved_space, "  "},
         {token_kind::word, "two"}, {token_kind::forced_break, ""},
         {token_kind::word, "three"}},
        "pre-wrap must preserve spaces and forced line breaks");
    require(aether_white_space_allows_soft_wrap(AETHER_WHITE_SPACE_PRE_WRAP),
            "pre-wrap must allow soft wrapping");

    require_tokens(
        aether_tokenize_inline_text("one  two\nthree", AETHER_WHITE_SPACE_PRE_LINE),
        {{token_kind::word, "one"}, {token_kind::collapsible_space, " "},
         {token_kind::word, "two"}, {token_kind::forced_break, ""},
         {token_kind::word, "three"}},
        "pre-line must collapse spaces while preserving explicit line breaks");

    require(!aether_inline_text_has_visible_content(" \t\n", AETHER_WHITE_SPACE_NORMAL) &&
                aether_inline_text_has_visible_content(" \t\n", AETHER_WHITE_SPACE_PRE),
            "anonymous flex-item creation must discard only collapsible whitespace runs");
    unsigned byte_cases = 0;
    const std::string spaces = " \t\v\f\r\n";
    for (unsigned byte = 0; byte < 256; ++byte) {
        const std::string value(1, static_cast<char>(byte));
        const bool space = spaces.find(value[0]) != std::string::npos;
        require_tokens(aether_tokenize_inline_text(value, AETHER_WHITE_SPACE_NORMAL),
            {{space ? token_kind::collapsible_space : token_kind::word, space ? " " : value}},
            "all-byte normal tokenizer oracle");
        require(aether_inline_text_has_visible_content(value, AETHER_WHITE_SPACE_NORMAL) == !space,
            "all-byte normal visibility oracle");
        ++byte_cases;
    }
    require(byte_cases == 256, "complete tokenizer byte domain");
    for (int mode : {AETHER_WHITE_SPACE_NORMAL, AETHER_WHITE_SPACE_NOWRAP,
                    AETHER_WHITE_SPACE_PRE, AETHER_WHITE_SPACE_PRE_WRAP, AETHER_WHITE_SPACE_PRE_LINE}) {
        const bool preserve = mode == AETHER_WHITE_SPACE_PRE || mode == AETHER_WHITE_SPACE_PRE_WRAP;
        const bool breaks = preserve || mode == AETHER_WHITE_SPACE_PRE_LINE;
        require_tokens(aether_tokenize_inline_text("a \t\v\fb\r\nc", mode),
            {{token_kind::word, "a"},
             {preserve ? token_kind::preserved_space : token_kind::collapsible_space, preserve ? " \t\v\f" : " "},
             {token_kind::word, "b"},
             {breaks ? token_kind::forced_break : token_kind::collapsible_space, breaks ? "" : " "},
             {token_kind::word, "c"}}, "five-mode historical whitespace tokens");
        for (const std::string newline : {"\n", "\r", "\r\n"})
            require_tokens(aether_tokenize_inline_text(newline, mode),
                {{breaks ? token_kind::forced_break : token_kind::collapsible_space, breaks ? "" : " "}},
                "single LF/CR/CRLF token");
        if (breaks)
            require_tokens(aether_tokenize_inline_text("\r\r", mode),
                {{token_kind::forced_break, ""}, {token_kind::forced_break, ""}}, "two CR breaks");
        else
            require_tokens(aether_tokenize_inline_text("\r\r", mode),
                {{token_kind::collapsible_space, " "}}, "two CR collapse");
        const std::string high = "\x80\xA0\xC3\xD0\xDD\xE9\xFF\xC2\xA0";
        require_tokens(aether_tokenize_inline_text(high, mode), {{token_kind::word, high}}, "opaque high-byte word");
        require(aether_inline_text_has_visible_content(high, mode), "high bytes visible in every mode");
        require(!aether_inline_text_has_visible_content("", mode), "empty remains invisible");
        require(aether_inline_text_has_visible_content(" \t\v\f", mode) == preserve, "horizontal-only visibility");
        require(aether_white_space_allows_soft_wrap(mode) ==
            (mode != AETHER_WHITE_SPACE_PRE && mode != AETHER_WHITE_SPACE_NOWRAP), "five-mode wrapping contract");
    }
    return 0;
}
