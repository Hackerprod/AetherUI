// Public-ABI cyclic intrinsic edge regressions. NDEBUG-safe checks, 0.001px.
// Normative ordinary-box cases and existing table-policy controls are separate.
#include <aether/aether.h>

#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

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
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "cyclic-intrinsic-edges.css", &sheet, &rules)
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

static const char* reset = "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}";
static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {5.f * static_cast<float>(std::strlen(text)), 10.f, 8.f};
}

// All widths below are independently derived from the intrinsic contribution
// and the final containing block, never sampled from the implementation.
static void geometry(const char* name, const char* edge, float contribution,
                     float border, float content, float padding_left=0,
                     float margin_left=0, float margin_right=0, float available=500,
                     const char* root="", float root_padding=0, float root_margin=0,
                     float expected_b=100, float gap=0, const char* tag="div") {
    const std::string html="<body><div id='a'><"+std::string(tag)+" id='ca'></"+tag+
        "></div><div id='b'><div id='cb'></div></div></body>";
    const std::string css=std::string(reset)+"body{display:flex;align-items:flex-start;width:"+
        std::to_string(available)+"px;gap:"+std::to_string(gap)+"px;}body>div{flex:0 1 content;min-width:0;height:20px;}"
        "#ca{display:block;width:100px;height:10px;"+edge+"}#cb{width:100px;height:10px;}#a{"+root+"}";
    Fixture f(html.c_str(),css.c_str());
    auto r=f.layout(900,700);
    const auto a=f.rect(r,"a"), b=f.rect(r,"b"), c=f.rect(r,"ca");
    const auto cb=aether_layout_result_find_primary_box(r,f.node("ca"));
    const auto co=aether_layout_box_get_content_box(cb), m=aether_layout_box_get_margin_box(cb);
    expect(a.width,contribution,name,"A border width");
    expect(b.width,expected_b,name,"B border width");
    expect(b.x-a.x,contribution+root_margin+gap,name,"B relative x");
    expect(c.width,border,name,"C border width");
    expect(co.width,content,name,"C content width");
    expect(c.x-a.x,root_padding+margin_left,name,"C border relative x");
    expect(co.x-c.x,padding_left,name,"C content inset (border plus padding)");
    expect(m.x-a.x,root_padding,name,"C margin origin");
    expect(m.width,border+margin_left+margin_right,name,"C signed margin width");
    expect(c.height,10,name,"C border height");
    expect(co.height,10,name,"C content height");
    expect(m.height,10,name,"C margin height");
    expect(b.y,a.y,name,"siblings top aligned");
    aether_layout_result_destroy(r);
}

static void sealed_oracles() {
    // Preserve all 27 proposal oracles and add B plus content/margin rectangles.
    geometry("sealed padding 500","padding-left:10%;padding-right:10%;",100,120,100,10);
    geometry("sealed padding 800","padding-left:10%;padding-right:10%;",100,120,100,10,0,0,800);
    geometry("sealed margin 500","margin-left:10%;margin-right:10%;",100,100,100,0,10,10);
    geometry("sealed margin 800","margin-left:10%;margin-right:10%;",100,100,100,0,10,10,800);
    geometry("sealed padding calc","padding-left:calc(10% + 5px);padding-right:calc(10% + 5px);",110,132,100,16);
    geometry("sealed margin calc","margin-left:calc(10% + 5px);margin-right:calc(10% + 5px);",110,100,100,0,16,16);
    geometry("sealed root padding definite","",200,100,100,0,0,0,500,"padding-left:10%;padding-right:10%;",50);
    geometry("sealed fixed padding","padding-left:50px;padding-right:50px;",200,200,100,50);
    geometry("sealed fixed margins","margin-left:50px;margin-right:50px;",200,100,100,0,50,50);
}

