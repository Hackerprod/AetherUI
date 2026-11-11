// Public C ABI only. Every check stays active in Release/NDEBUG builds.
#include <aether/aether.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
using Attributes = std::map<std::string, std::string>;
int checks = 0, failures = 0, cases = 0;

bool check(bool ok, const std::string& name, const char* detail) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), detail);
    }
    return ok;
}
struct Document {
    aether_doc_t handle = nullptr;
    Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    ~Document() { if (handle) aether_doc_destroy(handle); }
    bool parse(const std::string& html, const std::string& name) {
        if (!handle && !check(aether_doc_create(&handle) == AETHER_OK && handle,
                              name, "create document")) return false;
        return check(aether_doc_parse_html(handle, html.c_str()) == AETHER_OK, name, "parse HTML");
    }
};
void attributes(aether_node_t node, const Attributes& expected, const std::string& name) {
    const int count = aether_element_get_attribute_count(node);
    check(count == static_cast<int>(expected.size()), name, "exact attribute count");
    Attributes actual;
    for (int i = 0; i < count; ++i) {
        char* key = aether_element_get_attribute_name(node, i);
        char* value = aether_element_get_attribute_value(node, i);
        if (check(key && value, name, "enumerated owned strings"))
            check(actual.emplace(key, value).second, name, "unique enumerated name");
        aether_string_free(key);
        aether_string_free(value);
    }
    check(actual == expected, name, "complete attribute map and exact bytes");
    for (const auto& [key, value] : expected) {
        char* actual_value = aether_element_get_attribute(node, key.c_str());
        check(actual_value && value == actual_value, name, "direct attribute value bytes");
        check(aether_element_has_attribute(node, key.c_str()) != 0, name, "attribute present");
        aether_string_free(actual_value);
    }
    char* missing = aether_element_get_attribute(node, "data-absent");
    check(!missing && !aether_element_has_attribute(node, "data-absent"), name, "missing is not empty");
    aether_string_free(missing);
    check((aether_element_is_disabled(node) != 0) == (expected.count("disabled") != 0),
          name, "disabled state follows real attribute presence");
}
void width(aether_doc_t doc, aether_node_t target, float expected, const std::string& name) {
    aether_style_runtime_t runtime = nullptr;
    const aether_style_environment_t env{640, 480, 1, 16, 0, 0, 0};
    if (!check(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) == AETHER_OK && runtime,
               name, "create style runtime")) return;
    int64_t sheet = 0;
    int32_t rules = 0;
    if (!check(aether_style_runtime_add_stylesheet(runtime,
        "input{width:19px}input[disabled]{width:73px}", 0, "attribute-whitespace.css", &sheet, &rules) == AETHER_OK,
        name, "add public stylesheet")) { aether_style_runtime_destroy(runtime); return; }
    check(rules == 2, name, "two selector rules");
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    check(count > 0 && updates, name, "resolve subtree");
    int found = 0;
    for (int i = 0; updates && i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        if (check(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK,
                  name, "read style update") && node == target) {
            ++found;
            aether_computed_style_t style{};
            if (check(handle && aether_computed_style_copy(handle, &style) == AETHER_OK,
                      name, "copy target computed style"))
                check(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == expected,
                      name, "attribute selector computed width");
            aether_computed_style_free(&style);
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(found == 1, name, "one target update");
    if (updates) aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime);
}
void run(const std::string& name, const std::string& fragment, const Attributes& expected,
         bool style = false) {
    ++cases;
    Document doc;
    if (!doc.parse("<body><input id='target' " + fragment + "></body>", name)) return;
    auto target = aether_doc_get_element_by_id(doc.handle, "target");
    if (!check(target != nullptr, name, "target published")) return;
    Attributes all{{"id", "target"}};
    all.insert(expected.begin(), expected.end());
    attributes(target, all, name);
    auto body = aether_doc_get_body(doc.handle);
    check(body && aether_node_get_child_count(body) == 1 && aether_node_get_child(body, 0) == target,
          name, "only one body child");
    check(aether_node_get_parent(target) == body, name, "target parent");
    if (style) width(doc.handle, target, all.count("disabled") ? 73.f : 19.f, name);
}
void values() {
    struct Row { const char* name; const char* source; const char* expected; };
    const Row rows[] = {
        {"leading", "\valpha", "\valpha"}, {"middle", "a\vb", "a\vb"},
        {"trailing", "alpha\v", "alpha\v"}, {"only", "\v", "\v"},
        {"repeated", "\v\va\v\vb\v\v", "\v\va\v\vb\v\v"},
        {"double-quote", "\v\"a\"", "\v\"a\""}, {"single-quote", "\v'a'", "\v'a'"},
        {"unclosed-double-is-data", "\v\"a", "\v\"a"},
        {"unclosed-single-is-data", "\v'a", "\v'a"},
        {"backtick", "\v`a`", "\v`a`"}, {"equals", "\v=a=b", "\v=a=b"},
        {"slash", "\v/a/", "\v/a/"},
        {"quoted-double", "\"\va\v\"", "\va\v"}, {"quoted-single", "'\va\v'", "\va\v"},
        {"decimal-vt", "a&#11;b", "a\vb"}, {"hex-vt", "&#xB;a", "\va"},
        {"decode-once", "a&amp;#11;b", "a&#11;b"},
        {"decimal-cr", "a&#13;b", "a\rb"}, {"hex-cr", "a&#xD;b", "a\rb"},
        {"quoted-source-cr", "'a\rb'", "a\nb"},
        {"quoted-source-crlf", "\"a\r\nb\"", "a\nb"},
        {"utf8", "\v\xC3\xB1\xE2\x98\x83", "\v\xC3\xB1\xE2\x98\x83"},
        {"nbsp", "a\xC2\xA0" "b", "a\xC2\xA0" "b"},
        {"nel", "a\xC2\x85" "b", "a\xC2\x85" "b"},
        {"empty-unquoted", "", ""}, {"empty-single", "''", ""}, {"empty-double", "\"\"", ""},
    };
    for (const auto& row : rows)
        run(row.name, std::string("data-v=") + row.source, {{"data-v", row.expected}});
    for (const auto& row : rows) {
        // Empty-unquoted plus a following name is not an empty value.
        if (std::string(row.name) == "empty-unquoted") continue;
        run(std::string(row.name) + "/following", std::string("data-v=") + row.source + " title=after",
            {{"data-v", row.expected}, {"title", "after"}});
    }
    run("boolean", "data-v disabled", {{"data-v", ""}, {"disabled", ""}}, true);
    for (const auto& source : {"a\vdisabled", "a\v\vdisabled", "\vdisabled"})
        run(std::string("style/") + source, std::string("data-v=") + source, {{"data-v", source}}, true);
    run("real-disabled", "data-v=a\v disabled", {{"data-v", "a\v"}, {"disabled", ""}}, true);
    run("adjacent-slash", "data-v=a\v/", {{"data-v", "a\v/"}});
    run("separated-slash", "data-v=a\v /", {{"data-v", "a\v"}});
    std::string long_value(4096, 'x');
    long_value.front() = long_value[2048] = long_value.back() = '\v';
    run("4KiB-following", "data-v=" + long_value + " title=after",
        {{"data-v", long_value}, {"title", "after"}});
}
void whitespace_and_references() {
    struct Space { const char* name; const char* source; };
    const Space spaces[] = {{"space", " "}, {"tab", "\t"}, {"lf", "\n"},
                            {"ff", "\f"}, {"cr", "\r"}, {"crlf", "\r\n"}};
    for (const auto& s : spaces) {
        run(std::string(s.name) + "/before", std::string("data-v=") + s.source + "\va title=after",
            {{"data-v", "\va"}, {"title", "after"}});
        run(std::string(s.name) + "/after", std::string("data-v=a\v") + s.source + "disabled",
            {{"data-v", "a\v"}, {"disabled", ""}}, true);
        run(std::string(s.name) + "/quoted", std::string("data-v=") + s.source + "'\va' title=after",
            {{"data-v", "\va"}, {"title", "after"}});
    }
    const std::pair<const char*, const char*> references[] = {
        {"&#9;", "\t"}, {"&#10;", "\n"}, {"&#11;", "\v"}, {"&#12;", "\f"},
        {"&#13;", "\r"}, {"&#xD;", "\r"}, {"&#32;", " "}, {"&#34;", "\""},
        {"&#39;", "'"}, {"&#47;", "/"}, {"&#62;", ">"},
    };
    for (const auto& [source, value] : references)
        run(std::string("reference/") + source, std::string("data-v=a") + source + "disabled title=after",
            {{"data-v", std::string("a") + value + "disabled"}, {"title", "after"}});
    run("reference-leading-quote", "data-v=&#11;&#34;a&#34; title=after",
        {{"data-v", "\v\"a\""}, {"title", "after"}});
}
void duplicates_and_eof() {
    run("duplicate-first-retained", "data-v=a\vdisabled DATA-V=discard title=after",
        {{"data-v", "a\vdisabled"}, {"title", "after"}});
    run("duplicate-later-consumed", "data-v=first DATA-V=a\vdisabled title=after",
        {{"data-v", "first"}, {"title", "after"}}, true);
    run("duplicate-later-vt-quote", "data-v=first DATA-V=\v\"a title=after",
        {{"data-v", "first"}, {"title", "after"}});
    run("duplicate-first-empty", "data-v='' DATA-V=\vdiscard title=after",
        {{"data-v", ""}, {"title", "after"}});
    for (const auto& fragment : {"", " ", "\t\r\n", "\v", "\v\"a", "\v'a", "a\v",
                                 "a\v ", "'a\v", "\"a\v>", "'a\v'", "a\v/", "a\v /"}) {
        ++cases;
        const std::string name = "EOF/" + std::to_string(cases);
        Document doc;
        if (!doc.parse(std::string("<input id='target' data-v=") + fragment, name)) continue;
        check(!aether_doc_get_document_element(doc.handle), name, "unfinished opening not published");
        check(!aether_doc_get_body(doc.handle), name, "no invented body");
        check(!aether_doc_get_element_by_id(doc.handle, "target"), name, "no staged ID");
    }
}
void consumers_and_reparse() {
    for (const auto& newline : {"", "\r\n"}) {
        ++cases;
        const std::string name = *newline ? "consumers/normalized-source" : "consumers/direct-source";
        Document doc;
        {
            std::string source = std::string("<body>") + newline +
                "<section id=parent><input id=\vkey\v class=one\vtwo data-v=\vvalue\v>"
                "<span id=after></span></section></body>";
            if (!doc.parse(source, name)) continue;
            source.assign(source.size(), 'X');
        } // Source storage has been destroyed; read only live DOM handles.
        auto parent = aether_doc_get_element_by_id(doc.handle, "parent");
        auto target = aether_doc_get_element_by_id(doc.handle, "\vkey\v");
        auto after = aether_doc_get_element_by_id(doc.handle, "after");
        if (!check(parent && target && after, name, "full VT ID and siblings exist")) continue;
        check(!aether_doc_get_element_by_id(doc.handle, "key"), name, "truncated ID not indexed");
        attributes(target, {{"id", "\vkey\v"}, {"class", "one\vtwo"}, {"data-v", "\vvalue\v"}}, name);
        check(aether_element_has_class(target, "one\vtwo") && !aether_element_has_class(target, "one") &&
              !aether_element_has_class(target, "two"), name, "VT is inside a class token");
        check(aether_node_get_parent(target) == parent && aether_node_get_parent(after) == parent &&
              aether_node_get_child_count(parent) == 2 && aether_node_get_child(parent, 0) == target &&
              aether_node_get_child(parent, 1) == after, name, "exact parent and sibling shape");
        const std::string direct = "\v'a\r\n&#11;\v";
        check(aether_element_set_attribute(target, "data-v", direct.c_str()) == AETHER_OK,
              name, "set exact value bytes");
        check(aether_element_set_attribute(target, "id", "new\vid") == AETHER_OK, name, "set ID");
        check(aether_element_set_attribute(target, "class", "one\vtwo three\vfour") == AETHER_OK,
              name, "set classes");
        attributes(target, {{"id", "new\vid"}, {"class", "one\vtwo three\vfour"}, {"data-v", direct}}, name);
        check(aether_doc_get_element_by_id(doc.handle, "new\vid") == target &&
              !aether_doc_get_element_by_id(doc.handle, "\vkey\v") &&
              !aether_doc_get_element_by_id(doc.handle, "new"), name, "mutated ID exact");
        check(aether_element_has_class(target, "one\vtwo") && aether_element_has_class(target, "three\vfour") &&
              !aether_element_has_class(target, "three"), name, "mutated class token bytes");
        if (!doc.parse("<body><input id=again data-v=a\vdisabled></body>", name)) continue;
        // Old node handles are invalid after reparse and are not used again.
        target = aether_doc_get_element_by_id(doc.handle, "again");
        if (!check(target != nullptr, name, "fresh node after reparse")) continue;
        check(!aether_doc_get_element_by_id(doc.handle, "new\vid") &&
              !aether_doc_get_element_by_id(doc.handle, "parent"), name, "old IDs removed");
        attributes(target, {{"id", "again"}, {"data-v", "a\vdisabled"}}, name);
        width(doc.handle, target, 19, name);
    }
}
} // namespace

int main() {
    values();
    whitespace_and_references();
    duplicates_and_eof();
    consumers_and_reparse();
    std::printf("HTML attribute value whitespace: %d cases, %d checks, %d failures\n", cases, checks, failures);
    return failures ? 1 : 0;
}
