#include <aether/aether.h>

#include <cstdio>
#include <map>
#include <string>

namespace {
using Attributes = std::map<std::string, std::string>;
int checks = 0;
int failures = 0;
int cases = 0;

bool check(bool ok, const std::string& name, const std::string& detail) {
    ++checks;
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), detail.c_str());
    }
    return ok;
}

struct Document {
    aether_doc_t handle = nullptr;
    ~Document() { if (handle) aether_doc_destroy(handle); }
};

void check_attributes(aether_node_t node, const Attributes& expected,
                      const std::string& name) {
    const int32_t count = aether_element_get_attribute_count(node);
    check(count == static_cast<int32_t>(expected.size()), name, "exact attribute count");
    Attributes actual;
    for (int32_t i = 0; i < count; ++i) {
        char* key = aether_element_get_attribute_name(node, i);
        char* value = aether_element_get_attribute_value(node, i);
        check(key && value, name, "enumerated attribute strings");
        if (key && value) {
            check(actual.emplace(key, value).second, name, "unique enumerated name");
        }
        aether_string_free(key);
        aether_string_free(value);
    }
    // Compare the entire map, independent of enumeration order. This rejects
    // invented path attributes, not just a truncated value or the wrong count.
    check(actual == expected, name, "complete attribute map");
    for (const auto& [key, value] : expected) {
        check(aether_element_has_attribute(node, key.c_str()) != 0, name, "present: " + key);
        char* actual_value = aether_element_get_attribute(node, key.c_str());
        check(actual_value && value == actual_value, name, "value: " + key);
        aether_string_free(actual_value);
    }
    // A present empty value must not be mistaken for an absent attribute.
    char* missing = aether_element_get_attribute(node, "data-absent");
    check(missing == nullptr && !aether_element_has_attribute(node, "data-absent"),
          name, "absent attribute is null");
    aether_string_free(missing);
    const bool disabled = expected.count("disabled") != 0;
    check((aether_element_has_attribute(node, "disabled") != 0) == disabled,
          name, "disabled presence");
    check((aether_element_is_disabled(node) != 0) == disabled, name, "disabled state");
}

void check_width(aether_doc_t doc, aether_node_t target, const char* css,
                 float expected, const std::string& name) {
    aether_style_runtime_t runtime = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t count = 0;
    auto cleanup = [&] {
        if (updates) aether_style_updates_free(updates, count);
        if (runtime) aether_style_runtime_destroy(runtime);
    };
    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{640.f, 480.f, 1.f, 16.f, 0, 0, 0};
    if (!check(aether_style_runtime_create(&options, &environment, nullptr, nullptr,
                                          &runtime) == AETHER_OK && runtime,
               name, "runtime create")) { cleanup(); return; }
    int64_t sheet = 0;
    int32_t rules = 0;
    if (!check(aether_style_runtime_add_stylesheet(runtime, css, 0, "unquoted.css",
                                                  &sheet, &rules) == AETHER_OK,
               name, "add stylesheet")) { cleanup(); return; }
    check(rules == 2, name, "two compiled rules");
    count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    check(count > 0 && updates, name, "style updates");
    int matched_updates = 0;
    for (int32_t i = 0; updates && i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        const bool ok = check(aether_style_runtime_get_update(updates[i], &node, &handle,
                                                              nullptr) == AETHER_OK,
                              name, "read update");
        if (ok && node == target) {
            ++matched_updates;
            check(handle != nullptr, name, "computed style handle");
            if (handle) {
                aether_computed_style_t style{};
                if (check(aether_computed_style_copy(handle, &style) == AETHER_OK,
                          name, "copy computed style")) {
                    check(style.width.unit == AETHER_CSS_UNIT_PX && style.width.value == expected,
                          name, "cascade width");
                }
                aether_computed_style_free(&style);
            }
        }
        if (handle) aether_computed_style_handle_destroy(handle);
    }
    check(matched_updates == 1, name, "exactly one target style update");
    cleanup();
}

