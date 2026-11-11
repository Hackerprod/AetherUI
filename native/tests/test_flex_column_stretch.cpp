// Public regression for deferred column/nowrap stretch layout.
// Exact snapshots preserve rounding, ownership, primary lookup and paint order.
// All checks remain active under NDEBUG. Callback counts are deterministic;
// high-depth probes are gated by a small successful probe, never wall time.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_style.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0, scenarios = 0;
unsigned long long callbacks = 0;
std::string active_case = "setup";

void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL infrastructure %s: %s\n", active_case.c_str(), message);
        std::exit(2);
    }
}
void check(bool value, const std::string& name, const char* field) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), field);
    }
}
void number(float actual, float expected, const std::string& name, const char* field) {
    const bool pass = std::isfinite(actual) && actual == expected;
    if (!pass) std::fprintf(stderr, "  actual=%a expected=%a\n", actual, expected);
    check(pass, name, field);
}
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    ++callbacks;
    return {10.f * static_cast<float>(std::strlen(text)), 20.f, 15.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    bool contextual;
    float viewport_width = 640, viewport_height = 480;

    Fixture(const std::string& html, const std::string& css, bool context = false)
        : contextual(context) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK,
                "create runtime");
        add_css(css);
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "create engine");
        ++scenarios;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    void add_css(const std::string& css) {
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                    "flex-column-stretch.css", &sheet, nullptr) == AETHER_OK, "add stylesheet");
        refresh_map();
    }
    void refresh_map() {
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,
            aether_doc_get_document_element(doc), &updates);
        require(count >= 0, "resolve runtime styles");
        aether_style_updates_free(updates, count);
        aether_style_map_t next = nullptr;
        if (contextual) {
            aether_style_revision_t revision = nullptr;
            require(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK, "retain revision");
            require(aether_style_revision_create_map(revision, &next) == AETHER_OK, "contextual map");
            aether_style_revision_destroy(revision);
        } else {
            require(aether_style_runtime_create_resolved_style_map(runtime, doc, &next) == AETHER_OK,
                    "resolved map");
        }
        aether_style_map_destroy(map);
        map = next;
    }
    aether_node_t node(const char* id) const {
        const auto n = aether_doc_get_element_by_id(doc, id);
        require(n != nullptr, "find element");
        return n;
    }
    aether_layout_result_t layout(float width = 640, float height = 480) {
        if (contextual && (width != viewport_width || height != viewport_height)) {
            aether_style_environment_t env{width,height,1,16,0,0,0};
            aether_style_update_t* updates = nullptr;
            const auto count = aether_style_runtime_set_environment(runtime,&env,
                aether_doc_get_document_element(doc),&updates);
            require(count >= 0,"resize style environment");
            aether_style_updates_free(updates,count);
            viewport_width = width; viewport_height = height;
            refresh_map();
        }
        aether_layout_result_t result = nullptr;
        const auto status = aether_layout_layout(engine, doc, map, width, height, nullptr, nullptr, &result);
        if (status != AETHER_OK) std::fprintf(stderr, "  layout status=%d\n", int(status));
        require(status == AETHER_OK && result, "layout");
        return result;
    }
    aether_layout_box_t box(aether_layout_result_t result, const char* id) const {
        const auto b = aether_layout_result_find_primary_box(result, node(id));
        require(b != nullptr, "primary box");
        return b;
    }
    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        return aether_layout_box_get_border_box(box(result, id));
    }
};


bool snapshots = false;
const char* reset = "*{margin:0;padding:0;border:0;box-sizing:border-box;}"
    "html,body,div{display:block;}body{width:400px;height:400px;}";
const char* pair = "<body><div id='p'><div id='a'><div id='d'></div></div><div id='b'></div></div></body>";
const char* defaults = "#p{display:flex;flex-direction:column;align-items:stretch;width:200px;height:200px;}"
    "#a,#b{min-height:0;flex:0 0 80px;width:auto;}#d{height:20px;}";
