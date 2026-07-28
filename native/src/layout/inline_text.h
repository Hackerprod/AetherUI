#pragma once

#include <aether/aether.h>

#include <string>
#include <vector>

// The one canonical token stream for inline text.  Layout, intrinsic sizing,
// and painting all consume these tokens so a white-space mode cannot make one
// phase see different line boundaries from another.
enum class aether_inline_text_token_kind_e {
    word,
    collapsible_space,
    preserved_space,
    forced_break,
};

struct aether_inline_text_token_s {
    aether_inline_text_token_kind_e kind;
    std::string text;
};

std::vector<aether_inline_text_token_s> aether_tokenize_inline_text(
    const std::string& text, int32_t white_space);

bool aether_white_space_allows_soft_wrap(int32_t white_space);
bool aether_white_space_preserves_spaces(int32_t white_space);
bool aether_inline_text_has_visible_content(const std::string& text, int32_t white_space);
