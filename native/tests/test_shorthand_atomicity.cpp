// Production-ABI regression: intentionally no private headers and no assert().
#include <aether/aether.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>

namespace {
int checks = 0, failures = 0;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "HARNESS FAIL: %s\n", message); std::exit(2); }
}
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message.c_str()); }
}
bool near(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b)<0.001f; }
struct Style {
    aether_computed_style_t value{};
    Style() = default;
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
    Style(Style&& s) noexcept : value(s.value) { s.value = {}; }
    ~Style() { aether_computed_style_free(&value); }
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t rt{};
    std::map<aether_node_t,aether_computed_style_handle_t> snapshots;
    explicit Fixture(const std::string& inline_css = "", const std::string& html = "",
                     int max_depth = 32, int max_length = 65536) {
        require(aether_doc_create(&doc)==AETHER_OK,"create document");
        const std::string markup = html.empty() ? "<html><body><div id='target' style='"+inline_css+"'></div></body></html>" : html;
        require(aether_doc_parse_html(doc,markup.c_str())==AETHER_OK,"parse document");
        aether_style_runtime_options_t opts{};
        opts.max_variable_depth=max_depth; opts.max_variable_expansion_length=max_length;
        aether_style_environment_t env{800,600,1,16,0,0,0};
        require(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"create runtime");
        add("html,body,div{display:block;margin:0;padding:0;border:0;}#target{width:100px;height:20px;box-sizing:content-box;color:rgb(23,45,67);}");
    }
    ~Fixture() { for(auto item:snapshots) aether_computed_style_handle_destroy(item.second); aether_style_runtime_destroy(rt); aether_doc_destroy(doc); }
    int64_t add(const std::string& css) {
        int64_t id{};
        require(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"atomicity.css",&id,nullptr)==AETHER_OK,"add stylesheet");
        return id;
    }
    Style resolve(const char* id="target", uint32_t* impact=nullptr) {
        aether_style_update_t* updates{};
        const int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_document_element(doc),&updates);
        require(n>=0,"resolve styles");
        const auto node=aether_doc_get_element_by_id(doc,id);
        require(node!=nullptr,"find node");
        if (impact) *impact=0;
        for(int i=0;i<n;++i) {
            aether_node_t current{}; uint32_t flags{}; aether_computed_style_handle_t handle{};
            require(aether_style_runtime_get_update(updates[i],&current,&handle,&flags)==AETHER_OK,"read update");
            if(current==node && impact) *impact |= flags;
            if(snapshots.count(current)) aether_computed_style_handle_destroy(snapshots[current]);
            snapshots[current]=handle;
        }
        aether_style_updates_free(updates,n);
        require(snapshots.count(node)!=0,"style snapshot");
        Style result;
        require(aether_computed_style_copy(snapshots[node],&result.value)==AETHER_OK,"copy style");
        return result;
    }
};
void length(std::vector<float>& out, const aether_css_length& v) {
    out.insert(out.end(),{float(v.unit),v.value,v.linear_percent,v.linear_em,v.linear_rem,v.linear_vw,v.linear_vh});
}
void color(std::vector<float>& out, const aether_color_rgba& c) { out.insert(out.end(),{c.r,c.g,c.b,c.a}); }
std::vector<float> signature(const aether_computed_style_t& s, const std::string& property) {
    std::vector<float> out;
    if(property=="padding" || property=="margin" || property=="inset" || property=="border-width") {
        auto e=property=="padding" ? s.padding : property=="margin" ? s.margin : property=="inset" ? s.inset : s.border_width;
        for(auto v : {e.top,e.right,e.bottom,e.left}) length(out,v);
    } else if(property=="border-style") {
        out={float(s.border_styles.top),float(s.border_styles.right),float(s.border_styles.bottom),float(s.border_styles.left)};
    } else if(property=="border-color") {
        for(auto c : {s.border_colors.top,s.border_colors.right,s.border_colors.bottom,s.border_colors.left}) color(out,c);
    } else if(property=="overflow") out={float(s.overflow_x),float(s.overflow_y)};
    else if(property=="gap") out={s.gap};
    else if(property=="flex") {out={s.flex_grow,s.flex_shrink,float(s.flex_basis.kind)}; length(out,s.flex_basis.length);}
    else if(property=="transition") out={float(s.transition_properties),s.transition_duration,s.transition_delay,float(s.transition_timing)};
    else if(property=="background") {
        color(out,s.background_color);out.push_back(float(s.background_layers.count));
        for(int i=0;i<s.background_layers.count;++i) {
            const auto& layer=s.background_layers.items[i];
            out.insert(out.end(),{float(layer.kind),float(layer.size_kind),float(layer.repeat_x),float(layer.repeat_y)});
            for(auto v:{layer.position_x,layer.position_y,layer.size_x,layer.size_y}) length(out,v);
            if(layer.image_source) for(const char* ch=layer.image_source;*ch;++ch) out.push_back(float(static_cast<unsigned char>(*ch)));
            out.push_back(-1);
            out.insert(out.end(),{layer.gradient.direction_x,layer.gradient.direction_y,float(layer.gradient.stop_count)});
            for(int j=0;j<layer.gradient.stop_count;++j) {
                const auto& stop=layer.gradient.stops[j];color(out,stop.color);length(out,stop.position);
                out.insert(out.end(),{stop.offset,float(stop.has_position)});
            }
        }
    }
    else { // border and border-<side>: compare all sides, including untouched sides.
        for(const char* p : {"border-width","border-style","border-color"}) {
            const auto part=signature(s,p);out.insert(out.end(),part.begin(),part.end());
        }
    }
    return out;
}
void expect_equal(const Style& actual,const Style& expected,const std::string& property,const std::string& label) {
    auto a=signature(actual.value,property), b=signature(expected.value,property);
    bool same=a.size()==b.size();
    for(size_t i=0; i<a.size() && i<b.size(); ++i) same=near(a[i],b[i]) && same;
    check(same,label+" ["+property+"]");
    if(!same) for(size_t i=0;i<a.size() && i<b.size();++i) if(!near(a[i],b[i])) {
        std::fprintf(stderr,"  component %zu: actual=%g expected=%g\n",i,a[i],b[i]); break;
    }
}
void compare(const std::string& property,const std::string& declarations,const std::string& expected,bool inlined,const std::string& label) {
    Fixture actual(inlined ? declarations : ""), reference;
    if(!inlined) actual.add("#target{"+declarations+"}");
    reference.add("#target{"+expected+"}");
    expect_equal(actual.resolve(),reference.resolve(),property,label+(inlined ? " inline" : " sheet"));
}
struct Family { const char* name; std::array<const char*,4> values; };
const Family families[] = {
    {"padding",{"11px","13px","17px","19px"}}, {"margin",{"11px","13px","17px","19px"}},
    {"inset",{"11px","13px","17px","19px"}}, {"border-width",{"11px","13px","17px","19px"}},
    {"border-style",{"solid","dashed","dotted","double"}}, {"border-color",{"red","green","blue","yellow"}}
};
std::string joined(const Family& f,int count,int bad=-1) {
    std::string s;for(int i=0;i<count;++i) {if(i) s+=' ';s+=i==bad ? "bogus" : f.values[i];}return s;
}
std::string longhands(const Family& f,int count) {
    const int map[4][4]={{0,0,0,0},{0,1,0,1},{0,1,2,1},{0,1,2,3}};
    const char* sides[]={"top","right","bottom","left"};std::string out;
    for(int i=0;i<4;++i) {
        std::string name=f.name;
        if(name=="inset") name=sides[i];
        else if(name.find("border-")==0) name="border-"+std::string(sides[i])+name.substr(6);
        else name+="-"+std::string(sides[i]);
        out+=name+":"+f.values[map[count-1][i]]+";";
    }return out;
}
void box_gates() {
    for(const auto& f : families) for(bool inlined : {false,true}) {
        const std::string p=f.name, base=longhands(f,4);
        for(int count=1;count<=4;++count) {
            compare(p,p+":"+joined(f,count)+";",longhands(f,count),inlined,"A valid mapping "+std::to_string(count));
            compare(p,"--v:"+joined(f,count)+";"+base+p+":var(--v);",longhands(f,count),inlined,"B substituted mapping "+std::to_string(count));
        }
        for(int bad=0;bad<4;++bad) {
            const auto value=joined(f,4,bad);
            compare(p,base+p+":"+value+";",base,inlined,"A invalid position "+std::to_string(bad));
            compare(p,base+p+":"+value+" !important;",base,inlined,"A invalid important "+std::to_string(bad));
            compare(p,"--v:"+value+";"+base+p+":var(--v);",p+":unset;",inlined,"B invalid position "+std::to_string(bad));
        }
        for(const char* value : {"bogus","initial 12px","10px inherit","unset unset","revert 2px","1px 2px 3px 4px 5px"})
            compare(p,base+p+":"+value+";",base,inlined,"A invalid "+std::string(value));
        for(const char* global : {"initial","unset","inherit","revert"}) {
            // Compare complete global substitution with the engine's existing global policy.
            compare(p,base+p+":var(--missing,"+global+");",base+p+":"+global+";",inlined,"B complete global "+std::string(global));
        }
        compare(p,base+p+":var(--missing);",p+":unset;",inlined,"B missing is not parsing fallback");
        compare(p,base+p+":var();",base,inlined,"A malformed invocation");
        compare(p,base+"--v:initial "+std::string(f.values[0])+";"+p+":var(--v);",p+":unset;",inlined,"B mixed global");
        // Separate sheets and a stronger selector must not publish partial candidates.
        Fixture split(inlined ? p+":"+joined(f,4,2)+" !important;" : ""), ref;
        split.add("#target{"+base+"}"); split.add("html body #target{"+p+":"+joined(f,4,2)+";}");ref.add("#target{"+base+"}");
        expect_equal(split.resolve(),ref.resolve(),p,"A separate sheets specificity");
    }
}
void pending_syntax_and_priority() {
    for(bool inlined : {false,true}) {
        const std::string base="padding:5px;--p:8px;";
        for(const char* v : {"var()","var(color)","var(--p, var())","var(--p, var(color))","var(--p) !bogus","var(--p) !important junk","var(--p))","{} var(--p)","var(--p) {}","var(--p, url(bad\"url))","var(--p, \"bad\n)"})
            compare("padding",base+"padding:"+v+";","padding:5px;",inlined,"A pending syntax "+std::string(v));
        for(const char* v : {"var(--missing)","var(--missing,)","bogus var(--p)","var(--p) bogus"})
            compare("padding",base+"padding:"+v+";","padding:0;",inlined,"B computed "+std::string(v));
        for(const char* v : {"var(--missing,8px)","var(--missing,) 8px","8px var(--missing,)","var(--p, \"! var()\")","var(--p, url(\"! var()\"))"})
            compare("padding",base+"padding:"+v+";","padding:8px;",inlined,"B valid fallback "+std::string(v));
        for(const char* introduced:{"8px !bogus","8px !important junk"})
            compare("padding",std::string("--p:")+introduced+";padding:5px;padding:var(--p);","padding:unset;",inlined,"B introduced importance is computed invalid");
        compare("padding",base+"padding:var(--p) !important;padding:9px;","padding:8px;",inlined,"B terminal important");
        compare("padding",base+"--empty:;padding:var(--empty,9px) 8px;","padding:8px;",inlined,"B defined empty");
        compare("padding",base+"--a:var(--b);--b:var(--a);padding:var(--a,8px);","padding:8px;",inlined,"B cycle fallback");
        compare("padding",base+"--p:10px bogus;padding:var(--p,8px);","padding:0;",inlined,"B no fallback on invalid grammar");
        compare("padding",base+"--p:10px 20px 30px bogus;padding:var(--p);padding-left:7px;","padding:0;padding-left:7px;",inlined,"B overridden invalid sibling");
        compare("padding",base+"padding-left:7px !important;--p:10px bogus;padding:var(--p);","padding:0;padding-left:7px;",inlined,"B longhand important");
        compare("padding",base+"--p:10px bogus;padding:var(--p) !important;padding-left:7px;","padding:0;",inlined,"B shorthand important");
        compare("padding","padding-left:7px;padding:10px bogus;padding-top:9px;","padding-left:7px;padding-top:9px;",inlined,"A interleaved longhands");
        compare("padding","padding:5px !important;padding:10px bogus !important;","padding:5px;",inlined,"A important fallback");
        for(const char* value : {"10px border-box","10px 12qux","10px calc(1px * 1px)","10px/**/bogus"})
            for(const char* p : {"margin","padding"}) compare(p,std::string(p)+":5px;"+p+":"+value+";",std::string(p)+":5px;",inlined,"A grammar "+std::string(value));
        compare("margin","margin:auto -3px 20% calc(2px + 3px);","margin-top:auto;margin-right:-3px;margin-bottom:20%;margin-left:calc(2px + 3px);",inlined,"A supported margin values");
        compare("inset","inset:auto -3px 20% 5px;","top:auto;right:-3px;bottom:20%;left:5px;",inlined,"A supported inset values");
        compare("padding","padding:calc(2px + 3px)/**/8px;","padding-top:calc(2px + 3px);padding-right:8px;padding-bottom:calc(2px + 3px);padding-left:8px;",inlined,"A function and comment tokenization");
    }
}
void shared_controls() {
    struct Control { const char* p; const char* value; const char* expected; };
    const Control controls[]={
        {"overflow","hidden scroll","overflow-x:hidden;overflow-y:scroll;"},
        {"gap","9px 13px","row-gap:9px;column-gap:13px;"},
        {"background","linear-gradient(90deg, red, blue)","background-image:linear-gradient(90deg, red, blue);background-color:transparent;background-position:0% 0%;background-size:auto;background-repeat:repeat;"},
        {"background","red","background-color:red;background-image:none;background-position:0% 0%;background-size:auto;background-repeat:repeat;"},
        {"border","3px dashed red","border-width:3px;border-style:dashed;border-color:red;"},
        {"border-top","3px dashed red","border-top-width:3px;border-top-style:dashed;border-top-color:red;"},
        {"border-right","3px dashed red","border-right-width:3px;border-right-style:dashed;border-right-color:red;"},
        {"border-bottom","3px dashed red","border-bottom-width:3px;border-bottom-style:dashed;border-bottom-color:red;"},
        {"border-left","3px dashed red","border-left-width:3px;border-left-style:dashed;border-left-color:red;"},
        {"flex","2 3 12px","flex-grow:2;flex-shrink:3;flex-basis:12px;"},
        {"transition","opacity 2s ease-in 1s","transition-property:opacity;transition-duration:2s;transition-timing-function:ease-in;transition-delay:1s;"}
    };
    for(const auto& c:controls) for(bool inlined:{false,true}) {
        const std::string p=c.p, base=c.expected;
        compare(p,p+":"+c.value+";",base,inlined,"shared direct valid");
        compare(p,"--v:"+std::string(c.value)+";"+p+":var(--v);",base,inlined,"shared substituted valid");
        compare(p,base+p+":bogus;",base,inlined,"shared parsing invalid");
        compare(p,base+p+":var(--missing);",p+":unset;",inlined,"shared missing computed");
        compare(p,base+"--v:bogus;"+p+":var(--v);",p+":unset;",inlined,"shared incompatible computed");
    }
    for(bool inlined:{false,true}) {
        Fixture f(inlined?"--v:linear-gradient(90deg, red, blue);background:var(--v);":"");
        if(!inlined) f.add("#target{--v:linear-gradient(90deg, red, blue);background:var(--v);}");
        const auto s=f.resolve();check(s.value.background_layers.count==1,"shared gradient retained");
    }
}
void resource_limits() {
    for (bool inlined : {false, true}) for (bool fallback : {false, true}) {
        const std::string declaration = "--p:11px 13px;padding:5px;padding:var(--p" +
            std::string(fallback ? ",8px" : "") + ");";
        Fixture limited(inlined ? declaration : "", "", 32, 4);
        if (!inlined) limited.add("#target{" + declaration + "}");
        const auto style = limited.resolve();
        const float expected = fallback ? 8 : 0;
        check(near(style.value.padding.top.value, expected) &&
              near(style.value.padding.right.value, expected) &&
              near(style.value.padding.bottom.value, expected) &&
              near(style.value.padding.left.value, expected),
              "computed length limit follows resolver fallback policy without partial candidates");
    }
    for (bool inlined : {false, true}) {
        const std::string declaration = "padding:5px;padding:var(--missing,var(--missing,var(--missing,8px)));";
        Fixture limited(inlined ? declaration : "", "", 1, 65536);
        if (!inlined) limited.add("#target{" + declaration + "}");
        const auto style = limited.resolve();
        check(near(style.value.padding.top.value, 0) && near(style.value.padding.right.value, 0) &&
              near(style.value.padding.bottom.value, 0) && near(style.value.padding.left.value, 0),
              "computed depth limit yields unset for complete group");
    }
}
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) { return {}; }
void geometry(Fixture& f,float padding,const std::string& label) {
    aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t result{};
    require(aether_style_runtime_create_resolved_style_map(f.rt,f.doc,&map)==AETHER_OK,"layout map");
    require(aether_layout_create(measure,nullptr,&engine)==AETHER_OK,"layout engine");
    require(aether_layout_layout(engine,f.doc,map,800,600,nullptr,nullptr,&result)==AETHER_OK,"layout");
    auto box=aether_layout_result_find_primary_box(result,aether_doc_get_element_by_id(f.doc,"target"));
    require(box!=nullptr,"layout box");
    const auto border=aether_layout_box_get_border_box(box),content=aether_layout_box_get_content_box(box);
    check(near(border.width,100+2*padding)&&near(border.height,20+2*padding)&&near(content.y,padding)&&near(content.width,100)&&near(content.height,20),label);
    aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);
}
void mutation_and_scopes() {
    Fixture f; f.add("#target{padding:5px;}");
    int64_t sheet=f.add("#target{padding:8px;}");
    f.resolve(); geometry(f,8,"initial valid geometry");
    for(int pass=0;pass<3;++pass) for(bool computed:{false,true}) {
        require(aether_style_runtime_remove_stylesheet(f.rt,sheet)!=0,"remove old valid sheet");
        sheet=f.add(computed ? "#target{--p:10px bogus;padding:var(--p);}" : "#target{padding:10px bogus;}");
        uint32_t impact=0;const auto bad=f.resolve("target",&impact);
        check((impact&AETHER_STYLE_IMPACT_LAYOUT)!=0,"invalid replacement layout impact");
        check(near(bad.value.padding.top.value,computed?0:5),"replacement removes old style");
        geometry(f,computed?0:5,computed?"computed replacement geometry":"original 110x30 origin y=5 repro");
        require(aether_style_runtime_remove_stylesheet(f.rt,sheet)!=0,"remove invalid sheet");
        sheet=f.add("#target{--p:8px;padding:var(--p);}");
        const auto good=f.resolve("target",&impact);
        check((impact&AETHER_STYLE_IMPACT_LAYOUT)!=0 && near(good.value.padding.left.value,8),"valid replacement restores style and impact");
        geometry(f,8,"valid replacement geometry");
    }
    Fixture inline_mutation("--p:8px;padding:5px;padding:var(--p);");
    inline_mutation.resolve();geometry(inline_mutation,8,"inline initial valid geometry");
    auto inline_node=aether_doc_get_element_by_id(inline_mutation.doc,"target");
    for(int pass=0;pass<3;++pass) for(bool invalid:{true,false}) {
        require(aether_element_set_attribute(inline_node,"style",invalid ?
            "--p:10px bogus;padding:5px;padding:var(--p);" :
            "--p:8px;padding:5px;padding:var(--p);")==AETHER_OK,"mutate inline variable");
        uint32_t impact=0;const auto current=inline_mutation.resolve("target",&impact);
        check((impact&AETHER_STYLE_IMPACT_LAYOUT)!=0,"inline variable mutation layout impact");
        const float expected=invalid?0:8;
        check(near(current.value.padding.top.value,expected)&&near(current.value.padding.right.value,expected)&&
              near(current.value.padding.bottom.value,expected)&&near(current.value.padding.left.value,expected),
              "inline variable mutation does not retain prior group");
        geometry(inline_mutation,expected,"inline variable mutation geometry");
        aether_style_update_t* repeated{};
        const int count=aether_style_runtime_resolve_subtree(inline_mutation.rt,aether_doc_get_document_element(inline_mutation.doc),&repeated);
        require(count>=0,"repeat inline resolution");
        check(count==0,"unchanged inline resolution emits zero updates");
        aether_style_updates_free(repeated,count);
    }
    Fixture siblings("","<html><body><div id='parent'><div id='target'></div><div id='other'></div><div id='inherited'></div></div></body></html>");
    siblings.add("#parent{--p:9px;}#target{--p:8px;}#other{--p:10px bogus;}#target,#other,#inherited{padding:5px;padding:var(--p);}");
    const auto a=siblings.resolve(),b=siblings.resolve("other"),c=siblings.resolve("inherited");
    check(near(a.value.padding.top.value,8)&&near(b.value.padding.top.value,0)&&near(c.value.padding.top.value,9),"sibling scopes and inherited variable remain independent");
    Fixture pseudo;
    pseudo.add("#target{--p:10px bogus;padding:8px;}#target::before{content:\"marker\";opacity:0.375;padding:5px;padding:var(--p);}");
    aether_style_update_t* updates{};
    const int n=aether_style_runtime_resolve_subtree(pseudo.rt,aether_doc_get_document_element(pseudo.doc),&updates);
    require(n>=0,"resolve pseudo");bool found=false;
    for(int i=0;i<n;++i) {
        aether_computed_style_handle_t handle{};Style s;
        require(aether_style_runtime_get_update(updates[i],nullptr,&handle,nullptr)==AETHER_OK,"pseudo update");
        require(aether_computed_style_copy(handle,&s.value)==AETHER_OK,"copy pseudo");
        if(near(s.value.opacity,0.375f)) {found=true;check(near(s.value.padding.top.value,0)&&near(s.value.padding.right.value,0)&&near(s.value.padding.bottom.value,0)&&near(s.value.padding.left.value,0),"pseudo computed atomicity");}
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates,n);check(found,"generated pseudo observed through public updates");
}
} // namespace
int main() {
    box_gates(); pending_syntax_and_priority(); shared_controls(); resource_limits(); mutation_and_scopes();
    std::printf("shorthand atomicity public ABI: %d checks, %d failures (explicit checks remain active under NDEBUG)\n",checks,failures);
    return failures?1:0;
}
