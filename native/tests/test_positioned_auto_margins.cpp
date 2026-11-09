#include <aether/aether.h>
#include "animations/transition_runtime.h"
#include "layout/layout_types.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int snapshots = 0;
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
void equal(float actual, float expected, const char* field) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) >= .01f) {
        std::fprintf(stderr, "FAIL %s actual=%g expected=%g\n", field, actual, expected);
        std::exit(1);
    }
}
void rect(aether_rectf actual, aether_rectf expected, const char* field) {
    equal(actual.x, expected.x, field); equal(actual.y, expected.y, field);
    equal(actual.width, expected.width, field); equal(actual.height, expected.height, field);
}
aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) { return {}; }
struct owned_style {
    aether_computed_style_t value{};
    explicit owned_style(aether_computed_style_handle_t handle) {
        check(handle != nullptr, "style handle");
        auto status = aether_computed_style_copy(handle, &value);
        check(status == AETHER_OK, "style copy");
    }
    ~owned_style() { aether_computed_style_free(&value); }
};
struct fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_transition_runtime_t transitions{};
    aether_layout_result_t result{};
    std::unordered_map<aether_node_t,aether_computed_style_handle_t> current_handles;
    int64_t stylesheet{};
    std::string initial_rules;
    float viewport_width, viewport_height;
    fixture(const std::string& extra = "", const std::string& children = "", float width=300, float height=200)
        : viewport_width(width), viewport_height(height) {
        const auto markup = "<!doctype html><html><body><div id='host'><div id='item'>" + children + "</div></div></body></html>";
        check(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, markup.c_str()) == AETHER_OK, "document");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{width,height,1,16,0,0,0};
        check(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "runtime");
        initial_rules="html,body,div{display:block;margin:0;padding:0;border:0}#host{position:relative;width:300px;height:200px}"
            "#item{position:absolute;left:20px;right:40px;top:10px;bottom:30px;width:100px;height:60px;margin:auto}" + extra;
        add(initial_rules);
        resolve();
        check(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "layout engine");
        check(aether_transition_runtime_create(&transitions) == AETHER_OK, "transitions");
    }
    ~fixture() {
        aether_layout_result_destroy(result); aether_layout_destroy(engine);
        aether_transition_runtime_destroy(transitions); aether_style_map_destroy(map);
        for(auto& entry:current_handles) aether_computed_style_handle_destroy(entry.second);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void add(const std::string& css) {
        int32_t count{};
        check(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "positioned.css", &stylesheet, &count) == AETHER_OK, "stylesheet");
    }
    void replace_rules(const char* override_css) {
        check(aether_style_runtime_remove_stylesheet(runtime,stylesheet)!=0,"remove previous stylesheet");
        add(initial_rules+override_css);
    }
    void resolve() {
        aether_style_update_t* updates{};
        auto count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        check(count >= 0, "resolve");
        for(int i=0;i<count;i++) {
            aether_node_t element{};aether_computed_style_handle_t handle{};
            check(aether_style_runtime_get_update(updates[i],&element,&handle,nullptr)==AETHER_OK,"public style update");
            auto found=current_handles.find(element);
            if(found!=current_handles.end())aether_computed_style_handle_destroy(found->second);
            current_handles[element]=handle;
        }
        aether_style_updates_free(updates, count);
        aether_style_map_destroy(map); map = nullptr;
        check(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK, "fresh map");
    }
    aether_node_t node(const char* id) { auto n = aether_doc_get_element_by_id(doc,id); check(n != nullptr,"node"); return n; }
    aether_computed_style_handle_t style_handle(const char* id) {
        auto found=current_handles.find(node(id));check(found!=current_handles.end(),"fresh public style handle");return found->second;
    }
    aether_layout_box_t box(const char* id) {
        auto b = aether_layout_result_find_primary_box(result,node(id)); check(b != nullptr,"box"); return b;
    }
    bool prepare() {
        aether_bool_t changed{};
        check(aether_layout_prepare_transition_context(engine, transitions, map, result, &changed) == AETHER_OK, "prepare");
        return changed != 0;
    }
    void layout(bool prepared = false) {
        if (prepared) prepare();
        aether_layout_result_t next{};
        check(aether_layout_layout(engine, doc, map, viewport_width,viewport_height,nullptr,nullptr,&next) == AETHER_OK,"layout");
        aether_layout_result_destroy(result); result = next;
    }
    std::array<aether_rectf,4> rects(const char* id) {
        auto b=box(id); return {aether_layout_box_get_margin_box(b),aether_layout_box_get_border_box(b),
            aether_layout_box_get_padding_box(b),aether_layout_box_get_content_box(b)};
    }
    void all(const char* id, aether_rectf expected) { for(auto r:rects(id)) rect(r,expected,id); }
    void item(aether_rectf border, aether_rectf margin={20,10,240,160}) {
        auto r=rects("item"); rect(r[0],margin,"M"); for(int i=1;i<4;i++) rect(r[i],border,"B/P/C"); metadata();
    }
    void metadata() {
        auto b=box("item"); auto r=rects("item");
        equal(b->own_relative_dx,0,"own x"); equal(b->own_relative_dy,0,"own y");
        equal(b->collapsed_margin_before,r[1].y-r[0].y,"used before metadata");
        equal(b->collapsed_margin_after,r[0].y+r[0].height-r[1].y-r[1].height,"used after metadata");
        owned_style copied(style_handle("item"));
        check(b->style.margin.left.unit==copied.value.margin.left.unit && b->style.margin.top.unit==copied.value.margin.top.unit &&
              b->style.margin.right.unit==copied.value.margin.right.unit && b->style.margin.bottom.unit==copied.value.margin.bottom.unit,"CSS units intact");
    }
    void autos(bool l=true,bool t=true,bool r=true,bool b=true) {
        owned_style s(style_handle("item"));
        check((s.value.margin.left.unit==AETHER_CSS_UNIT_AUTO)==l && (s.value.margin.top.unit==AETHER_CSS_UNIT_AUTO)==t &&
              (s.value.margin.right.unit==AETHER_CSS_UNIT_AUTO)==r && (s.value.margin.bottom.unit==AETHER_CSS_UNIT_AUTO)==b,"CSS auto intact");
    }
};
void done(const char* name) { ++snapshots; std::printf("CASE %s\n",name); }
void simple(const char* name,const char* css,aether_rectf b,aether_rectf m={20,10,240,160}) {
    fixture f(css); f.layout(); f.item(b,m); done(name);
}
void groups_01_11() {
    {fixture f;f.autos();f.layout();f.item({90,60,100,60});f.autos();done("G01 both auto");}
    simple("G02 end auto","#item{margin:0;margin-right:auto;margin-bottom:auto}",{20,10,100,60});
    simple("G03 fixed nonzero","#item{margin-right:20px;margin-bottom:12px}",{140,98,100,60});
    {fixture f("#item{margin-right:10%;margin-bottom:10%}");f.layout();f.item({130,80,100,60});
     auto r=f.rects("item");equal(r[0].x+r[0].width-r[1].x-r[1].width,30,"right percent");
     equal(r[0].y+r[0].height-r[1].y-r[1].height,30,"bottom percent uses CB width");done("G04 percentages");}
    simple("G05 negative fixed","#item{margin-right:-20px;margin-bottom:-10px}",{180,120,100,60});
    simple("G06 negative residual both","#item{width:280px;height:200px}",{20,-10,280,200});
    simple("G07 start auto","#item{width:280px;height:200px;margin-right:8px;margin-bottom:6px}",{-28,-36,280,200});
    simple("G07 end auto","#item{width:280px;height:200px;margin-left:8px;margin-top:6px}",{28,16,280,200});
    simple("G08 zero residual","#item{width:240px;height:160px}",{20,10,240,160});
    simple("G09 auto size","#item{width:auto;height:auto}",{20,10,240,160});
    simple("G09 max","#item{width:auto;height:auto;box-sizing:border-box;max-width:100px;max-height:60px}",{90,60,100,60});
    simple("G09 min","#item{width:auto;height:auto;box-sizing:border-box;min-width:280px;min-height:200px}",{20,-10,280,200});
    {fixture f("#item{width:auto;height:auto;padding:5px;border:2px solid;right:270px;bottom:182px}");f.layout();
     auto rs=f.rects("item");rect(rs[0],{20,10,10,8},"floor M");rect(rs[1],{20,7,14,14},"floor B");
     rect(rs[2],{22,9,10,10},"floor P");rect(rs[3],{27,14,0,0},"floor C");f.metadata();done("G09 decoration floor");}
    simple("G10 crossed","#item{left:200px;right:150px;top:140px;bottom:100px}",{200,110,100,60},{200,140,0,0});
    simple("G10 crossed auto negative fixed","#item{left:200px;right:150px;top:140px;bottom:100px;width:auto;height:auto;margin-right:-20px;margin-bottom:-10px}",{200,140,20,10},{200,140,0,0});
    for(bool border_box:{false,true}) {
        fixture f(std::string("#item{padding:5px;border:2px solid;box-sizing:")+(border_box?"border-box}":"content-box}"));f.layout();
        auto rs=f.rects("item");rect(rs[0],{20,10,240,160},"decorated M");
        rect(rs[1],border_box?aether_rectf{90,60,100,60}:aether_rectf{83,53,114,74},"decorated B");
        rect(rs[2],border_box?aether_rectf{92,62,96,56}:aether_rectf{85,55,110,70},"decorated P");
        rect(rs[3],border_box?aether_rectf{97,67,86,46}:aether_rectf{90,60,100,60},"decorated C");
        f.metadata();done(border_box?"G11 border box":"G11 content box");
    }
    simple("G11 hidden border","#item{border:9px hidden}",{90,60,100,60});
    simple("G11 none border","#item{border:9px none}",{90,60,100,60});
}
void group_12() {
    const char* rows[]={"#item{right:auto}","#item{bottom:auto}","#item{left:auto;right:auto;top:auto;bottom:auto}"};
    for(auto css:rows) {
        // Only the eligible axis may distribute. Compare the ineligible axis with its zero-auto control.
        fixture automatic(std::string(css)+"#item{margin-right:8px;margin-bottom:6px}");
        fixture zero(std::string(css)+"#item{margin:0;margin-right:8px;margin-bottom:6px}");
        automatic.layout();zero.layout();
        auto a=automatic.rects("item"),z=zero.rects("item");
        const bool auto_x=std::string(css).find("right:auto")!=std::string::npos;
        const bool auto_y=std::string(css).find("bottom:auto")!=std::string::npos;
        for(int i=0;i<4;i++) {
            if(auto_x){equal(a[i].x,z[i].x,"ineligible X");equal(a[i].width,z[i].width,"ineligible width");}
            if(auto_y){equal(a[i].y,z[i].y,"ineligible Y");equal(a[i].height,z[i].height,"ineligible height");}
        }
        automatic.metadata();done(css);
    }
    {fixture f("#item{height:auto;bottom:auto}#child{height:36px;width:100px}","<div id='child'></div>");
     f.layout();f.item({90,10,100,36},{20,10,240,36});f.all("child",{90,10,100,36});done("G12 auto height");}
    {fixture f("#host{display:flex;justify-content:center;align-items:center}#item{left:auto;right:auto;top:auto;bottom:auto}");
     f.layout();f.item({100,70,100,60},{100,70,100,60});done("G12 static flex");}
}
void group_13() {
    for(bool prepared:{false,true}) {
        fixture f("#host{position:relative;left:37px;top:23px;width:150px;height:90px}#item{position:fixed}");
        f.layout(prepared);f.item({90,60,100,60});rect(f.rects("host")[1],{37,23,150,90},"relative host");
        done(prepared?"G13 prepared":"G13 legacy");
    }
}
const char* subtree="<div id='ctx'><div id='fixed'><div id='leaf'></div></div></div><div id='block'></div>"
                    "<div id='relative'><div id='relative-leaf'></div></div><div id='absolute'></div>";
