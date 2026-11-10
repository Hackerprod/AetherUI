// Public geometry regression tests. The original R2 reproducer oracles and
// 0.001px tolerance are retained verbatim below. No internal layout APIs.
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
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-decoration.css", &sheet, &rules)
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


static void probe(const char* name, const char* descendant_style, float expected_a, float expected_b, float available=250, const char* root_style="", bool atomic=false) {
 std::string css="*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}body{display:flex;width:"+std::to_string(available)+"px;align-items:flex-start;}body>div{flex:0 1 content;min-width:0;height:20px;}#ca{width:200px;height:10px;"+std::string(descendant_style)+"}#cb{width:200px;height:10px;}#a{"+root_style+"}";
 if(atomic)css+="#ca{display:inline-block;}";
 Fixture f(atomic ? "<body><div id='a'><span id='ca'></span></div><div id='b'><div id='cb'></div></div></body>" : "<body><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>",css.c_str());
 auto r=f.layout(700,700); auto a=f.rect(r,"a"),b=f.rect(r,"b"),ca=f.rect(r,"ca");
 printf("%s: A=%g B=%g descendant=%g expected A=%g B=%g\n",name,a.width,b.width,ca.width,expected_a,expected_b);
 expect(a.width,expected_a,name,"A width");expect(b.width,expected_b,name,"B width");
 aether_layout_result_destroy(r);
}
static void sealed_descendant_cases(){
 probe("descendant maximum shrink","max-width:80px;",250.f*80/280,250.f*200/280);
 probe("descendant maximum no shrink","max-width:80px;",80,200,500);
 probe("descendant minimum shrink","width:50px;min-width:100px;",250.f/3,500.f/3);
 probe("descendant minimum no shrink","width:50px;min-width:100px;",100,200,500);
 probe("descendant min wins max","width:50px;min-width:100px;max-width:80px;",100,200,500);
 probe("atomic descendant maximum","max-width:80px;",80,200,500,"",true);
 probe("root maximum remains separate","",80,170,250,"max-width:80px;");
 probe("root minimum remains separate","width:50px;",100,200,300,"min-width:100px;");

}

static void table_probe(const char* name, const char* extra, float expected_a, float expected_second_x, bool atomic=false, bool nested=false) {
 std::string inside=atomic?"<span id='ca'></span>":"<div id='ca'></div>";
 if(nested) inside="<table id='nested'><tr><td id='nestedcell'>"+inside+"</td></tr></table>";
 std::string html="<body><div id='a'><table id='table'><tr><td id='cell'>"+inside+"</td><td id='second'><div id='cb'></div></td></tr></table></div></body>";
 std::string css="*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}table{display:table;}tr{display:table-row;}td{display:table-cell;}body{display:flex;width:500px;align-items:flex-start;}#a{flex:0 1 content;min-width:0;}#ca{width:200px;height:10px;}#cb{width:30px;height:10px;}";
 if(atomic)css+="#ca{display:inline-block;}";
 css+=extra;
 Fixture f(html.c_str(),css.c_str());auto r=f.layout(700,700);
 auto a=f.rect(r,"a"),t=f.rect(r,"table"),c=f.rect(r,"cell"),second=f.rect(r,"second"),desc=f.rect(r,"ca");
 printf("%s: A=%g table=%g cell=%g descendant=%g second_x=%g expected A=%g second_x=%g\n",name,a.width,t.width,c.width,desc.width,second.x-t.x,expected_a,expected_second_x);
 // These assert contribution and column placement, not CSS parity of the
 // table/cell's final border box, which has separate pre-existing constraints.
 expect(a.width,expected_a,name,"A contribution");
 expect(second.x-t.x,expected_second_x,name,"second column offset");
 aether_layout_result_destroy(r);
}
static void sealed_table_cases(){
 table_probe("guard table own maximum","#table{max-width:80px;}",230,200);
 table_probe("guard cell own maximum","#cell{max-width:80px;}",230,200);
 table_probe("guard table and cell maxima","#table,#cell{max-width:80px;}",230,200);
 table_probe("guard nested table maximum","#nested{max-width:80px;}",230,200,false,true);
 table_probe("ordinary block maximum inside cell","#ca{max-width:80px;}",110,80);
 table_probe("ordinary atomic maximum inside cell","#ca{max-width:80px;}",110,80,true);
 table_probe("ordinary minimum inside cell","#ca{width:50px;min-width:100px;}",130,100);
 table_probe("ordinary min wins max inside cell","#ca{width:50px;min-width:100px;max-width:80px;}",130,100);
 table_probe("normal maximum crosses nested table","#ca{max-width:80px;}",110,80,false,true);
 table_probe("local exclusion with descendant maximum","#table,#cell{max-width:40px;}#ca{max-width:80px;}",110,80);

}

static aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {5.f * static_cast<float>(std::strlen(text)), 10.f, 8.f};
}

static const char* reset = "*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}";

// Geometrical oracles distinguish the intrinsic contribution from final layout.
// Percentages are indefinite during measurement, but resolve against final A.
static void geometry(const char* name, const char* child_style, float contribution,
                     float child_border, float child_content, float left=0, float right=0,
                     float gap=0, float available=700, const char* root_style="",
                     const char* inner="<div id='leaf'></div>", float expected_b=200) {
    const std::string html="<body><div id='a'><div id='ca'>"+std::string(inner)+
        "</div></div><div id='b'><div id='cb'></div></div></body>";
    const std::string css=std::string(reset)+"body{display:flex;align-items:flex-start;width:"+
        std::to_string(available)+"px;gap:"+std::to_string(gap)+
        "px;}body>div{flex:0 1 content;min-width:0;}#ca{height:20px;"+child_style+
        "}#leaf{width:200px;height:10px;}#cb{width:200px;height:10px;}#a{"+root_style+"}";
    Fixture f(html.c_str(),css.c_str(),measure_text);
    auto r=f.layout(900,600);
    const auto a=f.rect(r,"a"), b=f.rect(r,"b"), c=f.rect(r,"ca");
    auto box=aether_layout_result_find_primary_box(r,f.node("ca"));
    const auto content=aether_layout_box_get_content_box(box);
    const auto margin=aether_layout_box_get_margin_box(box);
    expect(a.width,contribution,name,"A width");
    expect(b.width,expected_b,name,"B width");
    expect(b.x-a.x,contribution+gap,name,"sibling offset including gap");
    expect(c.width,child_border,name,"descendant border width");
    expect(content.width,child_content,name,"descendant content width");
    expect(c.x-a.x,left,name,"descendant border offset");
    expect(margin.x-a.x,0,name,"descendant margin origin");
    expect(margin.width,child_border+left+right,name,"signed margin width");
    aether_layout_result_destroy(r);
}

static void geometry_matrix() {
    for(const char* display:{"block","inline-block"}) {
        const std::string prefix=std::string("display:")+display+";";
        geometry((prefix+"auto max").c_str(),(prefix+"max-width:80px;").c_str(),80,80,80);
        geometry((prefix+"auto min").c_str(),(prefix+"min-width:240px;").c_str(),240,240,240);
        geometry((prefix+"defined min wins").c_str(),(prefix+"width:50px;min-width:100px;max-width:80px;").c_str(),100,100,100);
    }
    geometry("content-box max", "width:200px;max-width:80px;box-sizing:content-box;padding:10px;border:5px solid;",110,110,80);
    geometry("border-box max", "width:200px;max-width:80px;padding:10px;border:5px solid;",80,80,50);
    geometry("border-box decoration floor", "width:200px;max-width:10px;padding:10px;border:5px solid;",30,30,0);
    geometry("content-box min wins", "width:50px;min-width:100px;max-width:80px;box-sizing:content-box;padding:10px;border:5px solid;",130,130,100);
    geometry("fraction positive margins gap", "width:200px;max-width:80.25px;margin-left:7.5px;margin-right:2.25px;",90,80.25f,80.25f,7.5f,2.25f,3.75f);
    geometry("negative margins gap", "width:200px;max-width:80.25px;margin-left:-7.5px;margin-right:-2.25px;",70.5f,80.25f,80.25f,-7.5f,-2.25f,3.75f);
    geometry("percentage minimum indefinite", "width:50px;min-width:50%;",50,50,50);
    geometry("percentage maximum indefinite", "width:200px;max-width:50%;",200,100,100);
    geometry("calc minimum absolute retained", "width:50px;min-width:calc(50% + 80px);",80,120,120);
    geometry("calc maximum percentage indefinite", "width:200px;max-width:calc(50% + 80px);",200,180,180);
    geometry("calc zero percentage minimum", "width:50px;min-width:calc(0% + 80px);",80,80,80);
    geometry("calc zero percentage maximum indefinite", "width:200px;max-width:calc(0% + 80px);",200,80,80);
    geometry("calc fixed maximum definite", "width:200px;max-width:calc(80px);",80,80,80);
    geometry("auto basis no preferred width", "width:200px;max-width:80px;",80,80,80,0,0,0,700,"flex-basis:auto;");
    geometry("auto basis preferred width control", "width:200px;max-width:80px;",160,80,80,0,0,0,700,"flex-basis:auto;width:160px;");
    geometry("fixed basis control", "width:200px;max-width:80px;",160,80,80,0,0,0,700,"flex-basis:160px;");
    geometry("fractional shrink with gap", "width:200px;max-width:80.25px;", 250.5f*80.25f/280.25f,80.25f,80.25f,0,0,3.75f,254.25f,"","",250.5f*200.f/280.25f);
    geometry("decorated content-box shrink", "width:200px;max-width:80px;box-sizing:content-box;padding:10px;border:5px solid;",250.f*110.f/310.f,110,80,0,0,0,250,"","",250.f*200.f/310.f);
    geometry("zero preferred width decoration floor", "width:0;padding:10px;border:5px solid;",30,30,0);
    geometry("percent width remains content measured", "width:50%;max-width:80px;",80,40,40);
    geometry("calc zero percent width remains content measured", "width:calc(0% + 50px);max-width:80px;",80,50,50);
}

