// HTML attribute-name identity at the selector compiler/matcher boundary.
// Fixture maps contain canonical names, as required by the DOM invariant.
// Every check and operation remains active with NDEBUG.
#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", label.c_str()); }
}
bool compile(const std::string& source, compiled_selector_s& out) {
    out = compiled_selector_s{};
    const bool ok = selector_parse(source.c_str(), out);
    check(ok, "compile " + source);
    if (ok) check(out.source == source, "source retained " + source);
    return ok;
}
aether_node_s element() {
    aether_node_s node;
    node.type = AETHER_NODE_ELEMENT;
    node.tag_name = "div";
    return node;
}
void expect(const compiled_selector_s& s, aether_node_s& node, bool expected,
            const std::string& label) {
    const auto before = node.attributes;
    check(selector_match(s, &node) == expected, label);
    check(node.attributes == before, "matching does not mutate " + label);
}
void expect(const std::string& source, aether_node_s& node, bool expected) {
    compiled_selector_s s;
    if (compile(source, s)) expect(s, node, expected, source);
}
void invalid(const std::string& source) {
    compiled_selector_s s;
    check(!selector_parse(source.c_str(), s), "reject " + source);
}
void metadata(const compiled_selector_s& s, specificity_s specificity, unsigned dependencies) {
    check(s.specificity == specificity, s.source + " specificity");
    check(s.dependencies == dependencies, s.source + " exact dependencies");
}
void attr_ast(const compiled_selector_s& s, const std::string& name,
              attribute_match_operator_t op, const std::string& value, bool insensitive) {
    const bool shape = s.steps.size() == 1 && s.steps[0].compound.attributes.size() == 1;
    check(shape, s.source + " single attribute AST");
    if (!shape) return;
    const auto& a = s.steps[0].compound.attributes[0];
    check(a.name == name, s.source + " canonical decoded name");
    check(a.op == op && a.has_value == (op != ATTR_EXISTS), s.source + " operator/presence");
    check(a.value == value, s.source + " value preserved");
    check(a.case_insensitive == insensitive, s.source + " value flag");
    metadata(s, {0,1,0}, SELECTOR_DEP_SELF_ATTRIBUTE);
    check(s.anchor.is_universal(), s.source + " universal anchor");
}
const char* names[] = {"data-x", "DATA-X", "DaTa-X", R"(\44 ATA-\58 )", R"(\000044ATA-X)"};
const char* flags[] = {"", " i", " I", " s", " S"};
void matrix() {
    struct row { const char* op; attribute_match_operator_t kind; const char* actual; const char* miss; };
    const row rows[] = {
        {"=", ATTR_EQUALS, "Value", "Values"},
        {"~=", ATTR_INCLUDES, "pre\tValue\npost", "pre ValueX post"},
        {"|=", ATTR_DASH_MATCH, "Value-tail", "ValueX-tail"},
        {"^=", ATTR_PREFIX, "Value-tail", "xValue"},
        {"$=", ATTR_SUFFIX, "head-Value", "ValueX"},
        {"*=", ATTR_CONTAINS, "head-Value-tail", "head-other-tail"},
    };
    for (const auto* name : names) {
        compiled_selector_s presence;
        const auto source = std::string("[") + name + "]";
        if (compile(source, presence)) {
            attr_ast(presence, "data-x", ATTR_EXISTS, "", false);
            auto node = element();
            expect(presence, node, false, source + " absent");
            for (const char* value : {"", "Value", " \t\r\n\f"}) {
                node.attributes["data-x"] = value;
                expect(presence, node, true, source + " present");
            }
        }
        for (const auto* flag : flags) for (const auto& row : rows) {
            const bool insensitive = std::string(flag) == " i" || std::string(flag) == " I";
            for (const auto* operand : {"Value", "value"}) {
                const auto selector = std::string("[") + name + row.op + "\"" + operand + "\"" + flag + "]";
                compiled_selector_s s;
                if (!compile(selector, s)) continue;
                attr_ast(s, "data-x", row.kind, operand, insensitive);
                auto node = element();
                expect(s, node, false, selector + " absent");
                node.attributes["data-x"] = row.actual;
                expect(s, node, insensitive || std::string(operand) == "Value", selector + " positive/case");
                node.attributes["data-x"] = row.miss;
                expect(s, node, false, selector + " negative boundary");
                node.attributes["data-x"] = "";
                expect(s, node, false, selector + " empty actual");
            }
            const auto selector = std::string("[") + name + row.op + "\"\"" + flag + "]";
            compiled_selector_s empty;
            if (!compile(selector, empty)) continue;
            attr_ast(empty, "data-x", row.kind, "", insensitive);
            auto node = element();
            expect(empty, node, false, selector + " absent");
            node.attributes["data-x"] = "";
            expect(empty, node, row.kind == ATTR_EQUALS || row.kind == ATTR_DASH_MATCH, selector + " empty actual");
            node.attributes["data-x"] = "-tail";
            expect(empty, node, row.kind == ATTR_DASH_MATCH, selector + " dash actual");
            node.attributes["data-x"] = "Value";
            expect(empty, node, false, selector + " nonempty actual");
        }
        for (const auto* flag : flags) {
            auto node = element();
            node.attributes["data-x"] = "\tValue\r\n\f Other ";
            expect(std::string("[") + name + "~=Value" + flag + "]", node, true);
            expect(std::string("[") + name + "~=\"Value Other\"" + flag + "]", node, false);
            node.attributes["data-x"] = " \t\r\n\f";
            expect(std::string("[") + name + "~=Value" + flag + "]", node, false);
        }
    }
}
void decoded_identity() {
    struct row { const char* source; const char* name; const char* value; };
    const row rows[] = {
        {R"([\44 ATA-X=\56 alue])", "data-x", "Value"},
        {R"([\044 ATA-X="\56 alue"])", "data-x", "Value"},
        {R"([\0044 ATA-X="Va\6c ue"])", "data-x", "Value"},
        {R"([\00044 ATA-X='Value'])", "data-x", "Value"},
        {R"([\000044ATA-X=Value])", "data-x", "Value"},
        // One-digit escapes cannot encode ASCII uppercase; preserve their byte.
        {R"([DATA-\1 X=Value])", "data-\x01x", "Value"},
        {R"([\G\H-X=Value])", "gh-x", "Value"},
        {R"([DATA\:X=Value])", "data:x", "Value"},
        {R"([DATA\.X=Value])", "data.x", "Value"},
        {R"([DATA_X=Value])", "data_x", "Value"},
        {R"([ABCDEFGHIJKLMNOPQRSTUVWXYZ=Value])", "abcdefghijklmnopqrstuvwxyz", "Value"},
        {R"([DATA-\C4 ="ÄValue"])", "data-Ä", "ÄValue"},
        {"[DATA-Ä=\"ÄValue\"]", "data-Ä", "ÄValue"},
        {"[DATA-é=\"é\"]", "data-é", "é"},
    };
    for (const auto& row : rows) {
        compiled_selector_s s;
        if (!compile(row.source, s)) continue;
        attr_ast(s, row.name, ATTR_EQUALS, row.value, false);
        auto node = element();
        node.attributes[row.name] = row.value;
        expect(s, node, true, row.source);
        node.attributes[row.name] = "value";
        expect(s, node, false, std::string(row.source) + " value remains exact");
    }
    auto node = element();
    node.attributes["data-ä"] = "Value";
    expect("[DATA-Ä=Value]", node, false);
    node.attributes["data-é"] = "Value";
    expect("[DATA-é=Value]", node, false);
    node.attributes["data-x"] = "Ä";
    expect("[DATA-X=ä]", node, false);
    node.attributes["id"] = "TaRget";
    node.attributes["class"] = "Kept";
    expect("#TaRget.Kept[DATA-X]", node, true);
    expect("#target.Kept[DATA-X]", node, false);
    expect("#TaRget.kept[DATA-X]", node, false);
    compiled_selector_s s;
    if (compile("#TaRget.Kept[DATA-X]", s)) {
        check(s.steps[0].compound.ids == std::vector<std::string>{"TaRget"} &&
              s.steps[0].compound.classes == std::vector<std::string>{"Kept"}, "ID/class AST values unchanged");
        metadata(s, {1,2,0}, SELECTOR_DEP_SELF_ID | SELECTOR_DEP_SELF_CLASS | SELECTOR_DEP_SELF_ATTRIBUTE);
    }
}
void composition() {
    auto parent = element(), child = element(), next = element();
    parent.attributes["data-parent"] = "P";
    child.attributes["data-x"] = "Value";
    next.attributes["data-next"] = "N";
    child.parent = next.parent = &parent;
    parent.children = {&child, &next};
    struct row { const char* source; aether_node_s* subject; bool match; specificity_s spec; unsigned deps; };
    const row rows[] = {
        {":is([DATA-X=Value])", &child, true, {0,1,0}, SELECTOR_DEP_SELF_ATTRIBUTE},
        {":where([DaTa-X=Value])", &child, true, {0,0,0}, SELECTOR_DEP_SELF_ATTRIBUTE},
        {":not([DATA-X=value])", &child, true, {0,1,0}, SELECTOR_DEP_SELF_ATTRIBUTE},
        {":not([DATA-X=Value])", &child, false, {0,1,0}, SELECTOR_DEP_SELF_ATTRIBUTE},
        {":has([DATA-X=Value])", &parent, true, {0,1,0}, SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_DESCENDANT},
        {"[DATA-PARENT] [DATA-X]", &child, true, {0,2,0}, SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_ANCESTOR},
        {"[DATA-PARENT] > [DATA-X]", &child, true, {0,2,0}, SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_ANCESTOR},
        {"[DATA-X] + [DATA-NEXT]", &next, true, {0,2,0}, SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_SIBLING},
        {"[DATA-X] ~ [DATA-NEXT]", &next, true, {0,2,0}, SELECTOR_DEP_SELF_ATTRIBUTE | SELECTOR_DEP_SIBLING},
        {"[DATA-X=Value]::before", &child, true, {0,1,1}, SELECTOR_DEP_SELF_ATTRIBUTE},
        {"[DATA-X=Value]::after", &child, true, {0,1,1}, SELECTOR_DEP_SELF_ATTRIBUTE},
    };
    for (const auto& row : rows) {
        compiled_selector_s s;
        if (!compile(row.source, s)) continue;
        metadata(s, row.spec, row.deps);
        expect(s, *row.subject, row.match, row.source);
        for (const auto& step : s.steps) {
            for (const auto& a : step.compound.attributes)
                check(a.name.find("DATA") == std::string::npos, "direct compound canonicalized");
            for (const auto& p : step.compound.pseudos) if (p.nested) {
                check(p.nested->size() == 1, "one logical branch");
                for (const auto& nested : *p.nested)
                    check(nested.steps[0].compound.attributes[0].name == "data-x", "nested attribute canonicalized");
            }
        }
        if (s.pseudo_element != aether_pseudo_element_t::none) {
            auto generated = element();
            generated.is_generated = true;
            generated.style_origin = &child;
            generated.pseudo_element = s.pseudo_element;
            generated.attributes["data-x"] = "stale";
            expect(s, generated, true, "pseudo uses live host");
        }
    }
}
void recovery() {
    for (const char* source : {"[ns|DATA-X]", "[*|DATA-X]", "[|DATA-X]", "[DATA:X]",
            "[DATA-X i]", "[DATA-X=Value z]", "[DATA-X=Value ii]", "[DATA-X=Value i s]",
            ":not([ns|DATA-X],[DATA-X])", ":has([ns|DATA-X],[DATA-X])"}) invalid(source);
    for (const auto* pseudo : {"is", "where"}) {
        const auto source = std::string(":") + pseudo + "([ns|DATA-X]:hover,[\\44 ATA-X=Value])";
        compiled_selector_s s;
        if (!compile(source, s)) continue;
        metadata(s, {0,std::string(pseudo) == "where" ? 0 : 1,0}, SELECTOR_DEP_SELF_ATTRIBUTE);
        const auto& p = s.steps[0].compound.pseudos[0];
        check(p.nested && p.nested->size() == 1, "forgiving list keeps exactly valid branch");
        if (p.nested && p.nested->size() == 1)
            check((*p.nested)[0].steps[0].compound.attributes[0].name == "data-x", "recovered name canonical");
        auto node = element(); node.attributes["data-x"] = "Value";
        expect(s, node, true, source);
    }
    std::vector<compiled_selector_s> list;
    check(selector_parse_list("[ns|DATA-X], [DATA-X=Value], [DATA-X=Other], [|DATA-X]", list), "top-level recovery");
    check(list.size() == 2, "invalid namespaces discarded without losing valid selectors");
    if (list.size() == 2) {
        check(list[0].steps[0].compound.attributes[0].value == "Value" &&
              list[1].steps[0].compound.attributes[0].value == "Other", "list source order retained");
        for (const auto& s : list) check(s.steps[0].compound.attributes[0].name == "data-x", "recovered list names canonical");
    }
}
} // namespace
int main() {
    matrix(); decoded_identity(); composition(); recovery();
    std::printf("SELECTOR_ATTRIBUTE_CASE CHECKS=%d FAILURES=%d\n", checks, failures);
    return failures ? 1 : 0;
}
