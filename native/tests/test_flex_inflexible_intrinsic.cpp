// Public-header-only regression: checks remain active in NDEBUG builds.
// Expectations are derived from Blink source, not browser observations:
// https://github.com/chromium/chromium/blob/f7daeeb80087f64ee108b65a5f106b603e9ddd28/third_party/blink/renderer/core/layout/flex/flex_layout_algorithm.cc#L2918-L3054
// Scope: definite bases in horizontal nowrap rows, plus signed outer sums.
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
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-inflexible-intrinsic.css", &sheet, &rules)
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

static const char* pair_html =
    "<body><div id='inner'><div id='a'><div id='ca'></div></div>"
    "<div id='b'><div id='cb'></div></div></div><div id='after'></div></body>";
static const char* reset =
    "*{margin:0;padding:0;border:0;box-sizing:content-box;}"
    "html,body,div{display:block;}"
    "body{display:flex;width:800px;align-items:flex-start;}"
    "#inner{display:flex;flex:0 0 content;align-items:flex-start;}"
    "#a,#b{height:20px;}#ca{width:100px;height:10px;}"
    "#cb{width:200px;height:10px;}#after{flex:0 0 10px;height:10px;}";

// All coordinates below are relative to the container border box. Check the
// trailing sibling as well: it exposes an incorrectly sized intrinsic container.
static void pair(const char* name, const char* extra, float width, float aw,
                 float bw, float ax, float bx, float ay=0, float by=0) {
    const std::string css=std::string(reset)+extra;
    Fixture f(pair_html,css.c_str());
    auto first=f.layout(900,700);
    auto next=f.layout(1100,700);
    for(auto result : {first,next}) {
        auto inner=f.rect(result,"inner"), a=f.rect(result,"a"), b=f.rect(result,"b");
        expect(inner.width,width,name,"container width");
        expect(a.width,aw,name,"first width");
        expect(b.width,bw,name,"second width");
        expect(a.x-inner.x,ax,name,"first x");
        expect(b.x-inner.x,bx,name,"second x / gap");
        expect(a.y-inner.y,ay,name,"first y");
        expect(b.y-inner.y,by,name,"second y");
        expect(f.rect(result,"after").x-inner.x,width,name,"following sibling x");
        expect(a.x+a.width-inner.x,ax+aw,name,"first right / overflow");
        expect(b.x+b.width-inner.x,bx+bw,name,"second right / overflow");
    }
    expect(f.rect(first,"inner").width,width,name,"retained result");
    aether_layout_result_destroy(next);aether_layout_result_destroy(first);
}

// "aaaa bb cc" gives raw min-content=40 and max-content=100. A zero-width
// parent forces the container down to its automatic min-content minimum;
// the wide parent independently observes max-content. This distinguishes the
// two C/H decisions rather than merely repeating max-content checks.
static void text_case(const char* name,const char* item_css,float minimum,float maximum,
                      const char* content="<div id='a'>aaaa bb cc</div>",
                      const char* inner_css="", bool automatic_minimum=false) {
    for(bool narrow : {false,true}) {
        const std::string css=std::string(reset)+
            "body{width:"+(narrow?std::string("0"):std::string("800"))+"px;}"
            "#inner{flex:0 1 content;}#a{height:auto;flex-basis:60px;}"
            +(automatic_minimum?std::string():std::string("#a{min-width:0;}"))+
            "#a{"+item_css+"}"+inner_css;
        const std::string html="<body><div id='inner'>"+std::string(content)+"</div>"
            "<div id='after'></div></body>";
        Fixture f(html.c_str(),css.c_str(),measure_text);
        auto first=f.layout(900,700), next=f.layout(1100,700);
        const float expected=narrow?minimum:maximum;
        for(auto result : {first,next}) {
            auto inner=f.rect(result,"inner");
            expect(inner.width,expected,name,narrow?"min-content under shrink":"max-content");
            expect(f.rect(result,"after").x-inner.x,expected,name,"following sibling");
        }
        expect(f.rect(first,"inner").width,expected,name,"retained min/max result");
        aether_layout_result_destroy(next);aether_layout_result_destroy(first);
    }
}