static void ordinary_geometry_matrix() {
    for(const char* display:{"block","inline-block","flex"}) {
        const std::string prefix=std::string("display:")+display+";";
        geometry((prefix+"padding").c_str(),(prefix+"padding-left:10%;padding-right:10%;").c_str(),100,120,100,10);
        geometry((prefix+"margin").c_str(),(prefix+"margin-left:10%;margin-right:10%;").c_str(),100,100,100,0,10,10);
    }
    geometry("left padding only","padding-left:10%;",100,110,100,10);
    geometry("right padding only","padding-right:10%;",100,110,100);
    geometry("left margin only","margin-left:10%;",100,100,100,0,10);
    geometry("right margin only","margin-right:10%;",100,100,100,0,0,10);
    geometry("mixed fixed percentage padding","padding-left:10%;padding-right:7.5px;",107.5f,118.25f,100,10.75f);
    geometry("mixed fixed percentage margins","margin-left:10%;margin-right:7.5px;",107.5f,100,100,0,10.75f,7.5f);
    geometry("negative percentage margins","margin-left:-10%;margin-right:-5%;",100,100,100,0,-10,-5);
    geometry("signed calc margins","margin-left:calc(10% - 5px);margin-right:calc(10% - 5px);",90,100,100,0,4,4);
    geometry("negative calc padding clamps intrinsically","padding-left:calc(10% - 5px);padding-right:calc(10% - 5px);",100,110,100,5);
    geometry("negative calc padding clamps finally","padding-left:calc(1% - 5px);padding-right:calc(1% - 5px);",100,100,100);
    geometry("zero percent calc padding retains absolute","padding-left:calc(0% + 5px);padding-right:calc(0% + 5px);",110,110,100,5);
    geometry("zero percent calc margin retains absolute","margin-left:calc(0% + 5px);margin-right:calc(0% + 5px);",110,100,100,0,5,5);
    geometry("fixed calc padding","padding-left:calc(5px);padding-right:calc(5px);",110,110,100,5);
    geometry("auto margin","margin-left:auto;margin-right:auto;",100,100,100);
    geometry("em rem padding","font-size:10px;padding-left:1em;padding-right:1rem;",126,126,100,10);
    // Runtime viewport is 640px, while layout viewport is 900px. Viewport
    // lengths are resolved by the runtime before layout, unlike percentages.
    geometry("viewport padding","padding-left:1vw;padding-right:1vw;",112.8f,112.8f,100,6.4f);
    geometry("fractional shrink gap","padding-left:10%;padding-right:10%;",75.25f,115.05f,100,7.525f,0,0,154.25f,"",0,0,75.25f,3.75f);
    geometry("root content ignores preferred 200","padding-left:10%;padding-right:10%;",100,120,100,10,0,0,500,"width:200px;");
    geometry("root auto without width","padding-left:10%;padding-right:10%;",100,120,100,10,0,0,500,"flex-basis:auto;");
    geometry("root auto definite 200","padding-left:10%;padding-right:10%;",200,140,100,20,0,0,500,"flex-basis:auto;width:200px;");
    geometry("root fixed basis 200","padding-left:10%;padding-right:10%;",200,140,100,20,0,0,500,"flex-basis:200px;");
    geometry("root definite margins","",100,100,100,0,0,0,500,"margin-left:10%;margin-right:10%;",0,50);
    geometry("content-box maximum with cyclic padding","max-width:80px;padding-left:10%;padding-right:10%;",80,96,80,8);
    geometry("content-box min wins max with calc padding","width:50px;min-width:100px;max-width:80px;padding-left:calc(10% + 5px);padding-right:calc(10% + 5px);",110,132,100,16);
    geometry("border-box maximum with cyclic padding","box-sizing:border-box;max-width:80px;padding-left:10%;padding-right:10%;",80,80,64,8);
    geometry("border-box fixed decoration floor plus percent","box-sizing:border-box;width:0;border-left:7px solid;border-right:7px solid;padding-left:calc(10% + 5px);padding-right:calc(10% + 5px);",24,28.8f,0,14.4f);
    geometry("percentage width remains indefinite","width:50%;padding-left:10%;padding-right:10%;",0,0,0);
    // Tag identity is not the scope boundary; computed display is.
    for(const char* tag:{"table","td"})
        geometry(tag,"display:block;padding-left:10%;padding-right:10%;",100,120,100,10,0,0,500,"",0,0,100,0,tag);
}

