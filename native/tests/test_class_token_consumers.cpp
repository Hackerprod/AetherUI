// Class identity and attribute-list modes. All checks run under NDEBUG.
// This executable alone changes LC_CTYPE, and restores it before exiting.
#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"
#include <aether/aether.h>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", why); }
}
void need(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "HARNESS %s\n", why); std::exit(2); }
}
bool match(const std::string& source, aether_node_t node) {
    compiled_selector_s sel;
    need(selector_parse(source.c_str(), sel), source.c_str());
    return selector_match(sel, node);
}
bool class_match(const std::string& name, aether_node_t node) {
    compiled_selector_s sel;
    need(selector_parse(".placeholder", sel), "class skeleton");
    sel.steps.front().compound.classes = {name};
    return selector_match(sel, node);
}
void byte_cases() {
    aether_node_s node;
    node.type = AETHER_NODE_ELEMENT; node.tag_name = "div";
    for (unsigned byte = 1; byte < 256; ++byte) {
        // NUL is deliberately reserved for the length-aware primitive tests.
        const std::string value = "a" + std::string(1, static_cast<char>(byte)) + "B";
        const bool space = byte == 9 || byte == 10 || byte == 12 || byte == 13 || byte == 32;
        need(aether_element_set_attribute(&node, "class", value.c_str()) == AETHER_OK, "set byte class");
        for (const auto& candidate : {std::string("a"), std::string("B"), value, std::string(""), std::string("a B")}) {
            const bool expected = space ? candidate == "a" || candidate == "B" : candidate == value;
            check((aether_element_has_class(&node, candidate.c_str()) != 0) == expected, "DOM all non-NUL byte boundaries");
            check(class_match(candidate, &node) == expected, "matcher all non-NUL byte boundaries");
        }
        check(node.attributes.at("class") == value, "queries preserve every source byte");
    }
}
void selectors() {
    aether_node_s node;
    node.type = AETHER_NODE_ELEMENT; node.tag_name = "div";
    node.attributes = {{"id", "I"}, {"class", "\t a\vB  a\vB \r"}, {"rel", "A\vB"}};
    for (const char* source : {R"(.a\b B)", R"(#I.a\b B)", R"([class~="a\b B"])", R"([class~="a\b B" s])", R"([class~="A\b b" i])",
                               R"(:is(.a\b B))", R"(:where(.a\b B))", R"(:not(.a))", R"([rel~="a\b b"])", R"([rel~="a\b b" i])"})
        check(match(source, &node), source);
    for (const char* source : {".a", ".B", R"(.a\b b)", R"(#i.a\b B)", R"([class~="A\b b"])", R"([class~="A\b b" s])",
                               R"([class~=""])", R"([class~=" "])", R"([class~="a B"])", R"([class~="a\9 B" i])", R"([rel~="a\b b" s])"})
        check(!match(source, &node), source);
    // Every HTML separator in an includes operand is invalid in all case modes.
    for (const char* escape : {"20", "9", "a", "c", "d"}) for (const char* mode : {"", " i", " s"})
        check(!match(std::string("[class~=\"a\\") + escape + " B\"" + mode + "]", &node), "includes rejects HTML-space operand");
    node.attributes["class"] = "I \xc3\x84 \xc4\xb0";
    check(match(".I", &node) && !match(".i", &node), "class identity is exact");
    check(match("[class~=i i]", &node) && !match("[class~=i]", &node) && !match("[class~=i s]", &node), "class includes modes");
    check(!match("[class~=\"\xc3\xa4\" i]", &node) && !match("[class~=i i]:not(.I)", &node), "ASCII-only folding, no Unicode folding");
    for (const auto& value : {std::string(""), std::string(" \t\n\f\r")}) {
        node.attributes["class"] = value;
        for (const char* query : {"", "a", " ", "\t"})
            check(!aether_element_has_class(&node, query) && !class_match(query, &node), "empty/all-space classes and invalid queries");
    }
    node.attributes.clear();
    check(!aether_element_has_class(&node, "a") && !class_match("a", &node), "missing class");
    check(!aether_element_has_class(nullptr, "a") && !aether_element_has_class(&node, nullptr), "null DOM boundaries");
    node.type = AETHER_NODE_TEXT; node.attributes["class"] = "a";
    check(!aether_element_has_class(&node, "a") && !class_match("a", &node), "non-element boundary");
}
void cross_locale(const std::string& build, const std::string& query) {
    need(std::setlocale(LC_CTYPE, build.c_str()), "build locale");
    const char* sources[] = {R"(.I\b B)", R"(#I.I\b B)", ".I", ".B", R"([class~="i\b b" i])", R"([class~="i\b b"])", R"([rel~="i\b b"])", R"([rel~="i\b b" s])"};
    std::vector<compiled_selector_s> selectors(8);
    for (size_t i = 0; i < selectors.size(); ++i) need(selector_parse(sources[i], selectors[i]), "cross-locale parse");
    aether_node_s node;
    node.type = AETHER_NODE_ELEMENT; node.tag_name = "div";
    node.attributes = {{"id", "I"}, {"class", "I\vB"}, {"rel", "I\vB"}};
    need(std::setlocale(LC_CTYPE, query.c_str()), "query locale");
    const bool expected[] = {true, true, false, false, true, false, true, false};
    for (size_t i = 0; i < selectors.size(); ++i) check(selector_match(selectors[i], &node) == expected[i], "cross-locale class/includes match");
    check(aether_element_has_class(&node, "I\vB") && !aether_element_has_class(&node, "I"), "cross-locale DOM match");
}
}
int main() {
    const std::string original = std::setlocale(LC_CTYPE, nullptr);
    std::vector<std::string> locales{"C"};
    for (const char* name : {"C.utf8", "tr_TR", "tr_TR.utf8", "en_US"}) {
        if (std::setlocale(LC_CTYPE, name)) locales.emplace_back(name);
        else std::printf("SKIP unavailable locale %s\n", name);
    }
    for (const auto& locale : locales) {
        need(std::setlocale(LC_CTYPE, locale.c_str()), "select locale");
        byte_cases(); selectors();
    }
    for (const auto& build : locales) for (const auto& query : locales) cross_locale(build, query);
    need(std::setlocale(LC_CTYPE, original.c_str()), "restore locale");
    check(original == std::setlocale(LC_CTYPE, nullptr), "original locale restored");
    std::printf("class_token_consumers: %u checks, %u failures; %zu locale pairs\n", checks, failures, locales.size() * locales.size());
    return failures ? 1 : 0;
}
