// Public regression for definite column bases. Independent geometry expectations
// and deterministic snapshots cover omitted provisional trees.
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
    const bool pass = std::isfinite(actual) && std::fabs(actual - expected) < .001f;
    if (!pass) std::fprintf(stderr, "  actual=%g expected=%g\n", actual, expected);
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
                    "flex-column-basis.css", &sheet, nullptr) == AETHER_OK, "add stylesheet");
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
const char* defaults = "#p{display:flex;flex-direction:column;align-items:flex-start;width:200px;height:200px;}"
    "#a,#b{min-height:0;flex:0 0 80px;width:100px;}#d{height:20px;}";
struct Expected { const char* id; float x,y,w,h; };
void rectangle(aether_rectf r, const Expected& e, const std::string& label) {
    number(r.x,e.x,label,"x"); number(r.y,e.y,label,"y");
    number(r.width,e.w,label,"width"); number(r.height,e.h,label,"height");
}
void append_rect(std::string& text, aether_rectf r) {
    char out[160]; std::snprintf(out,sizeof(out)," %.6g,%.6g,%.6g,%.6g",r.x,r.y,r.width,r.height); text+=out;
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
        out += " children";
        for(int j=0;j<aether_layout_box_get_child_count(b);++j) {
            auto child=aether_layout_box_get_child(b,j);int index=-1;
            for(int k=0;k<count;++k) if(child==aether_layout_result_get_paint_order_box(r,k))index=k;
            out+=" "+std::to_string(index);
        }
        char range[100];std::snprintf(range,sizeof(range)," scroll %.6g,%.6g\n",
            aether_layout_box_get_scrollable_overflow_x(b),aether_layout_box_get_scrollable_overflow_y(b));out+=range;
    }
    aether_display_list_builder_t builder=nullptr; aether_display_list_t list=nullptr;
    require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"snapshot painter");
    require(aether_display_list_build(builder,r,nullptr,&list)==AETHER_OK,"snapshot display list");
    for(int i=0;i<aether_display_list_get_count(list);++i) {
        aether_paint_command_t cmd{};
        require(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"snapshot command");
        out+="paint "+std::to_string(cmd.tag);
        if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT)append_rect(out,cmd.data.fill_rounded_rect.rect);
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
    scenario("fixed","",{{"p",0,0,200,200},{"a",0,0,100,80},{"b",0,80,100,80},{"d",0,0,100,20}});
    for(const char* basis:{"80px","40%","calc(20% + 40px)","calc(0% + 80px)","var(--basis)","auto"}) {
        std::string css=std::string("#p{--basis:80px}#a{height:80px;flex-basis:")+basis+"}";
        scenario(basis,css.c_str(),{{"a",0,0,100,80},{"b",0,80,100,80}});
    }
    for(const char* minimum:{"0","30px","25%","calc(10% + 30px)"}) {
        auto css=std::string("#a{min-height:")+minimum+"}";
        scenario(minimum,css.c_str(),{{"a",0,0,100,80},{"b",0,80,100,80}});
    }
    scenario("min exceeds max","#a{min-height:100px;max-height:50px}",{{"a",0,0,100,100},{"b",0,100,100,80}});
    scenario("positive margins","#a{margin-top:10px;margin-bottom:20px}",{{"a",0,10,100,80},{"b",0,110,100,80}});
    scenario("negative margins","#a{margin-top:-10px;margin-bottom:-20px}",{{"a",0,-10,100,80},{"b",0,50,100,80}});
    scenario("mixed margins","#a{margin-top:-10px;margin-bottom:20px}",{{"a",0,-10,100,80},{"b",0,90,100,80}});
    scenario("percentage calc margins","#a{margin-top:5%;margin-bottom:calc(5% + 5px)}",{{"a",0,10,100,80},{"b",0,105,100,80}});
    scenario("auto margins","#a{margin-top:auto;margin-bottom:auto}",{{"a",0,20,100,80},{"b",0,120,100,80}});
    // Nonzero margins keep the original measured path, including its rounding
    // when content and margins have very different magnitudes.
    scenario("large content margin cancellation",
        "#a,#b{flex:1 1 0px}#a{margin-top:.125px;margin-bottom:.125px}#d{height:16777216px}",
        {{"a",0,.125f,100,100},{"b",0,100.25f,100,100}});
    scenario("fraction grow gap","#p{gap:10px}#a,#b{flex-grow:.25}",{{"a",0,0,100,87.5f},{"b",0,97.5f,100,87.5f}});
    scenario("fraction shrink","#p{height:120px}#a,#b{flex-shrink:.25}",{{"a",0,0,100,70},{"b",0,70,100,70}});
    scenario("content decoration","#a{box-sizing:content-box;padding:5px;border:5px solid}",{{"a",0,0,120,100},{"b",0,100,100,80},{"d",10,10,100,20}});
    scenario("border decoration","#a{padding:5px;border:5px solid}",{{"a",0,0,100,80},{"b",0,80,100,80},{"d",10,10,80,20}});
    scenario("percent padding","#a{padding:5%;border:5px solid}",{{"a",0,0,100,80},{"d",15,15,70,20}});
    scenario("decoration larger than basis","#a{flex-basis:10px;padding:50px;border:5px solid}",{{"a",0,0,110,110},{"b",0,110,100,80}});
    scenario("one wrapped line","#p{flex-wrap:wrap}",{{"a",0,0,100,80},{"b",0,80,100,80}});
    scenario("multiple columns","#p{height:100px;flex-wrap:wrap;gap:10px;align-content:flex-start}",{{"a",0,0,100,80},{"b",110,0,100,80}});
    scenario("cross stretch","#p{align-items:stretch}#a,#b{width:auto}",{{"a",0,0,200,80},{"b",0,80,200,80}});
    scenario("cross center","#p{align-items:center}",{{"a",50,0,100,80},{"b",50,80,100,80}});
    scenario("content basis","#a{flex-basis:content}",{{"a",0,0,100,20},{"b",0,20,100,80}});
    scenario("auto content basis","#a{flex-basis:auto;height:auto}",{{"a",0,0,100,20},{"b",0,20,100,80}});
    for(const char* basis:{"50%","calc(0% + 80px)","calc(50% + 20px)"}) {
        auto css=std::string("body{height:auto}#p{height:auto}#a{flex-basis:")+basis+"}";
        scenario(("indefinite "+std::string(basis)).c_str(),css.c_str(),{{"a",0,0,100,20},{"b",0,20,100,80}});
    }
    scenario("numeric minimum not definite","body{height:auto}#p{height:auto;min-height:200px}#a{flex-basis:50%}#d{height:50%}",
        {{"p",0,0,200,200},{"a",0,0,100,20},{"d",0,0,100,20}},
        "<body><div id='p'><div id='a'><div id='d'>aa</div></div><div id='b'></div></div></body>");
    scenario("definite descendant percentage","#d{height:50%}",{{"a",0,0,100,80},{"d",0,0,100,40}});
    scenario("relative","#a{position:relative;left:10px;top:5px}",{{"a",10,5,100,80},{"d",10,5,100,20},{"b",0,80,100,80}});
    scenario("absolute","#a{position:relative}#d{position:absolute;left:10px;top:50%;width:20px;height:25%}",{{"d",10,40,20,20}});
    scenario("static anchor","#a{position:relative}#d{position:absolute;width:20px;height:10px}",{{"d",0,0,20,10}});
    scenario("fixed","#d{position:fixed;left:10px;top:30px;width:20px;height:10px}",{{"d",10,30,20,10}});
    scenario("anonymous text","",{{"a",0,20,100,80},{"b",0,100,100,80}},
        "<body><div id='p'>aa<div id='a'><div id='d'></div></div><div id='b'></div></div></body>");
    scenario("nested row wrapping","#d{display:flex;flex-wrap:wrap;height:auto;width:60px}span{display:block;flex:0 0 40px;height:20px}",
        {{"a",0,0,100,80},{"d",0,0,60,40}},
        "<body><div id='p'><div id='a'><div id='d'><span>aa</span><span>bb</span></div></div><div id='b'></div></div></body>");
    scenario("table descendant","table{display:table;width:100px}tr{display:table-row}td{display:table-cell}",{{"a",0,0,100,80},{"d",0,0,100,20}},
        "<body><div id='p'><div id='a'><table><tr><td><div id='d'></div></td></tr></table></div><div id='b'></div></div></body>");
    scenario("table item","#a{display:table}#row{display:table-row}#cell{display:table-cell}",{{"a",0,0,100,80},{"b",0,80,100,80}},
        "<body><div id='p'><div id='a'><div id='row'><div id='cell'><div id='d'></div></div></div></div><div id='b'></div></div></body>");
    scenario("controls","input{display:block;width:30px;height:15px}button{display:block;width:40px;height:20px}",
        {{"a",0,0,100,80},{"input",0,0,30,15},{"button",0,15,40,20}},
        "<body><div id='p'><div id='a'><input id='input' value='x'><button id='button'>ok</button></div><div id='b'></div></div></body>");
    for(const char* axis:{"x","y"})for(const char* overflow:{"visible","hidden","auto","scroll"}) {
        auto css=std::string("#a{overflow-")+axis+":"+overflow+"}#d{width:160px;height:120px;background:red}";
        scenario((std::string("overflow-")+axis+"-"+overflow).c_str(),css.c_str(),{{"a",0,0,100,80},{"d",0,0,160,120}});
    }
}
void scroll_metrics() {
    for(bool contextual:{false,true})for(const char* overflow:{"visible","hidden","auto","scroll"}) {
        active_case=std::string("scroll-metrics/")+overflow+(contextual?"/contextual":"/resolved");
        Fixture f(pair,std::string(reset)+defaults+"#a{overflow:"+overflow+"}#d{width:160px;height:240px;background:red}",contextual);
        auto frame=f.layout();const bool clipped=std::strcmp(overflow,"visible")!=0;
        const auto metrics=aether_layout_box_get_scroll_metrics(f.box(frame,"a"));
        number(metrics.scrollable_overflow_x,clipped?60.f:0.f,active_case,"horizontal range");
        number(metrics.scrollable_overflow_y,clipped?160.f:0.f,active_case,"vertical range");
        check((aether_layout_result_hit_test(frame,20,220)==f.node("d"))==!clipped,active_case,"vertical overflow hit clipping");
        aether_layout_result_destroy(frame);
    }
}