struct Expected { const char* id; float x,y,w,h; };
void rectangle(aether_rectf r, const Expected& e, const std::string& label) {
    number(r.x,e.x,label,"x"); number(r.y,e.y,label,"y");
    number(r.width,e.w,label,"width"); number(r.height,e.h,label,"height");
}
void append_rect(std::string& text, aether_rectf r) {
    char out[160]; std::snprintf(out,sizeof(out)," %a,%a,%a,%a",r.x,r.y,r.width,r.height); text+=out;
}
std::string snapshot(aether_layout_result_t r) {
    std::string out;
    const int count=aether_layout_result_get_paint_order_count(r);
    for(int i=0;i<count;++i) {
        auto b=aether_layout_result_get_paint_order_box(r,i);
        out += "box " + std::to_string(i) + " kind " + std::to_string(aether_layout_box_get_kind(b));
        auto node=aether_layout_box_get_element(b);
        char* id=node?aether_element_get_attribute(node,"id"):nullptr;
        if(id) {out+=" id ";out+=id;aether_string_free(id);}
        append_rect(out,aether_layout_box_get_margin_box(b));
        append_rect(out,aether_layout_box_get_border_box(b));
        append_rect(out,aether_layout_box_get_padding_box(b));
        append_rect(out,aether_layout_box_get_content_box(b));
        out += " primary " + std::to_string(node && aether_layout_result_find_primary_box(r,node)==b);
        out += " children";
        for(int j=0;j<aether_layout_box_get_child_count(b);++j) {
            auto child=aether_layout_box_get_child(b,j);int index=-1;
            for(int k=0;k<count;++k) if(child==aether_layout_result_get_paint_order_box(r,k))index=k;
            out+=" "+std::to_string(index);
        }
        auto chrome=aether_layout_box_get_scrollbar_geometry(b);
        out += " chrome " + std::to_string(chrome.horizontal_visible) + "," + std::to_string(chrome.vertical_visible);
        append_rect(out,chrome.horizontal_track);append_rect(out,chrome.horizontal_thumb);
        append_rect(out,chrome.vertical_track);append_rect(out,chrome.vertical_thumb);append_rect(out,chrome.corner);
        char range[160];std::snprintf(range,sizeof(range)," scroll %a,%a\n",
            aether_layout_box_get_scrollable_overflow_x(b),aether_layout_box_get_scrollable_overflow_y(b));out+=range;
    }
    aether_display_list_builder_t builder=nullptr; aether_display_list_t list=nullptr;
    require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"snapshot painter");
    require(aether_display_list_build(builder,r,nullptr,&list)==AETHER_OK,"snapshot display list");
    for(int i=0;i<aether_display_list_get_count(list);++i) {
        aether_paint_command_t cmd{};
        require(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"snapshot command");
        out+="paint "+std::to_string(cmd.tag);
        if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
            append_rect(out,cmd.data.fill_rounded_rect.rect);
            const auto color=cmd.data.fill_rounded_rect.color;char rgba[120];
            std::snprintf(rgba,sizeof(rgba)," rgba %a,%a,%a,%a",color.r,color.g,color.b,color.a);out+=rgba;
        }
        if(cmd.tag==AETHER_PAINT_DRAW_TEXT) {
            append_rect(out,cmd.data.draw_text.rect);
            if(cmd.data.draw_text.text)out+=std::string(" text ")+cmd.data.draw_text.text;
        }
        if(cmd.tag==AETHER_PAINT_PUSH_CLIP)append_rect(out,cmd.data.push_clip.rect);
        if(cmd.tag==AETHER_PAINT_FILL_BORDER_SIDE) {
            append_rect(out,cmd.data.fill_border_side.outer_rect);append_rect(out,cmd.data.fill_border_side.inner_rect);
            out+=" side "+std::to_string(cmd.data.fill_border_side.side);
        }
        out+='\n';aether_paint_command_free(&cmd);
    }
    aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);
    return out;
}
void scenario(const char* label, const char* css, std::initializer_list<Expected> expected,
              const char* html=pair) {
    for(bool contextual:{false,true}) {
        active_case=std::string(label)+(contextual?"/contextual":"/resolved");
        Fixture f(html,std::string(reset)+defaults+css,contextual);
        const int children=aether_node_get_child_count(f.node("p"));
        auto first=f.layout();const auto original=snapshot(first);
        aether_layout_result_t clone=nullptr;
        require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"clone snapshot");
        auto second=f.layout(800,600);
        check(snapshot(first)==original,active_case,"retained frame unchanged after resize");
        check(snapshot(clone)==original,active_case,"clone geometry/order/display list unchanged");
        // Viewport-sized root boxes may differ after resize. Named fixed boxes must not.
        for(auto frame:{first,clone,second}) for(const auto& e:expected) {
            rectangle(f.rect(frame,e.id),e,active_case+"/"+e.id);
            int occurrences=0;
            for(int j=0;j<aether_layout_result_get_paint_order_count(frame);++j)
                if(aether_layout_result_get_paint_order_box(frame,j)==f.box(frame,e.id))++occurrences;
            check(occurrences==1,active_case,"one primary paint entry");
        }
        check(aether_node_get_child_count(f.node("p"))==children,active_case,"DOM ownership unchanged");
        if(snapshots)std::printf("CASE %s\n%s",active_case.c_str(),original.c_str());
        aether_layout_result_destroy(first);aether_layout_result_destroy(second);
        check(snapshot(clone)==original,active_case,"clone survives source destruction");
        aether_layout_result_destroy(clone);
    }
}
void matrix() {
    scenario("large main margin cancellation",
        "#a,#b{flex:1 1 0px}#a{margin-top:.125px;margin-bottom:.125px}#d{height:16777216px}",
        {{"a",0,.125f,200,100},{"b",0,100.25f,200,100},{"d",0,.125f,200,16777216}});
    scenario("large opposite cross margins",
        "#a{margin-left:16777216px;margin-right:-16777216px}",
        {{"a",16777216,0,200,80},{"d",16777216,0,200,20}});
    scenario("subnormal cross margin remains nonzero",
        "#a{margin-left:0.000000000000000000000000000000000000000000001px}",
        {{"a",0x1p-149f,0,200,80},{"d",0x1p-149f,0,200,20}});
    scenario("content decoration exceeds target",
        "#p{height:100px}#a{box-sizing:content-box;flex:1 1 0px;padding-top:80px;border-top:30px solid}#b{display:none}",
        {{"a",0,0,200,110},{"d",0,110,200,20}});
    scenario("fixed", "", {{"p",0,0,200,200},{"a",0,0,200,80},{"b",0,80,200,80},{"d",0,0,200,20}});
    for (const char* width : {"200px","50%","calc(25% + 100px)","var(--w)"}) {
        auto css=std::string("#p{--w:200px;width:")+width+"}";
        scenario(width,css.c_str(),{{"p",0,0,200,200},{"a",0,0,200,80}});
    }
    scenario("zero cross","#p{width:0}",{{"a",0,0,0,80},{"d",0,0,0,20}});
    scenario("auto cross","#p{width:auto}",{{"p",0,0,400,200},{"a",0,0,400,80}});
    scenario("root cross min wins max","#p{width:50px;min-width:200px;max-width:100px}",{{"a",0,0,200,80}});
    scenario("forced definite main from row ancestor",
        "#outer{display:flex;width:200px;height:200px}#p{height:auto;min-width:0;flex:0 0 200px}#d{height:50%}",
        {{"p",0,0,200,200},{"a",0,0,200,80},{"d",0,0,200,40}},
        "<body><div id='outer'><div id='p'><div id='a'><div id='d'></div></div><div id='b'></div></div></div></body>");
    scenario("intrinsic ancestor content measurement",
        "#outer{display:flex;flex-direction:column;height:200px;width:200px}#p{height:auto;flex:0 0 content}",
        {{"p",0,0,200,160},{"a",0,0,200,80},{"b",0,80,200,80}},
        "<body><div id='outer'><div id='p'><div id='a'><div id='d'></div></div><div id='b'></div></div></div></body>");
    scenario("auto root","#p{height:auto}",{{"p",0,0,200,160},{"a",0,0,200,80},{"b",0,80,200,80}});
    scenario("minimum root","#p{height:auto;min-height:200px}",{{"p",0,0,200,200},{"a",0,0,200,80}});
    for (const char* basis : {"80px","40%","calc(20% + 40px)","calc(0% + 80px)","var(--b)","auto"}) {
        auto css=std::string("#p{--b:80px}#a{height:80px;flex-basis:")+basis+"}";
        scenario(basis,css.c_str(),{{"a",0,0,200,80},{"b",0,80,200,80}});
    }
    for (const char* minimum : {"0","30px","25%","calc(10% + 30px)"}) {
        auto css=std::string("#a{min-height:")+minimum+"}";
        scenario(minimum,css.c_str(),{{"a",0,0,200,80},{"b",0,80,200,80}});
    }
    scenario("min exceeds max","#a{min-height:100px;max-height:50px}",{{"a",0,0,200,100},{"b",0,100,200,80}});
    scenario("cross min exceeds max","#a{min-width:250px;max-width:50px}",{{"a",0,0,250,80},{"b",0,80,200,80}});
    scenario("cross max","#a{max-width:50%}",{{"a",0,0,100,80},{"b",0,80,200,80}});
    scenario("positive main margins","#a{margin-top:10px;margin-bottom:20px}",{{"a",0,10,200,80},{"b",0,110,200,80}});
    scenario("negative main margins","#a{margin-top:-10px;margin-bottom:-20px}",{{"a",0,-10,200,80},{"b",0,50,200,80}});
    scenario("auto main margins","#a{margin-top:auto;margin-bottom:auto}",{{"a",0,20,200,80},{"b",0,120,200,80}});
    scenario("percent main margins","#a{margin-top:5%;margin-bottom:calc(5% + 5px)}",{{"a",0,10,200,80},{"b",0,105,200,80}});
    scenario("nonzero opposite cross margins excluded","#a{margin-left:10px;margin-right:-10px}",{{"a",10,0,200,80},{"d",10,0,200,20}});
    scenario("negative cross margins","#a{margin-left:-10px;margin-right:-20px}",{{"a",-10,0,230,80}});
    scenario("percent cross margins","#a{margin-left:5%;margin-right:calc(5% + 5px)}",{{"a",10,0,175,80}});
    scenario("auto cross margin excluded","#a{margin-left:auto}#d{width:40px}",{{"a",160,0,40,80},{"d",160,0,40,20}});
    scenario("both auto cross margins excluded","#a{margin-left:auto;margin-right:auto}#d{width:40px}",{{"a",80,0,40,80}});
    scenario("self center excluded","#a{align-self:center}#d{width:40px}",{{"a",80,0,40,80}});
    scenario("self stretch included","#p{align-items:flex-start}#a{align-self:stretch}#b{width:40px}",{{"a",0,0,200,80},{"b",0,80,40,80}});
    scenario("percentage width not auto","#a{width:50%}",{{"a",0,0,100,80}});
    scenario("one line wrap excluded","#p{flex-wrap:wrap}",{{"a",0,0,200,80},{"b",0,80,200,80}});
    scenario("multiple wrapped lines","#p{height:100px;flex-wrap:wrap;gap:10px}#a,#b{min-width:20px}",{{"a",0,0,95,80},{"b",105,0,95,80}});
    scenario("content box decoration","#a{box-sizing:content-box;padding:5px;border:5px solid}",{{"a",0,0,200,100},{"d",10,10,180,20},{"b",0,100,200,80}});
    scenario("border box decoration","#a{padding:5px;border:5px solid}",{{"a",0,0,200,80},{"d",10,10,180,20}});
    scenario("percent padding","#a{padding:5%;border:5px solid}",{{"a",0,0,200,80},{"d",15,15,170,20}});
    scenario("decoration exceeds target","#a{flex-basis:10px;padding:50px;border:5px solid}",{{"a",0,0,200,110},{"b",0,110,200,80},{"d",55,55,90,20}});
    scenario("content box min max","#a{box-sizing:content-box;padding:5px;border:5px solid;min-height:100px;max-height:50px}",{{"a",0,0,200,120},{"b",0,120,200,80}});
    scenario("fraction grow gap","#p{gap:10px}#a,#b{flex-grow:.25}",{{"a",0,0,200,87.5f},{"b",0,97.5f,200,87.5f}});
    scenario("fraction shrink","#p{height:120px}#a,#b{flex-shrink:.25}",{{"a",0,0,200,70},{"b",0,70,200,70}});
    scenario("content basis retained","#a{flex-basis:content}#d{height:60px}",{{"a",0,0,200,60},{"b",0,60,200,80}});
    scenario("definite percentage descendant","#d{height:50%}",{{"a",0,0,200,80},{"d",0,0,200,40}});
    for(const char* basis:{"50%","calc(0% + 80px)","calc(50% + 20px)"}) {
        auto css=std::string("body{height:auto}#p{height:auto;min-height:200px}#a{flex-basis:")+basis+"}#d{height:50%}";
        scenario((std::string("indefinite ")+basis).c_str(),css.c_str(),{{"p",0,0,200,200},{"a",0,0,200,20},{"d",0,0,200,20}},
            "<body><div id='p'><div id='a'><div id='d'>aa</div></div><div id='b'></div></div></body>");
    }
    scenario("relative","#a{position:relative;left:10px;top:5px}",{{"a",10,5,200,80},{"d",10,5,200,20},{"b",0,80,200,80}});
    scenario("absolute percentage","#a{position:relative}#d{position:absolute;left:10px;top:50%;width:20px;height:25%}",{{"d",10,40,20,20}});
    scenario("static anchor","#a{position:relative}#d{position:absolute;width:20px;height:10px}",{{"d",0,0,20,10}});
    scenario("fixed","#d{position:fixed;left:10px;top:30px;width:20px;height:10px}",{{"d",10,30,20,10}});
    scenario("anonymous sibling","",{{"a",0,20,200,80},{"b",0,100,200,80}},
        "<body><div id='p'>aa<div id='a'><div id='d'></div></div><div id='b'></div></div></body>");
    scenario("controls","input{display:block;width:30px;height:15px}button{display:block;width:40px;height:20px}",
        {{"a",0,0,200,80},{"input",0,0,30,15},{"button",0,15,40,20}},
        "<body><div id='p'><div id='a'><input id='input' value='x'><button id='button'>ok</button></div><div id='b'></div></div></body>");
    scenario("table item excluded","#a{display:table}#row{display:table-row}#cell{display:table-cell}",{{"a",0,0,200,80},{"b",0,80,200,80}},
        "<body><div id='p'><div id='a'><div id='row'><div id='cell'><div id='d'></div></div></div></div><div id='b'></div></div></body>");
    scenario("pseudo table row item excluded","#a{display:table-row}",{{"a",0,0,200,80},{"b",0,80,200,80}});
    scenario("pseudo table cell item excluded","#a{display:table-cell}",{{"a",0,0,200,80},{"d",0,0,200,20},{"b",0,80,200,80}});
    scenario("nested flex wrap descendant","#d{display:flex;flex-wrap:wrap;height:auto;width:60px}span{display:block;flex:0 0 40px;height:20px}",
        {{"a",0,0,200,80},{"d",0,0,60,40}},
        "<body><div id='p'><div id='a'><div id='d'><span>aa</span><span>bb</span></div></div><div id='b'></div></div></body>");
    scenario("table descendant","table{display:table;width:100px}tr{display:table-row}td{display:table-cell}",{{"a",0,0,200,80},{"d",0,0,100,20}},
        "<body><div id='p'><div id='a'><table><tr><td><div id='d'></div></td></tr></table></div><div id='b'></div></div></body>");
}
// Constants are the exact binary32 outputs of the original two-pass algorithm.
// A .001 tolerance would silently accept removal of its first normalization.
void normalization_rounding() {
    const char* html="<body><div id='p'><div id='a'><div id='d'></div><div id='s'></div></div></div></body>";
    const char* css="*{margin:0;padding:0;border:0;box-sizing:content-box;}body,div{display:block;}body{width:800px;}"
        "#p{display:flex;flex-direction:column;width:200px;height:336.93px;}"
        "#a{flex:1 1 0px;min-height:0;padding-top:94.74px;border-top:38.7px solid black;overflow:auto;}"
        "#d{height:50%;}#s{height:300px;}";
    for(bool context:{false,true}) {
        active_case=context?"rounding/contextual":"rounding/resolved";
        Fixture f(html,css,context);
        auto first=f.layout();auto original=snapshot(first);
        aether_layout_result_t clone=nullptr;
        require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"rounding clone");
        auto next=f.layout(800,600);
        for(auto r:{first,clone,next}) {
            number(f.rect(r,"a").height,336.9300537109375f,active_case,"exact two-pass border height");
            number(f.rect(r,"d").height,101.7450103759765625f,active_case,"exact percentage height");
            number(aether_layout_box_get_scrollable_overflow_y(f.box(r,"a")),context ? 0x1.8c8288p+7f : 0x1.8c828ap+7f,active_case,"exact overflow range");
        }
        check(snapshot(first)==original && snapshot(clone)==original,active_case,"exact retained rounding");
        if(snapshots)std::printf("CASE %s\n%s",active_case.c_str(),original.c_str());
        aether_layout_result_destroy(first);aether_layout_result_destroy(next);
        check(snapshot(clone)==original,active_case,"exact clone after destruction");aether_layout_result_destroy(clone);
    }
}

