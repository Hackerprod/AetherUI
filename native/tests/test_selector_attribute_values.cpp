// Independent HTML legacy-value oracle. All checks remain active under NDEBUG.
// AETHER_ATTRIBUTE_VALUES_BASELINE omits only new AST fields for red/green runs.
#include "style_runtime_types.h"
#include "../dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>

namespace {
    bool count_allocations = false;
    size_t allocations = 0;
}

void* operator new(std::size_t n) {
    if (count_allocations)
        ++allocations;
    if (void* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}

void* operator new[](std::size_t n) {
    return ::operator new(n);
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {
    int checks = 0, failures = 0;
    void check(bool ok, const std::string& label) {
        ++checks;
        if (!ok) {
            ++failures;
            if (failures <= 30) std::fprintf(stderr, "FAIL %s\n", label.c_str());
        }
    }

    const char*legacy[] = {
        "accept-charset",
        "accept",
        "align",
        "alink",
        "axis",
        "bgcolor",
        "charset",
        "checked",
        "clear",
        "codetype",
        "color",
        "compact",
        "declare",
        "defer",
        "dir",
        "direction",
        "disabled",
        "enctype",
        "face",
        "frame",
        "hreflang",
        "http-equiv",
        "lang",
        "language",
        "link",
        "media",
        "method",
        "multiple",
        "nohref",
        "noresize",
        "noshade",
        "nowrap",
        "readonly",
        "rel",
        "rev",
        "rules",
        "scope",
        "scrolling",
        "selected",
        "shape",
        "target",
        "text",
        "type",
        "valign",
        "valuetype",
        "vlink"
    };
    static_assert(sizeof(legacy) / sizeof(*legacy) == 46, "independent legacy oracle");
    const char*ordinary[] = {
        "id",
        "class",
        "data-x",
        "title",
        "name",
        "value",
        "href",
        "src",
        "style",
        "role",
        "aria-label",
        "aria-checked",
        "autocomplete",
        "contenteditable",
        "draggable",
        "spellcheck",
        "translate",
        "crossorigin",
        "acceptcharset",
        "httpequiv",
        "selectedness",
        "typex",
        "x-type",
        "type-x",
        "data-type",
        "týpe"
    };
    const char* flags[] = {"", " i", " s"};
    struct Row {
        const char* op;
        attribute_match_operator_t kind;
        const char* actual;
        const char* miss;
    };
    const Row rows[] = {
        {"=", ATTR_EQUALS, "MiXeD", "MiXeDX"},
        {"~=", ATTR_INCLUDES, "pre\tMiXeD\npost", "pre MiXeDX post"},
        {"|=", ATTR_DASH_MATCH, "MiXeD-tail", "MiXeDX-tail"},
        {"^=", ATTR_PREFIX, "MiXeD-tail", "xMiXeD"},
        {"$=", ATTR_SUFFIX, "head-MiXeD", "MiXeDx"},
        {"*=", ATTR_CONTAINS, "head-MiXeD-tail", "head-other-tail"}
    };
    bool parse(const std::string& source, compiled_selector_s& s) {
        bool ok = selector_parse(source.c_str(), s);
        check(ok, "parse " + source);
        if (ok)
            check(s.source == source, "source retained");
        return ok;
    }

    std::string css(const std::string& name, const std::string& op, const std::string& value,
            const std::string& flag) {
        return "[" + name + op + "\"" + value + "\"" + flag + "]";
    }

    aether_node_s node(const std::string& tag = "div") {
        aether_node_s n;
        n.type = AETHER_NODE_ELEMENT;
        n.tag_name = tag;
        return n;
    }

    void expect(const compiled_selector_s& s, aether_node_s& n, bool expected, const std::string& label) {
        const auto before = n.attributes;
        check(selector_match(s, &n) == expected, label);
        check(n.attributes == before, "DOM unchanged");
    }

    void test(const std::string& source, const std::string& actual, bool expected,
            const std::string& name = "type") {
        compiled_selector_s s;
        if (parse(source, s)) {
            auto n = node();
            n.attributes[name] = actual;
            expect(s, n, expected, source);
        }
    }

    void ast(const compiled_selector_s& s, const std::string& name, const Row& row, const std::string& value,
            int mode, bool inherited) {
        check(s.steps.size() == 1 && s.steps[0].compound.attributes.size() == 1, "AST shape");
        if (s.steps.size() != 1 || s.steps[0].compound.attributes.size() != 1)
            return;
        const auto& a = s.steps[0].compound.attributes[0];
        check(a.name == name && a.value == value && a.op == row.kind && a.has_value, "AST bytes/operator");
        check(s.specificity == specificity_s{0, 1, 0}
              && s.dependencies == SELECTOR_DEP_SELF_ATTRIBUTE && s.anchor.is_universal(), "AST metadata");
#ifndef AETHER_ATTRIBUTE_VALUES_BASELINE
        const attribute_value_case_mode modes[] = {
            attribute_value_case_mode::default_html,
            attribute_value_case_mode::ascii_insensitive,
            attribute_value_case_mode::sensitive
        };
        check(a.case_mode == modes[mode], "three-state mode");
        check(a.html_default_ascii_insensitive == inherited, "compiled classification");
#else
        (void) mode;
        (void) inherited;
#endif
    }

    void matrix(const char* const* names, size_t size, bool inherited) {
        for (size_t i = 0; i < size; ++i)
        for (const auto& row : rows)
        for (int mode = 0; mode < 3; ++mode)
        for (const char* tag : {"div", "custom-widget"}) {
            const std::string name = names[i];
            auto n = node(tag);
            for (const char* operand : {"mixed", "MiXeD"}) {
                compiled_selector_s s;
                if (!parse(css(name, row.op, operand, flags[mode]), s))
                    continue;
                ast(s, name, row, operand, mode, inherited);
                expect(s, n, false, "missing attribute");
                n.attributes[name] = row.actual;
                expect(s, n, mode == 1 || (mode == 0 && inherited) || std::string(operand) == "MiXeD",
                        s.source + " mixed positive");
                n.attributes[name] = row.miss;
                expect(s, n, false, s.source + " boundary negative");
                n.attributes.clear();
            }
        }
    }

    void empties() {
        for (const char* name : {"type", "data-x"})
        for (const auto& r : rows)
        for (const char* flag : flags) {
            compiled_selector_s s;
            if (!parse(css(name, r.op, "", flag), s))
                continue;
            auto n = node();
            expect(s, n, false, "empty operand absent");
            for (const char*actual : {"", "-tail", "MiXeD"}) {
                n.attributes[name] = actual;
                expect(s, n, (r.kind == ATTR_EQUALS && !*actual) || (r.kind == ATTR_DASH_MATCH && (!*actual ||
                        *actual == '-')), "empty operand operator");
            }
        }
    }

    void parser_contract() {
        for (const char* name : {"type", "TYPE", "TyPe", R"(\54 YPE)", R"(\000054YPE)"})
        for (int m = 0; m < 3; ++m) {
            compiled_selector_s s;
            if (parse(css(name, "=", "MiXeD", flags[m]), s))
                ast(s, "type", rows[0], "MiXeD", m, true);
        }
        struct F {
            const char* text;
            int mode;
        };
        for (const auto& f : {
            F {" I", 1},
            F {" S", 2},
            F {R"( \69)", 1},
            F {R"( \49)", 1},
            F {R"( \73)", 2},
            F {R"( \53)", 2}
        })
        for (const char* name : {"type", "data-x"}) {
            compiled_selector_s s;
            if (parse(css(name, "=", "MiXeD", f.text), s))
                ast(s, name, rows[0], "MiXeD", f.mode, std::string(name) == "type");
        }
        // Attribute records are value types; copies/moves retain the cached policy.
#ifndef AETHER_ATTRIBUTE_VALUES_BASELINE
        compiled_selector_s original;
        if (parse("[type=MiXeD s]", original)) {
            auto copy = original.steps[0].compound.attributes[0];
            auto moved = std::move(copy);
            check(moved.name == "type" && moved.value == "MiXeD" &&
                    moved.case_mode == attribute_value_case_mode::sensitive &&
                    moved.html_default_ascii_insensitive, "attribute copy/move policy");
        }
#endif
        for (const char* invalid : {
            "[type=x q]",
            "[type=x ii]",
            "[type=x i s]",
            "[type i]",
            "[type=x",
            "[type=x i",
            "[type=x \\\\]",
            "[type=x 9]"
        }) {
            compiled_selector_s s;
            if (!parse("[type=MiXeD s]", s))
                continue;
            check(!selector_parse(invalid, s), std::string("reject ") + invalid);
            ast(s, "type", rows[0], "MiXeD", 2, true);
            check(s.source == "[type=MiXeD s]", "failure preserves source");
        }
        compiled_selector_s s;
        if (parse("[type=MiXeD i][data-x]", s)) {
            const auto& a = s.steps[0].compound.attributes[1];
            check(!a.has_value && a.value.empty() && a.op == ATTR_EXISTS, "presence resets value");
#ifndef AETHER_ATTRIBUTE_VALUES_BASELINE
            check(a.case_mode == attribute_value_case_mode::default_html && !a.html_default_ascii_insensitive,
                    "presence resets policy");
#endif
            auto n = node();
            n.attributes["type"] = "MiXeD";
            expect(s, n, false, "presence absent");
            n.attributes["data-x"] = "";
            expect(s, n, true, "presence empty");
        }
        test(R"([type\:=mixed])", "MiXeD", false, "type:");
        test(R"([type\:=mixed i])", "MiXeD", true, "type:");
    }

    void whitespace_and_unicode() {
        for (const char* flag : flags) {
            for (char c : std::string(" \t\n\r\f")) {
                test(css("type", "~=", "MiXeD", flag), "pre" + std::string(1, c) + "MiXeD" + c + "post", true);
            }
            for (const char* escaped : {R"(a\20 b)", R"(a\9 b)", R"(a\a b)", R"(a\d b)", R"(a\c b)"})
                test(css("type", "~=", escaped, flag), "a b", false);
            test(css("type", "~=", "B", flag), "a\vB", false);
            test(css("type", "~=", R"(a\b B)", flag), "a\vB", true);
            test(css("type", "~=", R"(\b)", flag), "\v", true);
            test(css("type", "~=", "B", flag), "a\u00a0B", false);
            test(css("type", "~=", "a\u00a0B", flag), "a\u00a0B", true);
        }
        for (const auto& p : {
            std::pair<std::string, std::string> {"Ä", "ä"},
            {"İ", "i"},
            {"ß", "SS"},
            {"Σ", "σ"},
            {"é", "e\u0301"},
            {"@", "`"},
            {"[", "{"}
        })
            test(css("type", "=", p.second, " i"), p.first, false);
        test(css("type", "=", "aÄz", " i"), "AÄZ", true);
        test(css("type", "=", "abcdefghijklmnopqrstuvwxyz", " i"), "ABCDEFGHIJKLMNOPQRSTUVWXYZ", true);
        for (int byte = 128; byte < 256; ++byte) {
            std::string value = "A" + std::string(1, static_cast<char>(byte)) + "Z";
            compiled_selector_s s;
            if (!parse("[type=x i]", s))
                continue;
            s.steps[0].compound.attributes[0].value = "a" + std::string(1, static_cast<char>(byte)) + "z";
            auto n = node();
            n.attributes["type"] = value;
            expect(s, n, true, "identical non-ASCII byte");
        }
    }

    void nested_and_long() {
        for (const char* source : {
            ":is([type=mixed])",
            ":where([type=mixed])",
            ":not([type=mixed s])",
            ":is([type=mixed s],[type=mixed i])"
        })
            test(source, "MiXeD", true);
        test(":not([type=mixed])", "MiXeD", false);
        auto parent = node(), child = node();
        child.parent = &parent;
        parent.children.push_back(&child);
        child.attributes["type"] = "MiXeD";
        compiled_selector_s has;
        if (parse(":has([type=mixed])", has)) {
            expect(has, parent, true, "has default");
            child.attributes["type"] = "other";
            expect(has, parent, false, "has mutation");
        }
        for (const char* source : {"#i", ".i"}) {
            auto n = node();
            n.attributes["id"] = "I";
            n.attributes["class"] = "I";
            compiled_selector_s s;
            if (parse(source, s)) expect(s, n, false, "ID/class remain exact");
        }
        test("[class=i i]", "I", true, "class");
        for (const auto& r : rows)
        for (const char* flag : flags) {
            const std::string operand(1024, 'A');
            std::string actual = operand;
            if (r.kind == ATTR_INCLUDES) actual = "prefix " + operand + " suffix";
            if (r.kind == ATTR_DASH_MATCH || r.kind == ATTR_PREFIX) actual = operand + "-tail";
            if (r.kind == ATTR_SUFFIX) actual = "head-" + operand;
            if (r.kind == ATTR_CONTAINS) actual = "head-" + operand;
            auto n = node();
            n.attributes["type"] = actual;
            compiled_selector_s s;
            if (!parse(css("type", r.op, operand, flag), s))
                continue;
            // Only matcher is measured: compilation, labels and DOM copies are outside.
            allocations = 0;
            count_allocations = true;
            bool matches = true;
            for (int repeat = 0; repeat < 20; ++repeat)
                matches = selector_match(s, &n) && matches;
            count_allocations = false;
            check(matches, "long matching");
            check(allocations == 0, "zero matching allocations " + s.source.substr(0, 10));
            n.attributes["type"] = "A";
            expect(s, n, false, "operand longer than actual");
        }
    }
}

int main() {
    matrix(legacy, sizeof(legacy) / sizeof(*legacy), true);
    matrix(ordinary, sizeof(ordinary) / sizeof(*ordinary), false);
    empties();
    parser_contract();
    whitespace_and_unicode();
    nested_and_long();
    std::printf("attribute values: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
