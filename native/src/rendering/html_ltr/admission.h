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
    std::vector<std::pair<std::string,std::string>> declarations;
    std::vector<std::size_t> children;
    std::vector<Trivia> trivia;
    InheritedStyle inherited;
    bool leaf() const noexcept { return tag == "h1" || tag == "p"; }
};
struct Inventory { std::vector<Node> nodes; std::size_t scalar_count = 0, leaf_count = 0; };
// Recognizes only the authored grammar; it never invokes the tolerant DOM parser.
// Every string in the inventory is owned. All structural/text limits precede DOM.
Inventory admit(std::string_view html);
}