std::string public_order(const Fixture& f,aether_layout_result_t frame) {
    std::string order;
    for(int i=0;i<aether_layout_result_get_paint_order_count(frame);++i) {
        auto box=aether_layout_result_get_paint_order_box(frame,i);
        for(const char* id:{"p","a","ad","b","bd","c","cd"}) {
            if(aether_layout_box_get_element(box)==f.node(id)) {if(!order.empty())order+=' ';order+=id;}
        }
    }
    return order;
}
void mixed_order() {
    for(bool context:{false,true}) {
        active_case=context?"mixed/contextual":"mixed/resolved";
        Fixture f("<body><div id='p'><div id='a'><div id='ad'></div></div><div id='b'><div id='bd'></div></div>"
            "<div id='c'><div id='cd'></div></div></div></body>",std::string(reset)+defaults+
            "#a,#b,#c{min-height:0;flex:0 0 80px}#b,#c{margin-top:-80px}#b{width:100px}"
            "#a{background:#ff0000}#b{background:#00ff00}#c{background:#0000ff}#ad,#bd,#cd{height:10px}",context);
        auto first=f.layout();auto original=snapshot(first);
        aether_layout_result_t clone=nullptr;require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"mixed clone");
        f.add_css("#p{width:240px}");auto next=f.layout(800,600);
        for(auto frame:{first,clone,next}) {
            check(public_order(f,frame)=="p b bd a ad c cd",active_case,"mixed final registration chronology");
            auto p=f.box(frame,"p");check(aether_layout_box_get_child_count(p)==3,active_case,"final child count");
            int i=0;for(const char* id:{"a","b","c"})
                check(aether_layout_box_get_child(p,i++)==f.box(frame,id),active_case,"DOM child order and primary lookup");
            check(aether_layout_result_hit_test(frame,5,5)==f.node("cd"),active_case,"hit final overlapping descendant");
            check(aether_layout_result_hit_test(frame,5,50)==f.node("c"),active_case,"hit final overlapping item");
            aether_display_list_builder_t builder=nullptr;aether_display_list_t list=nullptr;
            require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"mixed painter");
            require(aether_display_list_build(builder,frame,nullptr,&list)==AETHER_OK,"mixed paint list");
            std::string order;
            for(int k=0;k<aether_display_list_get_count(list);++k) {
                aether_paint_command_t cmd{};require(aether_display_list_get_command(list,k,&cmd)==AETHER_OK,"mixed command");
                if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
                    auto c=cmd.data.fill_rounded_rect.color;
                    const char* id=c.r==1&&c.g==0&&c.b==0?"a":c.r==0&&c.g==1&&c.b==0?"b":c.r==0&&c.g==0&&c.b==1?"c":nullptr;
                    if(id) {
                        order+=id;auto box=f.rect(frame,id);
                        rectangle(cmd.data.fill_rounded_rect.rect,{id,box.x,box.y,box.width,box.height},active_case);
                    }
                }
                aether_paint_command_free(&cmd);
            }
            check(order=="abc",active_case,"display order and one fill per item");
            aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);
        }
        number(f.rect(first,"a").width,200,active_case,"retained old width");
        number(f.rect(next,"a").width,240,active_case,"style update final width");
        check(snapshot(first)==original && snapshot(clone)==original,active_case,"mixed retained frames");
        if(snapshots)std::printf("CASE %s\n%s",active_case.c_str(),original.c_str());
        aether_layout_result_destroy(first);aether_layout_result_destroy(next);
        check(snapshot(clone)==original,active_case,"mixed clone surviving source");aether_layout_result_destroy(clone);
    }
}

