// Public API regression; assertions remain enabled in Release/NDEBUG.
#include <aether/aether.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <initializer_list>

static int failures = 0;
static int checks = 0;

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Collect geometric failures so the base implementation exercises both axes and
// every relayout, rather than stopping at the first incorrect shrink result.
static void expect(float actual, float expected, const char* scenario, const char* field) {
    ++checks;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.001f) {
        std::fprintf(stderr, "FAIL: %s: %s = %g, expected %g (finite)\n",
                     scenario, field, actual, expected);
        ++failures;
    }
}

static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 0.f, 0.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t update_count = 0;

    Fixture(const char* html, const char* css, aether_text_measurer_fn measurer = measure_empty) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-definite-basis.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measurer, nullptr, &engine) == AETHER_OK, "create layout engine");
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t node(const char* id) const {
        const auto element = aether_doc_get_element_by_id(doc, id);
        require(element != nullptr, "find fixture element");
        return element;
    }

    aether_layout_result_t layout(float width, float height) const {
        aether_layout_result_t result = nullptr;
        require(aether_layout_layout(engine, doc, styles, width, height, nullptr, nullptr, &result)
                    == AETHER_OK && result != nullptr, "perform layout");
        return result;
    }

    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        const auto box = aether_layout_result_find_primary_box(result, node(id));
        require(box != nullptr, "find primary layout box");
        return aether_layout_box_get_border_box(box);
    }
};

static aether_text_metrics_t measure_text(const char* text,
                                         const aether_computed_style_t*, float, void*) {
    return {10.f * float(std::strlen(text)), 20.f, 15.f};
}


// These are arithmetic expectations, not snapshots of the implementation.
// Each fixture runs twice, retaining the first frame across a viewport change.
static int scenarios = 0;
static const char* reset =
    "*{margin:0;padding:0;border:0;box-sizing:content-box;}"
    "html,body,div{display:block;}body{width:400px;}"
    "#f{display:flex;width:200px;align-items:flex-start;align-content:flex-start;}"
    "#a,#b{flex:0 0 80px;min-width:0;height:20px;}#b{background:red;}";
