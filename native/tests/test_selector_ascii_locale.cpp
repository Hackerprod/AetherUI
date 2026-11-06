// Separate executable: process-global LC_CTYPE is changed only here and restored.
// INPUT and UTF-8 tag fixtures below are private nodes, not claims about HTML grammar.
#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"
#include <aether/aether.h>
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label.c_str()); }
}
struct LocaleGuard {
    std::string saved;
    LocaleGuard() : saved(std::setlocale(LC_CTYPE, nullptr)) {}
    ~LocaleGuard() { std::setlocale(LC_CTYPE, saved.c_str()); }
};
bool set(const std::string& locale) {
    const bool ok = std::setlocale(LC_CTYPE, locale.c_str()) != nullptr;
    check(ok, "select available locale " + locale); return ok;
}
bool parse(const std::string& text, compiled_selector_s& s) {
    const bool ok = selector_parse(text.c_str(), s); check(ok, "parse " + text); return ok;
}
void private_cases(const std::string& build, const std::string& query) {
    set(build);
    const std::vector<std::string> sources = {"input", "INPUT", "div", "x-\xc3\x84", "x-\xc3\xa4", "#I", "#i", ".I", ".i", "input#I", "input.I", "*", "[type=i i]", "[type=i]", "[type=i s]", "[type=I s]", "[class=i i]", "[data-x=i]"};
    std::vector<compiled_rule_s> rules(sources.size());
    rule_index_s index;
    for (size_t i = 0; i < rules.size(); ++i) {
        rules[i].rule_id = static_cast<int32_t>(i + 1);
        if (parse(sources[i], rules[i].selector)) rule_index_add(&index, &rules[i]);
    }
    set(query);
    for (const auto& tag : {std::string("INPUT"), std::string("input"), std::string("DiV"), std::string("x-\xc3\x84"), std::string("x-\xc3\xa4")}) {
        for (const char* identity : {"I", "i"}) {
            aether_node_s node;
            node.type = AETHER_NODE_ELEMENT; node.tag_name = tag;
            node.attributes = {{"id", identity}, {"class", identity}, {"type", "I"}, {"data-x", "I"}};
            node_style_info_s info; info.type_name = tag; info.id = identity; info.classes = {identity};
            std::vector<const compiled_rule_s*> candidates;
            rule_index_get_candidates(&index, &info, candidates);
            for (size_t i = 0; i < rules.size(); ++i) {
                const bool match = selector_match(rules[i].selector, &node);
                bool expected = false;
                if (i < 2) expected = tag == "INPUT" || tag == "input";
                else if (i == 2) expected = tag == "DiV";
                else if (i == 3) expected = tag == "x-\xc3\x84";
                else if (i == 4) expected = tag == "x-\xc3\xa4";
                else if (i < 9) expected = std::string(identity) == ((i == 5 || i == 7) ? "I" : "i");
                else if (i < 11) expected = (tag == "INPUT" || tag == "input") && std::string(identity) == "I";
                else expected = i != 14 && i != 17;
                const std::string label = build + " -> " + query + " " + tag + " " + sources[i];
                check(match == expected, label + " direct match");
                const bool found = std::find(candidates.begin(), candidates.end(), &rules[i]) != candidates.end();
                check(!match || found, label + " conservative candidate superset");
                if (i < 9) check(found == expected, label + " exact anchor bucket");
                else if (i < 11) check(found == (std::string(identity) == "I"), label + " compound uses exact ID/class anchor");
            }
        }
    }
    // Compile operands in the build locale; evaluate all six operators in query locale.
    for (const char* op : {"=", "~=", "|=", "^=", "$=", "*="}) for (const char* flag : {"", " i", " s"}) {
        set(build);
        compiled_selector_s s;
        if (!parse(std::string("[type") + op + "i" + flag + "]", s)) continue;
        set(query);
        aether_node_s node; node.type = AETHER_NODE_ELEMENT; node.tag_name = "div"; node.attributes["type"] = "I";
        check(selector_match(s, &node) == (std::string(flag) != " s"), "six I/i operators " + build + " -> " + query);
    }
}
void public_case(const std::string& build, const std::string& query) {
    set(build);
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    if (aether_doc_create(&doc) != AETHER_OK) { check(false, "create public document"); return; }
    check(aether_doc_parse_html(doc, "<html><body><InPuT id='I' class='I' TYPE='I'></body></html>") == AETHER_OK, "parse mixed-case public markup");
    auto node = aether_doc_get_element_by_id(doc, "I");
    check(node != nullptr, "exact public ID lookup");
    check(aether_doc_get_element_by_id(doc, "i") == nullptr, "public ID is case sensitive");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300, 200, 1, 16, 0, 0, 0};
    if (aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) != AETHER_OK) {
        check(false, "create public runtime"); aether_doc_destroy(doc); return;
    }
    int64_t sheet = 0;
    check(aether_style_runtime_add_stylesheet(runtime,
        "*{width:1px;height:1px;opacity:1} INPUT[type=i]{width:31px} input[type=i i]{height:37px} #I.I{opacity:.5} #i,.i{width:99px} input:is([type=i s]){width:43px} input:is([type=I s])::before{content:\"I\";width:47px;height:11px}",
        0, "ascii-locale", &sheet, nullptr) == AETHER_OK, "compile public stylesheet");
    aether_computed_style_handle_t latest = nullptr;
    for (int step = 0; step < 4; ++step) {
        const char* value = step == 1 ? "i" : "I";
        // Same document, stylesheet and runtime. Mutate in the build locale,
        // then resolve in the other locale. Last round is a stable rerun.
        set(build);
        if (step == 1 || step == 2)
            check(aether_element_set_attribute(node, "TYPE", value) == AETHER_OK, "mutate TYPE without normalizing value");
        set(query);
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        check(count >= 0, "public resolve across locales");
        bool pseudo_seen = false;
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t updated = nullptr;
            aether_computed_style_handle_t handle = nullptr; uint32_t impact = 0;
            check(aether_style_runtime_get_update(updates[i], &updated, &handle, &impact) == AETHER_OK, "read public update");
            // The input has no authored children; only ::before can be here.
            if (updated && aether_node_get_parent(updated) == node) {
                pseudo_seen = true;
                aether_computed_style_t style{};
                check(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy generated style");
                check(std::fabs(style.width.value - 47) < .001f, "cross-locale ::before style");
                aether_computed_style_free(&style);
            }
            if (updated == node) {
                if (latest) aether_computed_style_handle_destroy(latest);
                latest = handle;
            } else aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
        // Creation/recreation is observable on transitions, but stable passes
        // need not emit any update. Never retain generated-node pointers.
        if (step < 3) check(pseudo_seen == (step != 1), "cross-locale pseudo creation/removal/reappearance");
        // Keep the last owned host snapshot if a stable pass emits no update.
        check(latest != nullptr, "resolved host style exists");
        if (latest) {
            aether_computed_style_t style{};
            check(aether_computed_style_copy(latest, &style) == AETHER_OK, "copy public style");
            check(std::fabs(style.width.value - (step == 1 ? 43 : 31)) < .001f && std::fabs(style.height.value - 37) < .001f && std::fabs(style.opacity - .5f) < .001f,
                  "public final style " + build + " -> " + query + " step " + std::to_string(step));
            aether_computed_style_free(&style);
        }
        for (const char* attr : {"id", "class", "type"}) {
            char* actual = aether_element_get_attribute(node, attr);
            const char* expected = std::string(attr) == "type" ? value : "I";
            check(actual && std::string(actual) == expected, "public DOM value preserved after resolution");
            aether_string_free(actual);
        }
        check(aether_node_get_child_count(node) == 0, "generated pseudo not an authored child");
        check(aether_doc_get_element_by_id(doc, "I") == node && aether_doc_get_element_by_id(doc, "i") == nullptr,
              "mutations and pseudos preserve exact ID lookup");
    }
    if (latest) aether_computed_style_handle_destroy(latest);
    aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}
}
int main() {
    const std::string original = std::setlocale(LC_CTYPE, nullptr);
    {
        LocaleGuard restore;
        std::vector<std::string> locales{"C"};
        for (const char* name : {"tr_TR", "tr_TR.utf8"}) {
            if (std::setlocale(LC_CTYPE, name)) { locales.emplace_back(name); std::printf("AVAILABLE %s\n", name); }
            else std::printf("SKIP unavailable locale %s\n", name);
        }
        for (const auto& a : locales) for (const auto& b : locales) {
            private_cases(a, b); public_case(a, b);
        }
        std::printf("Turkish locale variants actually executed: %zu\n", locales.size() - 1);
    }
    check(original == std::setlocale(LC_CTYPE, nullptr), "restore original LC_CTYPE");
    std::printf("selector_ascii_locale: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