static void intrinsic_text_and_nested() {
    // Four letters, one space, eight letters: min-content 40, max-content 65.
    // A 1px flex container exposes the automatic min-content floor; the roomy
    // flex:content case exposes max-content. This uses supported public CSS
    // rather than assuming width:min-content/max-content keyword support.
    for(const char* mode:{"min-content","max-content"}) {
        const bool minimum=std::strcmp(mode,"min-content")==0;
        for(const char* display:{"block","inline-block","flex"}) {
            for(const char* limit:{"", "max-width:30px;", "min-width:80px;"}) {
                std::string css=std::string(reset)+(minimum?"body{display:flex;width:1px;}#a{flex:0 1 content;}":"body{display:flex;width:700px;}#a{flex:0 1 content;min-width:0;}")+"#ca{display:"+display+";"+limit+"}";
                const char* inner=std::strcmp(display,"flex")==0 ? "<div>aaaa aaaaaaaa</div>" : "aaaa aaaaaaaa";
                std::string html="<body><div id='a'><div id='ca'>"+std::string(inner)+"</div></div></body>";
                Fixture f(html.c_str(),css.c_str(),measure_text);auto r=f.layout(700,500);
                float expected=*limit ? (limit[1]=='a'?30.f:80.f) : (minimum?40.f:65.f);
                const std::string name=std::string(mode)+" "+display+" "+limit;
                expect(f.rect(r,"a").width,expected,name.c_str(),"intrinsic text width");
                aether_layout_result_destroy(r);
            }
        }
        const std::string css=std::string(reset)+(minimum?"body{display:flex;width:1px;}#a{flex:0 1 content;}":"body{display:flex;width:700px;}#a{flex:0 1 content;min-width:0;}")+"span{display:inline;min-width:200px;max-width:10px;}";
        Fixture f("<body><div id='a'><span>aaaa aaaaaaaa</span></div></body>",css.c_str(),measure_text);
        auto r=f.layout(700,500);expect(f.rect(r,"a").width,minimum?40.f:65.f,mode,"nonreplaced inline ignores limits");aether_layout_result_destroy(r);
    }
    geometry("nested flex sum and gap", "display:flex;gap:5px;",115,115,115,0,0,0,700,"",
             "<div style='width:200px;max-width:80px;flex-shrink:0;'></div><div style='width:30px;flex-shrink:0;'></div>");
}

