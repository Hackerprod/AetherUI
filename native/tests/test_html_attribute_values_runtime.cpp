// Public C ABI only. Functional checks and mutations remain active with NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
void need(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "HARNESS: %s\n", why); std::exit(2); }
}
void check(bool ok, const char* why) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", why); }
}
bool near(float a, float b) { return std::fabs(a-b)<.001f; }
std::string take(char* s) { std::string v=s?s:""; aether_string_free(s); return v; }
struct Value { float width, height, opacity; int transform; std::string image; };
Value copy(aether_computed_style_handle_t h) {
    aether_computed_style_t s{};
    need(aether_computed_style_copy(h, &s)==AETHER_OK, "copy snapshot");
    Value v{s.width.value,s.height.value,s.opacity,s.text_transform,{}};
    if (s.background_layers.count && s.background_layers.items[0].image_source)
        v.image=s.background_layers.items[0].image_source;
    aether_computed_style_free(&s); return v;
}
struct Fixture {
    aether_doc_t doc=nullptr;
    aether_style_runtime_t rt=nullptr;
    aether_node_t root=nullptr;
    int64_t sheet=0;
    int32_t rules=0;
    std::map<aether_node_t,aether_computed_style_handle_t> latest;
    std::vector<aether_computed_style_handle_t> retained;
    std::map<aether_node_t,std::vector<std::string>> generated;
    Fixture(const std::string& html,const std::string& css) {
        need(aether_doc_create(&doc)==AETHER_OK,"document");
        need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"HTML");
        root=aether_doc_get_document_element(doc); need(root!=nullptr,"root");
        aether_style_runtime_options_t opts{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        need(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"runtime");
        add(css);
    }
    ~Fixture() {
        for(auto h:retained) aether_computed_style_handle_destroy(h);
        aether_style_runtime_destroy(rt); aether_doc_destroy(doc);
    }
    void add(const std::string& css) {
        need(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"attribute-values.css",&sheet,&rules)==AETHER_OK,"stylesheet");
    }
    aether_node_t id(const char* name) {
        auto n=aether_doc_get_element_by_id(doc,name); need(n!=nullptr,"ID"); return n;
    }
    void resolve(aether_node_t subtree=nullptr) {
        aether_style_update_t* updates=nullptr;
        int n=aether_style_runtime_resolve_subtree(rt,subtree?subtree:root,&updates);
        need(n>=0,"resolve"); generated.clear();
        for(int i=0;i<n;++i) {
            aether_node_t node=nullptr; aether_computed_style_handle_t h=nullptr; uint32_t impact=0;
            need(aether_style_runtime_get_update(updates[i],&node,&h,&impact)==AETHER_OK,"update metadata");
            need(node && h,"update owns node and snapshot");
            latest[node]=h; retained.push_back(h);
            auto parent=aether_node_get_parent(node);
            if(parent) {
                bool authored=false;
                for(int j=0;j<aether_node_get_child_count(parent);++j)
                    authored |= aether_node_get_child(parent,j)==node;
                if(!authored) generated[parent].push_back(take(aether_node_get_text_content(aether_node_get_child(node,0))));
            }
        }
        aether_style_updates_free(updates,n);
    }
    Value value(aether_node_t n) { need(latest.count(n)!=0,"resolved style"); return copy(latest.at(n)); }
    void set(aether_node_t n,const char* key,const char* v) {
        need(aether_element_set_attribute(n,key,v)==AETHER_OK,"set attribute");
        check(take(aether_element_get_attribute(n,key))==v,"setter preserves bytes");
    }
};
using Attributes=std::vector<std::pair<std::string,std::string>>;
Attributes attributes(aether_node_t n) {
    Attributes result;
    for(int i=0;i<aether_element_get_attribute_count(n);++i)
        result.emplace_back(take(aether_element_get_attribute_name(n,i)),take(aether_element_get_attribute_value(n,i)));
    return result;
}
void repro() {
    struct Row {const char* tag;const char* attr;const char* value;const char* selector;float expected;};
    const Row rows[]={
        {"input","type","TEXT","input[type=text]",.8f},
        {"input","type","TEXT","input[type=text i]",.8f},
        {"input","type","TEXT","input[type=text s]",.2f},
        {"input","type","TEXT","input[TYPE=teXT]",.8f},
        {"input","type","TEXT","input[type^=te]",.8f},
        {"input","type","TEXT","input[type$=xt]",.8f},
        {"input","type","TEXT","input[type*=ex]",.8f},
        {"input","type","text","input[type=text]",.8f},
        {"div","data-x","VALUE","div[data-x=value]",.2f},
        {"div","data-x","VALUE","div[data-x=value i]",.8f},
        {"div","dir","RTL","div[dir=rtl]",.8f},
        {"a","rel","NOFOLLOW EXTERNAL","a[rel~=external]",.8f}
    };
    for(const auto& r:rows) {
        Fixture f(std::string("<body><")+r.tag+" id='target' class='target' "+r.attr+"='"+r.value+"'></"+r.tag+"></body>",
                  std::string(".target{opacity:.2}")+r.selector+"{opacity:.8}");
        check(f.rules==2,"repro compiles both rules"); f.resolve();
        float actual=f.value(f.id("target")).opacity;
        std::printf("REPRO %s rules=%d expected=%.1f actual=%.1f\n",r.selector,f.rules,r.expected,actual);
        check(near(actual,r.expected),r.selector);
    }
}
void modes_and_guards() {
    Fixture f("<body><DIV id='I' class='I' TYPE='FoO' target='_BLANK' disabled='DiSaBlEd' data-x='VALUE'>MiXeD Ä</DIV><input id='missing'></body>",
        "#I{width:10px;height:10px;opacity:.2;--I:37px;--i:49px;width:var(--I);height:var(--i);"
        "background-image:url('Case/IMAGE.PnG');text-transform:uppercase}"
        "DIV#I[type=foo][target=_blank]{opacity:.8}"
        "#i,#I.i,#I[data-x=value],#I[type=foo s],#I[target=_blank s]{width:99px}"
        "#I[class=i i]{height:51px}#missing{opacity:.2;--i:49px;width:var(--I,17px);height:var(--i)}input[type=text]{opacity:.9}"
        "#I[disabled=disabled]{height:53px}#I:disabled{height:71px}#I:hover{height:73px}");
    auto host=f.id("I"); const auto before=attributes(host); auto version=aether_doc_get_version(f.doc);
    for(int i=0;i<3;++i) {
        f.resolve(); auto v=f.value(host);
        check(near(v.opacity,.8f)&&near(v.width,37)&&near(v.height,53),"defaults, strict values, custom-property identity, and explicit pseudo-state");
        check(v.image=="Case/IMAGE.PnG","URL bytes remain intact");
        check(v.transform==AETHER_TEXT_TRANSFORM_UPPERCASE,"text-transform stays a separate computed property");
        check(attributes(host)==before,"matching preserves enumeration order, names and values");
        check(take(aether_node_get_text_content(aether_node_get_child(host,0)))=="MiXeD Ä","matching never rewrites visible DOM text");
        check(aether_doc_get_version(f.doc)==version,"matching does not mutate document version");
    }
    check(!aether_doc_get_element_by_id(f.doc,"i"),"ID is exact");
    check(aether_element_has_class(host,"I")&&!aether_element_has_class(host,"i"),"class token is exact");
    check(near(f.value(f.id("missing")).width,17)&&near(f.value(f.id("missing")).height,49),"custom property --I never aliases --i");
    check(near(f.value(f.id("missing")).opacity,.2f),"missing type is not an effective text attribute");
    check(aether_element_is_disabled(host)&&aether_element_get_state(host)==AETHER_STATE_NONE,"boolean attribute does not set state");
    aether_element_set_state(host,AETHER_STATE_HOVER); f.resolve();
    check(near(f.value(host).height,73),"explicit hover pseudo-state still works");
    aether_element_set_state(host,AETHER_STATE_NONE); f.set(host,"disabled","false"); f.resolve();
    check(aether_element_is_disabled(host)&&near(f.value(host).height,51),"disabled=false retains presence without matching disabled value or pseudo-state");
}
void lifecycle() {
    const std::string css=
        "main{opacity:.1}#I{width:10px;height:10px;opacity:.1}aside{width:10px;height:10px}"
        "#I[type=text]{width:31px}#I[type=text i]{height:41px}#I[type=text s]{opacity:.7}"
        "#I:is([type=text s],[type=TEXT s]){height:43px}"
        "#I:where([type=text]):not([data-x=value]){width:33px}"
        "main:has(#I[type=text]){opacity:.8}"
        "main > #I[type=text] + aside{width:51px}main #I[type=text] ~ aside{height:61px}"
        "#I[type=text]::before{content:'BeFoRe';width:7px}"
        "#I[type=TEXT s]::after{content:'AfTeR';width:9px}";
    Fixture f("<body><main id='outer'><div id='I' class='I' TYPE='TEXT' data-x='VALUE'>Original</div><aside id='next'></aside></main></body>",css);
    auto host=f.id("I"),outer=f.id("outer"),next=f.id("next");
    f.resolve(); auto old=f.latest.at(host); const auto initial=copy(old);
    for(const char* type:{"TEXT","password","text","TEXT"}) {
        f.set(host,"TYPE",type); f.resolve();
        bool match=std::string(type)!="password", strict=std::string(type)=="text", upper=std::string(type)=="TEXT";
        auto v=f.value(host);
        check(near(v.width,match?33.f:10.f)&&near(v.height,match?43.f:10.f)&&near(v.opacity,strict?.7f:.1f),"same runtime default/i/s and nested pseudo modes");
        check(near(f.value(outer).opacity,match?.8f:.1f),"mutated descendant updates :has ancestor");
        check(near(f.value(next).width,match?51.f:10.f)&&near(f.value(next).height,match?61.f:10.f),"child descendant adjacent and general sibling combinators");
        check(f.generated[host].size()==(match?(upper?2u:1u):0u),"before/after creation destruction and reappearance");
        for(const auto& text:f.generated[host]) check(text=="BeFoRe"||text=="AfTeR","generated content retains literal case");
        check(aether_node_get_child_count(host)==1&&f.id("I")==host,"generated nodes do not contaminate children or ID lookup");
        check(take(aether_node_get_text_content(aether_node_get_child(host,0)))=="Original","host text excludes generated content");
        check(near(copy(old).width,initial.width)&&near(copy(old).height,initial.height),"retained snapshot does not track mutations");
        f.resolve(); check(near(f.value(host).width,v.width),"repeat resolution is stable");
    }
    auto attrs=attributes(host); bool canonical=false;
    for(const auto& p:attrs) if(p.first=="type") canonical=p.second=="TEXT";
    check(canonical&&attrs.size()==4,"TYPE setter keeps one canonical key and exact value");
    f.set(host,"data-x","value"); f.resolve(); check(near(f.value(host).width,31),"data default remains sensitive after mutation");
    f.set(host,"data-x","VALUE"); f.resolve(); check(near(f.value(host).width,33),"data mutation back restores :not");
    need(aether_node_remove_child(outer,host)==AETHER_OK,"detach"); f.resolve(host);
    check(near(f.value(host).width,33),"detached HTML node keeps default value policy");
    need(aether_node_append_child(outer,host)==AETHER_OK,"reinsert"); f.resolve();
    check(near(f.value(host).width,33)&&near(f.value(outer).opacity,.8f),"reinsertion keeps policy and ancestor match");
    need(aether_style_runtime_remove_stylesheet(f.rt,f.sheet),"remove stylesheet"); f.resolve();
    check(f.generated[host].empty()&&near(f.value(host).opacity,1),"removal clears generated nodes and selector effects");
    f.add(css); f.resolve();
    std::printf("LIFECYCLE rules=%d pseudos=%zu width=%g\n",f.rules,f.generated[host].size(),f.value(host).width);
    check(f.rules==13&&near(f.value(host).width,33)&&f.generated[host].size()==2,"readd stylesheet restores styles and both pseudos");
    check(near(copy(old).width,initial.width),"snapshot survives stylesheet removal/readd");
}
}
int main() {
    repro(); modes_and_guards(); lifecycle();
    std::printf("HTML attribute values public runtime: %d checks, %d failures; NDEBUG=%s\n",checks,failures,
#ifdef NDEBUG
        "yes"
#else
        "no"
#endif
    );
    return failures?1:0;
}
