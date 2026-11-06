#include "style_runtime_types.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Private AST regression coverage. Checks and parsing remain active in Release.
namespace {
int checks = 0, failures = 0;
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
void equivalent(const std::string& canonical, const std::string& variant) {
    compiled_selector_s a, b;
    const bool ok_a = parse(canonical, a), ok_b = parse(variant, b);
    if (ok_a && ok_b) same(a, b, variant);
}
void invalid(const std::string& source) {
    compiled_selector_s out;
    check(!selector_parse(source.c_str(), out), "reject " + source);
}
void metadata(const std::string& source, specificity_s spec, unsigned deps) {
    compiled_selector_s s;
    if (parse(source,s)) {
        check(s.specificity == spec, source + " specificity");
        check(s.dependencies == deps, source + " dependencies");
    }
}
void matrix() {
    const std::vector<std::string> bad = {
        ":aether-unknown", ":nth-child(bogus)", ".other >", "", " \t\n", "/**/",
        " /* , ) ] { */ ", ".bad\\\nname", ".bad\\\rname", ".bad\\\fname",
        ".bad\\\r\nname", "[x=]", ".other !", "> .ready", "+ .ready", "~ .ready"
    };
    for (const auto& branch : bad) {
        for (const auto& body : {branch+",.ready,.other", ".ready,"+branch+",.other", ".ready,.other,"+branch}) {
            invalid(".card:has("+body+")");
            for (const std::string fn : {"is", "where"})
                equivalent(".card:"+fn+"(.safe)", ".card:"+fn+"(:has("+body+"),.safe)");
            invalid(".card:not(:has("+body+"),.safe)");
        }
    }
    // Explicit two-branch repros, including empty first/middle/final branches.
    for (const std::string body : {".ready,:aether-unknown", ":aether-unknown,.ready",
            ".ready,", ",.ready", ".ready,,.other", ".ready,:nth-child(bogus)", ".ready,.other >"})
        invalid(".card:has("+body+")");
    for (const std::string body : {"", " ", "/**/", ":unknown", ":unknown,:nth-child(bogus)"}) {
        for (const std::string fn : {"has", "not"}) invalid(":"+fn+"("+body+")");
        for (const std::string fn : {"is", "where"}) {
            compiled_selector_s s;
            if (parse(":"+fn+"("+body+")", s)) {
                const bool shape = s.steps.size()==1 && s.steps[0].compound.pseudos.size()==1;
                check(shape, "empty forgiving shape");
                if (!shape) continue;
                const auto& pseudo = s.steps[0].compound.pseudos[0];
                check(pseudo.nested && pseudo.nested->empty(), "present empty forgiving list");
                check(s.specificity==specificity_s{0,0,0} && s.dependencies==0, "empty forgiving metadata");
            }
        }
    }
    // Valid syntax is independent of whether a DOM could match these branches.
    for (const std::string body : {".ready", ".ready,.other", ".missing", "body .leaf",
            ".mid > .leaf", ".pre + .leaf", ".pre ~ .leaf", ":has(.leaf)",
            ":is(.ready,:aether-unknown)", ":where(.ready,:unknown)", ".ready,#never"}) {
        compiled_selector_s s; parse(".card:has("+body+")",s);
    }
    equivalent(".card:is(.ready)", ".card:is(.ready,:aether-unknown)");
    equivalent(".card:where(.ready)", ".card:where(.ready,:aether-unknown)");
    equivalent(".card:is(.safe)", ".card:is(:has(.ready,:aether-unknown),.safe)");
    invalid(".card:not(.ready,:aether-unknown)");
}
void protected_delimiters() {
    for (const std::string branch : {R"([x="a,b"])", R"(.comma\,name)", R"(#comma\,name)",
            R"(.paren\)name)", R"(.paren\(name)", ".a/* , ) ] { */.b",
            ":is(.a,.b)", ":not(.a,.b)", ":has(.a,.b)"}) {
        compiled_selector_s s, leaf, safe;
        if (!parse(":has("+branch+",.safe)",s) || !parse(branch,leaf) || !parse(".safe",safe)) continue;
        check(s.steps.size()==1,"protected outer step count");
        if(s.steps.size()!=1)continue;
        const auto& ps=s.steps[0].compound.pseudos;
        check(ps.size()==1 && ps[0].kind==PSEUDO_HAS && ps[0].nested && ps[0].nested->size()==2,"protected delimiters retain two branches");
        if(ps.size()==1 && ps[0].nested && ps[0].nested->size()==2) {
            same((*ps[0].nested)[0],leaf,"protected first branch "+branch);
            same((*ps[0].nested)[1],safe,"protected second branch");
        }
    }
    equivalent(":has(.a.b,.safe)",":has(.a/* , ) ] { */.b,.safe)");
}
void spellings() {
    // Same ASCII/case/escape families as test_pseudo_name_parser.
    const std::vector<std::string> names = {"has","HAS","HaS",R"(\68 as)",R"(\48 AS)",
        R"(\000048AS)",R"(h\41 s)",R"(\Has)","\\48\tAS","\\48\r\nAS","\\48\nAS","\\48\fAS"};
    for (const auto& name:names) {
        equivalent(".card:has(.ready,#never)",".card:"+name+"(.ready,#never)");
        invalid(".card:"+name+"(.ready,:unknown)");
        invalid(".card:"+name+"(:unknown,.ready)");
        invalid(".card:"+name+"(> .ready,.ready)");
    }
}
void bounded_nesting() {
    // Exercise repeated success/failure cleanup at bounded depths only. This is
    // not a guarantee of unlimited recursion or a global complexity bound.
    for (const int depth : {1, 4, 16, 32}) {
        std::string valid=".ready";
        std::string rejected=":has(#bad:hover,:unknown)";
        for (int i=0;i<depth;++i) {
            valid=":has("+valid+")";
            if (i+1<depth) rejected=":has("+rejected+")";
        }
        for (int cycle=0;cycle<8;++cycle) {
            metadata(valid,{0,1,0},SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_DESCENDANT);
            invalid(rejected);
            for (const std::string fn : {"is", "where"}) {
                const auto recovered=".card:"+fn+"("+rejected+",.safe)";
                equivalent(".card:"+fn+"(.safe)",recovered);
                metadata(recovered,{0,fn=="where"?1:2,0},SELECTOR_DEP_SELF_CLASS);
            }
        }
    }
}
void transactions_and_metadata() {
    metadata(".card:has(.ready,#never)",{1,1,0},SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_SELF_ID|SELECTOR_DEP_DESCENDANT);
    metadata(".Parent:has(.Child:hover,.never)",{0,3,0},137);
    metadata(".card:is(:has(#missing:hover,:unknown),.safe)",{0,2,0},SELECTOR_DEP_SELF_CLASS);
    metadata(".card:where(:has(#missing:hover,:unknown),.safe)",{0,1,0},SELECTOR_DEP_SELF_CLASS);
    metadata(".card:has(:is(#bad:hover:unknown,.ready))",{0,2,0},SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_DESCENDANT);
    for (const auto& branches : std::vector<std::vector<std::string>>{
            {".card", ".ready", "#never"}, {".Parent", ".Child:hover", ".never"}}) {
        compiled_selector_s outer;
        if (!parse(branches[0]+":has("+branches[1]+","+branches[2]+")",outer)) continue;
        check(outer.steps.size()==1,"metadata example outer shape");
        if (outer.steps.size()!=1) continue;
        const auto& ps=outer.steps[0].compound.pseudos;
        check(ps.size()==1 && ps[0].kind==PSEUDO_HAS && ps[0].nested && ps[0].nested->size()==2,"metadata example exact nested count/kind");
        if (ps.size()!=1 || !ps[0].nested || ps[0].nested->size()!=2) continue;
        for (size_t i=0;i<2;++i) {
            compiled_selector_s expected;
            if(parse(branches[i+1],expected))same((*ps[0].nested)[i],expected,"metadata example exact recursive branch");
        }
    }
    equivalent(".card:is(.safe)",".card:is(:has(#missing:hover,:unknown),.safe)");
    equivalent(".card:where(.safe)",".card:where(:has(#missing:hover,:unknown),.safe)");
    equivalent(".card:has(:is(.ready))",".card:has(:is(#bad:hover:unknown,.ready))");
    for (const std::string branch : {"#bad:hover .leaf", "#bad:hover + .leaf", "#bad:hover ~ .leaf"})
        equivalent(".card:is(.safe)",".card:is(:has("+branch+",:unknown),.safe)");
    compiled_selector_s sentinel, safe;
    if (!parse("#sentinel:hover > .anchor",sentinel) || !parse(".safe",safe)) return;
    std::vector<compiled_selector_s> out;
    check(selector_parse_list("#sentinel:hover > .anchor",out),"seed list");
    if (out.empty()) return;
    out[0].source=sentinel.source; // Explicitly exercise preservation of caller-owned source metadata.
    const auto bad=".card:has(#bad:hover,:unknown)";
    check(!selector_parse_list(bad,out),"invalid list reports false");
    check(out.size()==1,"invalid list preserves prefix size");
    if(!out.empty()) { same(out[0],sentinel,"preserved sentinel"); check(out[0].source==sentinel.source,"sentinel source intact"); }
    check(selector_parse_list((std::string(bad)+",.safe").c_str(),out),"top-level forgiving recovery");
    check(out.size()==2,"append only safe to sentinel");
    if(out.size()==2) {same(out[0],sentinel,"prefix after recovery");same(out[1],safe,"only safe survives");}
}
}
int main() {
    matrix();protected_delimiters();spellings();transactions_and_metadata();bounded_nesting();
    std::printf("HAS_STRICT_LIST_PARSER CHECKS=%d FAILURES=%d\n",checks,failures);
    return failures?1:0;
}