// A direct flex item is blockified for sizing even when its frozen/computed
// display says inline. This must not change the ordinary inline controls above.
static void blockified_flex_items() {
    struct LimitCase { const char* css; float width; };
    const LimitCase limits[] = {
        {"width:200px;max-width:80px;",80},
        {"width:50px;min-width:100px;",100},
        {"width:50px;min-width:100px;max-width:80px;",100},
    };
    // Blockification also brings the content/border-box conversion and the
    // nonnegative content floor; merely honoring max-width is insufficient.
    struct DecoratedCase { const char* css; float border; float content; };
    const DecoratedCase decorated[] = {
        {"box-sizing:content-box;max-width:80px;",110,80},
        {"box-sizing:border-box;max-width:80px;",80,50},
        {"box-sizing:border-box;max-width:10px;",30,0},
        {"box-sizing:content-box;max-width:10px;",40,10},
    };
    for(const auto& c:decorated) {
        const std::string css=std::string(reset)+
            "body{display:flex;width:500px;}#a{display:flex;flex:0 1 content;min-width:0;}"
            "#ca{display:inline;width:200px;height:10px;flex-shrink:0;padding:10px;border:5px solid;"+
            c.css+"}#cb{width:30px;height:10px;flex-shrink:0;}";
        Fixture f("<body><div id='a'><span id='ca'></span><div id='cb'></div></div></body>",css.c_str());
        auto r=f.layout(700,500);
        const std::string name=std::string("blockified decorated inline ")+c.css;
        const auto box=aether_layout_result_find_primary_box(r,f.node("ca"));
        expect(f.rect(r,"a").width,c.border+30,name.c_str(),"intrinsic flex sum");
        expect(f.rect(r,"ca").width,c.border,name.c_str(),"item border width");
        expect(aether_layout_box_get_content_box(box).width,c.content,name.c_str(),"item content width");
        expect(f.rect(r,"cb").x-f.rect(r,"a").x,c.border,name.c_str(),"sibling offset");
        aether_layout_result_destroy(r);
    }
    for(const char* display:{"block","inline","inline-block"}) {
        for(const auto& limit:limits) {
            const std::string css=std::string(reset)+
                "body{display:flex;width:500px;}#a{display:flex;flex:0 1 content;min-width:0;}"
                "#ca{display:"+display+";height:10px;flex-shrink:0;"+limit.css+"}"
                "#cb{width:30px;height:10px;flex-shrink:0;}";
            Fixture f("<body><div id='a'><span id='ca'></span><div id='cb'></div></div></body>",css.c_str());
            auto r=f.layout(700,500);
            const std::string name=std::string("blockified flex item ")+display+" "+limit.css;
            expect(f.rect(r,"a").width,limit.width+30,name.c_str(),"intrinsic flex sum");
            expect(f.rect(r,"ca").width,limit.width,name.c_str(),"item final width");
            expect(f.rect(r,"cb").x-f.rect(r,"a").x,limit.width,name.c_str(),"sibling offset");
            aether_layout_result_destroy(r);
        }
        for(bool minimum:{true,false}) {
            for(const char* limit:{"", "max-width:30px;", "min-width:80px;", "min-width:80px;max-width:30px;"}) {
                const std::string css=std::string(reset)+
                    (minimum?"body{display:flex;width:1px;}#a{display:flex;flex:0 1 content;}":
                             "body{display:flex;width:700px;}#a{display:flex;flex:0 1 content;min-width:0;}")+
                    "#ca{display:"+display+";"+limit+"}";
                Fixture f("<body><div id='a'><span id='ca'>aaaa aaaaaaaa</span></div></body>",css.c_str(),measure_text);
                auto r=f.layout(700,500);
                const float expected=*limit ? (limit[1]=='a'?30.f:80.f) : (minimum?40.f:65.f);
                const std::string name=std::string("blockified text ")+display+(minimum?" min-content ":" max-content ")+limit;
                expect(f.rect(r,"a").width,expected,name.c_str(),"intrinsic text contribution");
                aether_layout_result_destroy(r);
            }
        }
    }
}

static void column_control() {
    // Column bases already measure real content at the definite cross width.
    const std::string css=std::string(reset)+
        "body{display:flex;flex-direction:column;width:300px;height:200px;align-items:flex-start;}"
        "body>div{flex:0 1 content;min-height:0;width:100px;}"
        "#ca{width:200px;max-width:80px;height:20px;}#cb{width:40px;height:30px;}";
    Fixture f("<body><div id='a'><div id='ca'></div></div><div id='b'><div id='cb'></div></div></body>",css.c_str());
    auto r=f.layout(700,500);
    expect(f.rect(r,"a").width,100,"column control","cross width");
    expect(f.rect(r,"a").height,20,"column control","content height");
    expect(f.rect(r,"ca").width,80,"column control","descendant cross maximum");
    expect(f.rect(r,"b").y-f.rect(r,"a").y,20,"column control","next item offset");
    expect(f.rect(r,"b").height,30,"column control","second content height");
    aether_layout_result_destroy(r);
}

