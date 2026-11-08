// Compile with the authentic core sources: intrinsic sizing is intentionally private.
#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::string scenario;
static unsigned checks = 0;
#define REQUIRE(condition) do { ++checks; if (!(condition)) { std::fprintf(stderr, "FAIL [%s] %s:%d: %s\n", scenario.c_str(), __FILE__, __LINE__, #condition); std::abort(); } } while (false)
using aether::style::MapMode;
using aether::layout::StyleLookup;
static bool near(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b) < .01f; }
struct Meter {
    bool nonlinear = false;
    bool inherited = false;
    std::vector<std::string> calls;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style, float, void* opaque) {
        auto& m = *static_cast<Meter*>(opaque);
        if (m.inherited) {
            REQUIRE(std::strcmp(style->font_family,"SequenceFont")==0);
            REQUIRE(near(style->font_size,18)); REQUIRE(style->font_weight==700);
            REQUIRE(near(style->line_height,20)); REQUIRE(style->text_transform==AETHER_TEXT_TRANSFORM_CAPITALIZE);
            REQUIRE(style->text_shadows.count==1); REQUIRE(near(style->text_shadows.items[0].offset_x,2));
        }
        m.calls.emplace_back(text);
        return {m.nonlinear && std::strcmp(text, "AB") == 0 ? 15.f : float(std::strlen(text))*10.f, 20.f, 15.f};
    }
    static aether_text_metrics_t contextual(const char* text, const aether_computed_style_t* style,
                                           float line_height, float maximum, void* opaque) {
        REQUIRE(near(line_height, 20));
        return measure(text, style, maximum, opaque);
    }
};
struct Draw { std::string text; aether_rectf rect; };
struct Geometry { int kind; std::string text; aether_rectf rect; int children; };
static bool same_rect(aether_rectf a, aether_rectf b) {
    return near(a.x,b.x) && near(a.y,b.y) && near(a.width,b.width) && near(a.height,b.height);
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t result = nullptr;
    MapMode mode;
    bool contextual;
    Meter meter;
    std::vector<std::string> layout_calls;
    Fixture(const std::string& content, const std::string& css, MapMode m, bool c,
            bool nonlinear = false, bool adjacent = false, bool height = false)
        : mode(m), contextual(c) {
        meter.nonlinear = nonlinear;
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        const std::string html = "<html><body><div id='outer'><div id='flex'>" + content + "</div></div></body></html>";
        REQUIRE(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK);
        if (adjacent) {
            // Public API mutation before resolving creates genuine adjacent text nodes.
            auto f = node();
            for (int i = 0; i < aether_node_get_child_count(f);) {
                auto child = aether_node_get_child(f, i);
                if (aether_node_get_type(child) == AETHER_NODE_COMMENT)
                    REQUIRE(aether_node_append_child(aether_doc_get_body(doc), child) == AETHER_OK);
                else ++i;
            }
        }
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640,480,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime) == AETHER_OK);
        const std::string rules = "html,body,#outer{display:block;margin:0;padding:0}#flex{display:flex;width:200px;gap:30px;font-size:16px;line-height:20px}"
            "#mark{display:block;width:10px;height:10px;flex-shrink:0}" +
            std::string(height ? "#outer{display:flex;flex-direction:column;height:20px;width:200px}#flex{flex-direction:column;height:100px;gap:10px}" : "") + css;
        int64_t id = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,rules.c_str(),0,"flex-sequences",&id,nullptr) == AETHER_OK);
        resolve();
        REQUIRE(aether_layout_create(Meter::measure,&meter,&engine) == AETHER_OK);
        if (contextual) aether_layout_set_contextual_text_measurer(engine,Meter::contextual,&meter);
        layout();
    }
    ~Fixture() {
        aether_layout_result_destroy(result); aether_layout_destroy(engine);
        aether_style_map_destroy(map); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id = "flex") const { return aether_doc_get_element_by_id(doc,id); }
    aether_layout_box_t box(const char* id = "flex") const {
        auto b = aether_layout_result_find_primary_box(result,node(id)); REQUIRE(b); return b;
    }
    static void elements(aether_node_t n, std::vector<aether_node_t>& out) {
        if (aether_node_get_type(n) == AETHER_NODE_ELEMENT) out.push_back(n);
        for (int i=0;i<aether_node_get_child_count(n);++i) elements(aether_node_get_child(n,i),out);
    }
    void resolve() {
        aether_style_map_destroy(map); map = nullptr;
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        REQUIRE(count >= 0); aether_style_updates_free(updates,count);
        if (mode == MapMode::contextual) {
            aether_style_revision_t revision = nullptr;
            REQUIRE(aether_style_runtime_retain_revision(runtime,&revision) == AETHER_OK);
            REQUIRE(aether_style_revision_create_map(revision,&map) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else if (mode == MapMode::legacy_complete) {
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&map) == AETHER_OK);
        } else {
            REQUIRE(aether_css_resolve(doc,nullptr,640,480,&map) == AETHER_OK);
            std::vector<aether_node_t> nodes; elements(aether_doc_get_document_element(doc),nodes);
            REQUIRE(aether_style_runtime_update_resolved_style_map(runtime,map,nodes.data(),int(nodes.size())) == AETHER_OK);
        }
        REQUIRE(map->current->mode() == mode);
    }
    void layout() {
        aether_layout_result_destroy(result); result = nullptr; meter.calls.clear();
        REQUIRE(aether_layout_layout(engine,doc,map,640,480,nullptr,nullptr,&result) == AETHER_OK);
        REQUIRE(result && !result->build_overlay);
        layout_calls = meter.calls;
    }
    const std::vector<aether_node_s*>& items() const {
        auto it = result->anonymous_flex_items.find(node()); REQUIRE(it != result->anonymous_flex_items.end());
        return it->second;
    }
    aether_layout_box_t first() const { REQUIRE(!items().empty());
        auto b = aether_layout_result_find_primary_box(result,items().front()); REQUIRE(b); return b; }
    std::vector<Draw> paint() {
        unsigned shadows = 0;
        aether_display_list_builder_t builder = nullptr;
        REQUIRE(aether_display_list_builder_create(Meter::measure,&meter,&builder) == AETHER_OK);
        if (contextual) aether_display_list_builder_set_contextual_text_measurer(builder,Meter::contextual,&meter);
        aether_display_list_t list = nullptr;
        REQUIRE(aether_display_list_build(builder,result,nullptr,&list) == AETHER_OK);
        std::vector<Draw> draws;
        for (int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t cmd{}; REQUIRE(aether_display_list_get_command(list,i,&cmd) == AETHER_OK);
            if (cmd.tag == AETHER_PAINT_DRAW_TEXT) {
                draws.push_back({cmd.data.draw_text.text,cmd.data.draw_text.rect});
                if(meter.inherited) {
                    REQUIRE(std::strcmp(cmd.data.draw_text.font_family,"SequenceFont")==0);
                    REQUIRE(near(cmd.data.draw_text.font_size,18)); REQUIRE(cmd.data.draw_text.font_weight==700);
                }
            }
            if(cmd.tag == AETHER_PAINT_DRAW_TEXT_SHADOW && meter.inherited) {
                ++shadows; REQUIRE(std::strcmp(cmd.data.draw_text_shadow.text,"Hello")==0);
                REQUIRE(std::strcmp(cmd.data.draw_text_shadow.font_family,"SequenceFont")==0);
                REQUIRE(near(cmd.data.draw_text_shadow.shadow.offset_x,2));
                REQUIRE(near(cmd.data.draw_text_shadow.shadow.offset_y,3));
            }
            aether_paint_command_free(&cmd);
        }
        if(meter.inherited) REQUIRE(shadows==1);
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder); return draws;
    }
    static void geometry(aether_layout_box_t b, std::vector<Geometry>& out) {
        char* text = aether_layout_box_get_text(b);
        out.push_back({aether_layout_box_get_kind(b),text ? text : "",aether_layout_box_get_border_box(b),aether_layout_box_get_child_count(b)});
        aether_string_free(text);
        for(int i=0;i<aether_layout_box_get_child_count(b);++i) geometry(aether_layout_box_get_child(b,i),out);
    }
    void intrinsic_pair(float minimum, float maximum, bool reverse) {
        aether_layout_result_s provisional;
        provisional.build_overlay = std::make_unique<aether::layout::MutableLayoutOverlay>();
        const StyleLookup lookup(map->current,provisional.build_overlay.get(),result->assets->default_style);
        const BoxStyleSnapshot style(lookup.lookup_node_owner(node()));
        engine->measure_styles = &lookup; engine->measure_document = doc->identity;
        for (int i=0;i<4;++i) {
            const bool max = (i%2==0) == reverse;
            const auto sizing = max ? aether_intrinsic_size_mode_e::max_content : aether_intrinsic_size_mode_e::min_content;
            const float value = measure_intrinsic_width(engine,&lookup,node(),style,{}, {},200,&provisional,sizing);
            REQUIRE(near(value,max?maximum:minimum));
            REQUIRE(provisional.primary_boxes.empty()); REQUIRE(provisional.paint_order.empty());
        }
        engine->measure_styles = nullptr; engine->measure_document.reset();
    }
    float intrinsic(bool height, aether_intrinsic_size_mode_e sizing = aether_intrinsic_size_mode_e::min_content) {
        // A real mutable result and lookup share the collector and overlay during measurement.
        aether_layout_result_s provisional;
        provisional.build_overlay = std::make_unique<aether::layout::MutableLayoutOverlay>();
        const StyleLookup lookup(map->current,provisional.build_overlay.get(),result->assets->default_style);
        const BoxStyleSnapshot style(lookup.lookup_node_owner(node()));
        engine->measure_styles = &lookup; engine->measure_document = doc->identity;
        const float value = height
            ? measure_intrinsic_height(engine,&lookup,&provisional,node(),style,{}, {},200)
            : measure_intrinsic_width(engine,&lookup,node(),style,{}, {},200,&provisional,sizing);
        engine->measure_styles = nullptr; engine->measure_document.reset();
        REQUIRE(provisional.primary_boxes.empty()); REQUIRE(provisional.paint_order.empty());
        return value;
    }
};
static std::string text(const std::vector<Draw>& draws) { std::string s; for(const auto& d:draws) s+=d.text; return s; }
static void equivalent(Fixture& reference, Fixture& split) {
    REQUIRE(reference.items().size() == split.items().size());
    REQUIRE(reference.layout_calls == split.layout_calls);
    std::vector<Geometry> a,b; Fixture::geometry(reference.box(),a); Fixture::geometry(split.box(),b);
    REQUIRE(a.size()==b.size());
    for(size_t i=0;i<a.size();++i) {
        REQUIRE(a[i].kind==b[i].kind); REQUIRE(a[i].text==b[i].text);
        REQUIRE(a[i].children==b[i].children); REQUIRE(same_rect(a[i].rect,b[i].rect));
    }
    const auto p=reference.paint(), q=split.paint(); REQUIRE(p.size()==q.size());
    for(size_t i=0;i<p.size();++i) { REQUIRE(p[i].text==q[i].text); REQUIRE(same_rect(p[i].rect,q[i].rect)); }
    REQUIRE(near(reference.intrinsic(false),split.intrinsic(false)));
    REQUIRE(near(reference.intrinsic(true),split.intrinsic(true)));
}
static void continuity(MapMode mode, bool contextual) {
    for(bool adjacent:{false,true}) for(bool nonlinear:{false,true}) {
        Fixture reference("AB","",mode,contextual,nonlinear);
        for(const char* content:{"A<!--x-->B","<!--head-->A<!----><!--<b> hidden &amp; -->B<!--tail-->",
                                "A<i style='display:none'>HIDDEN<b>nested</b></i>B",
                                "A<i style='display:none;position:absolute'>HIDDEN</i>B"}) {
            scenario = std::string("continuity: ")+content+(adjacent?" adjacent":" comments");
            Fixture split(content,"",mode,contextual,nonlinear,adjacent);
            REQUIRE(split.items().size()==1);
            REQUIRE(near(aether_layout_box_get_width(split.first()),nonlinear?15:20));
            equivalent(reference,split);
        }
    }
    for(const char* transform:{"capitalize","uppercase","lowercase"}) for(bool adjacent:{false,true}) {
        scenario = std::string("transform ")+transform;
        std::string css=std::string("#flex{text-transform:")+transform+"}";
        Fixture a("hello",css,mode,contextual), b("he<!--x-->llo",css,mode,contextual,false,adjacent);
        equivalent(a,b);
        REQUIRE(text(b.paint()) == (std::string(transform)=="capitalize"?"Hello":std::string(transform)=="uppercase"?"HELLO":"hello"));
    }
}
static void inherited_style(MapMode mode,bool contextual) {
    scenario="inherited font, transform and shadow";
    const std::string css="#outer{font-family:SequenceFont;font-size:18px;font-weight:700;line-height:20px;"
        "text-transform:capitalize;text-shadow:2px 3px 0 red}#flex{font-size:inherit;line-height:inherit}";
    Fixture a("hello",css,mode,contextual), b("he<!--x-->llo",css,mode,contextual);
    a.meter.inherited=true; b.meter.inherited=true; a.layout(); b.layout();
    equivalent(a,b); REQUIRE(text(b.paint())=="Hello");
    auto wrapper=b.first(); REQUIRE(wrapper->element->anonymous_flex_owner==b.node());
    REQUIRE(wrapper->presentation_owner==b.node());
    REQUIRE(wrapper->style.view().text_shadows().size()==1);
    REQUIRE(!wrapper->children.empty());
    for(auto* fragment:wrapper->children) {
        REQUIRE(fragment->element==wrapper->element); REQUIRE(fragment->presentation_owner==b.node());
    }
}
static void whitespace(MapMode mode,bool contextual) {
    for(const char* ws:{"normal","nowrap","pre","pre-wrap","pre-line"}) {
        std::string css=std::string("#flex{white-space:")+ws+"}";
        for(const char* space:{" ","  ","\t","\n"}) for(bool adjacent:{false,true}) {
            scenario=std::string("mixed whitespace ")+ws;
            Fixture a(std::string("A")+space+"B",css,mode,contextual);
            Fixture b(std::string("A<!--x-->")+space+"<!--y-->B",css,mode,contextual,false,adjacent);
            equivalent(a,b);
            if(std::string(space)==" " && std::string(ws)=="normal") REQUIRE(near(aether_layout_box_get_width(b.first()),30));
        }
        for(const char* content:{"","<!--only-->","   ","\t\n"," <!--x--> \n<i style='display:none'>HIDDEN</i>"}) {
            scenario=std::string("suppressed whitespace ")+ws;
            Fixture f(content,css,mode,contextual);
            REQUIRE(f.items().empty()); REQUIRE(f.paint().empty());
            REQUIRE(near(aether_layout_box_get_height(f.box()),0));
            REQUIRE(near(f.intrinsic(false),0)); REQUIRE(near(f.intrinsic(true),0));
        }
        Fixture nbsp("&#160;",css,mode,contextual); REQUIRE(nbsp.items().size()==1); REQUIRE(!nbsp.paint().empty());
        Fixture decorated(" <!--x--> ",css+"#flex{height:50px;padding:3px;border:2px solid black}",mode,contextual);
        REQUIRE(decorated.items().empty()); REQUIRE(decorated.paint().empty()); REQUIRE(near(aether_layout_box_get_height(decorated.box()),60));
    }
}
static void minimum_and_height(MapMode mode,bool contextual) {
    for(bool adjacent:{false,true}) {
        scenario="unbreakable minimum";
        Fixture a("AAAA","#flex{width:20px}",mode,contextual);
        Fixture b("AA<!--x-->AA","#flex{width:20px}",mode,contextual,false,adjacent);
        equivalent(a,b); REQUIRE(near(b.intrinsic(false),40));
        REQUIRE(near(aether_layout_box_get_width(b.first()),40)); REQUIRE(near(aether_layout_box_get_height(b.first()),20));
        const std::string nested="#outer{display:flex;width:20px}#flex{width:auto}";
        Fixture c("AAAA",nested,mode,contextual), d("AA<!--x-->AA",nested,mode,contextual,false,adjacent);
        equivalent(c,d); REQUIRE(near(aether_layout_box_get_width(d.box()),40));
    }
}
static void intrinsic_height(MapMode mode,bool contextual) {
    for(const char* content:{"AB<span id='mark'></span>","A<!--x-->B<span id='mark'></span>",
                            "A<i style='display:none'>HIDDEN</i>B<span id='mark'></span>"}) {
        scenario=std::string("height fixture ")+content;
        Fixture f(content,"",mode,contextual,false,false,true);
        REQUIRE(near(f.intrinsic(true),40)); REQUIRE(f.items().size()==2);
        REQUIRE(near(aether_layout_box_get_height(f.first()),20));
        REQUIRE(near(aether_layout_box_get_height(f.box()),40));
        const auto outer=aether_layout_box_get_border_box(f.box("outer"));
        const auto host=aether_layout_box_get_border_box(f.box());
        const auto marker=aether_layout_box_get_border_box(f.box("mark"));
        REQUIRE(near(outer.height,20)); REQUIRE(near(marker.y-host.y,30)); REQUIRE(near(marker.height,10));
        REQUIRE(near(marker.y+marker.height-host.y,40)); REQUIRE(marker.y+marker.height>outer.y+outer.height);
    }
}
static void boundaries(MapMode mode,bool contextual) {
    for(const char* direction:{"row","column"}) for(int gap:{0,30}) {
        const std::string css=std::string("#flex{flex-direction:")+direction+";gap:"+std::to_string(gap)+"px}";
        for(const char* position:{"static","absolute","fixed"}) {
            scenario=std::string("boundary ")+direction+" "+position;
            Fixture f(std::string("A<span id='mark' style='position:")+position+"'></span>B",css,mode,contextual);
            const bool in_flow=std::string(position)=="static";
            REQUIRE(f.items().size()==(in_flow?3u:2u)); REQUIRE(f.box("mark"));
            auto last=aether_layout_result_find_primary_box(f.result,f.items().back()); REQUIRE(last);
            const auto first=aether_layout_box_get_border_box(f.first()), end=aether_layout_box_get_border_box(last);
            const bool column=std::string(direction)=="column";
            REQUIRE(near(column?end.y-first.y:end.x-first.x,
                         (column?20.f:10.f)+(in_flow?10.f:0.f)+gap*(in_flow?2:1)));
        }
        Fixture hidden("A<span id='mark' style='visibility:hidden'>X</span>B",css,mode,contextual);
        REQUIRE(hidden.items().size()==3); REQUIRE(hidden.box("mark")); REQUIRE(text(hidden.paint())=="AB");
    }
    for(const char* justify:{"start","center","end","space-between"}) {
        scenario=std::string("justification ")+justify;
        const std::string css=std::string("#flex{justify-content:")+justify+"}";
        Fixture a("AB",css,mode,contextual), b("A<!--x-->B",css,mode,contextual); equivalent(a,b);
    }
    for(const char* css:{"#flex{width:50px;flex-wrap:wrap;gap:5px;align-items:center}",
                         "#flex{width:50px;flex-wrap:wrap;gap:5px;height:120px;align-items:flex-end}"}) {
        scenario="wrap with real items";
        Fixture a("<span id='mark'></span>A B<span style='width:10px;height:10px;flex-shrink:0'></span>",css,mode,contextual);
        Fixture b("<span id='mark'></span>A<!--x--> B<span style='width:10px;height:10px;flex-shrink:0'></span>",css,mode,contextual);
        equivalent(a,b); REQUIRE(b.items().size()==3);
    }
    scenario="generated pseudo-element boundaries";
    const std::string generated="#flex::before{content:'L'}#flex::after{content:'R'}";
    Fixture pseudo_a("AB",generated,mode,contextual), pseudo_b("A<!--x-->B",generated,mode,contextual);
    equivalent(pseudo_a,pseudo_b);
    REQUIRE(pseudo_b.items().size()==3); REQUIRE(text(pseudo_b.paint())=="LABR");
    scenario="dynamic display none";
    Fixture f("A<span id='mark' style='display:none'>X</span>B","",mode,contextual);
    REQUIRE(f.items().size()==1);
    REQUIRE(aether_element_set_attribute(f.node("mark"),"style","display:block") == AETHER_OK);
    f.resolve(); f.layout(); REQUIRE(f.items().size()==3);
    REQUIRE(aether_element_set_attribute(f.node("mark"),"style","display:none") == AETHER_OK);
    f.resolve(); f.layout(); REQUIRE(f.items().size()==1); REQUIRE(text(f.paint())=="AB");
}
static void intrinsic_modes(MapMode mode,bool contextual) {
    const std::string css="#outer{display:flex;width:600px}#flex{width:auto;flex:0 0 auto;flex-wrap:wrap;gap:20px;align-items:flex-start}"
        "#flex>span{height:20px;flex:0 0 auto}";
    for(bool reverse:{false,true}) {
        scenario="wrapping fixed-card intrinsic modes";
        Fixture f("<span style='width:100px'></span><span style='width:70px'></span><span style='width:90px'></span>",css,mode,contextual);
        f.intrinsic_pair(100,300,reverse);
        REQUIRE(near(aether_layout_box_get_width(f.box()),300)); REQUIRE(near(aether_layout_box_get_height(f.box()),20));
        std::vector<Geometry> before,after;Fixture::geometry(f.box(),before);f.layout();Fixture::geometry(f.box(),after);
        REQUIRE(before.size()==after.size());for(size_t i=0;i<before.size();++i) REQUIRE(same_rect(before[i].rect,after[i].rect));
        for(const char* content:{"AB CD<span style='width:70px'></span>",
            "A<!--comment-->B <i style='display:none'>hidden</i>CD<span style='width:70px'></span>",
            "AB CD<span id='mark' style='position:absolute'></span><span style='width:70px'></span>"}) {
            scenario="anonymous text wrapping intrinsic modes";
            Fixture t(content,css,mode,contextual); t.intrinsic_pair(70,140,reverse);
            REQUIRE(t.items().size()==2); REQUIRE(near(aether_layout_box_get_width(t.box()),140));
            REQUIRE(near(aether_layout_box_get_height(t.box()),20));
        }
    }
}
int main(int argc, char** argv) {
    const std::string group=argc>1?argv[1]:"all";
    for(auto mode:{MapMode::contextual,MapMode::legacy_complete,MapMode::legacy_partial}) for(bool contextual:{false,true}) {
        if(group=="all"||group=="intrinsic-modes") intrinsic_modes(mode,contextual);
        if(group=="all"||group=="continuity") continuity(mode,contextual);
        if(group=="all"||group=="whitespace") whitespace(mode,contextual);
        if(group=="all"||group=="inheritance") inherited_style(mode,contextual);
        if(group=="all"||group=="height") minimum_and_height(mode,contextual);
        if(group=="all"||group=="intrinsic-height") intrinsic_height(mode,contextual);
        if(group=="all"||group=="boundaries") boundaries(mode,contextual);
    }
    REQUIRE(checks>1); std::printf("flex-text-sequences: %u checks passed\n",checks);
}
