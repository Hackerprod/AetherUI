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
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-wrap-intrinsic.css", &sheet, &rules)
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


// Expected values derive from pinned Blink source, not a browser execution.
// Source: flex_layout_algorithm.cc at f7daeeb80087f64ee108b65a5f106b603e9ddd28,
// lines 2980-3050. min = max of natural signed minima; max = max(min,signed
// sum of selected max contributions plus all gaps,0).
static int scenarios = 0;
static const char* html = "<body><div id='inner'><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></div><div id='after'></div></body>";
static const char* reset = "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}body{display:flex;width:800px;align-items:flex-start;}#inner{display:flex;flex:0 1 content;flex-wrap:wrap;align-items:flex-start;align-content:flex-start;}#a,#b{height:20px;}#ca{width:100px;height:10px;}#cb{width:200px;height:10px;}#after{flex:0 0 10px;height:10px;}";
static void scenario(const char* name,const char* extra,float minimum,float maximum,const char* markup=html,aether_text_measurer_fn measure=measure_empty) {
    ++scenarios;
    for(bool narrow:{false,true}) {
        const std::string css=std::string(reset)+extra+(narrow?"body{width:0;}":"");
        Fixture f(markup,css.c_str(),measure);
        auto first=f.layout(900,700), next=f.layout(1100,700);
        const float expected=narrow?minimum:maximum;
        for(auto r:{first,next}) {
            auto root=f.rect(r,"inner"),a=f.rect(r,"a"),b=f.rect(r,"b");

            expect(root.width,expected,name,narrow?"min-content":"max-content");
            expect(f.rect(r,"after").x-root.x,expected,name,"following sibling");
        }
        expect(f.rect(first,"inner").width,expected,name,"retained result");
        aether_layout_result_destroy(next);aether_layout_result_destroy(first);
    }
}
static void original_matrix(){
    scenario("max floor from natural min","#a,#b{flex:0 0 50px;min-width:0}",200,200);
    scenario("no shrink larger bases","#a,#b{flex:1 0 250px;min-width:0}",200,500);
    scenario("mixed fixed and auto","#a{flex:0 0 50px;min-width:0}#b{flex:0 0 auto}",200,250);
    scenario("gap below minimum floor","#a,#b{flex:0 0 50px;min-width:0}#inner{gap:10px}",200,200);
    scenario("gap beyond minimum floor","#a,#b{flex:0 0 50px;min-width:0}#inner{gap:150px}",200,250);
    scenario("minimum maximum clamp","#a,#b{flex:0 0 50px;min-width:70px;max-width:80px}",80,140);
    scenario("content-box decoration","#a,#b{flex:0 0 150px;min-width:0;padding:10px;border:2px solid;margin:5px}",234,368);
    scenario("border-box decoration","#a,#b{flex:0 0 150px;min-width:0;box-sizing:border-box;padding:10px;border:2px solid;margin:5px}",234,320);
    scenario("signed negative outer","#a,#b{flex:0 0 auto;min-width:0}#a{margin-left:-150px}#inner{gap:100px}",200,250);
    scenario("signed sum min floor","#a,#b{flex:0 0 auto;min-width:0}#a{margin-left:-150px}",200,200);
    scenario("all negative with compensating gap","#a,#b{flex:0 0 auto;min-width:0;margin-left:-250px}#inner{gap:300px}",0,100);
    scenario("root decoration after floors","#a,#b{flex:0 0 50px;min-width:0}#inner{padding:10px;border:2px solid}",224,224);
    scenario("fractional bases","#a,#b{flex:0 0 150.25px;min-width:0}",200,300.5f);
    scenario("grow shrink allowed control","#a,#b{flex:1 1 50px;min-width:0}",200,300);
    scenario("percentage basis control","#a,#b{flex:0 0 50%;min-width:0}",200,300);
    scenario("calc dependency control","#a,#b{flex:0 0 calc(0% + 50px);min-width:0}",200,300);
    scenario("auto basis control","#a,#b{flex:0 0 auto}",200,300);
    scenario("automatic minimum control","#a,#b{flex:0 0 50px}",200,300);
    scenario("overflow automatic minimum","#a,#b{flex:0 0 50px;overflow:hidden}",200,200);
    scenario("nowrap control","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-wrap:nowrap}",100,100);
    scenario("column control","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-direction:column}",200,200);
    const char* text="<body><div id='inner'><div id='a'>aaaa bb cc</div><div id='b'>aaaa bb cc</div></div><div id='after'></div></body>";
    scenario("text inflexible","#a,#b{flex:0 0 60px;min-width:0}",40,120,text,measure_text);
    scenario("text no grow","#a,#b{flex:0 1 60px;min-width:0}",40,120,text,measure_text);
    scenario("text no shrink","#a,#b{flex:1 0 150px;min-width:0}",40,300,text,measure_text);
    std::printf("SUMMARY: %d scenarios, %d checks, %d failures\n",scenarios,checks,failures);
}