static void additional_selection_cases() {
    text_case("raw automatic minimum with specified width","flex:0 0 20px;width:80px",40,40,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("raw automatic minimum specified cap","flex:0 0 20px;width:30px",30,30,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("raw automatic minimum maximum cap","flex:0 0 20px;max-width:30px",30,30,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("specified and maximum cap","flex:0 0 60px;width:30px;max-width:20px",20,20,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("percentage width not specified cap","flex:0 0 20px;width:1%",40,40,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("percentage maximum not auto min cap","flex:0 0 20px;max-width:calc(0% + 10px)",40,40,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("overflow auto minimum","flex:0 0 20px;overflow:auto",20,20,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("overflow scroll minimum","flex:0 0 20px;overflow:scroll",20,20,
              "<div id='a'>aaaa bb cc</div>","",true);
    // Signed margins are added after C/B/H selection, never before comparison.
    text_case("margin cannot cancel selection condition","flex:0 0 60px;margin-left:-70px",0,0,
              "<div id='a'>aaaa bb cc</div>");
    for(const char* display:{"table","table-row","table-cell"}){
        const std::string css=std::string(reset)+"#a{display:"+display+
            ";width:100px;flex:0 0 50px;min-width:0;margin-left:-150px;}"
            "#b{flex:0 0 auto;min-width:0;}";
        Fixture f(pair_html,css.c_str());
        auto r=f.layout(900,700);
        expect(f.rect(r,"inner").width,150,display,"definite-base individual contribution preserved");
        aether_layout_result_destroy(r);
    }
}

static aether_style_map_t resolve_fresh_map(Fixture& f) {
    aether_style_update_t* updates=nullptr;
    const int count=aether_style_runtime_resolve_subtree(
        f.runtime,aether_doc_get_document_element(f.doc),&updates);
    require(count>=0,"resolve replacement styles");
    aether_style_updates_free(updates,count);
    aether_style_map_t map=nullptr;
    require(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&map)==AETHER_OK && map,
            "create distinct frozen style map");
    return map;
}

static void overflow_transaction_cases() {
    // Finite item contribution 2e38 + finite root decoration 2e38 overflows
    // float. A finite maximum must not disguise the non-finite intrinsic sum.
    // The inline-block control ignores flex bases and remains legitimately valid.
    int index=0;
    for(const char* extra : {"", "#inner{position:absolute;}",
            "body{display:block;}#inner{position:absolute;}",
            "body{display:block;}#inner{display:inline-block;}"}) {
        const std::string name="overflow transaction "+std::to_string(index);
        const std::string initial=std::string(reset)+
            "#a,#b{flex:0 0 50px;min-width:0;}";
        Fixture f(pair_html,initial.c_str());
        const auto first=f.layout(900,700);
        const auto before=f.rect(first,"inner");
        const auto before_a=f.rect(first,"a");
        const std::string huge=std::string(
            "#inner{max-width:2e38px;box-sizing:border-box;padding-right:2e38px;min-width:0;}"
            "#a,#b{flex:0 0 2e38px;min-width:0;}#b{display:none;}")+extra;
        int64_t sheet=0;
        require(aether_style_runtime_add_stylesheet(f.runtime,huge.c_str(),0,
                    "inflexible-overflow",&sheet,nullptr)==AETHER_OK,"add overflow sheet");
        auto replacement=resolve_fresh_map(f);
        require(replacement!=f.styles,"preserve original frozen map");
        aether_layout_result_t output=first;
        const auto status=aether_layout_layout(f.engine,f.doc,replacement,900,700,
                                               nullptr,nullptr,&output);
        if(index<3) {
            expect(float(status),float(AETHER_ERROR_INVALID_STATE),name.c_str(),"overflow status");
            expect(output==first?1.f:0.f,1.f,name.c_str(),"out_result not published");
            if(output && output!=first) aether_layout_result_destroy(output);
        } else {
            require(status==AETHER_OK && output && output!=first,"inline-block finite control");
            expect(f.rect(output,"inner").width,2e38f,name.c_str(),"finite non-flex control width");
            aether_layout_result_destroy(output);
        }
        expect(f.rect(first,"inner").width,before.width,name.c_str(),"previous frame width");
        expect(f.rect(first,"inner").x,before.x,name.c_str(),"previous frame x");
        expect(f.rect(first,"a").width,before_a.width,name.c_str(),"previous child width");
        expect(f.rect(first,"a").x,before_a.x,name.c_str(),"previous child x");
        // A new layout from the original frozen map must still work even while
        // the runtime contains the invalid replacement stylesheet.
        auto frozen=f.layout(900,700);
        expect(f.rect(frozen,"inner").width,before.width,name.c_str(),"frozen map unaffected");
        aether_layout_result_destroy(frozen);
        aether_style_map_destroy(replacement);
        require(aether_style_runtime_remove_stylesheet(f.runtime,sheet),"remove overflow sheet");
        auto recovered=resolve_fresh_map(f);
        output=nullptr;
        require(aether_layout_layout(f.engine,f.doc,recovered,900,700,nullptr,nullptr,&output)==AETHER_OK
                && output,"recover layout after overflow");
        expect(f.rect(output,"inner").width,before.width,name.c_str(),"recovered width");
        expect(f.rect(first,"inner").width,before.width,name.c_str(),"previous frame after recovery");
        aether_layout_result_destroy(output);
        aether_style_map_destroy(recovered);
        aether_layout_result_destroy(first);
        ++index;
    }
}

int main() {
    pair("fixed bases","#a,#b{flex:0 0 50px;min-width:0}",100,50,50,0,50);
    pair("grow forbidden","#a,#b{flex:0 1 50px;min-width:0}",100,50,50,0,50);
    pair("shrink forbidden","#a,#b{flex:1 0 250px;min-width:0}",500,250,250,0,250);
    pair("gap","#a,#b{flex:0 0 50px;min-width:0}#inner{gap:10px}",110,50,50,0,60);
    pair("minimum beats maximum","#a,#b{flex:0 0 50px;min-width:70px;max-width:60px}",140,70,70,0,70);
    pair("maximum","#a,#b{flex:0 0 250px;min-width:0;max-width:150px}",300,150,150,0,150);
    pair("content box decoration","#a,#b{flex:0 0 50px;min-width:0;padding:10px;border:2px solid;margin:5px}",168,74,74,5,89,5,5);
    pair("border box decoration","#a,#b{flex:0 0 50px;min-width:0;box-sizing:border-box;padding:10px;border:2px solid;margin:5px}",120,50,50,5,65,5,5);
    pair("decoration floor","#a,#b{flex:0 0 0;min-width:0;box-sizing:border-box;padding:10px;border:2px solid}",48,24,24,0,24);
    pair("automatic raw minimum","#a,#b{flex:0 0 50px}",300,100,200,0,100);
    pair("scrollable auto minimum","#a,#b{flex:0 0 50px;overflow:hidden}",100,50,50,0,50);
    pair("root decoration","#a,#b{flex:0 0 50px;min-width:0}#inner{padding:10px;border:2px solid}",124,50,50,12,62,12,12);
    pair("fractional basis","#a,#b{flex:0 0 50.25px;min-width:0}",100.5,50.25,50.25,0,50.25);
    pair("signed outer and gap","#a,#b{flex:0 0 50px;min-width:0}#a{margin-left:-80px}#inner{gap:10px}",30,50,50,-80,-20);
    pair("gap before floor","#a,#b{flex:0 0 50px;min-width:0}#a{margin-left:-150px}#inner{gap:60px}",10,50,50,-150,-40);
    pair("negative total floor","#a,#b{flex:0 0 50px;min-width:0}#a{margin-left:-150px}",0,50,50,-150,-100);
    pair("floor before root decoration","#a,#b{flex:0 0 50px;min-width:0}#a{margin-left:-150px}#inner{padding:10px;border:2px solid}",24,50,50,-138,-88,12,12);
    pair("content signed outer","#a,#b{flex:0 0 content;min-width:0}#a{margin-left:-150px}",150,100,200,-150,-50);
    pair("table individual contribution","#a,#b{flex:0 0 auto;min-width:0}#a{display:table;width:100px;margin-left:-150px}",150,100,200,-150,-50);
    pair("column unchanged","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-direction:column}",200,100,200,0,0,0,50);
    pair("wrap unchanged","#a,#b{flex:0 0 50px;min-width:0}#inner{flex-wrap:wrap}",300,50,50,0,50);

    text_case("min40 max60","flex-grow:0;flex-shrink:1",40,60);
    text_case("min60 max100","flex-grow:1;flex-shrink:0",60,100);
    text_case("min60 max60","flex-grow:0;flex-shrink:0",60,60);
    text_case("raw automatic minimum","flex:0 0 20px",40,40,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("scrollable automatic minimum","flex:0 0 20px;overflow:hidden",20,20,
              "<div id='a'>aaaa bb cc</div>","",true);
    text_case("fully flexible","flex-grow:1;flex-shrink:1",40,100);
    text_case("positive tiny grow","flex-grow:0.000001;flex-shrink:1",40,100);
    text_case("positive tiny shrink","flex-grow:1;flex-shrink:0.000001",40,100);
    text_case("percentage basis indefinite","flex:0 0 50%",40,100);
    text_case("calc percentage basis indefinite","flex:0 0 calc(0% + 60px)",40,100);
    text_case("content basis indefinite","flex:0 0 content",40,100);
    text_case("auto basis indefinite","flex:0 0 auto",40,100);
    // Width transfer also affects C, so both modes see the specified width.
    text_case("auto definite width transfer","flex:0 0 auto;width:60px",60,60);
    text_case("auto percentage width indefinite","flex:0 0 auto;width:50%",40,100);
    text_case("indefinite percentage constraints","flex:0 0 60px;min-width:50%;max-width:50%",60,60);
    // Cyclic minimum keeps the absolute calc term; cyclic maximum is unbounded.
    text_case("indefinite calc constraints","flex:0 0 60px;min-width:calc(0% + 90px);max-width:calc(0% + 10px)",90,90);
    // Percent terms resolve against zero only during intrinsic measurement;
    // final decoration is allowed to overflow the resulting container.
    text_case("percent decoration against zero","flex:0 0 60px;padding:10%;margin:5%;border:2px solid",64,64);
    text_case("calc decoration absolute terms","flex:0 0 60px;padding:calc(10% + 3px);margin:calc(5% + 2px);border:2px solid",74,74);
    text_case("inline blockification","display:inline;flex:0 0 60px",60,60,
              "<span id='a'>aaaa bb cc</span>");
    text_case("nested nowrap","flex:0 0 60px",60,60,
              "<div style='display:flex;flex:0 0 content'><div id='a'>aaaa bb cc</div></div>");
    text_case("anonymous text and excluded children","flex:0 0 60px",100,160,
              "aaaa bb cc<div id='a'>aaaa bb cc</div>"
              "<div style='display:none;width:900px'></div>"
              "<div style='position:absolute;width:800px'></div>"
              "<div style='position:fixed;width:700px'></div>");
    additional_selection_cases();
    overflow_transaction_cases();
    std::printf("flex inflexible intrinsic: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