// A nonzero margin can round differently when the provisional outer-minus-border
// path is replaced by a direct sum. A microscopic positive residual then changes
// observable scrollbar chrome, so this check must not use a geometry tolerance.
void margin_scroll_threshold() {
    const char* html =
        "<body><div id=\"outside\"><div id=\"p\">"
        "<div id=\"i0\"><div class=\"inner\"><div class=\"d\">alpha beta gamma delta epsilon</div></div></div>"
        "<div id=\"i1\"><div class=\"inner\"><div class=\"d\">alpha beta gamma delta epsilon</div></div></div></div></div></body>";
    const char* css =
        "#outside{display:block}"
        "#p{display:flex;flex-direction:column;width:80px;height:200px;flex-wrap:wrap;justify-content:center}"
        "#i0{flex:1 1 content;margin-top:2%;overflow:auto}"
        "#i1{flex:.25 .5 calc(20% + 20px);min-height:calc(10% + 5px);max-height:50px;margin-top:2%;margin-bottom:2%}"
        ".inner{display:flex}"
        ".d{flex:0 1 30px}";
    auto chrome = [](aether_layout_result_t frame) {
        aether_display_list_builder_t builder = nullptr;
        aether_display_list_t list = nullptr;
        require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"threshold painter");
        require(aether_display_list_build(builder,frame,nullptr,&list)==AETHER_OK,"threshold display list");
        bool present = false;
        for (int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t cmd{};
            require(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"threshold command");
            present = present || cmd.tag == AETHER_PAINT_DRAW_SCROLLBAR_CHROME;
            aether_paint_command_free(&cmd);
        }
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        return present;
    };
    for (bool contextual : {false,true}) {
        active_case = contextual ? "margin-scroll-threshold/contextual" : "margin-scroll-threshold/resolved";
        Fixture f(html,css,contextual);
        auto first = f.layout(); const auto original = snapshot(first);
        aether_layout_result_t clone = nullptr;
        require(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"threshold clone");
        auto second = f.layout(800,600);
        for (auto frame : {first,clone,second}) {
            auto box = f.box(frame,"i0");
            check(aether_layout_box_get_scrollable_overflow_y(box)==0.f,active_case,"exactly zero vertical range");
            check(!aether_layout_box_get_scrollbar_geometry(box).vertical_visible,active_case,"no spurious vertical scrollbar");
            check(!chrome(frame),active_case,"no scrollbar chrome command");
        }
        check(snapshot(first)==original && snapshot(clone)==original,active_case,"threshold snapshots retained");
        if (snapshots) std::printf("CASE %s\n%s",active_case.c_str(),original.c_str());
        aether_layout_result_destroy(first); aether_layout_result_destroy(clone); aether_layout_result_destroy(second);

        // A genuinely overflowing descendant still produces chrome even for a
        // small subpixel range. The optimization must not add a scroll epsilon.
        active_case = contextual ? "small-real-overflow/contextual" : "small-real-overflow/resolved";
        Fixture real(pair,std::string(reset)+defaults+"#a{overflow:auto}#d{height:80.001px}",contextual);
        auto frame = real.layout(); auto box = real.box(frame,"a");
        const float overflow = aether_layout_box_get_scrollable_overflow_y(box);
        check(overflow>0.f && overflow<.01f,active_case,"small real overflow retained");
        check(aether_layout_box_get_scrollbar_geometry(box).vertical_visible,active_case,"real overflow keeps scrollbar");
        check(chrome(frame),active_case,"real overflow keeps chrome command");
        if (snapshots) std::printf("CASE %s\n%s",active_case.c_str(),snapshot(frame).c_str());
        aether_layout_result_destroy(frame);
    }
}

