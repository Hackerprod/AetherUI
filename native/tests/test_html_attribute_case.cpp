// C ABI regression, plus one private allocator check. Every check runs with NDEBUG.
#include <aether/aether.h>
#include "../src/dom/dom_types.h"
#include "../src/dom/html_attribute_name.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
std::string context;
bool check(bool ok, const std::string& why) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s: %s\n", context.c_str(), why.c_str()); }
    return ok;
}
std::string take(char* value) {
    check(value != nullptr, "owned ABI string exists");
    std::string result = value ? value : "";
    aether_string_free(value);
    return result;
}
using Attributes = std::map<std::string, std::string>;
Attributes attributes(aether_node_t node) {
    Attributes result;
    const auto count = aether_element_get_attribute_count(node);
    for (int32_t i = 0; i < count; ++i) {
        auto name = take(aether_element_get_attribute_name(node, i));
        auto value = take(aether_element_get_attribute_value(node, i));
        check(result.emplace(name, value).second, "enumerated keys are unique");
    }
    check(static_cast<int32_t>(result.size()) == count, "complete enumeration");
    return result;
}
struct Document {
    aether_doc_t handle = nullptr;
    Document() { check(aether_doc_create(&handle) == AETHER_OK && handle, "create document"); }
    ~Document() { aether_doc_destroy(handle); }
    aether_node_t parse(const std::string& html) {
        const auto version = aether_doc_get_version(handle);
        check(aether_doc_parse_html(handle, html.c_str()) == AETHER_OK, "parse succeeds");
        check(aether_doc_get_version(handle) == version + 1, "parse advances once");
        return aether_doc_get_document_element(handle);
    }
    aether_node_t id(const char* value) { return aether_doc_get_element_by_id(handle, value); }
};
void set(aether_node_t node, const std::string& name, const std::string& value) {
    check(aether_element_set_attribute(node, name.c_str(), value.c_str()) == AETHER_OK, "set " + name);
}
void helper_identity() {
    context = "shared helper byte identity";
    std::string bytes, expected;
    for (int i = 0; i <= 255; ++i) {
        bytes += static_cast<char>(i);
        expected += static_cast<char>(i >= 'A' && i <= 'Z' ? i + ('a' - 'A') : i);
    }
    auto canonical = aether::canonical_html_attribute_name(bytes);
    check(canonical == expected && canonical.size() == bytes.size(), "exact 256 byte mapping including NUL");
    check(aether::canonical_html_attribute_name(canonical) == canonical, "canonicalization is idempotent");
    check(aether::canonical_html_attribute_name("").empty(), "empty helper input");
}
void identity() {
    const std::vector<std::vector<std::string>> names = {
        {"data-x", "DATA-X", "DaTa-X"}, {"abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "AbCdEfGhIjKlMnOpQrStUvWxYz"},
        {"a-b_c.d:e9", "A-B_C.D:E9", "a-B_c.D:e9"}, {"id", "ID", "Id"}, {"class", "CLASS", "cLaSs"}, {"style", "STYLE", "StYlE"}
    };
    for (const auto& variants : names) for (const auto& name : variants) {
        context = "identity/" + name;
        Document d;
        auto node = d.parse("<DiV " + name + "='TaRget &amp; CAF\xC3\x89'></div>");
        const auto version = aether_doc_get_version(d.handle);
        const Attributes initial{{variants[0], "TaRget & CAF\xC3\x89"}};
        check(attributes(node) == initial, "canonical storage, unchanged decoded value");
        for (const auto& query : variants) {
            check(aether_element_has_attribute(node, query.c_str()) == AETHER_TRUE, "has " + query);
            check(take(aether_element_get_attribute(node, query.c_str())) == initial.begin()->second, "get " + query);
        }
        check(attributes(node) == initial, "queries do not mutate attributes");
        check(aether_doc_get_version(d.handle) == version, "queries do not mutate version");
        for (const auto& query : variants) {
            set(node, query, "Literal &amp; URL/Path?Q=ABC");
            check(attributes(node) == Attributes{{variants[0], "Literal &amp; URL/Path?Q=ABC"}}, "setter replaces one key without entity decoding");
            set(node, query, "Literal &amp; URL/Path?Q=ABC");
            check(aether_element_get_attribute_count(node) == 1, "idempotent set does not grow map");
        }
        check(aether_doc_get_version(d.handle) == version, "set preserves explicit-invalidation version contract");
    }
}
void duplicates() {
    const std::vector<std::string> names{"data-x", "DATA-X", "DaTa-X"};
    const std::vector<std::pair<std::string, std::string>> first = {
        {"", ""}, {"=''", ""}, {"=First", "First"}, {"='A&amp;B'", "A&B"},
        {"=\"Kept > <b id=literal>\"", "Kept > <b id=literal>"}, {"=a/b/", "a/b/"}
    };
    for (const auto& a : names) for (const auto& b : names) for (const auto& [source, value] : first) {
        context = "duplicate/" + a + source + "/" + b;
        Document d;
        auto body = d.parse("<body><div " + a + source + " " + b + "='Discard > <b ID=phantom>&amp;'") ;
        check(aether_node_get_child_count(body) == 0, "EOF discards entire opening including first value");
        check(!d.id("phantom"), "duplicate quoted markup never creates phantom");
        body = d.parse("<body><div " + a + source + " " + b + "='Discard > <b ID=phantom>&amp;'><span id=child></span></div><p id=after></p></body>");
        auto node = aether_node_get_child(body, 0);
        check(attributes(node) == Attributes{{"data-x", value}}, "first ASCII-equivalent occurrence wins");
        check(aether_node_get_child_count(body) == 2 && aether_node_get_child_count(node) == 1, "duplicate value fully consumed");
        check(aether_node_get_parent(d.id("child")) == node && aether_node_get_parent(d.id("after")) == body, "parent links intact");
        check(!d.id("phantom") && !d.id("literal"), "markup values are not retokenized");
    }
    context = "duplicate/semantic attributes";
    Document d;
    auto node = d.parse("<div ID=TaRget id=Discard CLASS=Kept class=Lost STYLE='width:37px' style='width:91px' DISABLED disabled=false REQUIRED='' required=x></div>");
    check(attributes(node) == Attributes{{"id", "TaRget"}, {"class", "Kept"}, {"style", "width:37px"}, {"disabled", ""}, {"required", ""}}, "ID class style and boolean first values preserved");
    check(d.id("TaRget") == node && !d.id("target") && !d.id("Discard"), "ID first-wins with exact values");
    check(aether_element_has_class(node, "Kept") && !aether_element_has_class(node, "kept") && !aether_element_has_class(node, "Lost"), "class exact first value");
    std::string html = "<div DATA-X=First";
    for (int i = 0; i < 4096; ++i) html += " " + names[i % names.size()] + "='Discard > &amp;'";
    node = d.parse(html + "><span ID=after></span></div>");
    check(attributes(node) == Attributes{{"data-x", "First"}}, "4096 duplicates remain one key");
    check(aether_node_get_parent(d.id("after")) == node, "stress parser cursor and relation");
}
void api_edges_and_bytes() {
    context = "API edges and ASCII-only bytes";
    Document d;
    auto node = d.parse("<div>Text<!--Comment--></div>");
    const auto text = aether_node_get_child(node, 0);
    for (aether_node_t invalid : {static_cast<aether_node_t>(nullptr), text}) {
        check(aether_element_get_attribute(invalid, "ID") == nullptr, "invalid get");
        check(aether_element_has_attribute(invalid, "ID") == AETHER_FALSE, "invalid has");
        check(aether_element_set_attribute(invalid, "ID", "x") == AETHER_ERROR_NULL_ARG, "invalid set");
    }
    check(aether_element_get_attribute(node, nullptr) == nullptr && !aether_element_has_attribute(node, nullptr), "null name queries");
    check(aether_element_set_attribute(node, nullptr, "x") == AETHER_ERROR_NULL_ARG &&
          aether_element_set_attribute(node, "ID", nullptr) == AETHER_ERROR_NULL_ARG, "null setter arguments");
    check(aether_element_get_attribute(node, "MISSING") == nullptr && !aether_element_has_attribute(node, "MISSING"), "absent is not empty");
    set(node, "DISABLED", "");
    check(aether_element_has_attribute(node, "disabled") && take(aether_element_get_attribute(node, "DiSaBlEd")).empty(), "empty present attribute");
    set(node, "DiSaBlEd", "false");
    check(aether_element_is_disabled(node), "boolean presence not textual false");
    set(node, "", "EmptyName");
    check(take(aether_element_get_attribute(node, "")) == "EmptyName", "existing empty-name API behavior retained");
    const std::string upper = "DATA-\xC3\x84", same = "data-\xC3\x84", different = "data-\xC3\xA4";
    set(node, upper, "CAF\xC3\x89"); set(node, different, "Other");
    check(take(aether_element_get_attribute(node, same.c_str())) == "CAF\xC3\x89", "UTF-8 name bytes not folded");
    check(take(aether_element_get_attribute(node, different.c_str())) == "Other", "distinct non-ASCII case identity");
    std::string name = "BYTES-", expected = "bytes-", value;
    for (int i = 128; i <= 255; ++i) { name += static_cast<char>(i); expected += static_cast<char>(i); value += static_cast<char>(i); }
    set(node, name, value);
    check(take(aether_element_get_attribute(node, expected.c_str())) == value, "all non-ASCII bytes retained through API");
    check(attributes(node).count(expected) == 1 && attributes(node).count(upper) == 0, "enumeration uses canonical ASCII only");
    check(aether_element_get_attribute_name(node, -1) == nullptr && aether_element_get_attribute_value(node, 10000) == nullptr, "invalid enumeration indices");
}
void eof_and_reparse() {
    context = "EOF and reparse";
    Document d;
    for (const std::string suffix : {" DATA-X", " DATA-X=", " DATA-X='", " DATA-X='discard >", " DATA-X=discard", " DATA-X='closed'"}) {
        auto body = d.parse("<body ID=body><p ID=kept></p><div ID=Discard data-x=First" + suffix);
        check(aether_node_get_child_count(body) == 1 && !d.id("Discard"), "incomplete duplicate opening discarded");
        // No public node-construction API exposes this allocator cursor.
        check(d.handle->next_style_node_id == aether_node_get_style_node_id(d.id("kept")) + 1, "discard allocates no style ID");
    }
    for (int i = 0; i < 20; ++i) {
        const std::string current = i % 2 ? "New" : "Old", previous = i % 2 ? "Old" : "New";
        auto body = d.parse("<body><div " + std::string(i % 2 ? "ID" : "Id") + "=" + current + " id=Discard DATA-X=Kept></div><script>DATA-X='Raw' &amp;</script></body>");
        auto node = aether_node_get_child(body, 0);
        check(d.id(current.c_str()) == node && !d.id(previous.c_str()) && !d.id("Discard"), "fresh exact ID index after reparse");
        check(attributes(node) == Attributes{{"id", current}, {"data-x", "Kept"}}, "reparse canonical map");
        auto script = aether_node_get_child(body, 1);
        check(take(aether_node_get_text_content(aether_node_get_child(script, 0))) == "DATA-X='Raw' &amp;", "raw text not normalized or decoded");
        // Never retain or query a handle destroyed by a later parse.
    }
}
}
int main() {
    helper_identity(); identity(); duplicates(); api_edges_and_bytes(); eof_and_reparse();
    std::printf("HTML attribute ASCII identity: %d checks, %d failures; NDEBUG=%s\n", checks, failures,
#ifdef NDEBUG
        "yes"
#else
        "no"
#endif
    );
    return failures ? 1 : 0;
}
