// Public-ABI flex number boundary coverage; all oracles remain live with NDEBUG.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace {
int checks = 0, cases = 0;
std::string label = "setup";
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL flex numbers [%s]: %s\n", label.c_str(), message);
        std::exit(1);
    }
}
void number(float actual, float expected, const char* field) {
    if (!(std::isfinite(actual) && actual == expected &&
          (expected != 0 || !std::signbit(actual))))
        std::fprintf(stderr, "%s actual=%.9g expected=%.9g\n", field, actual, expected);
    require(std::isfinite(actual) && actual == expected &&
            (expected != 0 || !std::signbit(actual)), field);
}
struct Expected {
    float grow, shrink;
    int kind;
    aether_css_unit_t unit;
    float basis;
};
constexpr Expected baseline{3,4,AETHER_FLEX_BASIS_LENGTH,AETHER_CSS_UNIT_PX,27};
constexpr Expected initial{0,1,AETHER_FLEX_BASIS_AUTO,AETHER_CSS_UNIT_AUTO,0};
Expected px(float grow, float shrink, float basis) {
    return {grow,shrink,AETHER_FLEX_BASIS_LENGTH,AETHER_CSS_UNIT_PX,basis};
}
void expect(const aether_computed_style_t& s, Expected e, bool neighbors = true) {
    number(s.flex_grow,e.grow,"grow"); number(s.flex_shrink,e.shrink,"shrink");
    require(s.flex_basis.kind == e.kind,"basis kind");
    require(s.flex_basis.length.unit == e.unit,"basis unit");
    number(s.flex_basis.length.value,e.basis,"basis value");
    number(s.flex_basis.length.linear_percent,0,"basis percent coefficient");
    number(s.flex_basis.length.linear_em,0,"basis em coefficient");
    number(s.flex_basis.length.linear_rem,0,"basis rem coefficient");
    number(s.flex_basis.length.linear_vw,0,"basis vw coefficient");
    number(s.flex_basis.length.linear_vh,0,"basis vh coefficient");
    if (neighbors) {
        number(s.opacity,.75f,"neighbor opacity");
        number(s.color.r,1,"neighbor color red"); number(s.color.g,0,"neighbor color green");
        number(s.color.b,0,"neighbor color blue"); number(s.color.a,1,"neighbor color alpha");
    }
}
struct Diagnostic { std::string code, property, value, message; };
void diagnostic(const char* code,const char* property,const char* value,const char* message,void* user) {
    // The callback owns none of its arguments. Retain copies, never their pointers.
    static_cast<std::vector<Diagnostic>*>(user)->push_back(
        {code?code:"",property?property:"",value?value:"",message?message:""});
}
struct Style {
    aether_computed_style_t value{};
    explicit Style(aether_computed_style_handle_t handle) {
        require(handle != nullptr,"non-null style handle");
        require(aether_computed_style_copy(handle,&value)==AETHER_OK,"copy style");
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_node_t target{};
    int target_updates = 0;
    uint32_t impact = 0;
    bool ever_found = false;
    std::vector<Diagnostic> diagnostics;
    explicit Fixture(const std::string& sheet, const std::string& inline_style,
                     const char* html = "<html><body id='parent'><div id='x'></div></body></html>") {
        ++cases;
        require(aether_doc_create(&doc)==AETHER_OK,"create document");
        require(aether_doc_parse_html(doc,html)==AETHER_OK,"parse document");
        target=aether_doc_get_element_by_id(doc,"x"); require(target!=nullptr,"find target node");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{240,120,1,16,0,0,0};
        require(aether_style_runtime_create(&options,&env,diagnostic,&diagnostics,&runtime)==AETHER_OK,"create runtime");
        add("#parent{flex:7 8 41px;}#x{color:red;opacity:.75;}"+sheet);
        require(aether_element_set_attribute(target,"style",inline_style.c_str())==AETHER_OK,"set inline declaration");
        resolve(); require(target_updates==1,"exactly one initial target update");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    void add(const std::string& css) {
        int64_t id{}; int32_t rules{};
        require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"flex-number.css",&id,&rules)==AETHER_OK,"add stylesheet");
        require(rules>0,"stylesheet has rules");
    }
    void resolve() {
        aether_style_update_t* updates{};
        const int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(count>=0,"resolve subtree"); require(count==0 || updates!=nullptr,"update storage");
        target_updates=0; impact=0;
        for (int i=0;i<count;++i) {
            aether_node_t n{}; aether_computed_style_handle_t h{}; uint32_t flags{};
            require(aether_style_runtime_get_update(updates[i],&n,&h,&flags)==AETHER_OK,"get update");
            require(n!=nullptr && h!=nullptr,"update identities");
            if (n==target) { ++target_updates; impact|=flags; ever_found=true; }
            aether_computed_style_handle_destroy(h);
        }
        aether_style_updates_free(updates,count);
    }
    aether_style_map_t map() {
        aether_style_map_t result{};
        require(aether_style_runtime_create_resolved_style_map(runtime,doc,&result)==AETHER_OK,"resolved map");
        require(result!=nullptr,"non-null resolved map"); return result;
    }
    void current(Expected expected) {
        auto styles=map(); auto h=aether_style_map_get(styles,target);
        { Style s(h); expect(s.value,expected); }
        aether_computed_style_handle_destroy(h); aether_style_map_destroy(styles);
    }
    void stable(Expected expected) {
        require(ever_found,"target observed before no-op");
        resolve(); require(target_updates==0 && impact==0,"stable target has no update or impact");
        current(expected); // No-update is not evidence that the cached value is right.
    }
};
const std::string fallback="flex:3 4 27px;";
void run(bool sheet,const std::string& declaration,Expected expected) {
    label=std::string(sheet?"sheet ":"inline ")+declaration;
    const auto all=fallback+declaration;
    Fixture f(sheet?"#x{"+all+"}":"",sheet?"":all);
    f.current(expected);
}
void matrix() {
    const char* invalid[]={"nan","NaN","nan(payload)","inf","Infinity","+inf","-inf",
        "0x1p1","0X1P+1","-1","-1e-50","-1e-400","1.","1e","1e+","1e-",
        ".","+","1px","1%","1 2","1,2","1/2","1/**/e2","+/**/.5","calc(1)"};
    struct Valid { const char* source; float value; };
    const Valid valid[]={{".5",.5f},{"1E1",10},{"003",3},{"+0",0},{"-0e10",0},
        {"-0.000e-99",0},{"/**/2/**/",2},{"1e39",std::numeric_limits<float>::max()},
        {"1e400",std::numeric_limits<float>::max()},{"1e-400",0},{"1e-40",1e-40f}};
    for (bool sheet:{false,true}) {
        for (bool grow:{false,true}) {
            const std::string property=grow?"flex-grow":"flex-shrink";
            for (const char* value:invalid) {
                run(sheet,property+":"+value+";",baseline);
                run(sheet,"--v:"+std::string(value)+";"+property+":var(--v);",px(grow?0:3,grow?4:1,27));
            }
            for (const auto& value:valid) {
                const auto expected=px(grow?value.value:3,grow?4:value.value,27);
                run(sheet,property+":"+value.source+";",expected);
                run(sheet,"--v:"+std::string(value.source)+";"+property+":var(--v);",expected);
            }
            run(sheet,property+":var(--missing,2);",px(grow?2:3,grow?4:2,27));
            run(sheet,"--v:nan;"+property+":var(--v,2);",px(grow?0:3,grow?4:1,27));
            run(sheet,property+":var(--missing);",px(grow?0:3,grow?4:1,27));
        }
        for (const char* bad:{"nan","NaN","inf","Infinity","-inf","0x1p1","-1e-400","calc(1)"}) {
            for (bool first:{false,true}) {
                const std::string value=first?std::string(bad)+" 5 18px":"2 "+std::string(bad)+" 18px";
                run(sheet,"flex:"+value+";",baseline);
                run(sheet,"--v:"+value+";flex:var(--v);",initial);
            }
        }
        for (const char* bad:{"2 5 bogus","2 5 18px extra","2 5 -1px","2 5 calc(1px + 1s)"}) {
            run(sheet,"flex:"+std::string(bad)+";",baseline);
            run(sheet,"--v:"+std::string(bad)+";flex:var(--v);",initial);
        }
        struct Shorthand { const char* source; Expected value; };
        const Shorthand shorthands[]={
            {"2",{2,1,AETHER_FLEX_BASIS_LENGTH,AETHER_CSS_UNIT_PERCENT,0}},
            {"2 5",{2,5,AETHER_FLEX_BASIS_LENGTH,AETHER_CSS_UNIT_PERCENT,0}},
            {"2 18px",px(2,1,18)},
            {"2 5 calc(10px + 2px)",{2,5,AETHER_FLEX_BASIS_LENGTH,AETHER_CSS_UNIT_LINEAR,12}},
            {"none",{0,0,AETHER_FLEX_BASIS_AUTO,AETHER_CSS_UNIT_AUTO,0}},
            {"auto",{1,1,AETHER_FLEX_BASIS_AUTO,AETHER_CSS_UNIT_AUTO,0}},
            {"1e400 5 18px",px(std::numeric_limits<float>::max(),5,18)},
            {"2 1e400 18px",px(2,std::numeric_limits<float>::max(),18)},
            {"1e-400 1e-400 18px",px(0,0,18)},
            {"/**/2/**/ 5/**/ 18px/**/",px(2,5,18)}};
        for (const auto& value:shorthands) {
            run(sheet,"flex:"+std::string(value.source)+";",value.value);
            run(sheet,"--v:"+std::string(value.source)+";flex:var(--v);",value.value);
        }
        run(sheet,"--bad:nan 2 18px;flex:var(--bad)!important;flex-shrink:9!important;",
            {0,9,AETHER_FLEX_BASIS_AUTO,AETHER_CSS_UNIT_AUTO,0});
        run(sheet,"flex-grow:7!important;--bad:2 inf 18px;flex:var(--bad);",
            {7,1,AETHER_FLEX_BASIS_AUTO,AETHER_CSS_UNIT_AUTO,0});
        run(sheet,"flex:nan 2 18px!important;",baseline);
        run(sheet,"flex:2 5 18px;flex-shrink:9;",px(2,9,18));
        run(sheet,"flex:var(--missing,2 5 18px);",px(2,5,18));
        run(sheet,"--bad:nan;flex:var(--bad,2 5 18px);",initial);
        // Unbalanced exterior syntax is deliberately outside this matrix:
        // it need not admit a declaration or a deferred winner at all.
    }
    label="origin priority: inline beats later sheet";
    { Fixture f("#x{flex:3 4 27px;}","flex:2 5 18px;"); f.current(px(2,5,18));
      f.add("#x{flex:6 8 31px;}"); f.resolve(); f.current(px(2,5,18)); }
    label="origin priority: sheet important beats inline normal";
    { Fixture f("#x{flex:3 4 27px!important;}","flex:2 5 18px;"); f.current(baseline); }
    label="origin priority: inline important beats sheet important";
    { Fixture f("#x{flex:3 4 27px!important;}","flex:2 5 18px!important;"); f.current(px(2,5,18)); }
}
void diagnostics() {
    struct Case { const char* declaration; const char* code; const char* property; const char* value; Expected expected; };
    const Case values[]={
        {"flex-grow:nan;",nullptr,nullptr,nullptr,baseline},
        {"flex-shrink:inf;",nullptr,nullptr,nullptr,baseline},
        {"flex:nan 2 18px;",nullptr,nullptr,nullptr,baseline},
        {"flex-grow:calc(1);","AETHER-CSS-CALC","flex-grow","calc(1)",baseline},
        {"--v:nan;flex-grow:var(--v);","AETHER-CSS-VALUE","flex-grow","nan",px(0,4,27)},
        {"--v:inf;flex-shrink:var(--v);","AETHER-CSS-VALUE","flex-shrink","inf",px(3,1,27)},
        {"--v:calc(1);flex-grow:var(--v);","AETHER-CSS-CALC","flex-grow","calc(1)",px(0,4,27)},
        {"flex-grow:var(--missing);","AETHER-CSS-VAR","flex-grow","var(--missing)",px(0,4,27)},
        {"--v:var(--v);flex-shrink:var(--v);","AETHER-CSS-VAR","flex-shrink","var(--v)",px(3,1,27)},
        {"--v:2 inf 18px;flex:var(--v);","AETHER-CSS-VALUE","flex","var(--v)",initial},
        {"flex:var(--missing);","AETHER-CSS-VAR","flex","var(--missing)",initial}};
    for (bool sheet:{false,true}) for (const auto& c:values) {
        label=std::string("diagnostic ")+(sheet?"sheet ":"inline ")+c.declaration;
        const std::string all=fallback+c.declaration;
        Fixture f(sheet?"#x{"+all+"}":"",sheet?"":all);
        f.current(c.expected);
        require(f.diagnostics.size()==(c.code?1u:0u),"isolated fixture diagnostic count");
        if (c.code) {
            const auto& d=f.diagnostics[0];
            require(d.code==c.code,"copied diagnostic code");
            require(d.property==c.property,"copied diagnostic property");
            require(d.value==c.value,"copied diagnostic value");
            require(!d.message.empty(),"copied diagnostic message");
        }
        std::printf("diagnostic fixture %s: %zu callbacks\n",label.c_str(),f.diagnostics.size());
        // No assertion about global counts or whether later resolve rediagnoses.
    }
}
void lifecycle() {
    label="snapshot lifecycle";
    Fixture f("",fallback);
    auto retained=f.map(); auto old=aether_style_map_get(retained,f.target);
    require(old!=nullptr,"retained handle");
    aether_computed_style_handle_t clone{};
    require(aether_computed_style_handle_clone(old,&clone)==AETHER_OK,"clone retained handle");
    Style copied(old);
    f.current(baseline); f.stable(baseline);
    struct Mutation { const char* declaration; Expected expected; bool changed; };
    const Mutation mutations[]={
        {"flex:.5 2 18px;",px(.5f,2,18),true},
        {"flex:5e-1 2 18px;",px(.5f,2,18),false},
        {"flex:6 8 31px;flex:nan 2 18px;",px(6,8,31),true},
        {"flex:6 8 31px;--bad:2 inf 18px;flex:var(--bad);",initial,true},
        {"flex:2 5 18px;",px(2,5,18),true}};
    for (const auto& m:mutations) {
        label=std::string("snapshot mutation ")+m.declaration;
        require(aether_element_set_attribute(f.target,"style",m.declaration)==AETHER_OK,"mutate style");
        f.resolve();
        require(f.target_updates==(m.changed?1:0),"expected target delta count");
        require(m.changed?(f.impact&AETHER_STYLE_IMPACT_LAYOUT)!=0:f.impact==0,"layout impact or spelling no-op");
        f.current(m.expected); f.stable(m.expected);
        for (auto h:{old,clone}) { Style snapshot(h); expect(snapshot.value,baseline); }
        auto h=aether_style_map_get(retained,f.target);
        { Style snapshot(h); expect(snapshot.value,baseline); }
        aether_computed_style_handle_destroy(h);
        expect(copied.value,baseline);
    }
    aether_style_map_destroy(retained);
    { Style snapshot(old); expect(snapshot.value,baseline); }
    aether_computed_style_handle_destroy(old);
    { Style snapshot(clone); expect(snapshot.value,baseline); }
    aether_computed_style_handle_destroy(clone);
    expect(copied.value,baseline);
}
aether_text_metrics_t measure_empty(const char*,const aether_computed_style_t*,float,void*) { return {0,0,0}; }
void layout_cases() {
    struct Case { const char* name; int width; const char* first; const char* second; Expected a,b; float aw,bw; };
    const Case values[]={
        {"moderate grow",120,"flex:1 1 20px;flex-grow:nan;","flex:3 1 20px;flex-grow:0x1p1;",px(1,1,20),px(3,1,20),40,80},
        {"moderate shrink",90,"flex:0 1 60px;flex-shrink:inf;","flex:0 2 60px;flex-shrink:-1e-400;",px(0,1,60),px(0,2,60),50,40},
        {"single saturation control",120,"flex:1e400 1 20px;","flex:1e400 1 20px;",
            px(std::numeric_limits<float>::max(),1,20),px(std::numeric_limits<float>::max(),1,20),60,60}};
    for (const auto& c:values) {
        label=c.name;
        const std::string css="html,body{margin:0;padding:0;border:0;}#host{display:flex;flex-direction:row;width:"+
            std::to_string(c.width)+"px;height:20px;gap:0;align-items:flex-start;}"
            "#host>div{display:block;min-width:0;height:20px;margin:0;padding:0;border:0;}"
            "#x{"+c.first+"}#y{"+c.second+"}";
        Fixture f(css,"","<html><body><div id='host'><div id='x'></div><div id='y'></div></div></body></html>");
        f.current(c.a);
        auto styles=f.map(); const auto second=aether_doc_get_element_by_id(f.doc,"y");
        require(second!=nullptr,"second layout item");
        auto h=aether_style_map_get(styles,second);
        { Style s(h); expect(s.value,c.b,false); }
        aether_computed_style_handle_destroy(h);
        aether_layout_engine_t engine{}; aether_layout_result_t result{};
        require(aether_layout_create(measure_empty,nullptr,&engine)==AETHER_OK,"create layout engine");
        require(aether_layout_layout(engine,f.doc,styles,240,120,nullptr,nullptr,&result)==AETHER_OK,"perform layout");
        require(result!=nullptr,"non-null layout result");
        const auto rect=[&](const char* id) {
            auto node=aether_doc_get_element_by_id(f.doc,id); require(node!=nullptr,"layout node");
            auto box=aether_layout_result_find_primary_box(result,node); require(box!=nullptr,"primary layout box");
            return aether_layout_box_get_border_box(box);
        };
        const auto host=rect("host"), a=rect("x"), b=rect("y");
        number(host.width,static_cast<float>(c.width),"container width"); number(host.height,20,"container height");
        number(a.width,c.aw,"first width"); number(b.width,c.bw,"second width");
        number(a.x-host.x,0,"first position"); number(b.x-host.x,c.aw,"second position");
        number(a.y-host.y,0,"first cross position"); number(b.y-host.y,0,"second cross position");
        number(a.height,20,"first height"); number(b.height,20,"second height");
        aether_layout_result_destroy(result); aether_layout_destroy(engine); aether_style_map_destroy(styles);
    }
}
} // namespace
int main() {
#if defined(__unix__) || defined(__APPLE__)
    Dl_info info{};
    require(dladdr(reinterpret_cast<void*>(aether_abi_version),&info)!=0,"identify loaded DSO");
    std::printf("loaded=%s\n",info.dli_fname);
#endif
    require(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::radix==2 &&
        std::numeric_limits<float>::digits==24 && std::numeric_limits<float>::has_denorm==std::denorm_present,
        "binary32/subnormal premises for independent tiny-value oracle");
    require(std::fegetround()==FE_TONEAREST,"round-to-nearest premise for numeric oracles");
    require(1e-40f>0,"subnormal oracle remains nonzero");
    matrix(); diagnostics(); lifecycle(); layout_cases();
    std::printf("flex number pipeline: %d fixtures, %d checks (active with NDEBUG)\n",cases,checks);
    return 0;
}