static const char* pair = "<body><div id='f'><div id='a'></div><div id='b'></div></div></body>";
struct Expected { const char* id; float x,y,w,h; };
static void paint_hit(const Fixture& f,aether_layout_result_t r,const char* label) {
    const auto b=f.rect(r,"b");
    // B is deliberately empty, so its center selects the same node, including
    // after retained-frame relayout and with negative margins on its sibling.
    expect(aether_layout_result_hit_test(r,b.x+b.width/2,b.y+b.height/2)==f.node("b")?1.f:0.f,
           1,label,"B hit");
    aether_display_list_builder_t painter=nullptr;
    aether_display_list_t list=nullptr;
    require(aether_display_list_builder_create(measure_text,nullptr,&painter)==AETHER_OK,"create painter");
    require(aether_display_list_build(painter,r,nullptr,&list)==AETHER_OK,"paint retained frame");
    int matched=0;
    for(int i=0;i<aether_display_list_get_count(list);++i) {
        aether_paint_command_t c{};
        require(aether_display_list_get_command(list,i,&c)==AETHER_OK,"paint command");
        if(c.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& v=c.data.fill_rounded_rect;
            if(v.color.r>.99f&&v.color.g<.01f&&v.color.b<.01f) {
                ++matched;expect(v.rect.x,b.x,label,"paint x");expect(v.rect.y,b.y,label,"paint y");
                expect(v.rect.width,b.width,label,"paint width");expect(v.rect.height,b.height,label,"paint height");
            }
        }
        aether_paint_command_free(&c);
    }
    expect(float(matched),1,label,"one B background");
    aether_display_list_destroy(list);aether_display_list_builder_destroy(painter);
}
static void scenario(const char* label,const char* css,std::initializer_list<Expected> expected,
                     const char* html=pair) {
    ++scenarios;
    Fixture f(html,(std::string(reset)+css).c_str(),measure_text);
    auto first=f.layout(640,480),second=f.layout(800,600);
    for(auto r:{first,second}) {
        for(const auto& e:expected) {
            const auto v=f.rect(r,e.id);
            expect(v.x,e.x,label,(std::string(e.id)+" x").c_str());
            expect(v.y,e.y,label,(std::string(e.id)+" y").c_str());
            expect(v.width,e.w,label,(std::string(e.id)+" width").c_str());
            expect(v.height,e.h,label,(std::string(e.id)+" height").c_str());
        }
        paint_hit(f,r,label);
    }
    aether_layout_result_destroy(second);aether_layout_result_destroy(first);
}
static void bases_and_lines() {
    for(const char* basis:{"80px","40%","calc(0% + 80px)","calc(20% + 40px)","var(--basis)"}) {
        const std::string css=std::string("#f{--basis:80px;}#a{flex-basis:")+basis+";}";
        scenario(basis,css.c_str(),{{"a",0,0,80,20},{"b",80,0,80,20},{"f",0,0,200,20}});
    }
    scenario("contextual variable percentage", "#f{--basis:calc(20% + 40px)}#a{flex-basis:var(--basis)}",
             {{"a",0,0,80,20},{"b",80,0,80,20}});
    scenario("auto to specified width","#a{flex-basis:auto;width:80px;}",{{"a",0,0,80,20},{"b",80,0,80,20}});
    scenario("fractional grow","#a,#b{flex-grow:.25;}",{{"a",0,0,90,20},{"b",90,0,90,20}});
    scenario("positive grow","#a{flex-grow:1}#b{flex-grow:3}",{{"a",0,0,90,20},{"b",90,0,110,20}});
    scenario("shrink","#f{width:120px}#a,#b{flex-shrink:1}",{{"a",0,0,60,20},{"b",60,0,60,20}});
    scenario("fractional shrink","#f{width:120px}#a,#b{flex-shrink:.25}",{{"a",0,0,70,20},{"b",70,0,70,20}});
    scenario("nowrap overflow","#f{width:120px}",{{"a",0,0,80,20},{"b",80,0,80,20}});
    scenario("wrap exact gap threshold","#f{width:170px;flex-wrap:wrap;gap:10px}",{{"a",0,0,80,20},{"b",90,0,80,20},{"f",0,0,170,20}});
    scenario("wrap below gap threshold","#f{width:169px;flex-wrap:wrap;gap:10px}",{{"a",0,0,80,20},{"b",0,30,80,20},{"f",0,0,169,50}});
    scenario("column control","#f{flex-direction:column;height:200px}#a,#b{width:20px}",{{"a",0,0,20,80},{"b",0,80,20,80}});
    scenario("mixed content and definite","#a{flex-basis:content}",{{"a",0,0,60,20},{"b",60,0,80,20}},
             "<body><div id='f'><div id='a'>abcdef</div><div id='b'></div></div></body>");
}
static void edges_and_minima() {
    scenario("signed margins","#a{margin-left:-10px;margin-right:20px}",{{"a",-10,0,80,20},{"b",90,0,80,20}});
    scenario("percentage and calc margins","#a{margin-left:5%;margin-right:calc(5% + 5px)}",{{"a",10,0,80,20},{"b",105,0,80,20}});
    scenario("auto main margins","#a{margin-left:auto;margin-right:auto}",{{"a",20,0,80,20},{"b",120,0,80,20}});
    scenario("cross percent uses width","#a{margin-top:5%;margin-bottom:calc(5% + 5px)}",{{"a",0,10,80,20},{"b",80,0,80,20},{"f",0,0,200,45}});
    scenario("positive min","#a{min-width:100px}",{{"a",0,0,100,20},{"b",100,0,80,20}});
    scenario("percent min","#a{min-width:50%}",{{"a",0,0,100,20},{"b",100,0,80,20}});
    scenario("min wins max","#a{min-width:100px;max-width:50px}",{{"a",0,0,100,20},{"b",100,0,80,20}});
    scenario("max caps base","#a{max-width:50px}",{{"a",0,0,50,20},{"b",50,0,80,20}});
    const char* text="<body><div id='f'><div id='a'>abcdefghij</div><div id='b'></div></div></body>";
    // Runtime CSS rejects min-width:auto; genuine automatic minima are covered
    // by the companion immutable-map lifetime regression, not by this CSS fixture.
    scenario("explicit zero min with content","",{{"a",0,0,80,20},{"b",80,0,80,20}},text);
    scenario("specified width with overflowing content","#a{width:60px;flex-basis:auto}",{{"a",0,0,60,20},{"b",60,0,80,20}},text);
    scenario("max caps base with overflowing content","#a{max-width:50px}",{{"a",0,0,50,20},{"b",50,0,80,20}},text);
    for(const char* overflow:{"hidden","auto","scroll"}) {
        const std::string css=std::string("#a{overflow-x:")+overflow+";overflow-y:visible;}";
        scenario(overflow,css.c_str(),{{"a",0,0,80,20},{"b",80,0,80,20}},text);
    }
    scenario("content box decoration","#a{padding:5px;border:5px solid}",{{"a",0,0,100,40},{"b",100,0,80,20}});
    scenario("border box decoration","#a{box-sizing:border-box;padding:5px;border:5px solid}",{{"a",0,0,80,20},{"b",80,0,80,20}});
    scenario("raw base below decoration","#f{width:100px}#a{box-sizing:border-box;flex:1 1 0px;padding:20px;border:5px solid}#b{flex:1 1 0px}",
             {{"a",0,0,50,50},{"b",50,0,50,20}});
}
static void final_layout_contracts() {
    for(const char* align:{"flex-start","center","flex-end"}) {
        float y=std::strcmp(align,"center")==0?40:std::strcmp(align,"flex-end")==0?80:0;
        const std::string css=std::string("#f{height:100px;align-items:")+align+";}";
        scenario(align,css.c_str(),{{"a",0,y,80,20},{"b",80,y,80,20}});
    }
    scenario("stretch final pass","#f{height:100px;align-items:stretch}#a{height:auto}",{{"a",0,0,80,100},{"b",80,0,80,20}});
    scenario("multiple stretched lines","#f{width:100px;height:100px;flex-wrap:wrap;gap:10px;align-content:stretch;align-items:stretch}#a,#b{height:auto;min-height:20px}",
             {{"a",0,0,80,45},{"b",0,55,80,45}});
    scenario("self stretch override","#f{height:100px}#a{height:auto;align-self:stretch}",{{"a",0,0,80,100},{"b",80,0,80,20}});
    scenario("cross auto margins","#f{height:100px}#a{margin-top:auto;margin-bottom:auto}",{{"a",0,40,80,20},{"b",80,0,80,20}});
    scenario("min height auto container","#f{min-height:100px}",{{"a",0,0,80,20},{"b",80,0,80,20},{"f",0,0,200,100}});
    scenario("max height container","#f{max-height:10px}",{{"a",0,0,80,20},{"b",80,0,80,20},{"f",0,0,200,10}});
    const char* nested="<body><div id='f'><div id='a'><div id='c'></div></div><div id='b'></div></div></body>";
    scenario("definite child percent","#a{height:80px}#c{height:50%}",{{"a",0,0,80,80},{"c",0,0,80,40},{"b",80,0,80,20}},nested);
    scenario("min height is not definite","#a{height:auto;min-height:80px}#c{height:50%}",{{"a",0,0,80,80},{"c",0,0,80,0},{"b",80,0,80,20}},nested);
    scenario("relative item","#a{position:relative;left:10px;top:5px}",{{"a",10,5,80,20},{"b",80,0,80,20}});
    scenario("absolute child","#a{position:relative}#c{position:absolute;left:10px;top:5px;width:20px;height:10px}",
             {{"a",0,0,80,20},{"c",10,5,20,10},{"b",80,0,80,20}},nested);
    scenario("static absolute child","#a{position:relative}#c{position:absolute;width:20px;height:10px}",
             {{"a",0,0,80,20},{"c",0,0,20,10},{"b",80,0,80,20}},nested);
    scenario("fixed child","#c{position:fixed;left:10px;top:30px;width:20px;height:10px}",
             {{"a",0,0,80,20},{"c",10,30,20,10},{"b",80,0,80,20}},nested);
    scenario("anonymous text item","",{{"a",20,0,80,20},{"b",100,0,80,20}},
             "<body><div id='f'>ab<div id='a'></div><div id='b'></div></div></body>");
    scenario("control final layout","",{{"a",0,0,80,20},{"b",80,0,80,20}},
             "<body><div id='f'><input id='a' value='abcd'><div id='b'></div></div></body>");
    for(const char* display:{"table","table-row","table-cell"}) {
        const std::string css=std::string("#a{display:")+display+";}";
        scenario(display,css.c_str(),{{"a",0,0,80,20},{"b",80,0,80,20}});
    }
    scenario("table descendant final layout","#c{display:table;width:40px;height:10px}",
             {{"a",0,0,80,20},{"c",0,0,40,10},{"b",80,0,80,20}},nested);
}
int main() {
    bases_and_lines();edges_and_minima();final_layout_contracts();
    std::printf("flex-definite-basis: %d scenarios, %d checks, %d failures (NDEBUG-safe)\n",scenarios,checks,failures);
    return failures?1:0;
}