static void style_matrix() {
    scenario("review minimum beats maximum", "#a,#b{flex:0 0 50px;min-width:70px;max-width:60px}",70,140);
    scenario("review percentage min max", "#a,#b{flex:0 0 60px;min-width:50%;max-width:50%}",200,200);
    scenario("review calc min max", "#a,#b{flex:0 0 60px;min-width:calc(0% + 90px);max-width:calc(0% + 10px)}",200,200);
    scenario("review specified width below content", "#a,#b{flex:0 0 60px;width:40px;min-width:0}",40,120);
    scenario("review specified width auto minimum", "#a,#b{flex:0 0 60px;width:40px}",40,120);
    scenario("review specified width floor", "body{width:2000px}#a,#b{flex:0 0 60px;width:500px;min-width:0}",500,500);
    scenario("review auto definite width equality", "#a,#b{flex:0 0 auto;width:60px;min-width:0}",60,120);
    scenario("review content definite width", "#a,#b{flex:0 0 content;width:60px;min-width:0}",60,120);
    scenario("review auto margins", "#a,#b{flex:0 0 60px;min-width:0;margin:auto}",200,200);
    scenario("review calc cyclic decoration", "#a,#b{flex:0 0 60px;min-width:0;padding:calc(10% + 3px);border:2px solid;margin:calc(5% + 2px)}",214,214);
    scenario("review percent gap", "#a,#b{flex:0 0 60px;min-width:0}#inner{gap:10%}",200,200);
    scenario("review calc gap", "#a,#b{flex:0 0 60px;min-width:0}#inner{gap:calc(10% + 120px)}",200,240);
    scenario("review table no selection", "#a{display:table;width:150px;flex:0 0 50px;min-width:0}#b{flex:0 0 50px;min-width:0}",200,200);
    // The natural table contribution must be observable above the min floor:
    // 150 + selected non-table H250 = 400, not 50 + 250 = 300.
    for(const char* display : {"table","table-row","table-cell"}) {
        const std::string css=std::string("#a{display:")+display+
            ";width:150px;flex:0 0 50px;min-width:0}#b{flex:1 0 250px;min-width:0}";
        scenario(display,css.c_str(),200,400);
        scenario((std::string(display)+" signed outside").c_str(),
                 (css+"#a{margin-left:-170px}#inner{gap:100px}").c_str(),200,330);
    }
    const char* nested="<body><div id='inner'><div id='a'><div id='nested'><span class='leaf'>aaaa bb cc</span><span class='leaf'>aaaa bb cc</span></div></div><div id='b'>aaaa bb cc</div></div><div id='after'></div></body>";
    const char* nested_base="#a,#b{flex:0 0 auto;min-width:0}#nested{display:flex;flex-wrap:wrap}.leaf{min-width:0;flex:0 0 60px}";
    scenario("mixed wrap subtree",nested_base,40,220,nested,measure_text);
    scenario("mixed nowrap subtree",(std::string(nested_base)+"#nested{flex-wrap:nowrap}").c_str(),120,220,nested,measure_text);
    scenario("mixed column subtree",(std::string(nested_base)+"#nested{flex-direction:column}").c_str(),40,200,nested,measure_text);
    scenario("nested H must not replace natural minimum",(std::string(nested_base)+"#a{flex:0 0 60px}").c_str(),40,160,nested,measure_text);
    scenario("nested nowrap natural minimum floor",(std::string(nested_base)+"#a{flex:0 0 60px}#nested{flex-wrap:nowrap}").c_str(),120,160,nested,measure_text);
    const char* text="<body><div id='inner'><div id='a'>aaaa bb cc</div><div id='b'>aaaa bb cc</div></div><div id='after'></div></body>";
    scenario("review tiny grow", "#a,#b{flex:0.000001 1 60px;min-width:0}",40,200,text,measure_text);
    scenario("review tiny shrink", "#a,#b{flex:1 0.000001 150px;min-width:0}",40,200,text,measure_text);
    scenario("review equal base", "#a,#b{flex:0 0 100px;min-width:0}",40,200,text,measure_text);
    scenario("review overflow auto", "#a,#b{flex:0 0 20px;overflow:auto}",40,40,text,measure_text);
    scenario("review overflow clip", "#a,#b{flex:0 0 20px;overflow:clip}",40,80,text,measure_text);
    const char* inline_html="<body><div id='inner'><span id='a'>aaaa bb cc</span><span id='b'>aaaa bb cc</span></div><div id='after'></div></body>";
    scenario("review inline blockified", "#a,#b{display:inline;flex:0 0 60px;min-width:0}",40,120,inline_html,measure_text);
    const char* anon_html="<body><div id='inner'>x<!--comment--><div style='display:none;width:900px'></div><div id='a'>aaaa bb cc</div><div style='position:absolute;width:800px'></div><div id='b'>aaaa bb cc</div><div style='position:fixed;width:700px'></div>   </div><div id='after'></div></body>";
    scenario("review anonymous excluded", "#a,#b{flex:0 0 60px;min-width:0}",40,130,anon_html,measure_text);
}

