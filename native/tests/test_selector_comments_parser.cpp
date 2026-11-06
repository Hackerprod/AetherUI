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
        check(c.id == d.id, p + ".id");
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
void nth(const std::string& body, int a, int b) {
    compiled_selector_s s;
    if (!parse(":nth-child(" + body + ")", s)) return;
    check(s.steps.size() == 1 && s.steps[0].compound.pseudos.size() == 1, "nth shape");
    if (s.steps.size() != 1 || s.steps[0].compound.pseudos.size() != 1) return;
    const auto& p = s.steps[0].compound.pseudos[0];
    check(p.kind == PSEUDO_NTH_CHILD && p.nth.step == a && p.nth.offset == b, "nth coefficients " + body);
    check(s.specificity == specificity_s{0,1,0} && s.dependencies == SELECTOR_DEP_STRUCTURE, "nth metadata");
}
}
int main() {
    const std::vector<std::string> comments = {
        "/**/", "/***/", "/* */", "/*\n*/", "/*\r\n*/", "/*\f*/", "/**//**/",
        "/* , ] ) { [ ( } \" ' \\ */", std::string("/*") + '\xff' + "*/"
    };
    for (const auto& c : comments) {
        for (const auto& pair : std::vector<std::pair<std::string,std::string>>{
            {".card.active", ".card"+c+".active"},
            {"div.card#host[x=a]:hover", "div"+c+".card"+c+"#host"+c+"[x=a]"+c+":hover"},
            {"*.card", "*"+c+".card"}, {".card", "."+c+"card"},
            {":is(.card)", ":"+c+"is(.card)"},
            {".card::before", ".card:"+c+":"+c+"before"},
            {".card::after", ".card:"+c+":"+c+"after"},
            {"[x=a i]", "["+c+"x"+c+"="+c+"a"+c+"i"+c+"]"},
            {"[x]", "["+c+"x"+c+"]"},
            {".card.active", c+".card"+c+".active"+c},
            {".card>.active", ".card"+c+">"+c+".active"},
            {".card+.active", ".card"+c+"+"+c+".active"},
            {".card~.active", ".card"+c+"~"+c+".active"},
            {":is(.card.active,.safe)", ":is(.card"+c+".active,.safe)"}
        }) equivalent(pair.first,pair.second);
        for (const std::string op : {"=", "~=", "|=", "^=", "$=", "*="})
            equivalent("[x"+op+"a i]", "[x"+c+op+c+"a"+c+"i]");
        for (const char* ws : {" ", "\t", "\n", "\r", "\f", "\r\n"}) {
            equivalent(".card .active", ".card"+c+ws+".active");
            equivalent(".card .active", ".card"+std::string(ws)+c+".active");
            equivalent(".card .active", ".card"+c+ws+c+".active");
        }
        for (const std::string& bad : {".ca"+c+"rd", "ca"+c+"rd", "#"+c+"host",
                "#ho"+c+"st", ":is"+c+"(.a)", ":ho"+c+"ver", "[ab"+c+"cd]", "[x=ab"+c+"cd]",
                ".card"+c+"\v.active"}) invalid(bad);
        for (const char op : std::string("~|^$*")) invalid("[x"+std::string(1,op)+c+"=a]");
    }
    compiled_selector_s s;
    if (parse(".card/**/.active",s)) {
        check(s.steps.size()==1 && s.steps[0].relation_to_next==COMB_NONE,"literal compound shape");
        check(s.anchor.class_name=="card" && s.specificity==specificity_s{0,2,0},"literal compound anchor/spec");
        check(s.dependencies==SELECTOR_DEP_SELF_CLASS,"no invented dependency");
    }
    if (parse(".card/**/ .active",s)) {
        check(s.steps.size()==2 && s.steps[0].relation_to_next==COMB_DESCENDANT,"literal descendant shape");
        check(s.dependencies==(SELECTOR_DEP_SELF_CLASS|SELECTOR_DEP_ANCESTOR),"actual ancestor dependency");
    }
    // Forgiving branches cannot leak rejected specificity, state or anchors.
    for (const std::string bad : {"#bad:hover/**/name", "#/**/bad", ":is/**/(#bad:hover)", "[x~/**/=a]"}) {
        std::vector<compiled_selector_s> list;
        check(selector_parse_list((bad+",.safe").c_str(),list),"top-level recovery");
        check(list.size()==1 && list[0].anchor.class_name=="safe", "top-level exact surviving branch");
        for (const std::string pseudo : {"is", "where"}) {
            equivalent(":"+pseudo+"(.safe)", ":"+pseudo+"("+bad+",.safe)");
            equivalent(":is(:"+pseudo+"(.safe))", ":is(:"+pseudo+"("+bad+",.safe))");
        }
        invalid(":has("+bad+",.safe)");
        equivalent(":is()", ":is(:has("+bad+",.safe))");
        for (const std::string outer : {"is", "where"})
            equivalent(":"+outer+"(.fallback)", ":"+outer+"(:has("+bad+",.safe),.fallback)");
        invalid(":not("+bad+",.safe)");
    }
    if (parse(":is(#bad:hover/**/name,.safe)",s)) {
        check(s.specificity==specificity_s{0,1,0} && s.dependencies==SELECTOR_DEP_SELF_CLASS,"literal rejected metadata isolation");
        check(s.steps[0].compound.pseudos[0].nested->size()==1,"one surviving nested branch");
    }
    equivalent(":is(.a.b,.safe)", ":is(.a/* , ] ) { */.b,.safe)");
    equivalent(".\\31 .x", ".\\31 /**/.x");
    equivalent(".\\31  .x", ".\\31/**/ .x");
    invalid(".\\31/**/2");
    equivalent(".a\\/\\*x", ".a\\/\\*x/**/");
    equivalent(".a\\2f \\2a x", ".a\\2f \\2a x/**/");
    equivalent(R"([x="a/**/b*/,[\]\"c"])", R"([/**/x/**/=/**/"a/**/b*/,[\]\"c"/**/])");
    for (const char* nl : {"\n", "\r", "\f", "\r\n"}) {
        equivalent("[x=ab]", "[x=\"a\\"+std::string(nl)+"b\"]");
        invalid("[x=\"a"+std::string(nl)+"/**/b\"]");
        equivalent(":is(.safe)", ":is([x=\"bad"+std::string(nl)+"],.safe)");
        invalid(":not([x=\"bad"+std::string(nl)+"],.safe)");
    }
    for (const std::string eof : {"/*", "/*/", "/**", "/* , ) ] \" ", "/* nested /*"}) {
        equivalent(".card", ".card"+eof);
        equivalent(".card::before", ".card::before"+eof);
        equivalent("[x=a]", "[x=a]"+eof);
        invalid(eof); invalid("."+eof); invalid("#"+eof); invalid(":"+eof);
        invalid("[x=a"+eof); invalid(":is(.card"+eof+")");
    }
    equivalent(".card.active", ".card/* nested /* */.active");
    invalid(".card/* nested /* */oops */.active");
    nth("/**/odd/**/",2,1); nth("/**/EVEN/**/",2,0);
    for (const char* n : {"2n/**/+1", "2n+/**/1", "2n/**/+/**/1"}) nth(n,2,1);
    for (const char* n : {"n/**/-1", "n-/**/1", "n/**/-/**/1", "+/**/n-/**/1"}) nth(n,1,-1);
    nth("+/**/n",1,0); nth("n/**/+/**/1",1,1);
    nth("/**/+2/**/",0,2); nth("-2147483648/**/",0,(-2147483647-1));
    nth("2147483647n/**/+1",2147483647,1); nth("-2147483648n/**/+1",(-2147483647-1),1);
    nth("n/**/-/**/2147483648",1,(-2147483647-1));
    nth("n-/**/2147483648",1,(-2147483647-1));
    nth("n/**/+/**/2147483647",1,2147483647);
    for (const char* n : {"o/**/dd", "2/**/n", "1/**/2", "+/**/2", "-/**/n", "2n/**/1", "n-/**/+1",
            "+/**/ n", "+ /**/n", "n-/**/-1", "n/**/+/**/2147483648", "n/**/-/**/2147483649",
            "2147483648n/**/+1", "-2147483649n/**/+1", "2147483648/**/", "-2147483649/**/",
            "n/**/++1", "n/**/+-1", "n/**/-+1", "n/**/--1", "2.0n/**/+1", "2e0n/**/+1",
            "\\6e/**/+1", "2n/**/+1 of .item", "n+1/**/2", "odd/**/even", "/**/", "+/**/-n"})
        invalid(std::string(":nth-child(")+n+")");
    // Bounded stress, no claim of a global parser complexity/depth limit.
    std::string many;
    for (int i=0;i<10000;++i) many += "/**/";
    equivalent(".card.active", ".card"+many+".active");
    equivalent(".card.active", ".card/*"+std::string(200000,'x')+"*/.active");
    std::string deep=".card", plain=".card";
    for (int i=0;i<48;++i) { deep=":is("+deep+"/**/)"; plain=":is("+plain+")"; }
    equivalent(plain,deep);
    std::printf("SELECTOR_COMMENTS_PARSER CHECKS=%d FAILURES=%d\n",checks,failures);
    return failures?1:0;
}
