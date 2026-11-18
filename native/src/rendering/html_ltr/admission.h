#pragma once
#include <aether/aether.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aether::rendering::html_ltr {
// Private typed failure. The C entry point maps this and catches allocation errors.
// Contains no authored strings or dynamic diagnostic payload.
struct Error { aether_status_t status; };
struct InheritedStyle {
    unsigned font_size = 16, font_weight = 400, line_height = 24;
    std::uint32_t color_rgb = 0x202020;
};
struct Trivia { std::size_t before_child; std::string text; };
struct Node {
    static constexpr std::size_t no_parent = static_cast<std::size_t>(-1);
    std::string tag;
    std::size_t parent = no_parent;
    std::string text;
    std::string type, value, placeholder;
    std::vector<std::pair<std::string,std::string>> declarations;
    std::vector<std::size_t> children;
    std::vector<Trivia> trivia;
    InheritedStyle inherited;
    bool input() const noexcept { return tag == "input"; }
    bool control() const noexcept { return input() || tag == "button"; }
    bool leaf() const noexcept { return tag == "h1" || tag == "p" || tag == "button"; }
};
struct Inventory { std::vector<Node> nodes; std::size_t scalar_count = 0, leaf_count = 0, control_count = 0; };
// Shared UTF-8 decoder and scalar repertoire. Authored grammar may impose
// additional syntax restrictions; runtime text must not reuse those restrictions.
// Decoder rejects NUL and malformed UTF-8 with INVALID_ARGUMENT.
std::uint32_t decode_scalar_utf8(std::string_view, std::size_t& offset);
constexpr bool repertoire_scalar(std::uint32_t cp) noexcept {
    return (cp >= 32 && cp <= 126) || (cp >= 160 && cp <= 255 && cp != 173);
}
// Recognizes only the authored grammar; it never invokes the tolerant DOM parser.
// Every string in the inventory is owned. All structural/text limits precede DOM.
Inventory admit(std::string_view html, std::uint32_t profile = 1);
}
