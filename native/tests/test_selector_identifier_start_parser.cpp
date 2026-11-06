#include "style_runtime_types.h"
#include <cstdio>
#include <string>
#include <vector>

// Source-spelling admission, not validation of the decoded name. All checks
// remain active under NDEBUG; no parser types are added to the public ABI.
namespace {
int checks = 0, failures = 0;
std::string printable(const std::string& source) {
    std::string result;
    for (const unsigned char c : source) {
        if (c >= 32 && c < 127 && c != '\\') result += static_cast<char>(c);
        else { char byte[5]; std::snprintf(byte, sizeof(byte), "\\x%02x", c); result += byte; }
    }
    return result;
}
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", printable(label).c_str()); }
}
bool parse(const std::string& source, compiled_selector_s& result) {
    const bool ok = selector_parse(source.c_str(), result);
    check(ok, "accept " + source);
    if (ok) check(result.source == source, "retain source " + source);
    return ok;
}
void invalid(const std::string& source) {
    compiled_selector_s result;
    check(!selector_parse(source.c_str(), result), "reject " + source);
}
void valid(const std::string& source) { compiled_selector_s result; parse(source, result); }
// Five grammar positions share a source-ident requirement. Their decoded
// strings may contain digits, punctuation or a lone dash without restriction.
void decoded_all(const std::string& source, const std::string& expected) {
    const std::vector<std::string> forms = {"." + source, "#" + source, source,
                                           "[" + source + "]", "[x=" + source + "]"};
    for (size_t position = 0; position < forms.size(); ++position) {
        compiled_selector_s result;
        if (!parse(forms[position], result)) continue;
        check(result.steps.size() == 1, "one step " + forms[position]);
        if (result.steps.size() != 1) continue;
        const auto& c = result.steps[0].compound;
        const bool attr = c.attributes.size() == 1;
        bool same = false;
        switch (position) {
        case 0: same = c.classes == std::vector<std::string>{expected} && result.anchor.class_name == expected; break;
        case 1: same = c.ids == std::vector<std::string>{expected} && result.anchor.id == expected; break;
        case 2: same = c.type_name == expected && result.anchor.type_name == expected; break;
        case 3: same = attr && c.attributes[0].name == expected && !c.attributes[0].has_value; break;
        case 4: same = attr && c.attributes[0].name == "x" && c.attributes[0].has_value && c.attributes[0].value == expected; break;
        }
        check(same, "decoded value/anchor " + forms[position]);
        check(result.specificity == (position == 1 ? specificity_s{1,0,0} :
              position == 2 ? specificity_s{0,0,1} : specificity_s{0,1,0}), "specificity " + forms[position]);
        const auto dep = position == 0 ? SELECTOR_DEP_SELF_CLASS : position == 1 ? SELECTOR_DEP_SELF_ID :
                         position == 2 ? SELECTOR_DEP_NONE : SELECTOR_DEP_SELF_ATTRIBUTE;
        check(result.dependencies == dep, "dependencies " + forms[position]);
        check(result.steps[0].relation_to_next == COMB_NONE, "no decoded combinator " + forms[position]);
    }
}
void reject_all(const std::string& name) {
    for (const auto& source : std::vector<std::string>{"."+name, "#"+name, name,
                                                    "["+name+"]", "[x="+name+"]"}) invalid(source);
}
const std::vector<compiled_selector_s>* nested(const compiled_selector_s& result, size_t count,
                                               const std::string& label) {
    const bool shape = result.steps.size() == 1 && result.steps[0].compound.pseudos.size() == 1;
    check(shape, "single pseudo " + label);
    if (!shape) return nullptr;
    const auto& list = result.steps[0].compound.pseudos[0].nested;
    check(bool(list), "present nested vector " + label);
    if (!list) return nullptr;
    check(list->size() == count, "nested survivor count " + label);
    return list->size() == count ? list.get() : nullptr;
}
void recovered(const std::string& bad) {
    for (const std::string fn : {"is", "where"}) {
        for (const std::string& args : {bad+",.safe", ".safe,"+bad, bad+",.safe,"+bad}) {
            const std::string source = ":"+fn+"("+args+")";
            compiled_selector_s result;
            if (!parse(source, result)) continue;
            check(result.specificity == specificity_s{0,fn=="is"?1:0,0}, "discarded specificity " + source);
            check(result.dependencies == SELECTOR_DEP_SELF_CLASS, "discarded dependencies " + source);
            check(result.anchor.is_universal(), "no nested direct anchor " + source);
            check(result.pseudo_element == aether_pseudo_element_t::none, "discarded pseudo-element " + source);
            const auto* list = nested(result, 1, source);
            if (!list) continue;
            const auto& safe = list->front();
            check(safe.steps.size() == 1 && safe.steps[0].compound.classes == std::vector<std::string>{"safe"}, "exact safe AST " + source);
            check(safe.anchor.id.empty() && safe.anchor.class_name == "safe" && safe.anchor.type_name.empty(), "survivor anchor " + source);
            check(safe.specificity == specificity_s{0,1,0} && safe.dependencies == SELECTOR_DEP_SELF_CLASS, "survivor metadata " + source);
        }
        const std::string source = ":"+fn+"("+bad+")";
        compiled_selector_s result;
        if (parse(source, result)) {
            nested(result, 0, source);
            check(result.anchor.is_universal() && result.dependencies == SELECTOR_DEP_NONE &&
                  result.specificity == specificity_s{0,0,0}, "empty metadata " + source);
        }
    }
    for (const std::string fn : {"not", "has"}) {
        invalid(":"+fn+"("+bad+",.safe)");
        invalid(":"+fn+"(.safe,"+bad+")");
    }
}
void lexical() {
    for (const std::string name : {"a", "a1", "_", "_1", "-a", "-_", "--", "--1", "é", "😀", "-é"}) decoded_all(name, name);
    for (const std::string name : {"0", "123", "1e3", "1px", "-0", "-1", "-1e3", "-1px", "-", "+1", ".5", "-.5"}) {
        reject_all(name);
        invalid("."+name+"/**/.safe");
        invalid("#"+name+":hover");
    }
    struct escape_case { const char* source; const char* decoded; };
    for (const auto& row : std::vector<escape_case>{{R"(\31 23)","123"}, {R"(\00003123)","123"},
            {R"(\000031a)","1a"}, {R"(\-1)","-1"}, {R"(-\31)","-1"}, {R"(\-)","-"},
            {R"(\+)","+"}, {R"(\.)","."}, {R"(\,)",","}, {R"(\))",")"}, {R"(\])","]"},
            {R"(\{)","{"}, {R"(\2c )",","}, {R"(\0)","�"}, {R"(\d800)","�"}, {R"(\110000)","�"}})
        decoded_all(row.source, row.decoded);
    for (const std::string ws : {" ", "\t", "\n", "\r", "\f", "\r\n"}) decoded_all("\\31"+ws+"23", "123");
    for (const std::string nl : {"\n", "\r", "\f", "\r\n"}) {
        reject_all("\\"+nl+"x"); reject_all("-\\"+nl+"x"); reject_all("a\\"+nl+"x");
        valid("[x=\"1\\"+nl+"23\"]");
    }
    // These are actual selector EOF spans, not stylesheet braces after escapes.
    for (const std::string prefix : {"", ".", "#"}) for (const std::string name : {"\\", "-\\"}) {
        compiled_selector_s result;
        const std::string source = prefix+name;
        if (!parse(source, result) || result.steps.size() != 1) continue;
        const auto& c = result.steps[0].compound;
        const std::string expected = (name[0]=='-' ? "-" : "") + std::string("�");
        check(prefix.empty() ? c.type_name==expected : prefix=="." ? c.classes==std::vector<std::string>{expected} :
              c.ids==std::vector<std::string>{expected}, "EOF escape data " + source);
    }
    for (const std::string& bad : {std::string("\x80"), std::string("\xc0\xaf"), std::string("\xe2"),
            std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80")})
        for (const std::string prefix : {"", "a", "\\", "-"}) reject_all(prefix+bad);
    for (const std::string op : {"=", "~=", "|=", "^=", "$=", "*="}) {
        invalid("[x"+op+"123]"); valid("[x"+op+R"(\31 23])");
        for (const std::string value : {"123", "-", "", "-->", "a,)]{"}) valid("[x"+op+"\""+value+"\"]");
    }
    for (const std::string flag : {"i", "I", "s", "S", R"(\69)", R"(\73)"}) valid("[x=ok "+flag+"]");
    for (const std::string flag : {"123", "-", "-1", "-->", R"(\31)"}) invalid("[x=ok "+flag+"]");
    for (const std::string source : {R"(:HOVER)", R"(:\68 over)", R"(::\62 efore)", ":nth-child(2n+1)",
            "./**/name", R"(./**/\31 23)", ".--/**/.x", R"(.\31 /**/.x)", R"(.\31  /**/.x)"}) valid(source);
    for (const std::string source : {":123", ":-1", "::-", ":is /**/(.a)", "#/**/a", R"(#/**/\31 23)",
            "-/**/a", "-/**/-a", R"(.\31/**/23)", ". --"}) invalid(source);
    for (const bool descendant : {false, true}) {
        compiled_selector_s result;
        const std::string source = descendant ? R"(.\31  /**/.x)" : R"(.\31 /**/.x)";
        if (parse(source, result)) check(result.steps.size() == (descendant ? 2u : 1u) &&
            result.steps[0].relation_to_next == (descendant ? COMB_DESCENDANT : COMB_NONE), "escape terminator vs real space " + source);
    }
}
void cdc() {
    for (const std::string source : {".-->a", "-->a", ".-->a:hover", "[-->]", "[x=-->]", ".a -->b", "./**/-->a"}) invalid(source);
    for (const std::string source : {"#-->a", ".-- > a", ".--/**/>a", R"(.-\->a)", "--/**/>a", ".a-->b", "a-->b"}) {
        compiled_selector_s result;
        if (!parse(source, result)) continue;
        check(result.steps.size() == 2, "CDC two compounds " + source);
        if (result.steps.size() != 2) continue;
        const auto& right = result.steps[0]; const auto& left = result.steps[1];
        const bool interior = source==".a-->b" || source=="a-->b";
        const std::string expected = interior ? "a--" : "--";
        check(right.compound.type_name == (interior ? "b" : "a") && right.relation_to_next == COMB_CHILD &&
              left.relation_to_next == COMB_NONE, "CDC exact child " + source);
        check(source[0]=='#' ? left.compound.ids == std::vector<std::string>{expected} : source[0]=='.' ?
              left.compound.classes == std::vector<std::string>{expected} : left.compound.type_name == expected,
              "CDC exact left name " + source);
        check(result.anchor.id.empty() && result.anchor.class_name.empty() && result.anchor.type_name == right.compound.type_name,
              "CDC right-only anchor " + source);
    }
    decoded_all(R"(--\>a)", "-->a");
    compiled_selector_s result;
    if (parse(":is(#-->a,.safe)", result)) {
        nested(result, 2, "hash CDC");
        check(result.specificity == specificity_s{1,0,1} && result.dependencies ==
              (SELECTOR_DEP_SELF_ID|SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_ANCESTOR), "hash CDC metadata retained");
    }
}
void recovery() {
    for (const std::string bad : {"#123:hover", "#ghost:hover.123", ".ancestor > #ghost:hover[data-x=123]",
            "#ghost:hover.123::before", "#ghost:hover::before.123", ".-->a", "#ghost:hover.-->a",
            R"(.1\,#ghost:hover)", R"(.1\)#ghost:hover)", R"(.1\]#ghost:hover)", R"(.1\{#ghost:hover)"}) recovered(bad);
    for (const std::string source : {":is(:not(.safe,.123),.safe)", ":where(:has(.safe,.123),.safe)"}) {
        compiled_selector_s result;
        if (parse(source, result)) {
            nested(result, 1, source);
            check(result.dependencies == SELECTOR_DEP_SELF_CLASS, "strict rejection local " + source);
        }
    }
    struct row { const char* source; specificity_s spec; selector_dependency_t deps; size_t count; };
    for (const auto& r : std::vector<row>{{".outer:is(.123)",{0,1,0},SELECTOR_DEP_SELF_CLASS,0},
            {":not(:is(.123))",{0,0,0},SELECTOR_DEP_NONE,1}, {":has(:is(.123))",{0,0,0},SELECTOR_DEP_DESCENDANT,1},
            {":is(#ghost:is(.123),.safe)",{1,0,0},SELECTOR_DEP_SELF_ID|SELECTOR_DEP_SELF_CLASS,2},
            {R"(:is(#\31 23:hover,.safe))",{1,1,0},SELECTOR_DEP_SELF_ID|SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_SELF_STATE,2}}) {
        compiled_selector_s result;
        if (!parse(r.source, result)) continue;
        const auto* list = nested(result, r.count, r.source);
        check(result.specificity == r.spec && result.dependencies == r.deps, "composition metadata " + std::string(r.source));
        if (list && std::string(r.source)==":is(#ghost:is(.123),.safe)") {
            check(list->front().specificity == specificity_s{1,0,0} && list->front().dependencies == SELECTOR_DEP_SELF_ID,
                  "impossible valid ID branch retains only its ID metadata");
            nested(list->front(), 0, "impossible branch internal empty IS");
        }
    }
    valid(":has(:is(.a::before))"); invalid(":has(.a::before)");
    for (const std::string source : {".123,.safe", ".safe,.123", ".123,.safe,.123", ".-->a,.safe", "#123:hover,.safe"}) {
        std::vector<compiled_selector_s> list;
        check(selector_parse_list(source.c_str(), list) && list.size() == 1 && list[0].anchor.class_name == "safe" &&
              list[0].specificity == specificity_s{0,1,0} && list[0].dependencies == SELECTOR_DEP_SELF_CLASS,
              "public forgiving survivors " + source);
    }
    for (const std::string source : {".123", ".123,#-1", ".-->a,#123", ""}) {
        std::vector<compiled_selector_s> list;
        check(!selector_parse_list(source.c_str(), list) && list.empty(), "public requires survivor " + source);
    }
}
}
int main() {
    lexical(); cdc(); recovery();
    std::printf("SELECTOR_IDENTIFIER_START_PARSER CHECKS=%d FAILURES=%d\n", checks, failures);
    return failures ? 1 : 0;
}
