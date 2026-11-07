// Public ABI regression suite. All checks execute in Release/NDEBUG builds.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <limits>
#include <string>
#include <vector>

namespace {
unsigned checks = 0, failures = 0;
std::string context;
void need(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "HARNESS [%s] %s\n", context.c_str(), message); std::exit(2); }
}
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL [%s] %s\n", context.c_str(), message); }
}
bool near(float a, float b) { return std::isfinite(a) && std::fabs(a-b) < .001f; }
struct Diagnostic { std::string code, property, value, message; };
void diagnostic(const char* code, const char* property, const char* value, const char* message, void* data) {
    static_cast<std::vector<Diagnostic>*>(data)->push_back({code ? code : "", property ? property : "", value ? value : "", message ? message : ""});
}
struct Style {
    aether_computed_style_t s{};
    Style() = default;
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
    ~Style() { aether_computed_style_free(&s); }
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    std::vector<Diagnostic> diagnostics;
    std::map<aether_node_t, aether_computed_style_handle_t> latest;
    std::map<aether_node_t, uint32_t> impacts;
    Fixture() {
        need(aether_doc_create(&doc) == AETHER_OK, "create document");
        need(aether_doc_parse_html(doc,"<html><body><div id='p'><div id='c' class='target'><div id='g'></div></div></div></body></html>") == AETHER_OK,"parse document");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        need(aether_style_runtime_create(&options,&env,diagnostic,&diagnostics,&runtime) == AETHER_OK,"create runtime");
    }
    ~Fixture() {
        for (auto item : latest) aether_computed_style_handle_destroy(item.second);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) { auto n=aether_doc_get_element_by_id(doc,id); need(n != nullptr,"find node"); return n; }
    int64_t add(const std::string& css) {
        int64_t id{};
        need(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"transform-runtime.css",&id,nullptr) == AETHER_OK,"add sheet"); return id;
    }
    void set(const char* id,const std::string& css) { need(aether_element_set_attribute(node(id),"style",css.c_str()) == AETHER_OK,"set inline"); }
    void resolve() {
        impacts.clear(); aether_style_update_t* updates{};
        int count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        need(count >= 0,"resolve");
        for(int i=0;i<count;++i) {
            aether_node_t n{}; aether_computed_style_handle_t h{}; uint32_t impact{};
            need(aether_style_runtime_get_update(updates[i],&n,&h,&impact) == AETHER_OK,"read update");
            if(latest.count(n)) aether_computed_style_handle_destroy(latest[n]);
            latest[n]=h; impacts[n]=impact;
        }
        aether_style_updates_free(updates,count);
    }
    void copy(const char* id,Style& out) { need(latest.count(node(id)) != 0,"style exists"); need(aether_computed_style_copy(latest[node(id)],&out.s)==AETHER_OK,"copy style"); }
};
void none(const Style& s) { check(s.s.transforms.count==0,"computed transform is none"); }
void scale(const Style& s,float factor) {
    check(s.s.transforms.count==1&&s.s.transforms.items,"one transform operation");
    if(s.s.transforms.count!=1||!s.s.transforms.items) return;
    const auto& op=s.s.transforms.items[0];
    check(op.kind==AETHER_TRANSFORM_SCALE,"scale operation kind");
    check(near(op.scalar_x,factor)&&near(op.scalar_y,factor),"both scale axes");
}
void expect(Fixture& f,float factor) { Style s; f.copy("c",s); if(factor==0) none(s); else scale(s,factor); }
void diagnostics(const std::vector<Diagnostic>& ds,const char* code,unsigned count) {
    check(ds.size()==count,"exact diagnostic count");
    for(const auto& d:ds) {
        check(d.code==code,"owned diagnostic code"); check(d.property=="transform","owned diagnostic property");
        // Successfully resolved empty fallback legitimately has an empty value.
        check(!d.message.empty(),"owned diagnostic message");
    }
}
void value_case(bool inlined,const std::string& value,float expected,const char* code,unsigned count,const std::string& definitions="") {
    Style saved; std::vector<Diagnostic> copied;
    {
        Fixture f; f.add("#p{transform:scale(7)}#c{transform:scale(2)}");
        auto declaration=definitions+"transform:"+value+";";
        if(inlined) f.set("c",declaration); else f.add("#c{"+declaration+"}");
        f.resolve(); f.copy("c",saved); expect(f,expected); copied=f.diagnostics;
        diagnostics(copied,code,count);
        Style independent; f.copy("c",independent);
        check(aether_transform_list_equal(&saved.s.transforms,&independent.s.transforms),"equal independent snapshots");
        if(saved.s.transforms.count) check(saved.s.transforms.items!=independent.s.transforms.items,"transform snapshots own storage");
        for(int i=0;i<3;++i) {
            f.resolve(); expect(f,expected);
            check(!f.impacts.count(f.node("c"))||f.impacts[f.node("c")]==0,"repeat resolve has no impact");
        }
    }
    if(expected==0) none(saved); else scale(saved,expected);
    diagnostics(copied,code,count);
}
void invalid_values() {
    const char* invalid[]={"scale(2 3)","translate(10px 20px)","matrix(1 0 0 1 2 3)",
        "scale(0x1p2)","rotate(0x1p2deg)","translateX(12)","translateX(calc(0))","translateX(calc(1 + 2))","scale(2) translateX(12)",
        "translateZ(1px)","rotate3d(1,0,0,90deg)","matrix3d(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)",
        "scale(1e-999)","scale(1e999)","rotate(1e38turn)","scale(NaN)","scale(2,)","translate(,1px)","rotate(1)",
        "scale(2) garbage", "none scale(2)","sc/**/ale(2)", "translateX(c\\61 lc/**/(1px))","translateX(c\\61 lc (1px))","translateX(calc\\(1px\\))","translateX(c\\61 lc(0))", "translateX(1\\65 2px)", "translateX(calc(1p\\78 + 2%))", "rotate(1\\65 2deg)"};
    for(bool inlined:{false,true}) {
        for(const auto* v:invalid) {
            context=std::string(inlined?"inline/":"sheet/")+v;
            value_case(inlined,v,2,"AETHER-CSS-TRANSFORM",1);
            value_case(inlined,"var(--t,scale(9))",0,"AETHER-CSS-TRANSFORM",1,std::string("--t:")+v+";");
            value_case(inlined,std::string("var(--missing,")+v+")",0,"AETHER-CSS-TRANSFORM",1);
            value_case(inlined,std::string("var(--missing,var(--nested,")+v+"))",0,"AETHER-CSS-TRANSFORM",1);
        }
        context="empty resolved fallback";
        value_case(inlined,"var(--missing,)",0,"AETHER-CSS-TRANSFORM",1);
        context="empty nested resolved fallback";
        value_case(inlined,"var(--missing,var(--nested,))",0,"AETHER-CSS-TRANSFORM",1);
        context="var substitution cannot fuse dimension tokens";
        value_case(inlined,"translateX(var(--n)px)",0,"AETHER-CSS-TRANSFORM",1,"--n:12;");
        context="unresolved custom property";
        value_case(inlined,"var(--missing)",0,"AETHER-CSS-VAR",1);
        context="cyclic custom property";
        value_case(inlined,"var(--a)",0,"AETHER-CSS-VAR",1,"--a:var(--b);--b:var(--a);");
        context="resolved none"; value_case(inlined,"var(--t,scale(9))",0,"",0,"--t:none;");
    }
}
struct Valid { const char* source; int count; int kind; float x,y; };
void valid_values() {
    const Valid values[]={
        {"none",0,0,0,0},{"/**/NoNe/**/",0,0,0,0},{"\\6e one",0,0,0,0},
        {"scale(3)",1,AETHER_TRANSFORM_SCALE,3,3},{"ScAlE(3, .5)",1,AETHER_TRANSFORM_SCALE,3,.5f},
        {"\\73 cale(3)",1,AETHER_TRANSFORM_SCALE,3,3},{"scale(/**/3/**/,/**/.5/**/)",1,AETHER_TRANSFORM_SCALE,3,.5f},
        {"scaleX(3)",1,AETHER_TRANSFORM_SCALE,3,1},{"scaleY(3)",1,AETHER_TRANSFORM_SCALE,1,3},
        {"rotate(0)",1,AETHER_TRANSFORM_ROTATE,0,0},{"rotate(+0e2)",1,AETHER_TRANSFORM_ROTATE,0,0},
        {"rotate(-0.0)",1,AETHER_TRANSFORM_ROTATE,0,0},{"rotate(.25turn)",1,AETHER_TRANSFORM_ROTATE,1.5707963f,0},
        {"rotate(90d\\65 g)",1,AETHER_TRANSFORM_ROTATE,1.5707963f,0},
        {"translateX(10p\\78)",1,AETHER_TRANSFORM_TRANSLATE,10,0},
        {"translate(calc(10p\\78  + 25%),calc(20\\70x + 10%))",1,AETHER_TRANSFORM_TRANSLATE,10,20},
        {"translate(c\\61 lc(10px + 25%),\\63 alc(20px + 10%))",1,AETHER_TRANSFORM_TRANSLATE,10,20},
        {"translate(10px,20px)",1,AETHER_TRANSFORM_TRANSLATE,10,20},
        {"translateX(0)",1,AETHER_TRANSFORM_TRANSLATE,0,0},
        {"translate(calc(10px + 25%),calc(20px + 10%))",1,AETHER_TRANSFORM_TRANSLATE,10,20},
        {"matrix(1,0,0,1,3,4)",1,AETHER_TRANSFORM_MATRIX,3,4},
        {"skewX(0) skewY(0)",2,AETHER_TRANSFORM_SKEW_X,0,0},
        {"translate(10px,20px)/**/rotate(0) scale(3,.5)",3,AETHER_TRANSFORM_TRANSLATE,10,20}
    };
    for(bool inlined:{false,true}) for(const auto& v:values) for(int route=0;route<3;++route) {
        context=std::string(inlined?"inline valid/":"sheet valid/")+v.source+"/"+std::to_string(route);
        Style saved;
        {
            Fixture f; f.add("#c{transform:scale(2)}");
            std::string declaration="transform:"+std::string(v.source);
            if(route==1) declaration="--t:"+std::string(v.source)+";transform:var(--t)";
            if(route==2) declaration="transform:var(--absent,"+std::string(v.source)+")";
            if(inlined) f.set("c",declaration); else f.add("#c{"+declaration+"}");
            f.resolve(); f.copy("c",saved); diagnostics(f.diagnostics,"",0);
        }
        check(saved.s.transforms.count==v.count,"valid list operation count survives runtime destruction");
        if(!v.count||saved.s.transforms.count!=v.count||!saved.s.transforms.items) continue;
        const auto& op=saved.s.transforms.items[0]; check(op.kind==v.kind,"valid first operation kind");
        if(v.kind==AETHER_TRANSFORM_SCALE) check(near(op.scalar_x,v.x)&&near(op.scalar_y,v.y),"valid scale scalars");
        if(v.kind==AETHER_TRANSFORM_ROTATE) check(near(op.angle_radians,v.x),"valid angle radians");
        if(v.kind==AETHER_TRANSFORM_TRANSLATE) {
            check(near(op.x.value,v.x)&&near(op.y.value,v.y),"valid translation components");
            if(op.x.unit==AETHER_CSS_UNIT_LINEAR) {
                check(op.x.unit==AETHER_CSS_UNIT_LINEAR&&op.y.unit==AETHER_CSS_UNIT_LINEAR,"symbolic calc units");
                check(near(op.x.linear_percent,25)&&near(op.y.linear_percent,10),"symbolic calc percentages");
            }
        }
        if(v.kind==AETHER_TRANSFORM_MATRIX) check(near(op.matrix_e,v.x)&&near(op.matrix_f,v.y),"valid matrix translation");
        if(v.count==3) check(saved.s.transforms.items[1].kind==AETHER_TRANSFORM_ROTATE&&saved.s.transforms.items[2].kind==AETHER_TRANSFORM_SCALE,"authored operation order retained");
    }
}
void diagnostic_payloads() {
    for (bool inlined : {false,true}) for (bool deferred : {false,true}) {
        context = std::string("diagnostic payload/") + (inlined ? "inline/" : "sheet/") + (deferred ? "deferred" : "literal");
        std::vector<Diagnostic> retained;
        {
            Fixture f;
            const std::string value = deferred ? "var(--bad)" : "scale(2 3)";
            const std::string declaration = "--bad:scale(2 3);transform:" + value;
            if (inlined) f.set("c", declaration); else f.add("#c{" + declaration + "}");
            check(f.diagnostics.size() == (!inlined && !deferred ? 1u : 0u), "literal sheet diagnostic occurs during compilation");
            f.resolve();
            retained = f.diagnostics;
        }
        diagnostics(retained, "AETHER-CSS-TRANSFORM", 1);
        if (retained.size() == 1) check(retained[0].value == "scale(2 3)", "diagnostic owns rejected or substituted value after destruction");
    }
}
void priorities() {
    for(bool deferred:{false,true}) for(int route=0;route<7;++route) {
        context="cascade priority/"+std::to_string(route)+(deferred?"/computed invalid":"/literal invalid");
        Fixture f; std::string high=std::string("transform:")+(deferred?"var(--absent,scale(2 3))":"scale(2 3)");
        if(route==0) f.add("#c{transform:scale(2);"+high+"}");
        if(route==1) f.add("#c{transform:scale(2)}#c{"+high+"}");
        if(route==2) f.add("#c{"+high+"}.target{transform:scale(2)}");
        if(route==3) f.add(".target{"+high+"!important}#c{transform:scale(2)}");
        if(route==4) { f.add("#c{transform:scale(2)}"); f.set("c",high); }
        if(route==5) { f.add("#c{transform:scale(2)!important}"); f.set("c",high+"!important"); }
        if(route==6) f.set("c","transform:scale(2);"+high);
        f.resolve(); expect(f,deferred?0:2); diagnostics(f.diagnostics,"AETHER-CSS-TRANSFORM",1);
    }
    context="important sheet beats inline normal"; Fixture f;
    f.add("#c{transform:scale(2)!important;transform:scale(3)}"); f.set("c","transform:scale(4)"); f.resolve(); expect(f,2);
}
void check_impact(Fixture& f,uint32_t expected) {
    auto n=f.node("c"); check(f.impacts.count(n)!=0,"changed transform emits update");
    if(f.impacts.count(n)) {
        check(f.impacts[n]==expected,"exact transform impact bits");
        if(f.impacts[n]!=expected) std::fprintf(stderr,"  actual=%u expected=%u\n",f.impacts[n],expected);
    }
}
void mutations() {
    const uint32_t transform=AETHER_STYLE_IMPACT_COMPOSITE|AETHER_STYLE_IMPACT_HITTEST;
    for(bool inlined:{false,true}) {
        context=inlined?"inline mutation":"selector mutation"; Fixture f;
        f.add("#c{transform:scale(2);width:200px;height:100px;transform-origin:left top}"
              "#c.valid{transform:scale(3)}#c.literal{transform:scale(2 3)}#c.computed{transform:var(--missing,scale(2 3))}");
        auto change=[&](int step) {
            const char* names[]={"valid","literal","computed","valid"};
            const char* decls[]={"transform:scale(3)","transform:scale(2 3)","transform:var(--missing,scale(2 3))","transform:scale(3)"};
            if(inlined) f.set("c",decls[step]);
            else need(aether_element_set_attribute(f.node("c"),"class",names[step])==AETHER_OK,"change selector membership");
        };
        change(0); f.resolve(); expect(f,3); Style saved; f.copy("c",saved);
        for(int step=1;step<4;++step) {
            context=std::string(inlined?"inline mutation/":"selector mutation/")+std::to_string(step);
            for(int repeat=0;repeat<3;++repeat) {
                f.resolve(); check(!f.impacts.count(f.node("c"))||f.impacts[f.node("c")]==0,"stable resolve no impact");
            }
            change(step); f.resolve(); expect(f,step==1?2:step==2?0:3);
            check_impact(f,transform|(step>=2?AETHER_STYLE_IMPACT_LAYOUT:0));
            Style current; f.copy("c",current);
            check(near(current.s.width.value,200)&&near(current.s.height.value,100),"transform validation preserves dimensions");
            check(near(current.s.transform_origin.x.value,0)&&near(current.s.transform_origin.y.value,0),"transform validation preserves origin");
            scale(saved,3);
        }
    }
}
aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) { return {0,1,.8f}; }
void geometry() {
    for(bool invalid:{false,true}) {
        context=invalid?"computed-invalid display list":"ordered public paint matrix"; Fixture f;
        f.add("*{display:block;margin:0;padding:0}#c{width:200px;height:100px;transform-origin:left top;transform:"+
              std::string(invalid?"var(--missing,scale(2 3))":"translate(10px,20px) scale(2,3)")+"}");
        f.resolve();
        aether_style_map_t styles{}; aether_layout_engine_t engine{}; aether_layout_result_t layout{};
        aether_display_list_builder_t builder{}; aether_display_list_t list{};
        need(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&styles)==AETHER_OK,"paint style map");
        need(aether_layout_create(measure,nullptr,&engine)==AETHER_OK,"paint engine");
        need(aether_layout_layout(engine,f.doc,styles,800,600,nullptr,nullptr,&layout)==AETHER_OK,"paint layout");
        need(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"paint builder");
        need(aether_display_list_build(builder,layout,nullptr,&list)==AETHER_OK,"paint display list");
        int depth=0,pushes=0;
        for(int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t cmd{}; need(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"paint command");
            if(cmd.tag==AETHER_PAINT_PUSH_TRANSFORM) {
                ++depth; ++pushes; const auto& m=cmd.data.push_transform.matrix;
                check(near(m.m11,2)&&near(m.m22,3)&&near(m.m12,0)&&near(m.m21,0),"public affine scale components");
                check(near(m.dx,10)&&near(m.dy,20),"rightmost scale precedes translation");
                check(near(4*m.m11+5*m.m21+m.dx,18)&&near(4*m.m12+5*m.m22+m.dy,35),"public matrix maps point in CSS order");
            }
            if(cmd.tag==AETHER_PAINT_POP_TRANSFORM) { --depth; check(depth>=0,"transform pop has matching push"); }
            aether_paint_command_free(&cmd);
        }
        check(depth==0,"transform brackets balanced"); check(pushes==(invalid?0:1),"only valid list emits transform bracket");
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); aether_layout_destroy(engine); aether_style_map_destroy(styles);
    }
}
} // namespace
int main() {
    invalid_values(); valid_values(); diagnostic_payloads(); priorities(); mutations(); geometry();
    std::printf("transform public runtime: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