// child_of_target is -1 when no child fixture is requested. For self-closing
// non-void controls, 0 documents AetherUI's existing behavior, not HTML parity.
void run(const std::string& name, const std::string& html, const Attributes& expected,
         int child_of_target = -1, const char* css = nullptr, float width = 0.f) {
    ++cases;
    Document doc;
    if (!check(aether_doc_create(&doc.handle) == AETHER_OK && doc.handle,
               name, "document create")) return;
    if (!check(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK,
               name, "parse HTML")) return;
    auto target = aether_doc_get_element_by_id(doc.handle, "t");
    if (!check(target != nullptr, name, "target exists")) return;
    check_attributes(target, expected, name);
    auto styled = target;
    if (child_of_target >= 0) {
        auto child = aether_doc_get_element_by_id(doc.handle, "child");
        check(child != nullptr, name, "child exists");
        if (child) {
            check(aether_node_get_parent(child) == (child_of_target ? target : aether_doc_get_body(doc.handle)),
                  name, "exact child parent");
            check_attributes(child, {{"id", "child"}}, name + "/child");
        }
        styled = child;
    }
    if (css && styled) check_width(doc.handle, styled, css, width, name);
}

// Incomplete openings contribute neither a node nor any of their attributes.
// Keep the positive run() helper's target-existence requirement unchanged.
void run_absent(const std::string& name, const std::string& html) {
    ++cases;
    Document doc;
    if (!check(aether_doc_create(&doc.handle) == AETHER_OK && doc.handle,
               name, "document create")) return;
    if (!check(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK,
               name, "parse HTML")) return;
    check(aether_doc_get_document_element(doc.handle) == nullptr,
          name, "empty document has no nodes");
    check(aether_doc_get_body(doc.handle) == nullptr, name, "no invented body");
    check(aether_doc_get_element_by_id(doc.handle, "t") == nullptr,
          name, "discarded target ID absent");
}

void original_regressions() {
    struct Case { const char* name; const char* html; const char* attr; const char* value; };
    // The twelve original reproducer inputs are unchanged, including unquoted IDs.
    const Case originals[] = {
        {"absolute_url", "<body><img id=t src=https://example.test/a.png></body>", "src", "https://example.test/a.png"},
        {"root_relative_url", "<body><img id=t src=/icons/a.svg></body>", "src", "/icons/a.svg"},
        {"relative_path", "<body><a id=t href=docs/start>Open</a></body>", "href", "docs/start"},
        {"slash_data", "<body><div id=t data-path=a/b/c></div></body>", "data-path", "a/b/c"},
        {"slash_only", "<body><div id=t data-path=/></div></body>", "data-path", "/"},
        {"adjacent_slash", "<body><img id=t src=logo.svg/></body>", "src", "logo.svg/"},
        {"separated_slash", "<body><img id=t src=logo.svg /></body>", "src", "logo.svg"},
        {"double_quoted", "<body><img id=t src=\"https://example.test/a.png\"></body>", "src", "https://example.test/a.png"},
        {"single_quoted", "<body><img id=t src='https://example.test/a.png'></body>", "src", "https://example.test/a.png"},
        {"next_attribute", "<body><img id=t src=/a/b.png alt=hello></body>", "src", "/a/b.png"},
        {"entity_with_path", "<body><a id=t href=/a?x=1&amp;y=2>Open</a></body>", "href", "/a?x=1&y=2"},
        {"path_no_slash", "<body><img id=t src=logo.svg></body>", "src", "logo.svg"},
    };
    for (const auto& c : originals) {
        Attributes expected{{"id", "t"}, {c.attr, c.value}};
        if (std::string(c.name) == "next_attribute") expected.emplace("alt", "hello");
        run(c.name, c.html, expected);
    }
}

void slash_boundaries() {
    struct Case { const char* name; const char* fragment; const char* value; bool self_closing; };
    // Literal expectations from Blink's attribute-value and self-closing states.
    // A slash adjacent to an unquoted value is data, even immediately before >.
    const Case boundaries[] = {
        {"plain", "logo.svg>", "logo.svg", false},
        {"adjacent_close", "logo.svg/>", "logo.svg/", false},
        {"separated_close", "logo.svg />", "logo.svg", true},
        {"slash_space", "logo.svg/ >", "logo.svg/", false},
        {"separated_bad_close", "logo.svg / >", "logo.svg", false},
        {"slash_only", "/>", "/", false},
        {"space_slash", " />", "/", false},
        {"slash_space_end", "/ >", "/", false},
        {"two_slashes", "//>", "//", false},
        {"value_and_close", " / />", "/", true},
        {"double_quote_close", "\"logo.svg\"/>", "logo.svg", true},
        {"single_quote_close", "'logo.svg/'/>", "logo.svg/", true},
        {"empty", ">", "", false},
        {"space_empty", " >", "", false},
    };
    for (const auto& c : boundaries) {
        const Attributes expected{{"id", "t"}, {"src", c.value}};
        run(std::string("boundary/void/") + c.name,
            std::string("<body><img id=\"t\" src=") + c.fragment + "</body>", expected);
        // Unlike img, div makes an accidental self-closing transition observable.
        run(std::string("boundary/nonvoid/") + c.name,
            std::string("<body><div id=\"t\" src=") + c.fragment +
                "<span id=\"child\"></span></div></body>", expected, c.self_closing ? 0 : 1);
    }
}

