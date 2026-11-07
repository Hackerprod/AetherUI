// Public ABI regression suite. All checks execute in Release/NDEBUG builds.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
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
        need(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"origin-runtime.css",&id,nullptr) == AETHER_OK,"add sheet"); return id;
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
// Evaluate the public length aggregate without depending on private parser or matrix helpers.
float used(const aether_css_length& l,float extent,float font) {
    switch(l.unit) {
    case AETHER_CSS_UNIT_PX:return l.value;
    case AETHER_CSS_UNIT_PERCENT:return extent*l.value/100;
    case AETHER_CSS_UNIT_EM:return font*l.value;
    case AETHER_CSS_UNIT_REM:return 16*l.value;
    case AETHER_CSS_UNIT_VW:return 8*l.value;
    case AETHER_CSS_UNIT_VH:return 6*l.value;
    case AETHER_CSS_UNIT_LINEAR:return l.value+extent*l.linear_percent/100+font*l.linear_em+16*l.linear_rem+8*l.linear_vw+6*l.linear_vh;
    default:check(false,"origin has resolvable length unit");return -9999;
    }
}
void origin(const Style& s,float x,float y,float w=200,float h=100) {
    check(near(used(s.s.transform_origin.x,w,s.s.font_size),x),"origin x");
    check(near(used(s.s.transform_origin.y,h,s.s.font_size),y),"origin y");
}
void expect(Fixture& f,const char* id,float x,float y,float w=200,float h=100) { Style s; f.copy(id,s); origin(s,x,y,w,h); }
void diagnostics(const std::vector<Diagnostic>& d,const char* code,unsigned count) {
    check(d.size()==count,"exact diagnostic count");
    for(const auto& e:d) { check(e.code==code,"diagnostic code copied"); check(e.property=="transform-origin","diagnostic property copied"); check(!e.message.empty(),"diagnostic message copied"); }
}
struct Case { const char* value; float x,y; };
void value_case(bool inlined,const std::string& value,float x,float y,const char* code="",unsigned count=0,const std::string& definitions="") {
    std::vector<Diagnostic> copied;
    Style saved;
    {
        Fixture f;
        f.add("#p{transform-origin:right bottom}#c{transform-origin:left top}");
        std::string declaration=definitions+"transform-origin:"+value+";";
        if(inlined) f.set("c",declaration); else f.add("#c{"+declaration+"}");
        f.resolve(); f.copy("c",saved); origin(saved,x,y);
        copied=f.diagnostics;
        diagnostics(copied,code,count);
        f.diagnostics.clear(); f.resolve(); expect(f,"c",x,y); origin(saved,x,y);
        // Inline compilation may repeat diagnostics; ownership is the contract,
        // not a once-per-lifetime suppression promise.
    }
    diagnostics(copied,code,count); origin(saved,x,y);
}
void matrices() {
    const Case valid[]={
        {"center left",0,50},{"center right",200,50},{"left center",0,50},
        {"top right",200,0},{"bottom left",0,100},{"center",100,50},
        {"20%top",40,0},{"calc(20px)top",20,0},{"left calc(20px)",0,20},
        {"20% 30px",40,30},{"-20% +30px",-40,30},{"0",0,50},{"+0e2",0,50},
        {"CENTER LEFT",0,50},{"\\63 enter \\6c eft",0,50},{"center/**/left",0,50},
        {"calc(10% + 3px) calc(25% + 2px)",23,27}
    };
    const char* invalid[]={"top 20px","bottom 20%","20px left","20% right","left right","top bottom","center left 0px","left,top","banana","cen/**/ter","1","center 2","calc(0)","calc(1px + 2deg)","inherit left","revert-layer","left top bottom"};
    for(bool inlined:{false,true}) {
        for(const auto& c:valid) {
            context=std::string(inlined?"inline/":"sheet/")+c.value;
            value_case(inlined,c.value,c.x,c.y);
            value_case(inlined,"var(--position)",c.x,c.y,"",0,std::string("--position:")+c.value+";");
            value_case(inlined,std::string("var(--missing,")+c.value+")",c.x,c.y);
        }
        for(const auto* v:invalid) {
            context=std::string(inlined?"inline invalid/":"sheet invalid/")+v;
            value_case(inlined,v,0,0,"AETHER-CSS-TRANSFORM-ORIGIN",1);
            value_case(inlined,"var(--position,left top)",100,50,"AETHER-CSS-TRANSFORM-ORIGIN",1,std::string("--position:")+v+";");
            value_case(inlined,std::string("var(--missing,")+v+")",100,50,"AETHER-CSS-TRANSFORM-ORIGIN",1);
            value_case(inlined,std::string("var(--missing,var(--other,")+v+"))",100,50,"AETHER-CSS-TRANSFORM-ORIGIN",1);
        }
        for(const char* v:{"var(--missing)","var(--a)"}) {
            context=std::string("resolution failure/")+v;
            value_case(inlined,v,100,50,"AETHER-CSS-VAR",1,"--a:var(--b);--b:var(--a);");
        }
        context="empty successful substitution";
        value_case(inlined,"var(--missing,)",100,50,"AETHER-CSS-TRANSFORM-ORIGIN",1);
        context="case sensitive custom properties";
        value_case(inlined,"var(--Position, right bottom)",200,100,"",0,"--position:left top;");
    }
}
void wide() {
    const Case rows[]={{"initial",100,50},{"unset",100,50},{"inherit",200,100},{"revert",100,50},{"revert-layer",0,0},
                       {"/**/INHERIT/**/",200,100},{"\\69 nherit",200,100},{"\\72 evert",100,50},{"\\72 evert-layer",0,0}};
    for(bool inlined:{false,true}) for(const auto& c:rows) {
        context=std::string(inlined?"inline wide/":"sheet wide/")+c.value;
        bool unsupported=std::string(c.value).find("layer")!=std::string::npos;
        auto code=unsupported?"AETHER-CSS-TRANSFORM-ORIGIN":"";
        value_case(inlined,c.value,c.x,c.y,code,unsupported?1:0);
        for(const auto& value:{std::string("var(--missing,")+c.value+")",std::string("var(--missing,var(--nested,")+c.value+"))",std::string("var(--position,left top)")})
            // CSS-wide tokens must be produced after custom-property compilation:
            // --position:initial itself resets that custom property instead.
            value_case(inlined,value,unsupported?100:c.x,unsupported?50:c.y,code,unsupported?1:0,std::string("--position:var(--absent,")+c.value+");");
    }
    context="root explicit inherit"; Fixture f; f.add("html{transform-origin:inherit}"); f.resolve();
    Style root; need(aether_computed_style_copy(f.latest[aether_doc_get_document_element(f.doc)],&root.s)==AETHER_OK,"copy root"); origin(root,100,50);
}
void priorities() {
    for(const char* value:{"top 20px","revert-layer","var(--missing,top 20px)","var(--missing,revert-layer)"}) for(int route=0;route<6;++route) {
        context=std::string("priority/")+value+"/"+std::to_string(route);
        Fixture f; const bool deferred=std::string(value).find("var(")==0;
        std::string high="transform-origin:"+std::string(value);
        if(route==0) f.add("#c{transform-origin:left top;"+high+"}");
        if(route==1) f.add("#c{transform-origin:left top}#c{"+high+"}");
        if(route==2) f.add(".target{transform-origin:left top}#c{"+high+"}");
        if(route==3) f.add("#c{transform-origin:left top}.target{"+high+"!important}");
        if(route==4) { f.add("#c{transform-origin:left top}"); f.set("c",high); }
        if(route==5) { f.add("#c{transform-origin:left top!important}"); f.set("c",high+"!important"); }
        f.resolve(); expect(f,"c",deferred?100:0,deferred?50:0); diagnostics(f.diagnostics,"AETHER-CSS-TRANSFORM-ORIGIN",1);
    }
    context="valid important beats later normal"; Fixture f;
    f.add("#c{transform-origin:right bottom!important;transform-origin:left top}"); f.set("c","transform-origin:left top"); f.resolve(); expect(f,"c",200,100);
}
void inheritance() {
    for(const char* value:{"1em 25%","calc(1em + 10%) calc(2em + 5px)"}) for(bool deferred:{false,true}) {
        context=std::string("inherit chain/")+value+(deferred?"/deferred":"/literal"); Fixture f;
        f.add("#p{font-size:20px;transform-origin:"+std::string(value)+"}#c{font-size:10px;transform-origin:"+(deferred?"var(--missing,inherit)":"inherit")+"}#g{font-size:5px;transform-origin:inherit}");
        f.resolve(); bool calc=std::string(value).find("calc")==0;
        expect(f,"c",calc?50:20,calc?45:20,300,80); expect(f,"g",calc?30:20,calc?45:10,100,40);
        Style saved; f.copy("g",saved);
        for(int i=0;i<4;++i) { f.resolve(); expect(f,"g",calc?30:20,calc?45:10,100,40); }
        f.set("p","font-size:30px"); f.resolve(); expect(f,"c",calc?60:30,calc?65:20,300,80); expect(f,"g",calc?40:30,calc?65:10,100,40); origin(saved,calc?30:20,calc?45:10,100,40);
        diagnostics(f.diagnostics,"",0);
    }
    context="origin is not implicitly inherited"; Fixture f; f.add("#p{transform-origin:right bottom}"); f.resolve(); expect(f,"c",100,50);
}
void finite_inheritance() {
    // The contextual contract rejects arithmetic overflow before publishing a
    // revision. It deliberately replaces the old inherited-only saturation,
    // which could leave parent and child describing different coordinates.
    for(const char* value:{"3e37em -3e37em","calc(3e37em + 1px) calc(-3e37em - 1px)"}) {
        context=std::string("transactional origin overflow/")+value; Fixture f;
        f.add("#p{font-size:20px;transform-origin:10px 10px}#c{font-size:10px;transform-origin:inherit}#g{font-size:5px;transform-origin:var(--missing,inherit)}");
        f.resolve();
        aether_style_revision_t before{};
        need(aether_style_runtime_retain_revision(f.runtime,&before)==AETHER_OK,"retain valid revision");
        const auto generation=aether_style_revision_generation(before);
        Style saved; f.copy("c",saved);
        f.set("p", "font-size:20px;transform-origin:"+std::string(value));
        for(int round=0;round<3;++round) {
            aether_style_transaction_t transaction{};
            const auto status=aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&transaction);
            check(status==AETHER_ERROR_INVALID_STATE,"computed overflow has explicit contextual status");
            check(transaction==nullptr,"rejected prepare publishes no transaction");
            aether_style_revision_t current{};
            need(aether_style_runtime_retain_revision(f.runtime,&current)==AETHER_OK,"retain after rejection");
            check(aether_style_revision_generation(current)==generation,"rejected overflow preserves revision");
            aether_style_revision_destroy(current);
            for(const char* id:{"c","g"}) expect(f,id,10,10);
            origin(saved,10,10);
            // Legacy zero cannot distinguish rejection from an unchanged tree;
            // callers that need failure status must use prepare_revision.
            aether_style_update_t* updates{};
            const int count=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&updates);
            check(count==0 && updates==nullptr,"legacy overflow reports zero without partial updates");
        }
        aether_style_revision_destroy(before);
        diagnostics(f.diagnostics,"",0);
    }
}

aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) { return {0,1,.8f}; }
void geometry() {
    for(const auto& c:std::vector<Case>{{"center left",0,50},{"left center",0,50},{"center right",200,50}}) {
        context=std::string("public display geometry/")+c.value; Fixture f;
        f.add("*{display:block;margin:0;padding:0}#c{width:200px;height:100px;transform:rotate(90deg);transform-origin:"+std::string(c.value)+"}"); f.resolve();
        aether_style_map_t styles{}; aether_layout_engine_t engine{}; aether_layout_result_t layout{};
        aether_display_list_builder_t builder{}; aether_display_list_t list{};
        need(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&styles)==AETHER_OK,"geometry style map");
        need(aether_layout_create(measure,nullptr,&engine)==AETHER_OK,"geometry engine");
        need(aether_layout_layout(engine,f.doc,styles,800,600,nullptr,nullptr,&layout)==AETHER_OK,"geometry layout");
        need(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"geometry builder");
        need(aether_display_list_build(builder,layout,nullptr,&list)==AETHER_OK,"geometry display list");
        unsigned pushes=0;
        for(int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t cmd{}; need(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"geometry command");
            if(cmd.tag==AETHER_PAINT_PUSH_TRANSFORM) {
                ++pushes; const auto& m=cmd.data.push_transform.matrix;
                check(near(c.x*m.m11+c.y*m.m21+m.dx,c.x)&&near(c.x*m.m12+c.y*m.m22+m.dy,c.y),"rotation fixes origin pivot");
                float x=31*m.m11+17*m.m21+m.dx,y=31*m.m12+17*m.m22+m.dy;
                check(near(x,c.x-(17-c.y))&&near(y,c.y+(31-c.x)),"public matrix rotates test point about origin");
                float determinant=m.m11*m.m22-m.m12*m.m21;
                check(near(determinant,1),"rotation invertible");
                if(std::fabs(determinant)>.001f) {
                    check(near(((x-m.dx)*m.m22-(y-m.dy)*m.m21)/determinant,31),"inverse x round trip");
                    check(near(((y-m.dy)*m.m11-(x-m.dx)*m.m12)/determinant,17),"inverse y round trip");
                }
            }
            aether_paint_command_free(&cmd);
        }
        check(pushes==1,"one public transform matrix");
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); aether_layout_destroy(engine); aether_style_map_destroy(styles);
    }
}
void impact(Fixture& f,const char* id) {
    auto n=f.node(id); check(f.impacts.count(n)!=0,"origin change produced update");
    if(f.impacts.count(n)) {
        check(f.impacts[n]==(AETHER_STYLE_IMPACT_COMPOSITE|AETHER_STYLE_IMPACT_HITTEST),"exact COMPOSITE|HITTEST impact");
        if(f.impacts[n]!=(AETHER_STYLE_IMPACT_COMPOSITE|AETHER_STYLE_IMPACT_HITTEST)) std::fprintf(stderr,"  actual impact=%u expected=12\n",f.impacts[n]);
    }
}
void sheet_deltas() {
    context="sheet selector delta"; Fixture f;
    f.add("#p{transform-origin:right bottom}#c.a{transform-origin:left top}#c.b{transform-origin:right bottom}#c.bad{transform-origin:var(--missing,top 20px)}#c.inherit{transform-origin:inherit}");
    auto cls=[&](const char* value) { need(aether_element_set_attribute(f.node("c"),"class",value)==AETHER_OK,"change selector membership"); };
    cls("a"); f.resolve();
    for(const char* value:{"b","bad","inherit","a",""}) {
        context=std::string("sheet selector delta/")+value;
        for(int round=0;round<3;++round) { f.resolve(); check(f.impacts.count(f.node("c"))==0||f.impacts[f.node("c")]==0,"sheet repeat no change impact"); }
        cls(value); f.resolve(); impact(f,"c");
        if(std::string(value)=="a") expect(f,"c",0,0);
        else if(std::string(value)=="b"||std::string(value)=="inherit") expect(f,"c",200,100);
        else expect(f,"c",100,50);
    }
}
void mutations() {
    for(bool inlined:{false,true}) {
        context=inlined?"inline mutations":"sheet mutations"; Fixture f;
        f.add("#p{color:red;text-shadow:1px 2px currentColor;transform-origin:right bottom}#c{color:blue;text-shadow:inherit;transform:rotate(90deg);width:200px;height:100px}");
        int64_t sheet=0;
        auto change=[&](const std::string& decl) { if(inlined) f.set("c",decl); else { if(sheet) need(aether_style_runtime_remove_stylesheet(f.runtime,sheet),"remove sheet"); sheet=decl.empty()?0:f.add("#c{"+decl+"}"); } };
        change("transform-origin:left top"); f.resolve(); Style saved; f.copy("c",saved);
        for(const auto& decl:{"transform-origin:right bottom","transform-origin:var(--position);--position:top 20px","transform-origin:inherit","transform-origin:left top",""}) {
            for(int i=0;i<3;++i) { f.resolve(); check(f.impacts.count(f.node("c"))==0 || f.impacts[f.node("c")]==0,"repeat resolve no change impact"); }
            context=std::string(inlined?"inline mutation/":"sheet mutation/")+decl;
            change(decl); f.resolve();
            if(inlined) impact(f,"c");
            else {
                // add/remove_stylesheet deliberately clears runtime states:
                // these are fresh styles, not property-delta updates. Exercise
                // exact sheet deltas below by changing selector membership.
                check(f.impacts.count(f.node("c"))!=0,"sheet rebuild emits fresh style");
                const uint32_t all=AETHER_STYLE_IMPACT_LAYOUT|AETHER_STYLE_IMPACT_PAINT|AETHER_STYLE_IMPACT_HITTEST|AETHER_STYLE_IMPACT_COMPOSITE|AETHER_STYLE_IMPACT_TEXT_LAYOUT|AETHER_STYLE_IMPACT_CURSOR|AETHER_STYLE_IMPACT_INHERITED;
                check(f.impacts[f.node("c")]==all,"sheet cache rebuild has existing all-impact contract");
            }
            Style current; f.copy("c",current);
            check(aether_transform_list_equal(&saved.s.transforms,&current.s.transforms),"origin never mutates transform list");
            check(near(current.s.width.value,200)&&near(current.s.height.value,100),"origin leaves box dimensions");
            check(near(current.s.color.b,1)&&near(current.s.color.r,0),"origin leaves child color");
            check(current.s.text_shadows.count==saved.s.text_shadows.count,"origin leaves inherited shadows");
            if(current.s.text_shadows.count && saved.s.text_shadows.count) {
                check(current.s.text_shadows.items!=saved.s.text_shadows.items,"shadow snapshot ownership");
                check(near(current.s.text_shadows.items[0].color.b,saved.s.text_shadows.items[0].color.b),"origin leaves currentColor metadata");
            }
            origin(saved,0,0);
        }
        change("transform-origin:inherit"); f.resolve();
        f.set("p","transform-origin:left center"); f.resolve(); impact(f,"c"); expect(f,"c",0,50);
        f.set("c","color:green;transform-origin:inherit;text-shadow:inherit"); f.resolve();
        Style green; f.copy("c",green); check(near(green.s.color.g,128.f/255.f),"independent color mutation");
        if(green.s.text_shadows.count) check(near(green.s.text_shadows.items[0].color.g,128.f/255.f),"inherited currentColor follows child color");
        origin(saved,0,0); check(near(saved.s.color.b,1),"saved color remains independent");
    }
}
} // namespace
int main() {
    matrices(); wide(); priorities(); inheritance(); finite_inheritance(); geometry(); sheet_deltas(); mutations();
    std::printf("transform-origin public runtime: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