void overflow_and_clipping() {
    for(bool context:{false,true})for(const char* overflow:{"visible","hidden","auto","scroll","clip"}) {
        active_case=std::string("overflow/")+overflow+(context?"/contextual":"/resolved");
        Fixture f(pair,std::string(reset)+defaults+"#a{overflow:"+overflow+"}#d{width:240px;height:240px;background:red}",context);
        auto first=f.layout();auto original=snapshot(first);
        aether_layout_result_t clone=nullptr;require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"overflow clone");
        auto next=f.layout(800,600);
        const bool clipped=std::strcmp(overflow,"visible")!=0;
        const bool scroll=clipped&&std::strcmp(overflow,"clip")!=0;
        for(auto r:{first,clone,next}) {
            auto metrics=aether_layout_box_get_scroll_metrics(f.box(r,"a"));
            number(metrics.scrollable_overflow_x,scroll?40.f:0.f,active_case,"exact horizontal range");
            number(metrics.scrollable_overflow_y,scroll?160.f:0.f,active_case,"exact vertical range");
            check((aether_layout_result_hit_test(r,20,220)==f.node("d"))==!clipped,active_case,"overflow hit clipping");
            rectangle(f.rect(r,"d"),{"d",0,0,240,240},active_case);
        }
        check(snapshot(first)==original && snapshot(clone)==original,active_case,"overflow retained exact");
        if(snapshots)std::printf("CASE %s\n%s",active_case.c_str(),original.c_str());
        aether_layout_result_destroy(first);aether_layout_result_destroy(next);
        check(snapshot(clone)==original,active_case,"overflow clone surviving source");aether_layout_result_destroy(clone);
    }
}