void values_and_entities() {
    struct Case { const char* name; const char* raw; const char* decoded; };
    const Case values[] = {
        {"absolute_port", "https://example.test:8443/a/b?q=1&amp;x=2#frag", "https://example.test:8443/a/b?q=1&x=2#frag"},
        {"protocol_relative", "//example.test/a/b", "//example.test/a/b"},
        {"relative", "a/b", "a/b"},
        {"repeated_trailing", "a///b/", "a///b/"},
        {"utf8", "/caf\xC3\xA9/\xE6\x9D\xB1\xE4\xBA\xAC", "/caf\xC3\xA9/\xE6\x9D\xB1\xE4\xBA\xAC"},
        {"amp", "/a?x=1&amp;y=2", "/a?x=1&y=2"},
        {"numeric_slash", "/a&#47;b", "/a/b"},
        {"hex_slash", "/a&#x2F;b", "/a/b"},
        {"decoded_gt", "/a&gt;/b", "/a>/b"},
        {"decoded_space", "/a&#32;disabled", "/a disabled"},
        {"decoded_quote", "/a&quot;b/c", "/a\"b/c"},
        {"decode_once", "/a&amp;gt;/b", "/a&gt;/b"},
        {"unknown_entity", "/a&bogus;/b", "/a&bogus;/b"},
        {"percent_escape", "/a%2Fb", "/a%2Fb"},
        {"disabled_segment", "/disabled", "/disabled"},
        {"disabled_middle", "/x/disabled/y", "/x/disabled/y"},
        {"no_slash", "plain", "plain"},
    };
    for (const auto& c : values) {
        const Attributes expected{{"id", "t"}, {"data-route", c.decoded}, {"title", "after"}};
        // The raw corpus contains no literal quote/whitespace delimiters. Check
        // each form against a fixed expected value, not just against each other.
        for (const char* quote : {"", "'", "\""}) {
            run(std::string("value/") + c.name + "/" + quote,
                std::string("<input id=\"t\" data-route=") + quote + c.raw + quote + " title=after>",
                expected);
        }
    }
    const Case recovery[] = {
        {"mid_double_quote", "a\"/b", "a\"/b"},
        {"mid_single_quote", "a'/b", "a'/b"},
        {"mid_lt", "a</b", "a</b"},
        {"mid_equal", "a=/b", "a=/b"},
        {"mid_backtick", "a`/b", "a`/b"},
    };
    for (const auto& c : recovery) {
        run(c.name, std::string("<input id=\"t\" data-route=") + c.raw + " title=after>",
            {{"id", "t"}, {"data-route", c.decoded}, {"title", "after"}});
    }
    run("literal_gt_ends_value", "<body><div id=\"t\" data-route=/a/><span id=\"child\"></span></div></body>",
        {{"id", "t"}, {"data-route", "/a/"}}, 1);
    run("quoted_empty", "<input id=\"t\" data-route='' title=after>",
        {{"id", "t"}, {"data-route", ""}, {"title", "after"}});
    run("boolean", "<input id=\"t\" data-route disabled>",
        {{"id", "t"}, {"data-route", ""}, {"disabled", ""}});
}

void whitespace_and_eof() {
    struct Space { const char* name; const char* bytes; };
    for (const auto& space : {Space{"space", " "}, Space{"tab", "\t"}, Space{"lf", "\n"},
                              Space{"ff", "\f"}, Space{"cr", "\r"}, Space{"crlf", "\r\n"}}) {
        run(std::string("whitespace/after/") + space.name,
            std::string("<input id=\"t\" data-route=/a/b") + space.bytes + "title=after disabled>",
            {{"id", "t"}, {"data-route", "/a/b"}, {"title", "after"}, {"disabled", ""}});
        run(std::string("whitespace/before/") + space.name,
            std::string("<input id=\"t\" data-route=") + space.bytes + "/a/b title=after>",
            {{"id", "t"}, {"data-route", "/a/b"}, {"title", "after"}});
    }
    // Safety and compatibility checks for this parser, NOT Blink equivalence:
    // std::isspace also recognizes VT.
    run("existing_vt_divergence", "<input id=\"t\" data-route=x\vdisabled>",
        {{"id", "t"}, {"data-route", "x"}, {"disabled", ""}});
    // Preserve the historical EOF bytes; the entire unfinished opening is discarded.
    run_absent("existing_eof_recovery", "<input id=\"t\" data-route=/a/b");
    run_absent("quoted_eof_control", "<input id=\"t\" data-route='/a/b'");
    const std::string slashes(65536, '/');
    run_absent("long_slashes_eof", "<input id=\"t\" data-route=" + slashes);
    run("long_slashes_next_attribute", "<input id=\"t\" data-route=" + slashes + " title=after>",
        {{"id", "t"}, {"data-route", slashes}, {"title", "after"}});
}

