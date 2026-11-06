#include "style_runtime_types.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Private AST regression coverage. Checks and parsing remain active in Release.
namespace {
int checks = 0, failures = 0, variants = 0;
bool canonical_only = false;
void check(bool value, const std::string& label) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL %s\n", label.c_str()); }
}
bool parse(const std::string& source, compiled_selector_s& out) {
    out = compiled_selector_s{};
    const bool ok = selector_parse(source.c_str(), out);
    check(ok, "accept " + source);
    if (ok) check(out.source == source, "source bytes " + source);
    return ok;
}
void same(const compiled_selector_s& a, const compiled_selector_s& b, const std::string& path) {
#define FIELD(x) check(a.x == b.x, path + "." #x)
    FIELD(specificity); FIELD(dependencies); FIELD(pseudo_element);
    FIELD(anchor.id); FIELD(anchor.class_name); FIELD(anchor.type_name);
    FIELD(steps.size());
    for (size_t i = 0; i < std::min(a.steps.size(), b.steps.size()); ++i) {
        const auto& x = a.steps[i]; const auto& y = b.steps[i];
        const auto p = path + ".steps[" + std::to_string(i) + "]";
        check(x.relation_to_next == y.relation_to_next, p + ".combinator");
        const auto& c = x.compound; const auto& d = y.compound;
        check(c.type_name == d.type_name, p + ".type");
        check(c.ids == d.ids, p + ".ids");
        check(c.classes == d.classes, p + ".classes/order");
        check(c.attributes.size() == d.attributes.size(), p + ".attributes.size");
        for (size_t j = 0; j < std::min(c.attributes.size(), d.attributes.size()); ++j) {
            const auto& u = c.attributes[j]; const auto& v = d.attributes[j];
            const auto q = p + ".attributes[" + std::to_string(j) + "]";
            check(u.name == v.name, q + ".name"); check(u.op == v.op, q + ".op");
            check(u.value == v.value, q + ".value"); check(u.has_value == v.has_value, q + ".has_value");
            check(u.case_insensitive == v.case_insensitive, q + ".case_insensitive");
        }
        check(c.pseudos.size() == d.pseudos.size(), p + ".pseudos.size");
        for (size_t j = 0; j < std::min(c.pseudos.size(), d.pseudos.size()); ++j) {
            const auto& u = c.pseudos[j]; const auto& v = d.pseudos[j];
            const auto q = p + ".pseudos[" + std::to_string(j) + "]";
            check(u.kind == v.kind, q + ".kind"); check(u.state == v.state, q + ".state");
            check(u.nth.step == v.nth.step, q + ".nth.step");
            check(u.nth.offset == v.nth.offset, q + ".nth.offset");
            check(bool(u.nested) == bool(v.nested), q + ".nested presence");
            if (u.nested && v.nested) {
                check(u.nested->size() == v.nested->size(), q + ".nested.size");
                for (size_t k = 0; k < std::min(u.nested->size(), v.nested->size()); ++k)
                    same((*u.nested)[k], (*v.nested)[k], q + ".nested[" + std::to_string(k) + "]");
            }
        }
    }
#undef FIELD
}
// Independent literal ABI/state and dependency oracles, not the production mapper.
// Dependency bits: class=1, ID=2, attribute=4, state=8, ancestor=16,
// sibling=32, structure=64, descendant=128.
struct Name { const char* name; const char* args; pseudo_selector_kind_t kind; unsigned state; int classes; unsigned deps; };
const Name catalog[] = {
 {"hover","",PSEUDO_REQUIRED_STATE,1,2,9},
 {"active","",PSEUDO_REQUIRED_STATE,2,2,9},
 {"focus","",PSEUDO_REQUIRED_STATE,4,2,9},
 {"focus-within","",PSEUDO_REQUIRED_STATE,64,2,9},
 {"disabled","",PSEUDO_REQUIRED_STATE,16,2,9},
 {"checked","",PSEUDO_REQUIRED_STATE,32,2,9},
 {"invalid","",PSEUDO_REQUIRED_STATE,8,2,9},
 {"read-only","",PSEUDO_REQUIRED_STATE,128,2,9},
 {"selected","",PSEUDO_REQUIRED_STATE,256,2,9},
 {"expanded","",PSEUDO_REQUIRED_STATE,512,2,9},
 {"enabled","",PSEUDO_FORBIDDEN_STATE,16,2,9},
 {"read-write","",PSEUDO_FORBIDDEN_STATE,128,2,9},
 {"root","",PSEUDO_ROOT,0,2,1},
 {"empty","",PSEUDO_EMPTY,0,2,65},
 {"first-child","",PSEUDO_FIRST_CHILD,0,2,65},
 {"last-child","",PSEUDO_LAST_CHILD,0,2,65},
 {"only-child","",PSEUDO_ONLY_CHILD,0,2,65},
 {"nth-child","(2N+1)",PSEUDO_NTH_CHILD,0,2,65},
 {"is","(.Item)",PSEUDO_IS,0,2,1},
 {"where","(.Item)",PSEUDO_WHERE,0,1,1},
 {"not","(.Item)",PSEUDO_NOT,0,2,1},
 {"has","(.Item)",PSEUDO_HAS,0,2,129},
};
std::string upper(std::string s) { for(auto& c:s) if(c>='a'&&c<='z') c-='a'-'A'; return s; }
std::string hex(unsigned char c,bool six=false) { char buf[32]; std::snprintf(buf,sizeof buf,six?"\\%06X":"\\%X ",c); return buf; }

