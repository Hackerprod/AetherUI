#include "style_runtime_types.h"
#include <cstdio>
#include <string>
#include <vector>

// Private AST contract: literal expectations, with every check active under NDEBUG.
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
void invalid(const std::string& source) {
    compiled_selector_s out;
    const bool ok = selector_parse(source.c_str(), out);
    check(!ok, "reject " + source);
}
void metadata(const compiled_selector_s& s, specificity_s spec, unsigned deps,
              const std::string& label) {
    check(s.specificity == spec, label + " specificity");
    check(s.dependencies == deps, label + " exact dependencies");
}
void direct(const std::string& source, const std::vector<std::string>& ids,
            specificity_s spec, unsigned deps) {
    compiled_selector_s s;
    if (!parse(source, s)) return;
    check(s.steps.size() == 1, source + " one compound");
    if (s.steps.size() != 1) return;
    check(s.steps[0].compound.ids == ids, source + " ordered ID occurrences");
    check(s.steps[0].relation_to_next == COMB_NONE, source + " no relation");
    check(s.anchor.id == (ids.empty() ? std::string() : ids.front()), source + " first direct ID anchor");
    metadata(s, spec, deps, source);
}
const std::vector<compiled_selector_s>* nested(const compiled_selector_s& s,
        pseudo_selector_kind_t kind, size_t count, const std::string& label) {
    const bool shape = s.steps.size() == 1 && s.steps[0].compound.pseudos.size() == 1;
    check(shape, label + " one step/pseudo");
    if (!shape) return nullptr;
    const auto& p = s.steps[0].compound.pseudos[0];
    check(p.kind == kind, label + " pseudo kind");
    check(bool(p.nested), label + " present nested list");
    if (!p.nested) return nullptr;
    check(p.nested->size() == count, label + " exact branch count");
    return p.nested->size() == count ? p.nested.get() : nullptr;
}
void basic_and_lexical() {
    direct("*", {}, {0,0,0}, 0);
    direct("#target", {"target"}, {1,0,0}, 2);
    direct("#target#target", {"target","target"}, {2,0,0}, 2);
    direct("#target#other", {"target","other"}, {2,0,0}, 2);
    direct("#other#target", {"other","target"}, {2,0,0}, 2);
    direct("#target#target#target", {"target","target","target"}, {3,0,0}, 2);
    direct("#target#other#target", {"target","other","target"}, {3,0,0}, 2);
    direct("#target#target#other", {"target","target","other"}, {3,0,0}, 2);
    direct("#target#Target", {"target","Target"}, {2,0,0}, 2);
    direct("#é#é", {"é","é"}, {2,0,0}, 2); // No Unicode normalization.
    direct(R"(#target#tar\67 et)", {"target","target"}, {2,0,0}, 2);
    direct(R"(#a\#b#a\23 b)", {"a#b","a#b"}, {2,0,0}, 2);
    direct(R"(#a\,b#a\2c b)", {"a,b","a,b"}, {2,0,0}, 2);
    direct(R"(#a\>b#a\3e b)", {"a>b","a>b"}, {2,0,0}, 2);
    direct(R"(#a\ b#a\20 b)", {"a b","a b"}, {2,0,0}, 2);
    direct("#a/**/#a", {"a","a"}, {2,0,0}, 2);
    direct("#a/* , ) #fake */#b", {"a","b"}, {2,0,0}, 2);
    direct("#a#\\", {"a","\xef\xbf\xbd"}, {2,0,0}, 2); // Existing EOF-escape contract.
    direct(R"([data-x="#a#b"])", {}, {0,1,0}, 4);
    direct("div#target.item#target", {"target","target"}, {2,1,1}, 3);
    direct("#target[data-x]#target", {"target","target"}, {2,1,0}, 6);
    direct("#target:hover#target", {"target","target"}, {2,1,0}, 10);
    compiled_selector_s s;
    if (parse("DIV#a.hot[data-x=ok]:hover#b#a", s) && s.steps.size() == 1) {
        const auto& c = s.steps[0].compound;
        check(c.ids == std::vector<std::string>{"a","b","a"}, "interleaved full IDs");
        check(c.type_name == "div" && c.classes == std::vector<std::string>{"hot"}, "interleaved type/classes");
        check(c.attributes.size() == 1 && c.attributes[0].name == "data-x" &&
              c.attributes[0].value == "ok", "interleaved attribute");
        check(c.pseudos.size() == 1 && c.pseudos[0].kind == PSEUDO_REQUIRED_STATE &&
              c.pseudos[0].state == PSEUDO_STATE_HOVER, "interleaved state");
        check(s.anchor.id == "a" && s.anchor.class_name == "hot" && s.anchor.type_name == "div", "all direct anchors");
        metadata(s, {3,3,1}, 15, "interleaved");
    }
    for (const auto pe : {aether_pseudo_element_t::before, aether_pseudo_element_t::after}) {
        const std::string source = std::string("#target#target::") + (pe == aether_pseudo_element_t::before ? "before" : "after");
        direct(source, {"target","target"}, {2,0,1}, 2);
        if (parse(source, s)) check(s.pseudo_element == pe, "pseudo element retained");
    }
}
void relations() {
    struct row { const char* source; std::vector<std::string> right, left; selector_combinator_t comb; specificity_s spec; unsigned deps; const char* cls; };
    for (const auto& r : std::vector<row>{
        {"#a/**/ #a", {"a"},{"a"},COMB_DESCENDANT,{2,0,0},18,""},
        {"#parent#parent > .item", {},{"parent","parent"},COMB_CHILD,{2,1,0},19,"item"},
        {"#previous#previous + #target", {"target"},{"previous","previous"},COMB_ADJACENT_SIBLING,{3,0,0},34,""},
        {"#previous#other ~ #target#target", {"target","target"},{"previous","other"},COMB_GENERAL_SIBLING,{4,0,0},34,""},
        {"#a#b .ready", {},{"a","b"},COMB_DESCENDANT,{2,1,0},19,"ready"}}) {
        compiled_selector_s s;
        if (!parse(r.source,s)) continue;
        check(s.steps.size() == 2, "two separate compounds");
        if (s.steps.size() != 2) continue;
        check(s.steps[0].compound.ids == r.right && s.steps[1].compound.ids == r.left, "right-to-left complete IDs");
        check(s.steps[0].relation_to_next == r.comb && s.steps[1].relation_to_next == COMB_NONE, "relation direction");
        check(s.anchor.id == (r.right.empty() ? std::string() : r.right.front()) && s.anchor.class_name == r.cls, "no ancestor anchor");
        metadata(s,r.spec,r.deps,r.source);
    }
}
void logical_lists() {
    struct row { const char* fn; pseudo_selector_kind_t kind; specificity_s spec; unsigned deps; };
    for (const auto& r : std::vector<row>{{"is",PSEUDO_IS,{2,0,0},3},
            {"where",PSEUDO_WHERE,{0,0,0},3}, {"not",PSEUDO_NOT,{2,0,0},3},
            {"has",PSEUDO_HAS,{2,0,0},131}}) {
        const std::string source = std::string(":") + r.fn + "(#never#other,.item)";
        compiled_selector_s s;
        if (!parse(source,s)) continue;
        metadata(s,r.spec,r.deps,source);
        check(s.anchor.is_universal(), source + " no nested ID anchor");
        const auto* branches = nested(s,r.kind,2,source);
        if (branches) {
            check((*branches)[0].steps.size() == 1 && (*branches)[0].steps[0].compound.ids == std::vector<std::string>{"never","other"}, "impossible branch retained");
            metadata((*branches)[0],{2,0,0},2,"impossible branch");
            check((*branches)[1].steps.size() == 1 && (*branches)[1].steps[0].compound.classes == std::vector<std::string>{"item"}, "class branch retained");
        }
    }
    for (const std::string fn : {"is","not","has"}) {
        compiled_selector_s s;
        if (parse(":"+fn+"(#a#a,.a.b.c.d,div#b)",s)) metadata(s,{2,0,0},fn=="has"?131:3,"lexicographic max "+fn);
    }
    for (const std::string body : {"#target#other","#target#target",":is(#target#target)",":not(#target#other)"}) {
        compiled_selector_s s;
        if (parse(".item:not("+body+")",s)) {
            metadata(s,{2,1,0},3,"negation "+body);
            check(s.anchor.id.empty() && s.anchor.class_name == "item", "negation outer anchor");
            const auto* branches=nested(s,PSEUDO_NOT,1,body);
            if (branches && body.front() == '#') check((*branches)[0].steps[0].compound.ids.size() == 2,"negation preserves both IDs");
        }
    }
    compiled_selector_s s;
    if (parse(":has(:is(#leaf#leaf,::before),.ready)",s)) {
        metadata(s,{2,0,0},131,"HAS nested forgiving recovery");
        const auto* has=nested(s,PSEUDO_HAS,2,"HAS");
        if (has) {
            const auto* is=nested((*has)[0],PSEUDO_IS,1,"IS in HAS");
            if (is) check((*is)[0].steps[0].compound.ids == std::vector<std::string>{"leaf","leaf"},"HAS keeps repeated IDs, drops pseudo-element");
        }
    }
}
void rejection_and_recovery() {
    for (const std::string source : {"#target#","#target##other","#a#b:unknown","#a#b >", "#a#/**/b",
            ":not(#a#a,:unknown)",":has(#a#a,:unknown)",":has(#a#a,::before)",
            ":has(#a#a,:not(::after))",":not(#a#b,)",":has(,#a#a)"}) invalid(source);
    for (const char nl : {'\n','\r','\f'}) invalid(std::string("#a#a\\")+nl+"b");
    invalid(std::string("#a#b")+'\xe2');
    invalid(std::string("#a#\\")+'\xf4'+'\x90'+'\x80'+'\x80');
    invalid(std::string("#a#\\")+'\xed'+'\xa0'+'\x80');
    for (const std::string fn : {"is","where"}) {
        const auto kind=fn=="is"?PSEUDO_IS:PSEUDO_WHERE;
        compiled_selector_s s;
        if (parse(":"+fn+"(#a#a,:unknown)",s)) {
            metadata(s,fn=="is"?specificity_s{2,0,0}:specificity_s{},2,"forgiving surviving IDs");
            const auto* list=nested(s,kind,1,"surviving IDs");
            if(list) check((*list)[0].steps[0].compound.ids==std::vector<std::string>{"a","a"},"full surviving IDs");
        }
        for (const std::string body : {"", "#a#b:unknown", "#a#b:hover:unknown,:unknown"}) {
            if(parse(":"+fn+"("+body+")",s)) {
                nested(s,kind,0,"present empty forgiving list");
                metadata(s,{0,0,0},0,"discarded metadata does not leak");
                check(s.anchor.is_universal(),"empty logical anchor");
            }
        }
        if(parse(".item:"+fn+"(#bad#bad:hover > .leaf:unknown,.safe)",s)) {
            metadata(s,fn=="is"?specificity_s{0,2,0}:specificity_s{0,1,0},1,"failed ID/state/ancestor metadata discarded");
            nested(s,kind,1,"only safe branch");
        }
    }
    std::vector<compiled_selector_s> list;
    const bool seed=selector_parse_list("#seed#seed",list);
    check(seed && list.size()==1,"seed list");
    const bool bad=selector_parse_list("#a#b:unknown",list);
    check(!bad && list.size()==1,"failed list preserves prefix");
    const bool good=selector_parse_list("#a#b,:unknown,.ready,#c#c",list);
    check(good && list.size()==4,"top-level recovery retains impossible and repeated branches");
    if(list.size()==4) {
        check(list[0].steps[0].compound.ids==std::vector<std::string>{"seed","seed"},"prefix retains full vector");
        check(list[1].steps[0].compound.ids==std::vector<std::string>{"a","b"},"impossible list branch order");
        check(list[2].anchor.class_name=="ready", "valid middle branch");
        check(list[3].steps[0].compound.ids==std::vector<std::string>{"c","c"},"repeated last branch");
    }
}
void bounded_counts_and_ownership() {
    for (const int count : {0,1,2,3,32,1024}) {
        std::string source=count==0?"*":"";
        std::vector<std::string> ids;
        for(int i=0;i<count;++i) { source+="#same"; ids.push_back("same"); }
        direct(source,ids,{count,0,0},count==0?0:2);
        if(count>1) {
            source+="#other"; ids.push_back("other");
            direct(source,ids,{count+1,0,0},2);
        }
    }
    compiled_selector_s original;
    std::string source="#first#second#first";
    if(parse(source,original)) {
        source.assign(2048,'x');
        compiled_selector_s moved=std::move(original);
        check(moved.steps[0].compound.ids==std::vector<std::string>{"first","second","first"},"IDs own their bytes through move");
        check(moved.anchor.id=="first", "anchor owns first ID");
        moved.steps[0].compound.ids[0]="changed";
        check(moved.anchor.id=="first","anchor is not a view into ID vector");
    }
}
}
int main() {
    basic_and_lexical(); relations(); logical_lists(); rejection_and_recovery(); bounded_counts_and_ownership();
    std::printf("SELECTOR_MULTIPLE_IDS_PARSER CHECKS=%d FAILURES=%d\n",checks,failures);
    return failures ? 1 : 0;
}