static void table_scope_matrix() {
    // Preserve the pre-existing specified-width route for table/cell nodes.
    table_probe("table specified width remains separate", "#table{width:150px;max-width:80px;}",150,200);
    table_probe("cell specified width remains separate", "#cell{width:150px;max-width:80px;}",180,150);
    for(const char* limit:{"max-width:80px;", "max-width:50%;", "max-width:calc(50% + 80px);", "max-width:calc(0% + 80px);", "max-width:calc(80px);", "min-width:300px;", "min-width:50%;", "min-width:calc(50% + 80px);", "min-width:calc(0% + 80px);", "min-width:calc(80px);"}) {
        for(const char* target:{"#table", "#cell", "tr", "#table,#cell,tr"}) {
            const std::string name=std::string("local table exclusion ")+target+limit;
            // A table minimum expands final columns by 35px each; this is
            // existing table layout, separate from its 230px contribution.
            const float column = std::strcmp(limit,"min-width:300px;")==0 &&
                std::strstr(target,"#table") ? 235.f : 200.f;
            table_probe(name.c_str(),(std::string(target)+"{"+limit+"}").c_str(),230,column);
        }
    }
    for(bool css_table:{false,true}) for(bool nested:{false,true}) {
        const char* table=css_table?"div":"table";
        const char* row=css_table?"div":"tr";
        const char* cell=css_table?"div":"td";
        const std::string prefix=std::string("<")+table+" class='table'><"+row+" class='row'><"+cell+" class='cell'>";
        const std::string suffix=std::string("</")+cell+"></"+row+"></"+table+">";
        std::string inside="<div id='ca'></div>";
        if(nested)inside=prefix+inside+suffix;
        const std::string html="<body><div id='a'>"+prefix+inside+"</"+cell+"><"+cell+" class='cell' id='second'><div id='cb'></div>"+suffix+"</div></body>";
        const std::string css=std::string(reset)+"body{display:flex;width:500px;}#a{flex:0 1 content;min-width:0;}.table{display:table;max-width:40px;}.row{display:table-row;max-width:40px;}.cell{display:table-cell;max-width:40px;}#ca{width:200px;max-width:80px;height:10px;}#cb{width:30px;height:10px;}";
        Fixture f(html.c_str(),css.c_str());auto r=f.layout(700,500);
        const std::string name=std::string(css_table?"CSS table":"HTML table")+(nested?" nested":" direct");
        expect(f.rect(r,"a").width,110,name.c_str(),"ordinary descendant contribution");
        expect(f.rect(r,"second").x-f.rect(r,"a").x,80,name.c_str(),"second column position");
        aether_layout_result_destroy(r);
    }
    // Tag identity must not exempt table/td nodes with computed display:block.
    for(const char* tag:{"table","td"}) {
        const std::string html="<body><div id='a'><"+std::string(tag)+" id='ca'></"+tag+"></div></body>";
        const std::string css=std::string(reset)+"body{display:flex;width:500px;}#a{flex:0 1 content;min-width:0;}#ca{display:block;width:200px;max-width:80px;height:10px;}";
        Fixture f(html.c_str(),css.c_str());auto r=f.layout(700,500);
        expect(f.rect(r,"a").width,80,tag,"computed block receives ordinary clamp");aether_layout_result_destroy(r);
    }
    // Own table/cell limits preserve two DISTINCT intrinsic text contributions.
    for(const char* mode:{"min-content","max-content"}) for(const char* limit:{"", "max-width:10px;", "max-width:50%;", "max-width:calc(0% + 10px);", "min-width:300px;"}) {
        const bool minimum=std::strcmp(mode,"min-content")==0;
        for(bool descendant:{false,true}) {
            const std::string html="<body><div id='a'><table><tr><td><div id='ca'>aaaa aaaaaaaa</div></td><td><div style='width:30px;'></div></td></tr></table></div></body>";
            const std::string css=std::string(reset)+(minimum?"body{display:flex;width:1px;}#a{flex:0 1 content;}":"body{display:flex;width:700px;}#a{flex:0 1 content;min-width:0;}")+"table{display:table;}tr{display:table-row;}td{display:table-cell;}table,td{"+limit+"}"+(descendant?"#ca{max-width:30px;}":"");
            Fixture f(html.c_str(),css.c_str(),measure_text);auto r=f.layout(700,500);
            const std::string name=std::string("table text ")+mode+limit+(descendant?" descendant max":" own only");
            expect(f.rect(r,"a").width,descendant?60.f:(minimum?70.f:95.f),name.c_str(),"table intrinsic contribution");
            aether_layout_result_destroy(r);
        }
    }
}

int main() {
    sealed_descendant_cases();
    sealed_table_cases();
    geometry_matrix();
    intrinsic_text_and_nested();
    blockified_flex_items();
    column_control();
    table_scope_matrix();
    std::printf("%d checks; %d failures\n",checks,failures);
    return failures?1:0;
}