void cascade_regressions() {
    const char* disabled_css = "input{width:11px} input[disabled]{width:37px}";
    for (const char* value : {"/disabled", "'/disabled'"}) {
        run(std::string("cascade/no_false_disabled/") + value,
            std::string("<body><input id=\"t\" data-route=") + value + "></body>",
            {{"id", "t"}, {"data-route", "/disabled"}}, -1, disabled_css, 11.f);
    }
    for (const char* value : {"/x", "'/x'"}) {
        run(std::string("cascade/real_disabled/") + value,
            std::string("<body><input id=\"t\" data-route=") + value + " disabled></body>",
            {{"id", "t"}, {"data-route", "/x"}, {"disabled", ""}}, -1, disabled_css, 37.f);
    }
    const char* path_css = "input{width:11px} input[data-route=\"/a/b\"]{width:37px}";
    for (const char* value : {"/a/b", "'/a/b'"}) {
        run(std::string("cascade/path_equality/") + value,
            std::string("<body><input id=\"t\" data-route=") + value + "></body>",
            {{"id", "t"}, {"data-route", "/a/b"}}, -1, path_css, 37.f);
    }
    const char* child_css = "span{width:11px} #t > span{width:37px}";
    run("cascade/child_unquoted", "<body><div id=\"t\" data-path=a/b/><span id=\"child\"></span></div></body>",
        {{"id", "t"}, {"data-path", "a/b/"}}, 1, child_css, 37.f);
    run("cascade/child_quoted", "<body><div id=\"t\" data-path='a/b/'><span id=\"child\"></span></div></body>",
        {{"id", "t"}, {"data-path", "a/b/"}}, 1, child_css, 37.f);
    run("cascade/existing_nonvoid_self_close", "<body><div id=\"t\" data-path='a/b' /><span id=\"child\"></span></div></body>",
        {{"id", "t"}, {"data-path", "a/b"}}, 0, child_css, 11.f);
}

void reparse_regression() {
    ++cases;
    const std::string name = "reparse";
    Document doc;
    if (!check(aether_doc_create(&doc.handle) == AETHER_OK && doc.handle, name, "document create")) return;
    for (const char* value : {"/disabled", "'/disabled'", "/x/disabled/y"}) {
        const std::string html = std::string("<body><input id=\"t\" data-route=") + value + "></body>";
        if (!check(aether_doc_parse_html(doc.handle, html.c_str()) == AETHER_OK, name, "parse HTML")) return;
        // No borrowed node survives this iteration; reparse invalidates it.
        auto target = aether_doc_get_element_by_id(doc.handle, "t");
        if (!check(target != nullptr, name, "new target exists")) return;
        const char* expected = std::string(value) == "/x/disabled/y" ? "/x/disabled/y" : "/disabled";
        check_attributes(target, {{"id", "t"}, {"data-route", expected}}, name);
        auto body = aether_doc_get_body(doc.handle);
        check(body && aether_node_get_child_count(body) == 1, name, "only current tree retained");
        check(aether_node_get_child(body, 0) == target, name, "new target is sole body child");
    }
}
} // namespace

int main() {
    if (!check(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
               "ABI", "compatible major version")) return 1;
    const auto required = AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME;
    if (!check((aether_abi_capabilities() & required) == required,
               "ABI", "DOM and style runtime capabilities")) return 1;
    original_regressions();
    slash_boundaries();
    values_and_entities();
    whitespace_and_eof();
    cascade_regressions();
    reparse_regression();
    std::printf("HTML unquoted attributes: %d cases, %d checks, %d failures\n", cases, checks, failures);
    return failures ? 1 : 0;
}