void paint_hit_and_style_change() {
    for(bool contextual:{false,true}) {
        active_case=contextual?"paint/contextual":"paint/resolved";
        Fixture f("<body><div id='p'><div id='a'></div><div id='b'></div><div id='c'></div></div></body>",
            std::string(reset)+defaults+"#a,#b,#c{height:80px;width:100px;flex:0 0 80px;min-height:0}"
            "#a{background:#ff0000}#b{margin-top:-80px;background:#00ff00}#c{margin-top:-80px;background:#0000ff}",contextual);
        auto first=f.layout();auto original=snapshot(first);
        check(aether_layout_result_hit_test(first,5,5)==f.node("c"),active_case,"last overlapping column paints on top");
        f.add_css("#a,#b,#c{width:120px}");auto next=f.layout();
        check(snapshot(first)==original,active_case,"old snapshot retained after style update");
        number(f.rect(next,"c").width,120,active_case,"style update applied");
        aether_display_list_builder_t builder=nullptr;aether_display_list_t list=nullptr;
        require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"paint builder");
        require(aether_display_list_build(builder,first,nullptr,&list)==AETHER_OK,"paint list");
        std::string order;
        for(int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t cmd{};require(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"paint command");
            if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
                const auto c=cmd.data.fill_rounded_rect.color;
                if(c.r==1&&c.g==0&&c.b==0)order+='a';
                if(c.r==0&&c.g==1&&c.b==0)order+='b';
                if(c.r==0&&c.g==0&&c.b==1)order+='c';
            }aether_paint_command_free(&cmd);
        }
        check(order=="abc",active_case,"display list preserves DOM paint order");
        aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(first);aether_layout_result_destroy(next);
    }
}
}
int main(int argc,char** argv) {
    snapshots=argc>1&&std::strcmp(argv[1],"--snapshot")==0;
    matrix();scroll_metrics();margin_scroll_threshold();paint_hit_and_style_change();
    std::printf("flex-column-basis: %d fixtures, %d checks, %d failures\n",scenarios,checks,failures);
    return failures?1:0;
}
