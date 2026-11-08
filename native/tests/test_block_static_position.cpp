// Public-only block static-position contract. Literal oracles, active under NDEBUG.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <dlfcn.h>
#endif

static unsigned checks=0, failures=0, control_failures=0, scenarios=0, rectangles=0;
static std::string label;
static void check(bool ok, const char* expression, int line, bool control=true) {
    ++checks;
    if (!ok) { ++failures; if(control) ++control_failures;
        std::fprintf(stderr,"FAIL %s:%d %s [%s]\n",label.c_str(),line,expression,control?"control":"normative"); }
}
#define CHECK(x) check(bool(x),#x,__LINE__)
#define ORACLE(x) check(bool(x),#x,__LINE__,false)
#define REQUIRE(x) do { bool ok=bool(x); CHECK(ok); if(!ok) std::exit(2); } while(false)
static void rect(aether_rectf actual,aether_rectf expected,bool control=false) {
    ++rectangles;
    std::printf("RECT %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g) %s\n",label.c_str(),
        actual.x,actual.y,actual.width,actual.height,expected.x,expected.y,expected.width,expected.height,
        control?"control":"normative");
    check(std::isfinite(actual.x)&&std::isfinite(actual.y)&&std::isfinite(actual.width)&&std::isfinite(actual.height),"finite rectangle",__LINE__,control);
    check(std::fabs(actual.x-expected.x)<.01f,"x",__LINE__,control);
    check(std::fabs(actual.y-expected.y)<.01f,"y",__LINE__,control);
    check(std::fabs(actual.width-expected.width)<.01f,"width",__LINE__,control);
    check(std::fabs(actual.height-expected.height)<.01f,"height",__LINE__,control);
}
struct Meter {
    unsigned calls=0;
    static aether_text_metrics_t measure(const char* text,const aether_computed_style_t*,float,void* context) {
        if(context) ++static_cast<Meter*>(context)->calls;
        // Token-specific ascent/descent intentionally differs while height stays10.
        const float baseline=std::strcmp(text,"UP")==0?9:std::strcmp(text,"down")==0?1:8;
        return {float(std::strlen(text))*10,10,baseline};
    }
};
struct Fixture {
    bool prepared;
    Meter meter;
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_style_map_t map{};
    aether_layout_engine_t engine{}; aether_layout_result_t result{}; int64_t sheet{};
    Fixture(bool p,const std::string& html):prepared(p) {
        REQUIRE(aether_doc_create(&doc)==AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,("<html><body>"+html+"</body></html>").c_str())==AETHER_OK);
        aether_style_environment_t env{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
        REQUIRE(aether_layout_create(Meter::measure,&meter,&engine)==AETHER_OK);
    }
    ~Fixture(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    void css(const std::string& extra) {
        if(sheet) REQUIRE(aether_style_runtime_remove_stylesheet(runtime,sheet));
        const std::string css="*{margin:0;padding:0;border:0;box-sizing:content-box}"
            "html,body,div{display:block}#host{position:relative;width:300px;height:200px;line-height:20px}"
            "#prev{width:80px;height:30px}#item{position:absolute;display:block;width:40px;height:20px}"+extra;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"block-static",&sheet,nullptr)==AETHER_OK);
        aether_style_update_t* updates{};
        int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        REQUIRE(n>=0);aether_style_updates_free(updates,n);
        aether_style_map_destroy(map);map=nullptr;
        REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK);
    }
    void layout(){
        ++scenarios;aether_layout_result_destroy(result);result=nullptr;meter.calls=0;
        if(prepared){aether_bool_t changed{};REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,map,nullptr,&changed)==AETHER_OK);}
        REQUIRE(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&result)==AETHER_OK);REQUIRE(result);
    }
    aether_node_t node(const char* id){auto n=aether_doc_get_element_by_id(doc,id);REQUIRE(n);return n;}
    aether_layout_box_t box(const char* id){auto b=aether_layout_result_find_primary_box(result,node(id));REQUIRE(b);return b;}
    void border(const char* id,aether_rectf expected,bool control=false){rect(aether_layout_box_get_border_box(box(id)),expected,control);}
};
struct Expected{const char* id;aether_rectf border;bool control=false;};
struct Case{const char* name;std::string html,css;std::vector<Expected> expected;bool paired=true;};
static const std::string previous="<div id='prev'></div>",item="<div id='item'></div>";
static std::string host(const std::string& children){return "<div id='host'>"+children+"</div>";}
static void start(bool p,const std::string& name){label=(p?"prepared/":"legacy/")+name;}
static std::vector<Case> cases(){
    std::vector<Case> out;
    auto basic=[&](const char* name,const char* css,aether_rectf expected,aether_rectf prev={0,0,80,30},bool control=false){
        out.push_back({name,host(previous+item),css,{{"item",expected,control},{"prev",prev,true},{"host",{0,0,300,200},true}}});};
    basic("auto","",{0,30,40,20});
    out.push_back({"first",host(item),"",{{"item",{0,0,40,20}},{"host",{0,0,300,200},true}}});
    basic("explicit","#item{left:7px;top:9px}",{7,9,40,20},{0,0,80,30},true);
    basic("top-only","#item{top:9px}",{0,9,40,20});
    basic("left-only","#item{left:7px}",{7,30,40,20});
    basic("right-only","#item{right:7px}",{253,30,40,20});
    basic("bottom-only","#item{bottom:9px}",{0,171,40,20});
    basic("opposing-auto-size","#item{left:7px;right:13px;top:9px;bottom:11px;width:auto;height:auto}",{7,9,280,180},{0,0,80,30},true);
    basic("start-precedence","#item{left:7px;right:13px;top:9px;bottom:11px}",{7,9,40,20},{0,0,80,30},true);
    out.push_back({"padding",host(previous+item),"#host{padding:10px 20px}",{{"item",{20,40,40,20}},{"prev",{20,10,80,30},true},{"host",{0,0,340,220},true}}});
    out.push_back({"origin-border", "<div id='prefix'></div>"+host(previous+item),"#prefix{height:50px}#host{margin-left:30px;padding:10px 20px;border:5px solid black}",{{"item",{55,95,40,20}},{"prev",{55,65,80,30},true},{"host",{30,50,350,230},true}}});
    basic("relative-predecessor","#prev{position:relative;left:25px;top:15px}",{0,30,40,20},{25,15,80,30});
    out.push_back({"hidden-predecessor",host(previous+item),"#prev{display:none}",{{"item",{0,0,40,20}},{"host",{0,0,300,200},true}}});
    for(bool reverse:{false,true}) out.push_back({reverse?"siblings-reversed":"siblings",host(previous+"<!--ignored--><div id='hidden'></div>"+(reverse?"<div id='other'></div>"+item:item+"<div id='other'></div>")),"#hidden{display:none}#other{position:absolute;width:90px;height:10px}",{{"item",{0,30,40,20}},{"other",{0,30,90,10}},{"prev",{0,0,80,30},true},{"host",{0,0,300,200},true}}});
    basic("positive-margins","#host{overflow:hidden}#prev{margin-top:5px;margin-bottom:7px}",{0,42,40,20},{0,5,80,30});
    basic("negative-margin","#host{overflow:hidden}#prev{margin-bottom:-7px}",{0,23,40,20});
    basic("absolute-margins-once","#item{margin-left:3px;margin-top:4px}",{3,34,40,20});
    out.push_back({"empty-strut-signed",host("<div id='empty'></div>"+item),"#host{overflow:hidden}#empty{margin-top:9px;margin-bottom:-4px}",{{"item",{0,5,40,20}},{"empty",{0,9,300,0},true},{"host",{0,0,300,200},true}}});
    for(bool add:{false,true}) out.push_back({add?"auto-height-with-item":"auto-height-no-item",host(previous+(add?item:"")+"<div id='next'></div>"),"#host{height:auto;overflow:hidden}#next{width:60px;height:10px}",add?std::vector<Expected>{{"item",{0,30,40,20}},{"prev",{0,0,80,30},true},{"next",{0,30,60,10},true},{"host",{0,0,300,40},true}}:std::vector<Expected>{{"prev",{0,0,80,30},true},{"next",{0,30,60,10},true},{"host",{0,0,300,40},true}}});
    // No whitespace nodes: an OOF marker must not split adjoining margins.
    for(bool add:{false,true}) out.push_back({add?"pending-margin-with-item":"pending-margin-no-item",
        host(previous+(add?item:"")+"<div id='next'></div>"),
        "#host{overflow:hidden;height:auto}#prev{margin-bottom:7px}#next{width:60px;height:10px;margin-top:5px}",
        add?std::vector<Expected>{{"prev",{0,0,80,30},true},{"next",{0,37,60,10},true},{"host",{0,0,300,47},true},{"item",{0,37,40,20}}}
           :std::vector<Expected>{{"prev",{0,0,80,30},true},{"next",{0,37,60,10},true},{"host",{0,0,300,47},true}}});
    const std::string collapse=host("<div id='before'></div>"+previous+item+"<div id='next'></div><div id='after'></div>");
    out.push_back({"parent-collapse",collapse,"#prev{margin-top:5px;margin-bottom:7px}#next{width:60px;height:10px}#before,#after{position:absolute;width:10px;height:10px}",{{"before",{0,0,10,10}},{"item",{0,37,40,20}},{"after",{0,52,10,10}},{"prev",{0,0,80,30},true},{"next",{0,42,60,10},true}},false});
    out.push_back({"empty-collapse",host("<div id='empty'></div>"+item),"#empty{margin-top:5px;margin-bottom:7px}",{{"item",{0,7,40,20}},{"empty",{0,0,300,0},true}},false});
    out.push_back({"parent-collapse-negative",host(previous+item),"#prev{margin-top:-5px;margin-bottom:7px}",{{"item",{0,37,40,20}},{"prev",{0,0,80,30},true}},false});
    out.push_back({"flow-button-center","<button id='host'><div id='before'></div><div id='prev'></div><div id='item'></div></button>","button{display:block}#host{height:100px}#prev{height:20px}#before{position:absolute;width:10px;height:10px}",{{"before",{0,40,10,10}},{"item",{0,60,40,20}},{"prev",{0,40,80,20},true},{"host",{0,0,300,100},true}},false});
    auto inl=[&](const char* name,const std::string& content,const std::string& css,aether_rectf expected){out.push_back({name,host(content),css,{{"item",expected},{"host",{0,0,300,200},true}}});};
    inl("inline-prefix", "ab"+item,"",{0,20,40,20});
    inl("inline-both-sides","ab"+item+"cd","",{0,20,40,20});
    out.push_back({"inline-wrapped",host("ab cd"+item),"#host{width:30px}",{{"item",{0,40,40,20}},{"host",{0,0,30,200},true}}});
    inl("inline-forced-break","ab<br>"+item,"",{0,20,40,20});
    inl("inline-ignored-whitespace","   "+item,"",{0,0,40,20});
    inl("inline-atomic-prefix","<span id='atom'></span>"+item,"#atom{display:inline-block;width:15px;height:30px}",{0,30,40,20});
    inl("inline-centered","ab"+item+"cd","#host{text-align:center}",{0,20,40,20});
    out.push_back({"causal-collapse-inline",host(previous+"ab"+item),"#prev{margin-top:5px;margin-bottom:7px}",{{"item",{0,62,40,20}},{"prev",{0,0,80,30},true}}});
    inl("causal-later-tall-atom","ab"+item+"<span id='atom'></span>","#atom{display:inline-block;width:15px;height:30px}",{0,20,40,20});
    inl("causal-mixed-baselines","<span>UP</span><span>down</span>"+item,"#host{font-size:1px;line-height:1px}",{0,18,40,20});
    basic("inline-block-boundary","#item{display:inline-block}",{80,0,40,20},{0,0,80,30},true);
    basic("inline-boundary","#item{display:inline}",{80,0,40,20},{0,0,80,30},true);
    basic("fixed-boundary","#item{position:fixed}",{80,0,40,20},{0,0,80,30},true);
    inl("inline-centered-boundary","ab"+item,"#host{text-align:center}#item{display:inline-block}",{160,0,40,20});out.back().expected[0].control=true;
    basic("outer-flex","#item{display:flex}",{0,30,40,20});
    out.push_back({"outer-flex-internal",host(previous+"<div id='item'><div id='inner'></div></div>"),"#item{display:flex}#inner{width:10px;height:6px}",{{"item",{0,30,40,20}},{"inner",{0,30,10,6}},{"prev",{0,0,80,30},true}}});
    out.push_back({"outer-table",host(previous+"<table id='item'></table>"),"#item{display:table}",{{"item",{0,30,40,20}},{"prev",{0,0,80,30},true}}});
    out.push_back({"static-intermediate",host("<div id='nested'>"+previous+item+"</div>"),"#nested{margin-left:25px;padding-top:10px;width:200px;height:100px}",{{"item",{25,40,40,20}},{"prev",{25,10,80,30},true},{"nested",{25,0,200,110},true},{"host",{0,0,300,200},true}}});
    out.push_back({"deferred-cell","<table id='table'><tr><td id='host'>"+previous+item+"</td><td id='tall'></td></tr></table>","table{display:table;width:400px}tr{display:table-row}td{display:table-cell}#host{width:200px;height:auto;padding:10px}#tall{width:200px;height:100px}",{{"item",{10,40,40,20}},{"prev",{10,10,80,30},true},{"host",{0,0,220,100},true}},false});
    return out;
}
struct TextBox{std::string text;aether_rectf border;};
static void texts(aether_layout_box_t box,std::vector<TextBox>& output){
    char* text=aether_layout_box_get_text(box);
    if(text&&*text) output.push_back({text,aether_layout_box_get_border_box(box)});
    aether_string_free(text);
    for(int i=0;i<aether_layout_box_get_child_count(box);++i) texts(aether_layout_box_get_child(box,i),output);
}
static void text_rect(Fixture& f,const char* token,aether_rectf expected,bool local=false){
    std::vector<TextBox> found;texts(aether_layout_result_get_root(f.result),found);unsigned count=0;
    for(auto t:found) if(t.text==token){++count;if(local){auto origin=aether_layout_box_get_content_box(f.box("host"));t.border.x-=origin.x;t.border.y-=origin.y;}rect(t.border,expected,true);}
    CHECK(count==1);
}
static void paint(aether_layout_result_t result,aether_rectf expected,bool control=false){
    aether_display_list_builder_t builder{};aether_display_list_t list{};
    REQUIRE(aether_display_list_builder_create(Meter::measure,nullptr,&builder)==AETHER_OK);
    REQUIRE(aether_display_list_build(builder,result,nullptr,&list)==AETHER_OK);
    unsigned found=0;
    for(int i=0;i<aether_display_list_get_count(list);++i){
        aether_paint_command_t command{};REQUIRE(aether_display_list_get_command(list,i,&command)==AETHER_OK);
        if(command.tag==AETHER_PAINT_FILL_ROUNDED_RECT){auto color=command.data.fill_rounded_rect.color;
            if(std::fabs(color.r-18.f/255)<.001f&&std::fabs(color.g-52.f/255)<.001f&&std::fabs(color.b-86.f/255)<.001f){++found;rect(command.data.fill_rounded_rect.rect,expected,control);}}
        aether_paint_command_free(&command);
    }
    CHECK(found==1);aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);
}
static void consumers(bool p){
    start(p,"consumers");Fixture f(p,host(previous+item));f.css("#host{height:40px;overflow:hidden}#item{background:#123456}");f.layout();
    f.border("item",{0,30,40,20});f.border("prev",{0,0,80,30},true);f.border("host",{0,0,300,40},true);
    ORACLE(aether_layout_result_hit_test(f.result,5,35)==f.node("item"));
    ORACLE(aether_layout_result_hit_test(f.result,85,5)!=f.node("item"));
    auto scroll=aether_layout_box_get_scroll_metrics(f.box("host"));
    ORACLE(std::fabs(scroll.scrollable_overflow_y-10.f)<.01f);CHECK(std::fabs(scroll.scrollable_overflow_x)<.01f);
    paint(f.result,{0,30,40,20});
    aether_layout_result_t clone{};REQUIRE(aether_layout_clone_snapshot(f.result,&clone)==AETHER_OK);
    auto clone_box=aether_layout_result_find_primary_box(clone,f.node("item"));REQUIRE(clone_box);
    rect(aether_layout_box_get_border_box(clone_box),{0,30,40,20});
    ORACLE(aether_layout_result_hit_test(clone,5,35)==f.node("item"));aether_layout_result_destroy(clone);
}
static void callback_nonparticipation(bool p){
    unsigned reference=0;
    for(bool add:{false,true}){
        start(p,add?"callback-with-marker":"callback-no-marker");
        Fixture f(p,host("ab"+(add?item:"")+"<span id='atom'></span>cd"));
        f.css("#host{height:auto}#atom{display:inline-block;width:15px;height:30px}");f.layout();
        f.border("host",{0,0,300,30},true);f.border("atom",{20,0,15,30},true);
        text_rect(f,"ab",{0,0,20,10});text_rect(f,"cd",{35,0,20,10});
        if(add){f.border("item",{0,20,40,20});CHECK(f.meter.calls==reference);}else reference=f.meter.calls;
        CHECK(f.meter.calls>0);
    }
}
static void relayout(bool p){
    start(p,"relayout");Fixture f(p,host(previous+item));f.css("#item{background:#123456}");f.layout();
    auto old=f.result;f.result=nullptr;
    auto oldmap=f.map;f.map=nullptr;
    aether_layout_result_t clone{};REQUIRE(aether_layout_clone_snapshot(old,&clone)==AETHER_OK);
    f.css("#item{display:none}");f.layout();CHECK(f.map!=oldmap);CHECK(f.result!=old);
    CHECK(aether_layout_result_find_primary_box(f.result,f.node("item"))==nullptr);
    f.css("#item{left:7px;top:9px}");f.layout();f.border("item",{7,9,40,20},true);
    f.css("");f.layout();f.border("item",{0,30,40,20});f.border("prev",{0,0,80,30},true);
    auto oldbox=aether_layout_result_find_primary_box(old,f.node("item"));REQUIRE(oldbox);rect(aether_layout_box_get_border_box(oldbox),{0,30,40,20});
    auto clonebox=aether_layout_result_find_primary_box(clone,f.node("item"));REQUIRE(clonebox);rect(aether_layout_box_get_border_box(clonebox),{0,30,40,20});
    paint(old,{0,30,40,20});aether_layout_result_destroy(clone);aether_layout_result_destroy(old);aether_style_map_destroy(oldmap);
}
static void boundaries(bool p){
    start(p,"pseudo-after-centered");Fixture f(p,host("ab"));
    f.css("#host{text-align:center}#host::after{content:'';position:absolute;display:block;width:10px;height:6px;background:#123456}");f.layout();
    paint(f.result,{160,0,10,6},true);text_rect(f,"ab",{140,0,20,10});f.border("host",{0,0,300,200},true);
    start(p,"intrinsic-input-boundary");Fixture input(p,"<input id='host' value='ab'>");input.css("input{display:block}#host{height:100px}");input.layout();input.border("host",{0,0,300,100},true);
}
int main(){
#if !defined(_WIN32)
    Dl_info identity{};
    REQUIRE(dladdr(reinterpret_cast<void*>(&aether_layout_layout),&identity)!=0);
    std::printf("DSO=%s\n",identity.dli_fname);
#endif
    const auto matrix=cases();CHECK(matrix.size()==46);
    unsigned matrix_scenarios=0;
    for(bool p:{false,true}) for(const auto& c:matrix){
        if(p&&!c.paired)continue;
        start(p,c.name);Fixture f(p,c.html);f.css(c.css);f.layout();++matrix_scenarios;
        const bool local=std::string(c.name)=="parent-collapse"||std::string(c.name)=="empty-collapse"||std::string(c.name)=="parent-collapse-negative"||std::string(c.name)=="causal-collapse-inline";
        for(const auto& expected:c.expected){
            if(local){auto actual=aether_layout_box_get_border_box(f.box(expected.id));auto origin=aether_layout_box_get_content_box(f.box("host"));actual.x-=origin.x;actual.y-=origin.y;rect(actual,expected.border,expected.control);}
            else f.border(expected.id,expected.border,expected.control);
        }
        if(std::string(c.name)=="inline-both-sides"){text_rect(f,"ab",{0,0,20,10});text_rect(f,"cd",{20,0,20,10});}
        if(std::string(c.name)=="causal-later-tall-atom"){text_rect(f,"ab",{0,0,20,10});f.border("atom",{20,0,15,30},true);}
        if(std::string(c.name)=="causal-mixed-baselines"){text_rect(f,"UP",{0,0,20,10});text_rect(f,"down",{20,8,40,10});}
        if(std::string(c.name)=="causal-collapse-inline")text_rect(f,"ab",{0,42,20,10},true);
    }
    CHECK(matrix_scenarios==87);
    for(bool p:{false,true}){consumers(p);callback_nonparticipation(p);relayout(p);boundaries(p);}
    CHECK(scenarios==105);CHECK(rectangles==306);CHECK(checks>=2000);
    std::printf("block-static scenarios=%u rectangles=%u checks=%u failures=%u control_failures=%u normative_failures=%u\n",scenarios,rectangles,checks,failures,control_failures,failures-control_failures);
    return failures?1:0;
}