std::vector<std::string> spellings(const std::string& n) {
    if (canonical_only) return {n};
    const auto u = upper(n);
    auto mixed = n;
    for (size_t i = 0; i < n.size(); i += 2) mixed[i] = u[i];
    std::vector<std::string> out{n, u, mixed, hex(n[0]) + n.substr(1),
        hex(u[0]) + u.substr(1), hex(u[0], true) + u.substr(1)};
    std::string multiple;
    for (size_t i = 0; i < n.size(); ++i) multiple += i % 2 ? hex(u[i]) : n.substr(i, 1);
    out.push_back(multiple);
    for (size_t i = 0; i < n.size(); ++i) {
        if (u[i] >= 'G' && u[i] <= 'Z') {
            out.push_back(n.substr(0, i) + "\\" + u[i] + n.substr(i + 1));
            break;
        }
    }
    for (const char* term : {"\t", "\r\n", "\n", "\f"}) {
        auto esc = hex(u[0]); esc.pop_back();
        out.push_back(esc + term + u.substr(1));
    }
    return out;
}
void equivalent(const std::string& canonical, const std::string& variant) {
    compiled_selector_s a, b;
    const bool ok_a = parse(canonical, a), ok_b = parse(variant, b);
    if (ok_a && ok_b) same(a, b, variant);
}
void invalid(const std::string& source) {
    compiled_selector_s out;
    check(!selector_parse(source.c_str(), out), "reject " + source);
}
const std::vector<compiled_selector_s>* branches(const compiled_selector_s& s, size_t count, const std::string& label) {
    if (s.steps.size() != 1 || s.steps[0].compound.pseudos.size() != 1) {
        check(false, label + " one step/pseudo"); return nullptr;
    }
    const auto& nested = s.steps[0].compound.pseudos[0].nested;
    check(bool(nested), label + " nested exists");
    if (!nested) return nullptr;
    check(nested->size() == count, label + " exact branch count");
    return nested->size() == count ? nested.get() : nullptr;
}
void meta(const std::string& source, specificity_s spec, unsigned deps, int count = -1) {
    compiled_selector_s s;
    if (!parse(source, s)) return;
    check(s.specificity == spec, source + " literal specificity");
    check(s.dependencies == deps, source + " literal dependencies");
    if (count >= 0) branches(s, static_cast<size_t>(count), source);
}
void catalog_matrix() {
    static_assert(sizeof(catalog) / sizeof(catalog[0]) == 22, "complete supported catalog");
    for (const auto& e : catalog) {
        compiled_selector_s canonical;
        const std::string source = std::string(".Item:") + e.name + e.args;
        if (!parse(source, canonical)) continue;
        for (const auto& n : spellings(e.name)) {
            ++variants;
            compiled_selector_s s;
            const auto input = ".Item:" + n + e.args;
            if (!parse(input, s)) continue;
            same(canonical, s, input);
            check(s.specificity == specificity_s{0,e.classes,0}, input + " literal specificity");
            check(s.dependencies == e.deps, input + " literal dependency bits");
            check(s.steps.size() == 1, input + " one step");
            if (s.steps.size() != 1) continue;
            const auto& ps = s.steps[0].compound.pseudos;
            check(ps.size() == 1, input + " one pseudo");
            if (ps.size() != 1) continue;
            check(ps[0].kind == e.kind, input + " literal kind");
            check(ps[0].state == e.state, input + " literal state bits");
            if (e.kind == PSEUDO_NTH_CHILD)
                check(ps[0].nth.step == 2 && ps[0].nth.offset == 1, input + " literal nth");
            if (e.kind >= PSEUDO_IS) branches(s, 1, input);
            else check(!ps[0].nested, input + " no nested list");
        }
    }
}
void metadata_and_policies() {
    struct Case { const char* lower; const char* variant; specificity_s spec; unsigned deps; int count; };
    const Case cases[] = {
        {".Item:hover", ".Item:HOVER", {0,2,0},9,-1},
        {".Item:enabled", ".Item:ENABLED", {0,2,0},9,-1},
        {".Item:root", ".Item:ROOT", {0,2,0},1,-1},
        {".Item:empty", ".Item:EMPTY", {0,2,0},65,-1},
        {".Item:nth-child(2n+1)", ".Item:NTH-CHILD(2n+1)", {0,2,0},65,-1},
        {".Item:is(:hover,.fallback)", ".Item:IS(:HOVER,.fallback)", {0,2,0},9,2},
        {".Item:is(#Target:empty,.fallback)", ".Item:IS(#Target:EMPTY,.fallback)", {1,2,0},67,2},
        {".Item:where(#Target:hover,.fallback)", ".Item:WHERE(#Target:HOVER,.fallback)", {0,1,0},11,2},
        {".Item:not(:hover)", ".Item:NOT(:HOVER)", {0,2,0},9,1},
        {".Parent:has(.Child:hover,.never)", ".Parent:HAS(.Child:HOVER,.never)", {0,3,0},137,2},
        {".Item:hover::before", ".Item:HOVER::BEFORE", {0,2,1},9,-1},
        {".Outer:hover .Item", ".Outer:HOVER .Item", {0,3,0},25,-1},
        {".Toggle:hover + .Item", ".Toggle:HOVER + .Item", {0,3,0},41,-1},
        {".Item:is(:not(:hover),.fallback)", R"(.Item:\49 S(:\4e OT(:\48 OVER),.fallback))", {0,2,0},9,2},
    };
    for (const auto& c : cases) {
        meta(c.lower, c.spec, c.deps, c.count);
        if (!canonical_only) {
            meta(c.variant, c.spec, c.deps, c.count);
            equivalent(c.lower, c.variant);
        }
    }
    for (const char* fn : {"is", "where", "not", "has"}) {
        for (const auto& name : spellings(fn)) {
            const auto start = ":" + name;
            if (std::string(fn) == "not" || std::string(fn) == "has") {
                invalid(start + "(:UNKNOWN)");
                invalid(start + "(.Item,:UNKNOWN)");
                invalid(start + "(#Bad:UNKNOWN,.Item)");
                continue;
            }
            meta(start + "(:UNKNOWN)", {0,0,0}, 0, 0);
            const int classes = std::string(fn) == "where" ? 0 : 1;
            const unsigned deps = 1;
            const auto source = start + "(#Bad:UNKNOWN,.Item)";
            meta(source, {0,classes,0}, deps, 1);
            compiled_selector_s s;
            if (parse(source, s)) {
                const auto* list = branches(s, 1, source);
                if (list) check((*list)[0].anchor.class_name == "Item", source + " retained branch identity");
            }
        }
    }
    for (const auto& name : spellings("is")) {
        meta(":" + name + "(:not(.Item,:UNKNOWN),.fallback)", {0,1,0},1,1);
        const auto source = ":not(:" + name + "(.Item,:UNKNOWN))";
        compiled_selector_s s;
        if (parse(source, s)) {
            const auto* outer = branches(s, 1, source);
            if (outer) branches((*outer)[0], 1, source + " inner");
            check(s.specificity == specificity_s{0,1,0} && s.dependencies == 1, source + " strict/forgiving isolation");
        }
    }
    for (const auto& name : spellings("has")) {
        invalid(":" + name + "(> .Child)");
        invalid(":" + name + "(> .Child,.Child)");
    }
}
void sensitive_arguments() {
    for (const char* fn : {"is", "where", "not", "has"}) {
        for (const auto& n : spellings(fn)) {
            const auto args = R"((DIV.\49 tem#Target[data-V='\41 bC' s],.\69 tem#target[data-v='abc' I]))";
            const auto source = ":" + n + args;
            equivalent(std::string(":") + fn + args, source);
            compiled_selector_s s;
            if (!parse(source, s)) continue;
            const auto* list = branches(s, 2, source);
            if (!list || (*list)[0].steps.empty() || (*list)[1].steps.empty()) continue;
            const auto& a = (*list)[0].steps[0].compound;
            const auto& b = (*list)[1].steps[0].compound;
            check(a.type_name == "div", source + " type normalized separately");
            check(a.classes == std::vector<std::string>{"Item"} && b.classes == std::vector<std::string>{"item"}, source + " class identity");
            check(a.ids == std::vector<std::string>{"Target"} && b.ids == std::vector<std::string>{"target"}, source + " ID identity");
            check(a.attributes.size() == 1 && b.attributes.size() == 1, source + " attribute count");
            if (a.attributes.size() != 1 || b.attributes.size() != 1) continue;
            check(a.attributes[0].name == "data-V" && b.attributes[0].name == "data-v", source + " attribute names");
            check(a.attributes[0].value == "AbC" && b.attributes[0].value == "abc", source + " attribute values");
            check(!a.attributes[0].case_insensitive && b.attributes[0].case_insensitive, source + " attribute flags");
        }
    }
    equivalent(".HOVER#IS[data-v='HOVER']:hover:focus", canonical_only ? ".HOVER#IS[data-v='HOVER']:hover:focus" : ".HOVER#IS[data-v='HOVER']:HOVER:FOCUS");
    equivalent(":nth-child(odd)", canonical_only ? ":nth-child(ODD)" : ":NTH-CHILD(ODD)");
    equivalent(":nth-child(2n+1)", canonical_only ? ":nth-child(2N+1)" : ":NTH-CHILD(2N+1)");
}
void negatives_and_elements() {
    meta(":WHERE( )", {0,0,0}, 0, 0);
    for (const auto& e : catalog) {
        for (const auto& n : spellings(e.name)) {
            if (e.kind <= PSEUDO_ONLY_CHILD) invalid(":" + n + "(1)");
            else {
                invalid(":" + n); invalid(":" + n + "(.Item");
                if (e.kind == PSEUDO_IS || e.kind == PSEUDO_WHERE) meta(":" + n + "()", {0,0,0}, 0, 0);
                else invalid(":" + n + "()");
            }
        }
    }
    for (const auto& n : spellings("nth-child")) {
        invalid(":" + n + "(bogus)"); invalid(":" + n + "(2n of .Item)");
    }
    for (const char* source : {":UNKNOWN", ":HOVER-extra", ":hover()", ":before", ":BEFORE", "::hover", "::BEFORE()", "::unknown", "::before::after", ".Item::BEFORE > .Child",
        R"(::\42EFORE)", R"(:\ACTIVE)", R"(:\0 HOVER)", R"(:\d800 HOVER)", R"(:\110000 HOVER)", R"(:is\28 (.Item))", R"(:\3a HOVER)",
        ":\xc5\xbf" "ELECTED", ":\xc4\xb0" "S(.Item)", ":h\xce\xbf" "ver", R"(:\17f ELECTED)", R"(:\130 S(.Item))", R"(:h\3bf ver)", ":ho\\\nver"}) invalid(source);
    for (const char* fn : {"is", "where", "has", "not"}) {
        for (const auto& n : spellings(fn)) {
            // Decoded punctuation remains identifier data, not another pseudo.
            const auto balanced = ":" + n + R"((:\3a HOVER,.safe))";
            if (std::string(fn) == "not" || std::string(fn) == "has") invalid(balanced);
            else meta(balanced, {0,std::string(fn) == "where" ? 0 : 1,0}, 1,1);
        }
    }
    for (const char* element : {"before", "after"}) {
        for (const auto& n : spellings(element)) {
            compiled_selector_s s;
            const auto source = "::" + n;
            if (!parse(source, s)) continue;
            check(s.pseudo_element == (std::string(element) == "before" ? aether_pseudo_element_t::before : aether_pseudo_element_t::after), source + " literal element kind");
            check(s.specificity == specificity_s{0,0,1} && s.dependencies == 0, source + " element metadata");
            equivalent(std::string("::") + element, source);
        }
    }
}
}
int main(int argc, char** argv) {
    canonical_only = argc == 2 && std::string(argv[1]) == "--canonical-only";
    catalog_matrix(); metadata_and_policies(); sensitive_arguments(); negatives_and_elements();
    std::printf("PSEUDO_NAME_PARSER CHECKS=%d FAILURES=%d NAME_VARIANTS=%d MODE=%s\n", checks, failures, variants, canonical_only ? "canonical" : "full");
    return failures ? 1 : 0;
}