struct Geometry {
    float width, height, ax, ay, aw, ah, bx, by, bw, bh;
};

static void geometry(const char* name, const char* extra, Geometry wide, Geometry narrow,
                     const char* markup=html, aether_text_measurer_fn measure=measure_empty) {
    for (bool minimum : {false, true}) {
        const std::string css=std::string(reset)+extra+
            "#a{background:#ff0000;}#b{background:#0000ff;}"+
            (minimum?"body{width:0;}":"");
        Fixture f(markup,css.c_str(),measure);
        const Geometry g=minimum?narrow:wide;
        auto first=f.layout(900,700), second=f.layout(1100,700);
        for (auto result : {first,second}) {
            const auto root=f.rect(result,"inner"), a=f.rect(result,"a"), b=f.rect(result,"b");
            expect(root.width,g.width,name,"root width");
            expect(root.height,g.height,name,"root height / line count");
            expect(a.x-root.x,g.ax,name,"a x"); expect(a.y-root.y,g.ay,name,"a y");
            expect(a.width,g.aw,name,"a width"); expect(a.height,g.ah,name,"a height");
            expect(b.x-root.x,g.bx,name,"b x / actual gap");
            expect(b.y-root.y,g.by,name,"b y / actual line gap");
            expect(b.width,g.bw,name,"b width"); expect(b.height,g.bh,name,"b height");
            expect(a.x+a.width-root.x,g.ax+g.aw,name,"a overflow edge");
            expect(b.x+b.width-root.x,g.bx+g.bw,name,"b overflow edge");
            expect(f.rect(result,"after").x-root.x,g.width,name,"following sibling x");
            expect(f.rect(result,"after").y-root.y,0,name,"following sibling y");
            aether_display_list_builder_t builder=nullptr;
            require(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,
                    "create paint builder");
            aether_display_list_t list=nullptr;
            require(aether_display_list_build(builder,result,nullptr,&list)==AETHER_OK,"paint frame");
            int colored=0;
            for(int i=0;i<aether_display_list_get_count(list);++i) {
                aether_paint_command_t cmd{};
                require(aether_display_list_get_command(list,i,&cmd)==AETHER_OK,"read paint command");
                if(cmd.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
                    const auto& fill=cmd.data.fill_rounded_rect;
                    const bool red=fill.color.r>.99f && fill.color.g<.01f && fill.color.b<.01f;
                    const bool blue=fill.color.b>.99f && fill.color.r<.01f && fill.color.g<.01f;
                    if(red || blue) {
                        ++colored; const auto expected=red?a:b;
                        expect(fill.rect.x,expected.x,name,"paint x");
                        expect(fill.rect.y,expected.y,name,"paint y");
                        expect(fill.rect.width,expected.width,name,"paint width");
                        expect(fill.rect.height,expected.height,name,"paint height");
                    }
                }
                aether_paint_command_free(&cmd);
            }
            expect(float(colored),2,name,"both item backgrounds painted");
            aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
            // The descendants occupy only the upper 10px. Probe a lower point
            // within the root, so neither descendant nor overflow clipping wins.
            for (const char* id : {"a","b"}) {
                const auto item=f.rect(result,id);
                const float x=item.x+item.width-1, y=item.y+item.height-1;
                if(x>=root.x && x<root.x+root.width && y>=root.y && y<root.y+root.height)
                    expect(aether_layout_result_hit_test(result,x,y)==f.node(id)?1.f:0.f,
                           1,name,"hit resolves item in newly sized region");
            }
        }
        expect(f.rect(first,"inner").height,g.height,name,"retained height");
        expect(f.rect(first,"b").y-f.rect(first,"inner").y,g.by,name,"retained line position");
        aether_layout_result_destroy(second); aether_layout_result_destroy(first);
    }
}
static void geometry_matrix() {
    const char* text="<body><div id='inner'><div id='a'>aaaa bb cc</div><div id='b'>aaaa bb cc</div></div><div id='after'></div></body>";
    geometry("text min stays below hypothetical60","#a,#b{flex:0 0 60px;min-width:0}",
             {120,20,0,0,60,20,60,0,60,20},{40,40,0,0,60,20,0,20,60,20},text,measure_text);
    geometry("text shrink still retains natural min","#a,#b{flex:0 1 60px;min-width:0}",
             {120,20,0,0,60,20,60,0,60,20},{40,40,0,0,40,20,0,20,40,20},text,measure_text);
    geometry("text min stays below hypothetical150","#a,#b{flex:1 0 150px;min-width:0}",
             {300,20,0,0,150,20,150,0,150,20},{40,40,0,0,150,20,0,20,150,20},text,measure_text);
    geometry("content-box child decoration","#a,#b{flex:0 0 150px;min-width:0;padding:10px;border:2px solid;margin:5px}",
             {368,54,5,5,174,44,189,5,174,44},{234,108,5,5,174,44,5,59,174,44});
    geometry("border-box child decoration","#a,#b{flex:0 0 150px;min-width:0;box-sizing:border-box;padding:10px;border:2px solid;margin:5px}",
             {320,34,5,5,150,24,165,5,150,24},{234,68,5,5,150,24,5,39,150,24});

    geometry("natural minimum floor","#a,#b{flex:0 0 50px;min-width:0}",
             {200,20,0,0,50,20,50,0,50,20},{200,20,0,0,50,20,50,0,50,20});
    geometry("large bases one line vs overflow lines","#a,#b{flex:1 0 250px;min-width:0}",
             {500,20,0,0,250,20,250,0,250,20},{200,40,0,0,250,20,0,20,250,20});
    geometry("mixed selection wraps only at minimum","#a{flex:0 0 50px;min-width:0}#b{flex:0 0 auto}",
             {250,20,0,0,50,20,50,0,200,20},{200,40,0,0,50,20,0,20,200,20});
    geometry("small gap","#a,#b{flex:0 0 50px;min-width:0}#inner{gap:10px}",
             {200,20,0,0,50,20,60,0,50,20},{200,20,0,0,50,20,60,0,50,20});
    geometry("large gap","#a,#b{flex:0 0 50px;min-width:0}#inner{gap:150px}",
             {250,20,0,0,50,20,200,0,50,20},{200,190,0,0,50,20,0,170,50,20});
    geometry("clamps","#a,#b{flex:0 0 50px;min-width:70px;max-width:80px}",
             {140,20,0,0,70,20,70,0,70,20},{80,40,0,0,70,20,0,20,70,20});
    geometry("root decoration once","#a,#b{flex:0 0 50px;min-width:0}#inner{padding:10px;border:2px solid}",
             {224,44,12,12,50,20,62,12,50,20},{224,44,12,12,50,20,62,12,50,20});
    geometry("fractional bases","#a,#b{flex:0 0 150.25px;min-width:0}",
             {300.5f,20,0,0,150.25f,20,150.25f,0,150.25f,20},
             {200,40,0,0,150.25f,20,0,20,150.25f,20});
    geometry("signed negative exterior","#a,#b{flex:0 0 auto;min-width:0}#a{margin-left:-150px}#inner{gap:100px}",
             {250,20,-150,0,100,20,50,0,200,20},{200,140,-150,0,100,20,0,120,200,20});
    geometry("negative items compensated only by gap","#a,#b{flex:0 0 auto;min-width:0;margin-left:-250px}#inner{gap:300px}",
             {100,20,-250,0,100,20,-100,0,200,20},{0,340,-250,0,100,20,-250,320,200,20});
    geometry("signed sum floor","#a,#b{flex:0 0 auto;min-width:0}#a{margin-left:-150px}",
             {200,20,-150,0,100,20,-50,0,200,20},{200,20,-150,0,100,20,-50,0,200,20});
    geometry("nowrap invariant","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-wrap:nowrap}",
             {100,20,0,0,50,20,50,0,50,20},{100,20,0,0,50,20,50,0,50,20});
    geometry("column invariant","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-direction:column}",
             {200,100,0,0,100,50,0,50,200,50},{200,100,0,0,100,50,0,50,200,50});
}
static void empty_and_singleton() {
    struct Case { const char* name; const char* content; const char* style; float minimum,maximum,height; };
    const Case cases[]={
        {"empty","","#inner{gap:300px}",0,0,0},
        {"whitespace comment","   <!-- no flex item -->   ","#inner{gap:300px}",0,0,0},
        {"excluded only","<div style='display:none;width:900px'></div><div style='position:absolute;width:800px'></div><div style='position:fixed;width:700px'></div>","#inner{gap:300px}",0,0,0},
        {"single natural floor","<div id='a'><div id='ca'></div></div>","#a{flex:0 0 50px;min-width:0}#inner{gap:300px}",100,100,20},
        {"single text Cmin below H","<div id='a'>aaaa bb cc</div>","#a{flex:0 0 60px;min-width:0}#inner{gap:300px}",40,60,20},
        {"single negative exterior","<div id='a'><div id='ca'></div></div>","#a{flex:0 0 auto;min-width:0;margin-left:-150px}#inner{gap:300px}",0,0,20}
    };
    for(const auto& c:cases) for(bool minimum:{false,true}) {
        const std::string markup="<body><div id='inner'>"+std::string(c.content)+"</div><div id='after'></div></body>";
        const std::string css=std::string(reset)+c.style+(minimum?"body{width:0;}":"");
        Fixture f(markup.c_str(),css.c_str(),measure_text);
        for(float viewport:{900.f,1100.f}) {
            auto result=f.layout(viewport,700);
            const auto root=f.rect(result,"inner");
            expect(root.width,minimum?c.minimum:c.maximum,c.name,"no phantom items or gaps");
            expect(root.height,c.height,c.name,"empty/singleton height");
            expect(f.rect(result,"after").x-root.x,minimum?c.minimum:c.maximum,c.name,"following sibling");
            aether_layout_result_destroy(result);
        }
    }
}
int main() {
    original_matrix(); style_matrix(); geometry_matrix(); empty_and_singleton();
    std::printf("flex wrap intrinsic: %d scenarios, %d checks, %d failures\n",scenarios,checks,failures);
    return failures?1:0;
}
