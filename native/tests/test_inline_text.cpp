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
    return 0;
}
