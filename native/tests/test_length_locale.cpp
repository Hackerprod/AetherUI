// Dedicated serial process: never mutate LC_ALL concurrently with product work.
// All checks survive NDEBUG; signatures compare fields, never struct padding.
#include <aether/aether.h>
#include <bit>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
std::string context;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL [%s]: %s\n", context.c_str(), label.c_str()); }
}
void need(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "HARNESS: %s\n", label); std::exit(2); }
}
std::string current() { const char* p = std::setlocale(LC_ALL, nullptr); return p ? p : ""; }
struct LocaleGuard { std::string saved = current(); ~LocaleGuard() { std::setlocale(LC_ALL, saved.c_str()); } };
void select(const std::string& name) { need(std::setlocale(LC_ALL, name.c_str()) != nullptr, "select locale"); }
using Bits = std::vector<uint32_t>;
void append(Bits& b, float v) { b.push_back(std::bit_cast<uint32_t>(v)); }
void append(Bits& b, aether_css_length v) {
    b.push_back(static_cast<uint32_t>(v.unit));
    for (float f : {v.value, v.linear_percent, v.linear_em, v.linear_rem, v.linear_vw, v.linear_vh}) append(b, f);
}
void append(Bits& b, aether_color_rgba v) { for (float f : {v.r,v.g,v.b,v.a}) append(b,f); }
Bits signature(const aether_computed_style_t& s) {
    Bits b;
    for (auto v : {s.width,s.height,s.min_width,s.min_height,s.max_width,s.max_height}) append(b,v);
    for (auto e : {s.margin,s.padding,s.border_width,s.inset})
        for (auto v : {e.top,e.right,e.bottom,e.left}) append(b,v);
    append(b,s.line_height); append(b,s.font_size); append(b,s.color);
    for (auto list : {s.box_shadows,s.text_shadows}) {
        b.push_back(static_cast<uint32_t>(list.count));
        for (int i=0;i<list.count;++i) {
            const auto& v=list.items[i];
            for (float f : {v.offset_x,v.offset_y,v.blur_radius,v.spread_radius}) append(b,f);
            append(b,v.color); b.push_back(static_cast<uint32_t>(v.inset));
        }
    }
    return b;
}
struct Result { bool valid; Bits bits; };
Result parse(const std::string& text) {
    aether_css_length out{};
    const bool valid=aether_css_length_try_parse(text.c_str(),&out);
    Bits b; append(b,out); return {valid,b};
}
struct Case { const char* label; const char* css; const char* expected; };
const Case cases[] = {
    {"literal", "width:73px;width:1.5px", "width:1.5px"},
    {"calc", "width:73px;width:calc(1.5px + 2px)", "width:calc(3.5px)"},
    {"variable", "--w:1.5px;width:73px;width:var(--w)", "width:1.5px"},
    {"integer control", "width:15px", "width:15px"},
    {"invalid literal", "width:73px;width:1,5px", "width:73px"},
    {"invalid defined var", "--w:1,5px;width:73px;width:var(--w,9px)", "width:auto"},
    {"missing fallback", "width:var(--missing,1.5px)", "width:1.5px"},
    {"dimension boundary", "--n:1.5;width:73px;width:var(--n)px", "width:auto"},
    {"percentage boundary", "--n:1.5;width:73px;width:var(--n)%", "width:auto"},
    {"number times unit", "--n:1.5;width:calc(var(--n) * 1px)", "width:calc(1.5px)"},
    {"comment boundary", "width:73px;width:1/**/.5px", "width:73px"},
    {"unit comment boundary", "width:73px;width:1.5/**/px", "width:73px"},
    {"peripheral comments", "width:/**/1.5px/**/", "width:1.5px"},
    {"mixed components", "width:calc(1.5px + 2.5% + 3.5em + 4.5rem + 5.5vw + 6.5vh)", "width:calc(1.5px + 2.5% + 3.5em + 4.5rem + 5.5vw + 6.5vh)"},
    {"all units", "width:1.5%;height:2.5em;min-width:3.5rem;min-height:4.5vw;max-width:5.5vh;max-height:6.5px", "width:1.5%;height:2.5em;min-width:3.5rem;min-height:4.5vw;max-width:5.5vh;max-height:6.5px"},
    {"invalid shorthand atomic", "margin:7px 8px 9px 10px;margin:1.5px 2,5px 3.5px 4.5px", "margin:7px 8px 9px 10px"},
    {"invalid variable shorthand", "--m:1.5px 2,5px;margin:7px 8px;margin:var(--m,9px)", "margin:0px"},
    {"valid variable shorthand", "--m:1.5px 2.5px 3.5px 4.5px;margin:var(--m)", "margin:1.5px 2.5px 3.5px 4.5px"},
    {"line height literal", "line-height:1.5", "line-height:1.5"},
    {"line height inherited computed invalid", "--h:1,5;line-height:2;line-height:var(--h,3)", "line-height:inherit"},
    {"line height invalid literal", "line-height:2;line-height:1,5", "line-height:2"},
    {"shadow RGB", "box-shadow:1.5px 2.5px 3.5px 4.5px rgba(127.5,0,255,.5)", "box-shadow:1.5px 2.5px 3.5px 4.5px rgba(127.5,0,255,.5)"},
    {"shadow currentColor", "text-shadow:1.5px 2.5px 3.5px currentColor", "text-shadow:1.5px 2.5px 3.5px currentColor"}
};
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t rt{};
    std::vector<aether_node_t> nodes;
    std::vector<aether_computed_style_t> styles;
    std::vector<uint32_t> impacts;
    std::vector<bool> seen;
    aether_style_environment_t env{799,600,1,16,0,0,0};
    explicit Fixture(const std::vector<std::string>& declarations, bool inlined, bool media=false) {
        need(aether_doc_create(&doc)==AETHER_OK,"create doc");
        std::string html="<html><body><div id='parent'>", css="#parent{color:#2468ac;font-size:16px;line-height:1.75;}";
        for (size_t i=0;i<declarations.size();++i) {
            const auto id="n"+std::to_string(i);
            html+="<div id='"+id+"' style='"+(inlined?declarations[i]:"")+"'></div>";
            if (!inlined) css+="#"+id+"{"+declarations[i]+"}";
        }
        html+="</div></body></html>";
        need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse HTML");
        aether_style_runtime_options_t opts{}; opts.max_variable_depth=32; opts.max_variable_expansion_length=65536;
        need(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"create runtime");
        if(media) css+="@media (min-width:799.5px){#n0{width:1.5px;}}@media (min-width:calc(799px + .5px)){#n1{width:3.5px;}}";
        int64_t sheet{};
        need(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"length-locale.css",&sheet,nullptr)==AETHER_OK,"compile stylesheet");
        for(size_t i=0;i<declarations.size();++i) nodes.push_back(aether_doc_get_element_by_id(doc,("n"+std::to_string(i)).c_str()));
        styles.resize(nodes.size()); impacts.resize(nodes.size()); seen.resize(nodes.size());
    }
    ~Fixture() { for(auto& s:styles) aether_computed_style_free(&s); aether_style_runtime_destroy(rt); aether_doc_destroy(doc); }
    int resolve(bool environment=false) {
        const auto before=current();
        impacts.assign(nodes.size(),0); seen.assign(nodes.size(),false);
        aether_style_update_t* updates{};
        const auto root=aether_doc_get_document_element(doc);
        const int n=environment?aether_style_runtime_set_environment(rt,&env,root,&updates):aether_style_runtime_resolve_subtree(rt,root,&updates);
        need(n>=0,"resolve");
        for(int i=0;i<n;++i) {
            aether_node_t node{}; aether_computed_style_handle_t h{}; uint32_t impact{};
            need(aether_style_runtime_get_update(updates[i],&node,&h,&impact)==AETHER_OK,"get update");
            for(size_t j=0;j<nodes.size();++j) if(node==nodes[j]) {
                check(!seen[j],"no duplicate target update"); seen[j]=true; impacts[j]=impact;
                aether_computed_style_free(&styles[j]);
                need(aether_computed_style_copy(h,&styles[j])==AETHER_OK,"copy style");
            }
            aether_computed_style_handle_destroy(h);
        }
        aether_style_updates_free(updates,n);
        check(before==current(),"resolution preserves LC_ALL"); return n;
    }
    void mutate(size_t i,const char* css) { need(aether_element_set_attribute(nodes[i],"style",css)==AETHER_OK,"mutate inline"); }
    void no_op() { check(resolve()==0,"second unchanged resolve emits zero updates"); }
};
std::vector<Bits> expected_styles() {
    select("C"); std::vector<std::string> declarations;
    for(const auto& c:cases) declarations.emplace_back(c.expected);
    Fixture f(declarations,false); f.resolve(); std::vector<Bits> result;
    for(size_t i=0;i<f.nodes.size();++i) { check(f.seen[i],"oracle target present"); result.push_back(signature(f.styles[i])); }
    return result;
}
void pipeline(const std::string& a,const std::string& b,const std::vector<Bits>& expected) {
    for(bool inlined:{false,true}) {
        context=a+" -> "+b+(inlined?" inline":" sheet"); select(a); const auto before=current();
        std::vector<std::string> declarations; for(const auto& c:cases) declarations.emplace_back(c.css);
        Fixture f(declarations,inlined); check(before==current(),"compilation preserves LC_ALL"); select(b); f.resolve();
        for(size_t i=0;i<f.nodes.size();++i) {
            check(f.seen[i],std::string(cases[i].label)+" target inspected");
            check(signature(f.styles[i])==expected[i],std::string(cases[i].label)+" all numeric fields bit-exact against C");
        }
        f.no_op();
    }
}
void width(const aether_computed_style_t& s,float value,const char* label) {
    aether_css_length expected{}; expected.unit=AETHER_CSS_UNIT_PX; expected.value=value;
    Bits a,b; append(a,s.width); append(b,expected); check(a==b,label);
}
// The same timeline is run under every compile/resolve pair; exact impacts and
// complete field snapshots are compared with C, in addition to semantic checks.
std::vector<Bits> timeline(const std::string& a,const std::string& b) {
    context=a+" -> "+b+" mutations"; select(a);
    Fixture f({"width:73px","width:73px","margin:7px 8px","line-height:inherit"},false);
    select(b); f.resolve(); f.no_op(); std::vector<Bits> trace;
    const char* direct[]={"width:1.5px","width:2.5px","width:1,5px","width:4.5px"};
    const char* variable[]={"--w:1.5px;width:var(--w,9px)","--w:2.5px;width:var(--w,9px)","--w:1,5px;width:var(--w,9px)","width:var(--missing,4.5px)"};
    const char* margin[]={"--m:1.5px 2.5px;margin:var(--m)","--m:3.5px 4.5px;margin:var(--m)","--m:3.5px 4,5px;margin:var(--m,9px)","--m:5.5px 6.5px;margin:var(--m)"};
    const char* line_height[]={"--h:1.5;line-height:var(--h)","--h:2.5;line-height:var(--h)","--h:1,5;line-height:2;line-height:var(--h,3)","line-height:var(--missing,1.25)"};
    for(int step=0;step<4;++step) {
        select(step%2?b:a); const auto before=current();
        f.mutate(0,direct[step]); f.mutate(1,variable[step]); f.mutate(2,margin[step]); f.mutate(3,line_height[step]);
        check(before==current(),"mutation preserves LC_ALL"); select(step%2?a:b); f.resolve();
        for(size_t i=0;i<f.nodes.size();++i) {
            check(f.seen[i],"mutated target updated"); check((f.impacts[i]&(i==3?AETHER_STYLE_IMPACT_TEXT_LAYOUT:AETHER_STYLE_IMPACT_LAYOUT))!=0,"length mutation has expected layout/text-layout impact");
            auto bits=signature(f.styles[i]); bits.push_back(f.impacts[i]); trace.push_back(bits);
        }
        const float values[]={1.5f,2.5f,73.f,4.5f}; width(f.styles[0],values[step],"inline mutation width");
        if(step==2) check(f.styles[1].width.unit==AETHER_CSS_UNIT_AUTO,"defined invalid variable becomes AUTO");
        else width(f.styles[1],values[step],"variable/fallback mutation width");
        const float heights[]={1.5f,2.5f,1.75f,1.25f};
        check(std::bit_cast<uint32_t>(f.styles[3].line_height)==std::bit_cast<uint32_t>(heights[step]),"line-height mutation inherits on computed invalid and uses missing fallback");
        f.no_op();
    }
    return trace;
}
std::vector<Bits> media(const std::string& a,const std::string& b) {
    context=a+" -> "+b+" media"; select(a); Fixture f({"width:73px","width:73px"},false,true);
    select(b); f.resolve(); width(f.styles[0],73,"media below literal"); width(f.styles[1],73,"media below calc"); f.no_op();
    std::vector<Bits> trace;
    for(float viewport:{799.5f,800.f,799.f}) {
        f.env.viewport_width=viewport; f.resolve(true);
        for(size_t i=0;i<2;++i) {
            width(f.styles[i],viewport>=799.5f?(i?3.5f:1.5f):73.f,"media threshold width");
            if(viewport!=800.f) { check(f.seen[i],"crossing media threshold updates target"); check((f.impacts[i]&AETHER_STYLE_IMPACT_LAYOUT)!=0,"media has layout impact"); }
            else check(!f.seen[i],"same side of threshold has no target update");
            auto bits=signature(f.styles[i]); bits.push_back(f.impacts[i]); trace.push_back(bits);
        }
        f.no_op();
    }
    return trace;
}
} // namespace
int main(int argc,char** argv) {
    const bool require_all=argc==2 && std::string(argv[1])=="--require-all";
    if(argc>2 || (argc==2&&!require_all)) { std::fprintf(stderr,"usage: test_length_locale [--require-all]\n"); return 2; }
    const auto original=current();
    {
        LocaleGuard restore; std::vector<std::string> locales{"C"};
        const char* families[][4]={{"de_DE.UTF-8","de_DE.utf8","de_DE","German_Germany.1252"},{"fr_FR.UTF-8","fr_FR.utf8","fr_FR","French_France.1252"},{"tr_TR.UTF-8","tr_TR.utf8","tr_TR","Turkish_Turkey.1254"}};
        for(const auto& family:families) {
            bool found=false;
            for(const char* name:family) if(std::setlocale(LC_ALL,name)) {
                check(std::string(std::localeconv()->decimal_point)==",",std::string(name)+" actual decimal comma");
                locales.emplace_back(name); found=true; std::printf("AVAILABLE %s decimal_point=%s\n",name,std::localeconv()->decimal_point); break;
            }
            if(!found) std::printf("SKIP unavailable locale family %s\n",family[0]);
            if(require_all) check(found,std::string("required locale ")+family[0]);
        }
        // No AetherUI API has been called yet: detect a cache seeded from caller locale.
        select(locales.back()); const auto before=current();
        auto first=parse("1.5px"); aether_css_length expected{}; expected.unit=AETHER_CSS_UNIT_PX; expected.value=1.5f; Bits expected_bits; append(expected_bits,expected);
        check(first.valid&&first.bits==expected_bits,"first numeric conversion under non-C");
        check(before==current(),"first conversion preserves LC_ALL");
        std::vector<std::string> inputs={"auto","0","-0","+.5px","1.5e2px","calc(1.5px + 2.5% + 3.5em + 4.5rem + 5.5vw + 6.5vh)","calc((16777217 - 16777216) * 1px)","calc(1e100 * 1e-100 * 1px)","1e-46px","1e-300px","1e-400px","3.4028235e38px","1,5px","1/**/.5px","1.5 px","NaNpx","0x1p0px","calc(1px / 0)"};
        for(const char* unit:{"px","%","em","rem","vw","vh"}) for(const char* n:{"1.5","-.5","1.25e1"}) inputs.push_back(std::string(n)+unit);
        select("C"); std::vector<Result> baseline; for(const auto& s:inputs) baseline.push_back(parse(s));
        for(const auto& locale:locales) {
            select(locale); context=locale+" direct"; const auto saved=current();
            for(size_t i=0;i<inputs.size();++i) { const auto r=parse(inputs[i]); check(r.valid==baseline[i].valid&&r.bits==baseline[i].bits,inputs[i]+" validity, unit and all component bits against C"); }
            check(saved==current(),"direct conversions preserve LC_ALL");
        }
        auto expected_styles_c=expected_styles(); auto timeline_c=timeline("C","C"); auto media_c=media("C","C");
        for(const auto& a:locales) for(const auto& b:locales) {
            pipeline(a,b,expected_styles_c);
            check(timeline(a,b)==timeline_c,"mutation snapshots and impacts match C");
            check(media(a,b)==media_c,"media snapshots and impacts match C");
        }
        std::printf("length_locale: %zu locales, %zu compile/resolve pairs (sheet and inline)\n",locales.size(),locales.size()*locales.size());
    }
    check(original==current(),"restore original LC_ALL");
    std::printf("length_locale: %u checks, %u failures\n",checks,failures); return failures?1:0;
}