static void text_modes_and_nested_flex() {
    // Deterministic 5px glyphs: max-content 65, min-content 40.
    for(bool minimum:{false,true}) for(const char* display:{"block","inline-block","flex"}) {
        const std::string css=std::string(reset)+(minimum?
            "body{display:flex;width:1px;}#a{flex:0 1 content;}":
            "body{display:flex;width:700px;}#a{flex:0 1 content;min-width:0;}")+
            "#ca{display:"+display+";padding-left:calc(10% + 5px);padding-right:calc(10% + 5px);}";
        const std::string inner=std::strcmp(display,"flex")==0 ? "<div>aaaa aaaaaaaa</div>" : "aaaa aaaaaaaa";
        Fixture f(("<body><div id='a'><div id='ca'>"+inner+"</div></div></body>").c_str(),css.c_str(),measure_text);
        auto r=f.layout(900,700);
        const std::string name=std::string(minimum?"min text ":"max text ")+display;
        expect(f.rect(r,"a").width,minimum?50:75,name.c_str(),"independent intrinsic mode");
        aether_layout_result_destroy(r);
    }
    for(const char* direction:{"row","column"}) for(const char* wrap:{"nowrap","wrap"}) for(const char* display:{"block","inline","inline-block"}) {
        const bool column=std::strcmp(direction,"column")==0;
        const std::string css=std::string(reset)+"body{display:flex;width:700px;}#a{display:flex;flex:0 1 content;min-width:0;flex-direction:"+
            direction+";flex-wrap:"+wrap+";gap:5px;}#ca{display:"+display+
            ";width:100px;height:10px;flex-shrink:0;padding-left:10%;padding-right:10%;}#cb{width:30px;height:10px;flex-shrink:0;}";
        Fixture f("<body><div id='a'><span id='ca'></span><div id='cb'></div></div></body>",css.c_str());
        auto r=f.layout(900,700);
        const std::string name=std::string("nested ")+direction+" "+wrap+" "+display;
        expect(f.rect(r,"a").width,column?100:135,name.c_str(),"intrinsic flex contribution");
        expect(f.rect(r,"ca").width,column?120:127,name.c_str(),"final flex child padding");
        expect(f.rect(r,"cb").width,30,name.c_str(),"nested sibling width");
        aether_layout_result_destroy(r);
    }
}

static void auto_width_and_anonymous_text() {
    for(const char* display:{"block","inline-block","flex"}) for(bool minimum:{false,true}) {
        const std::string css=std::string(reset)+(minimum?
            "body{display:flex;width:1px;}#a{flex:0 1 content;}":
            "body{display:flex;width:700px;}#a{flex:0 1 content;min-width:0;}")+
            "#ca{display:"+display+";padding-left:10%;padding-right:10%;}#leaf{width:100px;height:10px;flex-shrink:0;}";
        Fixture f("<body><div id='a'><div id='ca'><div id='leaf'></div></div></div></body>",css.c_str());
        auto r=f.layout(900,700);
        const std::string name=std::string("auto child ")+display+(minimum?" min":" max");
        expect(f.rect(r,"a").width,100,name.c_str(),"intrinsic auto contribution");
        expect(f.rect(r,"leaf").width,100,name.c_str(),"fixed leaf preserved");
        aether_layout_result_destroy(r);
    }
    for(bool minimum:{false,true}) for(const char* wrap:{"nowrap","wrap"}) {
        const std::string css=std::string(reset)+(minimum?
            "body{display:flex;width:1px;}#a{display:flex;flex:0 1 content;}":
            "body{display:flex;width:700px;}#a{display:flex;flex:0 1 content;min-width:0;}")+
            "#a{flex-wrap:"+wrap+";}#ca{width:100px;flex-shrink:0;padding-left:10%;padding-right:10%;}";
        Fixture f("<body><div id='a'>aaaa aaaaaaaa<div id='ca'></div></div></body>",css.c_str(),measure_text);
        auto r=f.layout(900,700);
        const float width=minimum?(std::strcmp(wrap,"wrap")==0?100:140):165;
        const std::string name=std::string("anonymous text ")+wrap+(minimum?" min":" max");
        expect(f.rect(r,"a").width,width,name.c_str(),"anonymous text plus cyclic child");
        aether_layout_result_destroy(r);
    }
}

static void definite_controls_and_resize() {
    const std::string css=std::string(reset)+"body{display:flex;flex-direction:column;width:200px;height:200px;align-items:flex-start;}#a{width:200px;flex:0 1 content;}#ca{width:100px;height:10px;padding-left:10%;padding-right:10%;}#b{width:100px;height:20px;}";
    Fixture f("<body><div id='a'><div id='ca'></div></div><div id='b'></div></body>",css.c_str());
    auto r=f.layout(900,700);
    expect(f.rect(r,"a").width,200,"definite column","cross width");
    expect(f.rect(r,"ca").width,140,"definite column","noncyclic padding");
    expect(f.rect(r,"a").height,10,"definite column","height unchanged");
    expect(f.rect(r,"b").y-f.rect(r,"a").y,10,"definite column","next item y");
    aether_layout_result_destroy(r);

    const std::string resizable=std::string(reset)+"body{display:flex;align-items:flex-start;}#a,#b{flex:0 1 content;min-width:0;}#ca{width:100px;height:10px;padding-left:10%;padding-right:10%;}#cb{width:100px;height:10px;}";
    Fixture resize("<body><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>",resizable.c_str());
    aether_layout_result_t retained=nullptr;
    for(float width:{500.f,800.f,500.f}) {
        auto frame=resize.layout(width,700);
        expect(resize.rect(frame,"a").width,100,"500-800-500 resize","A");
        expect(resize.rect(frame,"b").width,100,"500-800-500 resize","B");
        expect(resize.rect(frame,"ca").width,120,"500-800-500 resize","C");
        if(retained){expect(resize.rect(retained,"ca").width,120,"retained frame","C");aether_layout_result_destroy(frame);}
        else retained=frame;
    }
    aether_layout_result_destroy(retained);
}