const char* subtree_css="#ctx{position:absolute;left:0;top:0;width:10px;height:10px}"
    "#fixed{position:fixed;left:2px;top:3px;width:4px;height:4px}#leaf,#relative-leaf{width:1px;height:1px}"
    "#block,#relative{width:10px;height:10px}#relative{position:relative;left:7px;top:-3px}"
    "#absolute{position:absolute;left:5px;top:6px;width:4px;height:4px}";
void snapshot_14(fixture& f,bool automatic,float fx,float fy,const char* name,bool prepared,bool local) {
    float x=automatic?90.f:20.f,y=automatic?60.f:10.f;
    f.item({x,y,100,60},automatic?aether_rectf{20,10,240,160}:aether_rectf{20,10,100,60});
    f.all("ctx",{x,y,10,10});f.all("block",{x,y,10,10});
    f.all("relative",{x+7,y+7,10,10});f.all("relative-leaf",{x+7,y+7,1,1});f.all("absolute",{x+5,y+6,4,4});
    f.all("fixed",{fx,fy,4,4});f.all("leaf",{fx,fy,1,1});
    equal(f.box("relative")->own_relative_dx,7,"relative own X");equal(f.box("relative")->own_relative_dy,-3,"relative own Y");
    check(f.result->effective_context_enabled==prepared,"prepared result metadata");
    if(prepared)check((f.result->effective_fixed_container(f.node("ctx"))!=0)==local,"local context snapshot");
    done(name);
}
void group_14() {
    struct row { const char* name;const char* css;bool prepared,local;float zx,zy,ax,ay; };
    const row rows[]={
        {"G14.1 legacy","",false,false,22,13,92,63},
        {"G14.2 prepared empty","",true,false,2,3,2,3},
        {"G14.3 auto X","#fixed{left:auto;right:auto}",true,false,20,3,90,3},
        {"G14.4 auto Y","#fixed{top:auto;bottom:auto}",true,false,2,10,2,60},
        {"G14.5 auto XY","#fixed{left:auto;right:auto;top:auto;bottom:auto}",true,false,20,10,90,60},
        {"G14.6 local static","#ctx{transform:translate(0)}",true,true,22,13,92,63},
        {"G14.7 external static","#host{transform:translate(0)}",true,false,2,3,2,3}
    };
    for(auto& row:rows)for(bool automatic:{false,true}) {
        fixture f(std::string(subtree_css)+row.css+(automatic?"":"#item{margin:0}"),subtree);
        f.layout(row.prepared);snapshot_14(f,automatic,automatic?row.ax:row.zx,automatic?row.ay:row.zy,row.name,row.prepared,row.local);
    }
    for(bool automatic:{false,true}) {
        fixture f(std::string(subtree_css)+(automatic?"":"#item{margin:0}"),subtree);
        f.layout(true); // Initial prepared result is only the previous-layout input to the two specified snapshots.
        owned_style before(f.style_handle("ctx")),after(f.style_handle("ctx"));
        aether_transform_operation operation{};operation.kind=AETHER_TRANSFORM_TRANSLATE;
        operation.x={10,AETHER_CSS_UNIT_PX};operation.y={0,AETHER_CSS_UNIT_PX};
        // The owned copy initially has no transforms; this stack view lives only during observe.
        auto entry=before.value;entry.transforms={&operation,1};
        after.value.transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM;
        after.value.transition_duration=.1f;after.value.transition_delay=0;after.value.transition_timing=AETHER_TRANSITION_TIMING_LINEAR;
        check(aether_transition_runtime_observe(f.transitions,f.node("ctx"),&entry,&after.value,1000)==AETHER_OK,"G14 observe");
        aether_transition_runtime_tick(f.transitions,1050);check(f.prepare(),"G14 enter context changed");f.layout();
        snapshot_14(f,automatic,automatic?92.f:22.f,automatic?63.f:13.f,"G14.8 active1050",true,true);
        aether_transition_runtime_tick(f.transitions,1100);check(f.prepare(),"G14 retire context changed");f.layout();
        snapshot_14(f,automatic,2,3,"G14.9 retired1100",true,false);
    }
}
void group_15() {
    fixture f;
    const char* changes[]={"","#item{margin:0}","#item{margin:0;margin-right:auto;margin-bottom:auto}","#item{margin:auto;width:120px}"};
    const aether_rectf borders[]={{90,60,100,60},{20,10,100,60},{20,10,100,60},{80,60,120,60}};
    const aether_rectf margins[]={{20,10,240,160},{20,10,100,60},{20,10,240,160},{20,10,240,160}};
    for(int i=0;i<4;i++) {
        if(i){aether_layout_result_destroy(f.result);f.result=nullptr;f.replace_rules(changes[i]);f.resolve();}
        f.layout(true);f.item(borders[i],margins[i]);f.autos(i==0||i==3,i==0||i==3,i!=1,i!=1);done("G15 fresh resolve/map/handles");
    }
}
void scroll_groups() {
    struct row { const char* name;const char* css;aether_rectf border,margin;float x,y; };
    const row rows[]={
        {"S1 negative auto overflow","#item{width:400px;height:300px}",{20,-60,400,300},{20,10,240,160},120,40},
        {"S2 positive end auto no false overflow","#item{right:-200px;bottom:-150px;margin:0 auto auto 0}",{20,10,100,60},{20,10,480,340},0,0},
        {"S3 absolute fixed margins","#item{right:auto;bottom:auto;margin:0 400px 300px 0}",{20,10,100,60},{20,10,500,360},0,0},
        {"S3 fixed fixed margins","#item{position:fixed;right:auto;bottom:auto;margin:0 400px 300px 0}",{20,10,100,60},{20,10,500,360},0,0}
    };
    for(auto& row:rows) {
        fixture f(std::string("#host{overflow:hidden}")+row.css,"",800,600);f.layout();
        f.item(row.border,row.margin);
        const auto metrics=aether_layout_box_get_scroll_metrics(f.box("host"));
        equal(metrics.scrollable_overflow_x,row.x,"host overflow X");equal(metrics.scrollable_overflow_y,row.y,"host overflow Y");
        done(row.name);
    }
}
}
int main() {
    groups_01_11();group_12();group_13();group_14();group_15();scroll_groups();
    check(snapshots==52,"explicit snapshot count");
    std::puts("PASS G01-G15 plus S1-S3: 52 enumerated candidate snapshots; G14 exactly nine states x two margin fixtures");
    return 0;
}
