#include "style_runtime_types.h"
#include "../src/dom/dom_types.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// Real parser and matcher coverage. All checks remain active under NDEBUG.
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
            check(u.case_mode == v.case_mode && u.html_default_ascii_insensitive == v.html_default_ascii_insensitive, q + ".case_policy");
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
const std::vector<std::string> empty_bodies = {
    "", " ", "\t\r\n\f", "/**/", " /* , ) ] { */ ", ",", ",,", ":unknown",
    ":unknown,:nth-child(bogus)", "#ghost:hover:unknown", "[x=]", ".bad >",
    "> .child", "+ .child", "~ .child", ":hover()", ":not()", ":has()",
    ":unknown(a,b)", ":not(.safe,:unknown)", ":has(.ready,:unknown)", "]",
    "#ghost[data-x=yes]:first-child:hover:unknown", ".ancestor > #ghost:hover:unknown",
    ":has(#ghost:hover,:unknown)", ".bad\\\n", ".bad\\\r\n", "[x=\"bad\n]"
};
void empty_ast() {
    for (const std::string fn : {"is", "where"}) for (const auto& body : empty_bodies) {
        const auto source = ":" + fn + "(" + body + ")";
        compiled_selector_s s;
        if (!parse(source,s)) continue;
        check(s.specificity == specificity_s{}, source + " zero specificity");
        check(s.dependencies == SELECTOR_DEP_NONE, source + " rejected dependencies discarded");
        check(s.pseudo_element == aether_pseudo_element_t::none, source + " ordinary subject");
        check(s.anchor.id.empty() && s.anchor.class_name.empty() && s.anchor.type_name.empty(), source + " empty anchors");
        check(s.steps.size() == 1, source + " one real step");
        if (s.steps.size() != 1) continue;
        const auto& step = s.steps[0]; const auto& c = step.compound;
        check(step.relation_to_next == COMB_NONE && c.type_name.empty() && c.ids.empty() &&
              c.classes.empty() && c.attributes.empty(), source + " no leaked compound data");
        check(c.pseudos.size() == 1, source + " pseudo retained");
        if (c.pseudos.size() != 1) continue;
        const auto& p = c.pseudos[0];
        check(p.kind == (fn == "is" ? PSEUDO_IS : PSEUDO_WHERE), source + " correct kind");
        check(p.state == PSEUDO_STATE_NONE && p.nth.step == 0 && p.nth.offset == 0, source + " no leaked state or nth");
        check(p.nested && p.nested->empty(), source + " present vector with no sentinel branch");
        compiled_selector_s outer;
        if (parse(".item:not(" + source + ")",outer)) {
            check(outer.specificity == specificity_s{0,1,0} && outer.dependencies == SELECTOR_DEP_SELF_CLASS,
                  source + " enclosing NOT retains only item metadata");
            const auto& not_pseudo = outer.steps[0].compound.pseudos[0];
            check(not_pseudo.nested && not_pseudo.nested->size() == 1, source + " NOT has one valid branch");
            if (not_pseudo.nested && not_pseudo.nested->size() == 1) {
                check(not_pseudo.nested->front().source.empty(), source + " nested source contract");
                same(s,not_pseudo.nested->front(),source + " exact nested AST");
            }
        }
    }
}
void boundaries_and_recovery() {
    for (const std::string fn : {"not","has"}) for (const auto& body : empty_bodies)
        invalid(":" + fn + "(" + body + ")");
    for (const std::string fn : {"not","has"}) {
        for (const auto* body : {":is()", ":where()", ":is(),.safe", ":where(),.safe"}) {
            compiled_selector_s s; parse(":" + fn + "(" + body + ")",s);
        }
        for (const auto* body : {":is(),", ":where(),:unknown", ",:is()", ".safe,:unknown", ".safe,,.other"})
            invalid(":" + fn + "(" + body + ")");
    }
    for (const auto* source : {":is(", ":where(", ":is([)", ":where([x=)", ":is(:unknown(",
            ":is(/*)", ":is([x=\"bad])", ":is())", ":where()]", ":is()!", ":is() >",
            ":is/**/()", ":is", ":where", ":hover()", ":root()", ":empty()", ":nth-child()", ":unknown()"})
        invalid(source);
    for (const auto* name : {"IS", "iS", R"(\49 S)", R"(i\73)"}) equivalent(":is()", ":" + std::string(name) + "()");
    for (const auto* name : {"WHERE", "WhErE", R"(\77 here)", R"(wher\65)"}) equivalent(":where()", ":" + std::string(name) + "()");
    for (const std::string fn : {"is","where"}) for (const auto& body : empty_bodies)
        equivalent(":" + fn + "(.safe,.other)", ":" + fn + "(" + body + ",.safe,.other)");
    for (const std::string branch : {R"css([x="a,b)"])css", R"(.comma\,name)", R"(.paren\)name)", ".a/* , ) ] { */.b"}) {
        equivalent(":is(" + branch + ")", ":is(:unknown," + branch + ",:unknown)");
        equivalent(":where(" + branch + ")", ":where(:unknown," + branch + ",:unknown)");
    }
    std::vector<compiled_selector_s> list;
    check(selector_parse_list(".prefix",list),"prefix parses");
    for (const char* source : {"", ":unknown", ",,", "/**/", ":not(.safe,:unknown)"}) {
        check(!selector_parse_list(source,list),std::string("top-level still requires a branch ") + source);
        check(list.size() == 1 && list[0].anchor.class_name == "prefix", "failed append is atomic");
    }
    check(!selector_parse_list(nullptr,list) && list.size() == 1, "null append is atomic");
    check(selector_parse_list(":unknown,:is(),.safe,:where(),:not(:is())",list),"empty pseudos are valid top-level branches");
    check(list.size() == 5, "exact appended branch count");
    if (list.size() == 5) {
        check(list[0].anchor.class_name == "prefix" && list[2].anchor.class_name == "safe", "append order");
        check(list[1].steps[0].compound.pseudos[0].kind == PSEUDO_IS &&
              list[3].steps[0].compound.pseudos[0].kind == PSEUDO_WHERE &&
              list[4].steps[0].compound.pseudos[0].kind == PSEUDO_NOT, "retained pseudo order");
        for (const auto& s : list) check(s.source.empty(),"list API source remains empty");
    }
}
void metadata_and_impossible_branches() {
    metadata(":is(#ghost:is())",{1,0,0},SELECTOR_DEP_SELF_ID);
    metadata(":is(.safe,#ghost:is())",{1,0,0},SELECTOR_DEP_SELF_ID|SELECTOR_DEP_SELF_CLASS);
    metadata(":where(.safe,#ghost:is())",{},SELECTOR_DEP_SELF_ID|SELECTOR_DEP_SELF_CLASS);
    metadata(":is(:hover:is())",{0,1,0},SELECTOR_DEP_SELF_STATE);
    metadata(":is(:has(:is()))",{},SELECTOR_DEP_DESCENDANT);
    metadata(":is(:has(#ghost:hover,:unknown))",{},SELECTOR_DEP_NONE);
    metadata(":has(:is())",{},SELECTOR_DEP_DESCENDANT);
    metadata(":not(:is(:hover:unknown))",{},SELECTOR_DEP_NONE);
    for (const std::string source : {":is(:where())", ":is(#ghost:is())", ":is(:hover:is())", ":has(:is())"}) {
        compiled_selector_s s;
        if (!parse(source,s)) continue;
        const auto& p = s.steps[0].compound.pseudos[0];
        check(p.nested && p.nested->size() == 1,source + " retains valid impossible branch");
        if (p.nested && p.nested->size() == 1) {
            const auto& branch = p.nested->front();
            check(branch.source.empty() && branch.steps.size() == 1,source + " branch source and step");
            const auto& inner = branch.steps[0].compound.pseudos.back();
            check(inner.nested && inner.nested->empty(),source + " inner empty retained");
        }
    }
}
aether_node_s element(const char* tag="div", const char* classes="item") {
    aether_node_s n{}; n.type=AETHER_NODE_ELEMENT; n.tag_name=tag; n.attributes["class"]=classes; return n;
}
void add(aether_node_s& parent,aether_node_s& child) { parent.children.push_back(&child); child.parent=&parent; }
void matching_and_nesting() {
    auto leaf=element(), parent=element(), child=element("span","ready"), grandchild=element("i","deep");
    add(parent,child); add(child,grandchild);
    struct Case { const char* source; bool leaf,parent; };
    for (auto c : std::vector<Case>{
            {":is()",false,false}, {":where(:unknown)",false,false}, {":not(:is())",true,true},
            {":not(:where())",true,true}, {":not(:not(:is()))",false,false}, {":is(:where())",false,false},
            {":has(:is())",false,false}, {":has(:not(:where()))",false,true}, {":not(:has(:is()))",true,true},
            {":not(:is(),.skip)",true,true}, {":has(:where(),.ready)",false,true},
            {":is(.item,#ghost:is())",true,true}, {":where(.item,#ghost:is())",true,true},
            {":has(:has(:not(:is())))",false,true}, {".absent:not(:is())",false,false},
            {":is(:has(:is()))",false,false}, {":is(:hover:is())",false,false}}) {
        compiled_selector_s s;
        if (parse(c.source,s)) {
            check(selector_match(s,&leaf) == c.leaf,std::string("leaf matches ")+c.source);
            check(selector_match(s,&parent) == c.parent,std::string("parent matches ")+c.source);
        }
    }
    aether_node_s doc{}; doc.type=AETHER_NODE_DOCUMENT;
    aether_node_s text{}; text.type=AETHER_NODE_TEXT; text.text="x";
    auto text_parent=element(); add(text_parent,text);
    auto generated_parent=element(), generated=element(), generated_child=element();
    generated.is_generated=true; generated.pseudo_element=aether_pseudo_element_t::before;
    generated.style_origin=&generated_parent; add(generated_parent,generated); add(generated,generated_child);
    auto wrapper_parent=element(), wrapper=element(), wrapper_child=element();
    wrapper.anonymous_flex_owner=&wrapper_parent; add(wrapper_parent,wrapper); add(wrapper,wrapper_child);
    compiled_selector_s n,h;
    if (parse(":not(:is())",n) && parse(":has(:not(:is()))",h)) {
        check(!selector_match(n,&doc) && !selector_match(n,&text) && !selector_match(n,&wrapper),"ordinary subject boundaries");
        check(!selector_match(h,&text_parent),"text-only parent does not match HAS");
        check(!selector_match(h,&generated_parent),"generated subtree pruned including unflagged grandchild");
        check(!selector_match(h,&wrapper_parent),"anonymous subtree pruned including unflagged grandchild");
        check(selector_match(n,&generated),"generated style context normalizes to host");
    }
    // Disabled is an internal selector-state mask, not a public AETHER_STATE flag.
    for (auto state : std::vector<std::pair<const char*,style_pseudo_state_t>>{
            {"hover",PSEUDO_STATE_HOVER}, {"focus",PSEUDO_STATE_FOCUS},
            {"active",PSEUDO_STATE_ACTIVE}, {"disabled",PSEUDO_STATE_DISABLED}}) {
        compiled_selector_s kept, discarded;
        if (!parse(":not(:is(:unknown,:"+std::string(state.first)+"))",kept) ||
            !parse(":not(:is(:"+std::string(state.first)+":unknown))",discarded)) continue;
        for (int on : {0,1,0,1,0}) {
            leaf.state=on ? state.second : PSEUDO_STATE_NONE;
            check(selector_match(kept,&leaf)==!on,"surviving nested state on/off");
            check(selector_match(discarded,&leaf),"rejected nested state never survives");
        }
    }
    for (int depth : {1,4,16,32}) for (int run=0; run<8; ++run) {
        std::string source=":is()";
        for (int i=0; i<depth; ++i) source=":not(:where("+source+"))";
        compiled_selector_s s;
        if (parse(source,s)) check(selector_match(s,&leaf) == bool(depth%2),"bounded nested parse/match/destruction");
    }
}
} // namespace
int main() {
    empty_ast(); boundaries_and_recovery(); metadata_and_impossible_branches(); matching_and_nesting();
    std::printf("FORGIVING_EMPTY_LIST_PARSER CHECKS=%d FAILURES=%d\n",checks,failures);
    return failures ? 1 : 0;
}