// Column content bases call inline-height measurement with a definite width.
// Normal white-space reaches the legacy atomic branch; pre-wrap reaches the
// hanging resolver (ASCII atoms, no flattened decorations or block anchors).
// Own atomic edges keep the 100px basis in BOTH branches, whereas an auto
// atom's ordinary descendant is measured in a fresh intrinsic context.
static void inline_height_context_controls() {
    for(const char* whitespace:{"normal","pre-wrap"}) for(const char* edge:{"padding","margin"}) {
        const std::string css=std::string(reset)+"body{display:flex;flex-direction:column;width:100px;height:200px;align-items:flex-start;}#a{width:100px;flex:0 1 content;min-height:0;line-height:10px;white-space:"+
            whitespace+";}span{display:inline-block;width:40px;height:10px;"+edge+"-left:10%;"+edge+"-right:10%;}#b{width:100px;height:10px;flex:none;}";
        Fixture f("<body><div id='a'><span id='left'></span><span id='right'></span></div><div id='b'></div></body>",css.c_str(),measure_text);
        auto r=f.layout(900,700);
        const std::string name=std::string("inline height own edges ")+whitespace+" "+edge;
        expect(f.rect(r,"a").height,20,name.c_str(),"two-line content basis");
        expect(f.rect(r,"b").y-f.rect(r,"a").y,20,name.c_str(),"next column item y");
        expect(f.rect(r,"left").width,std::strcmp(edge,"padding")==0?60:40,name.c_str(),"defined own edge basis");
        expect(f.rect(r,"right").y-f.rect(r,"left").y,10,name.c_str(),"line wrap");
        aether_layout_result_destroy(r);
    }
    for(const char* whitespace:{"normal","pre-wrap"}) {
        const std::string css=std::string(reset)+"body{display:flex;flex-direction:column;width:100px;height:200px;align-items:flex-start;}#a{width:100px;flex:0 1 content;min-height:0;line-height:10px;white-space:"+
            whitespace+";}span{display:inline-block;padding-left:10%;padding-right:10%;}.leaf{width:30px;height:10px;padding-left:10%;padding-right:10%;}#b{width:100px;height:10px;flex:none;}";
        Fixture f("<body><div id='a'><span id='left'><div class='leaf' id='leaf'></div></span><span id='right'><div class='leaf'></div></span></div><div id='b'></div></body>",css.c_str(),measure_text);
        auto r=f.layout(900,700);
        const std::string name=std::string("inline height local descendant reset ")+whitespace;
        expect(f.rect(r,"a").height,10,name.c_str(),"two 50px atoms fit one line");
        expect(f.rect(r,"b").y-f.rect(r,"a").y,10,name.c_str(),"content basis excludes cyclic descendant edges");
        expect(f.rect(r,"left").width,50,name.c_str(),"own padding remains defined");
        expect(f.rect(r,"right").x-f.rect(r,"left").x,50,name.c_str(),"same line atom advance");
        expect(f.rect(r,"right").y-f.rect(r,"left").y,0,name.c_str(),"single line");
        expect(f.rect(r,"leaf").width,36,name.c_str(),"descendant resolves against final content 30");
        aether_layout_result_destroy(r);
    }
}