unsigned long long column_chain(const std::string& mode,int depth,bool context) {
    active_case=mode+"/depth"+std::to_string(depth)+(context?"/contextual":"/resolved");
    std::string html="<body>";
    for(int i=0;i<depth;++i)html+=i?"<div class='n'>":"<div class='n' id='root'>";
    html+="<div id='leaf'>aaaa bb cc</div>";
    for(int i=0;i<depth;++i)html+="</div>";
    html+="</body>";
    std::string css=std::string(reset)+".n{display:flex;flex-direction:column;align-items:stretch;min-width:0;min-height:0;height:200px;flex:0 0 200px;}"
        "#root{width:200px}#leaf{min-width:0;min-height:0;flex:0 0 200px;height:200px;}";
    if(mode=="auto-root")css+="#root{height:auto}";
    if(mode=="minimum-root")css+="#root{height:auto;min-height:200px}";
    if(mode=="auto-basis")css+=".n,#leaf{flex-basis:auto}";
    if(mode=="percent-basis")css+=".n,#leaf{flex-basis:100%}";
    if(mode=="calc-basis")css+=".n,#leaf{flex-basis:calc(0% + 200px)}";
    if(mode=="explicit-width")css+=".n,#leaf{width:200px}";
    if(mode=="wrap")css+=".n{flex-wrap:wrap}";
    if(mode=="content")css+=".n,#leaf{flex-basis:content}";
    if(mode=="margins")css+=".n:not(#root),#leaf{margin-left:1px;margin-right:1px}";
    if(mode=="opposite-margins")css+=".n:not(#root),#leaf{margin-left:1px;margin-right:-1px}";
    Fixture f(html,css,context);
    callbacks=0;auto first=f.layout();const auto count=callbacks;
    auto original=snapshot(first);aether_layout_result_t clone=nullptr;
    require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"chain clone");
    callbacks=0;auto next=f.layout(800,600);
    check(callbacks==count,active_case,"repeat resized callback budget");
    for(auto r:{first,clone,next}) {
        number(f.rect(r,"root").width,200,active_case,"root cross size");
        number(f.rect(r,"root").height,200,active_case,"root main size");
        number(f.rect(r,"leaf").width,mode=="margins"?200.f-2.f*depth:200.f,active_case,"leaf cross size");
        number(f.rect(r,"leaf").height,mode=="content"?20.f:200.f,active_case,"leaf main size");
    }
    check(snapshot(first)==original&&snapshot(clone)==original,active_case,"chain retained frames");
    aether_layout_result_destroy(first);aether_layout_result_destroy(next);
    check(snapshot(clone)==original,active_case,"chain clone after source destroyed");aether_layout_result_destroy(clone);
    std::printf("callbacks %s: %llu\n",active_case.c_str(),count);return count;
}
void numeric_failure_and_threshold() {
    for(bool context:{false,true}) {
        for(const char* css:{
            "#a{box-sizing:content-box;padding-top:300000000000000000000000000000000000000px;padding-bottom:300000000000000000000000000000000000000px}",
            "#p{width:300000000000000000000000000000000000000px}#a{padding-left:100%;padding-right:100%}"}) {
            active_case=context?"numeric overflow/contextual":"numeric overflow/resolved";
            Fixture f(pair,std::string(reset)+defaults,context);auto first=f.layout();auto original=snapshot(first);
            f.add_css(css);auto output=first;
            const auto status=aether_layout_layout(f.engine,f.doc,f.map,640,480,nullptr,nullptr,&output);
            check(status==AETHER_ERROR_INVALID_STATE,active_case,"nonfinite arithmetic rejects invalid layout");
            check(output==first,active_case,"failed layout preserves output");
            check(snapshot(first)==original,active_case,"failed layout preserves retained tree");
            if(status==AETHER_OK && output!=first)aether_layout_result_destroy(output);
            aether_layout_result_destroy(first);
        }
        for(bool real:{false,true}) {
            active_case=context?"scroll threshold/contextual":"scroll threshold/resolved";
            Fixture f(pair,std::string(reset)+defaults+"#a{overflow-x:auto;overflow-y:visible}#d{width:240px;height:"+
                (real?"80.001px}":"80px}"),context);
            auto r=f.layout();auto b=f.box(r,"a");
            number(aether_layout_box_get_scrollable_overflow_x(b),40,active_case,"horizontal-only range");
            number(aether_layout_box_get_scrollable_overflow_y(b),real?0x1.06p-10f:0.f,active_case,"exact tiny vertical range");
            check(bool(aether_layout_box_get_scrollbar_geometry(b).vertical_visible)==real,active_case,"zero versus tiny real scrollbar");
            if(snapshots)std::printf("CASE %s/%d\n%s",active_case.c_str(),int(real),snapshot(r).c_str());
            aether_layout_result_destroy(r);
        }
    }
}
void scaling() {
    for(bool context:{false,true}) {
        for(const char* mode:{"fixed","auto-root","minimum-root","auto-basis","percent-basis","calc-basis","explicit-width"}) {
            for(int depth:{1,4,8,12,16,32,64}) {
                auto count=column_chain(mode,depth,context);
                check(count==5,active_case,"five final callbacks independent of depth");
                // Gate deep probes: a broken optimization must fail quickly,
                // never execute an exponential depth-64 layout.
                if(count!=5)break;
            }
        }
        for(const char* mode:{"wrap","margins","opposite-margins"})for(int depth:{1,2,4}) {
            auto count=column_chain(mode,depth,context);
            check(count==5*((1ULL<<(depth+1))-1),active_case,"excluded two-pass callback count");
        }
        // Content must still be measured; no five-call promise is made here.
        check(column_chain("content",2,context)>5,active_case,"content basis still measured");
    }
}
} // namespace
int main(int argc,char** argv) {
    bool geometry_only=false;
    for(int i=1;i<argc;++i) {
        if(std::strcmp(argv[i],"--snapshot")==0)snapshots=true;
        if(std::strcmp(argv[i],"--geometry-only")==0)geometry_only=true;
    }
    matrix();normalization_rounding();mixed_order();overflow_and_clipping();numeric_failure_and_threshold();
    if(!geometry_only)scaling();
    std::printf("flex-column-stretch: %d fixtures, %d checks, %d failures\n",scenarios,checks,failures);
    return failures?1:0;
}