// NORMATIVE ordinary descendants inside tables: zero the descendant's cyclic
// edge basis locally even through nested tables. Do not assert CSS table parity.
static void ordinary_descendants_inside_tables() {
    for(bool css_table:{false,true}) for(bool nested:{false,true}) for(bool atomic:{false,true}) {
        const char* table=css_table?"div":"table";
        const char* row=css_table?"div":"tr";
        const char* cell=css_table?"div":"td";
        const std::string open=std::string("<")+table+" class='table'><"+row+" class='row'><"+cell+" class='cell'>";
        const std::string close=std::string("</")+cell+"></"+row+"></"+table+">";
        std::string inside="<div id='ca'></div>";
        if(nested)inside=open+inside+close;
        const std::string html="<body><div id='a'>"+open+inside+"</"+cell+"><"+cell+" class='cell' id='second'><div id='cb'></div>"+close+"</div><div id='b'><div style='width:100px;'></div></div></body>";
        const std::string css=std::string(reset)+"body{display:flex;width:500px;}#a,#b{flex:0 1 content;min-width:0;}.table{display:table;}.row{display:table-row;}.cell{display:table-cell;}#ca{width:100px;height:10px;padding-left:10%;padding-right:10%;display:"+(atomic?"inline-block":"block")+";}#cb{width:30px;height:10px;}";
        Fixture f(html.c_str(),css.c_str());auto r=f.layout(900,700);
        const std::string name=std::string(css_table?"computed table ":"HTML table ")+(nested?"nested ":"direct ")+(atomic?"atomic":"block");
        expect(f.rect(r,"a").width,130,name.c_str(),"ordinary descendant contribution");
        expect(f.rect(r,"second").x-f.rect(r,"a").x,100,name.c_str(),"second column offset");
        expect(f.rect(r,"b").width,100,name.c_str(),"B width");
        expect(f.rect(r,"b").x-f.rect(r,"a").x,130,name.c_str(),"B offset");
        aether_layout_result_destroy(r);
    }
}

// POLICY CONTROLS ONLY: these values intentionally freeze the pre-existing
// table-owned percentage edge behavior. They are NOT normative CSS oracles.
static void current_table_policy_controls() {
    for(const char* target:{"#table","#row","#cell"}) for(const char* edge:{"padding","margin"}) {
        const std::string css=std::string(reset)+"body{display:flex;width:500px;}#a,#b{flex:0 1 content;min-width:0;}#table{display:table;}#row{display:table-row;}#cell{display:table-cell;}#leaf,#cb{width:100px;height:10px;}"+
            target+"{"+edge+"-left:10%;"+edge+"-right:10%;}";
        Fixture f("<body><div id='a'><div id='table'><div id='row'><div id='cell'><div id='leaf'></div></div></div></div></div><div id='b'><div id='cb'></div></div></body>",css.c_str());auto r=f.layout(900,700);
        const std::string name=std::string("policy only computed ")+target+" "+edge;
        const float expected=std::strcmp(target,"#row")==0?100:200;
        expect(f.rect(r,"a").width,expected,name.c_str(),"unchanged own-edge contribution");
        expect(f.rect(r,"b").width,100,name.c_str(),"B width");
        expect(f.rect(r,"b").x-f.rect(r,"a").x,expected,name.c_str(),"B offset");
        aether_layout_result_destroy(r);
    }
}

// Combine the intentional table policy with the normative ordinary-box rule:
// exempting a table's own edges must never exempt its ordinary descendants.
static void local_table_exclusion_with_ordinary_edges() {
    for(const char* target:{"#table","#row","#cell"}) for(const char* edge:{"padding","margin"}) {
        const std::string css=std::string(reset)+"body{display:flex;width:500px;}#a,#b{flex:0 1 content;min-width:0;}#table{display:table;}#row{display:table-row;}#cell,#second{display:table-cell;}#leaf{width:100px;height:10px;"+
            edge+"-left:10%;"+edge+"-right:10%;}#other{width:30px;height:10px;}#cb{width:100px;}"+
            target+"{padding-left:10%;padding-right:10%;}";
        Fixture f("<body><div id='a'><div id='table'><div id='row'><div id='cell'><div id='leaf'></div></div><div id='second'><div id='other'></div></div></div></div></div><div id='b'><div id='cb'></div></div></body>",css.c_str());
        auto r=f.layout(900,700);
        const std::string name=std::string("local table policy plus ordinary ")+target+" "+edge;
        const float expected=std::strcmp(target,"#row")==0?130:230;
        expect(f.rect(r,"a").width,expected,name.c_str(),"local exclusion contribution");
        expect(f.rect(r,"b").width,100,name.c_str(),"B width");
        expect(f.rect(r,"b").x-f.rect(r,"a").x,expected,name.c_str(),"B offset");
        aether_layout_result_destroy(r);
    }
}

int main() {
    sealed_oracles();
    ordinary_geometry_matrix();
    text_modes_and_nested_flex();
    auto_width_and_anonymous_text();
    definite_controls_and_resize();
    inline_height_context_controls();
    ordinary_descendants_inside_tables();
    current_table_policy_controls();
    local_table_exclusion_with_ordinary_edges();
    std::printf("cyclic intrinsic edges: %d checks; %d failures\n",checks,failures);
    return failures?1:0;
}
