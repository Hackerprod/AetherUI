// Public-header-only regression. All checks remain active under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#if defined(__unix__)
#include <dlfcn.h>
#endif
static int failures=0, checks=0;
static std::string scenario;
static void require(bool ok,const char* what) {
    ++checks; if(!ok) { std::fprintf(stderr,"SETUP [%s]: %s\n",scenario.c_str(),what); std::exit(2); }
}
static void near(float actual,float expected,const char* what) {
    ++checks; if(!std::isfinite(actual)||!std::isfinite(expected)||std::fabs(actual-expected)>.01f) {
        ++failures; std::fprintf(stderr,"FAIL [%s] %s: actual=%g expected=%g\n",scenario.c_str(),what,actual,expected);
    }
}
static bool baseline_metrics = false;
static bool equal_baselines = false;
static std::vector<std::string> measurement_trace;
static aether_text_metrics_t measure(const char*t,const aether_computed_style_t* style,float,void*) {
    if (style && style->font_size == 37) measurement_trace.emplace_back(t);
    if (baseline_metrics) return {float(std::strlen(t))*10,20,
        !equal_baselines && (std::strchr(t, 'B') || (style && style->font_size == 38)) ? 2.f : 18.f};
    return {float(std::strlen(t))*10,20,15};
}
static const char* cards="<div id='a'></div><div id='b'></div><div id='c'></div>";
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_style_map_t map{};
    aether_layout_engine_t engine{}; aether_layout_result_t result{};
    std::vector<aether_layout_result_t> retained; int64_t sheet{};
    Fixture(std::string extra="",std::string content=cards,bool atomic=false) {
        require(aether_doc_create(&doc)==AETHER_OK,"doc");
        const std::string inner="<div id='inner'>"+content+"</div>";
        const std::string html="<html><body><div id='outer'>"+(atomic?"<span id='atomic'>"+inner+"</span>":inner)+"</div></body></html>";
        require(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse");
        aether_style_runtime_options_t options{}; aether_style_environment_t env{600,400,1,16,0,0,0};
        require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"runtime");
        require(aether_layout_create(measure,nullptr,&engine)==AETHER_OK,"engine");
        set(extra+(atomic?"#outer{display:block}#atomic{display:inline-block}":"")); layout();
    }
    ~Fixture() {
        aether_layout_result_destroy(result); for(auto r:retained)aether_layout_result_destroy(r);
        aether_layout_destroy(engine); aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void set(const std::string& extra) {
        if(sheet) require(aether_style_runtime_remove_stylesheet(runtime,sheet),"remove stylesheet");
        const std::string css="html,body{margin:0;padding:0}#outer{display:flex;width:600px;align-items:flex-start}"
            "#inner{display:flex;flex:0 0 auto;flex-wrap:wrap;gap:20px;align-items:flex-start}"
            "#inner>div{height:20px;flex:0 0 auto}#a{width:100px}#b{width:70px}#c{width:90px}"+extra;
        require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"intrinsic-width",&sheet,nullptr)==AETHER_OK,"stylesheet");
        aether_style_update_t* updates{};
        const int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(n>=0,"resolve"); aether_style_updates_free(updates,n);
        aether_style_map_destroy(map); map=nullptr;
        require(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK && map,"map");
    }
    void layout() {
        if(result)retained.push_back(result); result=nullptr;
        require(aether_layout_layout(engine,doc,map,600,400,nullptr,nullptr,&result)==AETHER_OK && result,"layout");
        for(auto old:retained) require(old!=result,"distinct owned result");
    }
    aether_layout_box_t box(const char* id,aether_layout_result_t r=nullptr) const {
        auto node=aether_doc_get_element_by_id(doc,id); require(node,"node");
        auto b=aether_layout_result_find_primary_box(r?r:result,node); require(b,"primary box"); return b;
    }
    aether_rectf rect(const char* id="inner",aether_layout_result_t r=nullptr) const {return aether_layout_box_get_border_box(box(id,r));}
    void width(float w)const {near(rect().width,w,"inner width");}
    void row(float w=300,float offset=0,float gap=20,float margin=0,float decoration=0)const {
        auto host=rect(); near(host.width,w,"inner width"); near(host.height,20+decoration,"inner height");
        float x=offset+margin; int i=0;
        for(const char* id:{"a","b","c"}) {
            const float widths[]={100,70,90}; auto r=rect(id);
            near(r.x-host.x,x,"card x"); near(r.y-host.y,offset,"card y");
            near(r.width,widths[i]+decoration,"card width");
            x+=widths[i++]+decoration+2*margin+gap;
        }
    }
    void lines(float w)const {
        width(w); auto host=rect(); near(host.height,100,"three line height");int i=0;
        for(const char* id:{"a","b","c"}) {auto r=rect(id);near(r.x-host.x,0,"line x");near(r.y-host.y,40*i++,"line y");}
    }
};

// The automatic main-axis minimum of a column flex child must use the
// same per-run white-space wrapping policy as its final inline fragments.
static void inline_height_case(const std::string& mode, const std::string& text,
                               float height, float last_y, bool zero_min = false,
                               const std::string& extra = "") {
    scenario = "inline height " + mode + "/" + text + (zero_min ? "/min0" : "");
    Fixture f("#outer{flex-direction:column;width:50px;height:20px;align-items:stretch}"
              "#inner{display:block;width:auto;height:60px;flex:0 1 auto;white-space:" +
              mode + ";line-height:20px}" + (zero_min ? "#inner{min-height:0}" : "") + extra, text);
    const auto host = f.rect();
    near(f.rect("outer").width, 50, "column width");
    near(f.rect("outer").height, 20, "column height");
    near(host.width, 50, "child width");
    near(host.height, height, "automatic minimum height");
    int words = 0;
    float final_y = 0;
    const auto box = f.box("inner");
    for (int i = 0; i < aether_layout_box_get_child_count(box); ++i) {
        const auto fragment = aether_layout_box_get_child(box, i);
        char* value = aether_layout_box_get_text(fragment);
        if (value && std::strcmp(value, "aaaa") == 0) {
            const auto r = aether_layout_box_get_border_box(fragment);
            near(r.width, 40, "word fragment width");
            near(r.height, 20, "word fragment height");
            if (words == 0) near(r.y - host.y, 0, "first fragment y");
            final_y = r.y - host.y;
            ++words;
        }
        aether_string_free(value);
    }
    near(float(words), 2, "two word fragments");
    near(final_y, last_y, "last fragment y");
}
static void inline_height_regressions() {
    for (const char* raw : {"normal", "nowrap", "pre", "pre-wrap", "pre-line"}) {
        const std::string mode(raw);
        const bool wraps = mode != "nowrap" && mode != "pre";
        for (bool zero_min : {false, true}) {
            inline_height_case(mode, "aaaa aaaa", zero_min ? 20 : wraps ? 40 : 20,
                               wraps ? 20 : 0, zero_min);
            for (const char* sep : {"\n", "\r\n", "<br>"}) {
                const bool forced = std::string(sep) == "<br>" ||
                    mode == "pre" || mode == "pre-wrap" || mode == "pre-line";
                const bool two_lines = forced || wraps;
                inline_height_case(mode, "aaaa" + std::string(sep) + "aaaa",
                                   zero_min ? 20 : two_lines ? 40 : 20,
                                   two_lines ? 20 : 0, zero_min);
            }
        }
    }
    inline_height_case("normal", "<span style='white-space:nowrap'>aaaa aaaa</span>", 20, 0);
    inline_height_case("nowrap", "<span style='white-space:normal'>aaaa aaaa</span>", 40, 20);
    inline_height_case("normal", "aaaa <span style='white-space:nowrap'>aaaa</span>", 20, 0);
    inline_height_case("nowrap", "aaaa <span style='white-space:normal'>aaaa</span>", 40, 20);
    // Atomic placement retains its existing independent wrapping behavior.
    for (const char* mode : {"normal", "nowrap", "pre"}) {
        scenario = std::string("atomic height ") + mode;
        Fixture f(std::string("#outer{flex-direction:column;width:50px;height:20px;align-items:stretch}") +
                  "#inner{display:block;height:60px;flex:0 1 auto;white-space:" + mode +
                  ";line-height:20px}#inner span{display:inline-block;width:40px;height:20px}",
                  "<span id='first'></span><span id='last'></span>");
        near(f.rect().height, 40, "atomic minimum height unchanged");
        near(f.rect("last").y-f.rect("first").y, 20, "atomic line unchanged");
    }
}

// Narrow parity contract with final layout, not full CSS whitespace conformance.
static void collapsed_height_case(const char* name, const std::string& text,
                                  float height, float bx, float by, int spaces,
                                  bool reject_marked_space = false) {
    scenario = std::string("cross-run collapse ") + name;
    measurement_trace.clear();
    Fixture f("#outer{flex-direction:column;width:50px;height:20px;align-items:stretch}"
              "#inner{display:block;width:auto;height:60px;flex:0 1 auto;white-space:normal;line-height:20px}", text);
    near(f.rect().width, 50, "child width");
    near(f.rect().height, height, "automatic minimum height");
    int count = 0, words = 0;
    auto host = f.rect(); auto box = f.box("inner");
    for (int i = 0; i < aether_layout_box_get_child_count(box); ++i) {
        auto child = aether_layout_box_get_child(box, i);
        char* value = aether_layout_box_get_text(child);
        if (value && std::strcmp(value, " ") == 0) ++count;
        if (value && std::strcmp(value, "bb") == 0) {
            auto r = aether_layout_box_get_border_box(child);
            near(r.x-host.x, bx, "bb fragment x"); near(r.y-host.y, by, "bb fragment y");
            near(r.width, 20, "bb width"); near(r.height, 20, "bb height"); ++words;
        }
        aether_string_free(value);
    }
    near(float(words), 1, "one bb fragment");
    near(float(count), float(spaces), "space fragment count");
    if (reject_marked_space) {
        int measured_spaces = 0;
        for (const auto& t : measurement_trace) if (t == " ") ++measured_spaces;
        // The shared stream discards the redundant separator before every consumer.
        near(float(measured_spaces), 0, "redundant marked space is never measured");
    }
}
static void collapsed_height_regressions() {
    collapsed_height_case("plain", "aa bb", 20, 30, 0, 1);
    collapsed_height_case("split", "<span>aa </span><span> bb</span>", 20, 30, 0, 1);
    collapsed_height_case("nested", "<span>aa </span><span><span> </span> bb</span>", 20, 30, 0, 1);
    collapsed_height_case("marked", "<span>aa </span><span style='font-size:37px'> </span><span>bb</span>", 20, 30, 0, 1, true);
    collapsed_height_case("word reset", "a <span>a </span><span> bb</span>", 40, 0, 20, 1);
    collapsed_height_case("br", "<span>aa </span><br><span> bb</span>", 40, 0, 20, 0);
    collapsed_height_case("atomic", "<span>aa </span><span style='display:inline-block;width:10px;height:20px'></span><span> bb</span>", 40, 0, 20, 2);
    for (const char* mode : {"nowrap", "pre-line"})
        collapsed_height_case(mode, "<span>aa </span><span style='white-space:"+std::string(mode)+"'> bb</span>", 20, 30, 0, 1);
    for (const char* mode : {"pre", "pre-wrap"}) {
        collapsed_height_case(mode, "<span>aa </span><span style='white-space:"+std::string(mode)+"'> </span><span>bb</span>", 40, 0, 20, 2);
        collapsed_height_case(mode, "<span style='white-space:"+std::string(mode)+"'>aa </span><span> bb</span>", 40, 0, 20, 2);
    }
    collapsed_height_case("normal CRLF", "<span>aa </span><span>\r\n bb</span>", 20, 30, 0, 1);
    collapsed_height_case("pre-line CRLF", "<span>aa </span><span style='white-space:pre-line'>\r\n bb</span>", 40, 0, 20, 1);
    collapsed_height_case("break resets state", "aa <br>b <span>bb</span>", 40, 20, 20, 1);
}
// Equal heights can still require a taller line when run baselines differ.
// These public-ABI checks compare the flex automatic minimum with final geometry.
struct BaselineFragment { const char* text; float x, y; };
static void baseline_height_case(const char* name, const std::string& content,
                                 float height, std::initializer_list<BaselineFragment> expected,
                                 const std::string& extra = "", bool equal = false) {
    scenario = std::string("inline baseline ") + name;
    baseline_metrics = true;
    equal_baselines = equal;
    Fixture f("#outer{flex-direction:column;width:50px;height:20px;align-items:stretch}"
              "#inner{display:block;width:auto;height:120px;flex:0 1 auto;white-space:normal;line-height:20px}" + extra, content);
    baseline_metrics = false;
    near(f.rect().height, height, "automatic baseline minimum");
    near(f.rect().width, 50, "baseline child width");
    const auto host = f.rect();
    const auto box = f.box("inner");
    auto next = expected.begin();
    for (int i = 0; i < aether_layout_box_get_child_count(box); ++i) {
        auto child = aether_layout_box_get_child(box, i);
        char* text = aether_layout_box_get_text(child);
        if (text && *text) {
            require(next != expected.end(), "no unexpected text fragment");
            require(std::strcmp(text, next->text) == 0, "fragment text/order");
            const auto rect = aether_layout_box_get_border_box(child);
            near(rect.x-host.x, next->x, "baseline fragment x");
            near(rect.y-host.y, next->y, "baseline fragment y");
            near(rect.width, float(std::strlen(text))*10, "baseline fragment width");
            near(rect.height, 20, "baseline fragment height");
            ++next;
        }
        aether_string_free(text);
    }
    require(next == expected.end(), "all text fragments present");
    if (aether_doc_get_element_by_id(f.doc, "atom")) {
        near(f.rect("atom").y-host.y, 0, "atomic remains top aligned");
        near(f.rect("atom").height, 40, "atomic height unchanged");
    }
}
static void baseline_height_regressions() {
    baseline_height_case("different", "<span>A</span><span>B</span>", 36, {{"A",0,0},{"B",10,16}});
    baseline_height_case("equal", "<span>A</span><span>B</span>", 20, {{"A",0,0},{"B",10,0}}, "", true);
    baseline_height_case("reversed", "<span>B</span><span>A</span>", 36, {{"B",0,16},{"A",10,0}});
    baseline_height_case("br resets", "<span>A</span><span>B</span><br><span>B</span>", 56,
                         {{"A",0,0},{"B",10,16},{"B",0,36}});
    baseline_height_case("separate lines", "A<br>B", 40, {{"A",0,0},{"B",0,20}});
    // No separator exists: both physical fragments belong to one overflowing word.
    baseline_height_case("joined word baseline union", "<span>AAAA</span><span>BBBB</span>", 36,
                         {{"AAAA",0,0},{"BBBB",40,16}});
    baseline_height_case("joined word retains tall baseline", "<span>A</span><span>B</span><span>AAAA</span>", 36,
                         {{"A",0,0},{"B",10,16},{"AAAA",20,0}});
    baseline_height_case("leading rejected space", "<span style='font-size:38px'> </span>A", 20, {{"A",0,0}});
    baseline_height_case("redundant rejected space", "<span>A </span><span style='font-size:38px'> </span>A", 20,
                         {{"A",0,0},{" ",10,0},{"A",20,0}});
    baseline_height_case("accepted space baseline", "A<span style='font-size:38px'> </span>A", 36,
                         {{"A",0,0},{" ",10,16},{"A",20,0}});
    for (const char* mode : {"nowrap", "pre"}) {
        baseline_height_case(mode, "<span>AAAA</span><span>BBBB</span>", 36,
                             {{"AAAA",0,0},{"BBBB",40,16}}, std::string("#inner{white-space:")+mode+"}");
    }
    baseline_height_case("pre newline", "A\nB", 40, {{"A",0,0},{"B",0,20}}, "#inner{white-space:pre}");
    baseline_height_case("min-height zero", "<span>A</span><span>B</span>", 20,
                         {{"A",0,0},{"B",10,16}}, "#inner{min-height:0}");
    // Preserve existing atomic intrinsic-height behavior; only text contributes
    // ascent/descent. The explicit atomic box remains 40px in final geometry.
    baseline_height_case("atomic mixed", "A<span id='atom' style='display:inline-block;width:10px;height:40px'></span>B", 36,
                         {{"A",0,0},{"B",20,16}});
    baseline_height_case("atomic only", "<span id='atom' style='display:inline-block;width:10px;height:40px'></span>", 20, {});
    baseline_height_case("empty forced line", "A<br><br>B", 60, {{"A",0,0},{"B",0,40}});
}
// Leading preserved whitespace is content, including on a newly forced line.
static void preserved_height_case(const char* name, const char* mode,
                                  const char* content, float height,
                                  float outer_height = 20, bool zero_minimum = false) {
    scenario = std::string("leading preserved ") + name + "/" + mode;
    Fixture f("#outer{flex-direction:column;width:50px;height:" + std::to_string(outer_height) +
              "px;align-items:stretch}#inner{display:block;width:auto;height:120px;flex:0 1 auto;line-height:20px;white-space:" +
              mode + "}" + (zero_minimum ? "#inner{min-height:0}" : ""), content);
    near(f.rect().height, height, "automatic preserved minimum");
    near(f.rect().width, 50, "preserved width");
}
static void preserved_height_regressions() {
    for (const char* mode : {"normal", "nowrap", "pre", "pre-wrap", "pre-line"}) {
        const bool preserved = !std::strcmp(mode, "pre") || !std::strcmp(mode, "pre-wrap");
        const bool wrap = !std::strcmp(mode, "pre-wrap");
        preserved_height_case("leading", mode, "  aa bb", wrap ? 40 : 20);
        preserved_height_case("split", mode, "<span>  </span><span>aa bb</span>", wrap ? 40 : 20);
        preserved_height_case("br", mode, "<br>  aa bb", wrap ? 60 : 40);
        preserved_height_case("CRLF", mode, "\r\n  aa bb", wrap ? 60 : (preserved || !std::strcmp(mode,"pre-line") ? 40 : 20));
        preserved_height_case("whitespace only", mode, "  ", preserved ? 20 : 5, 5);
        preserved_height_case("explicit zero minimum", mode, "  aa bb", 20, 20, true);
    }
    // Intrinsic-minimum regression only: preserved spaces do not trigger a
    // wrap before themselves in this measurement path. These assertions do
    // not establish final-layout conformance. Final layout already wraps the
    // spaces in "aa    bb" onto a separate line (60px extent versus this 40px
    // minimum); keep that pre-existing hanging-space defect for a separate fix.
    preserved_height_case("overflowing spaces intrinsic minimum only", "pre-wrap", "aa    bb", 40);
    preserved_height_case("leading overflow", "pre-wrap", "      aa", 40);
    baseline_height_case("leading preserved baseline", "<span style='font-size:38px;white-space:pre'> </span>A", 36,
                         {{" ",0,16},{"A",10,0}});
    baseline_height_case("leading preserved wrap baseline", "<span style='font-size:38px;white-space:pre-wrap'> </span>A", 36,
                         {{" ",0,16},{"A",10,0}});
}
int main(int argc,char**argv) {
#if defined(__unix__)
    Dl_info info{}; require(dladdr(reinterpret_cast<void*>(&aether_layout_layout),&info)!=0,"dladdr");
    std::printf("DSO=%s\n",info.dli_fname);
    if(const char* expected=std::getenv("AETHER_EXPECT_DSO")) {
        char* actual=realpath(info.dli_fname,nullptr); char* wanted=realpath(expected,nullptr);
        require(actual&&wanted&&!std::strcmp(actual,wanted),"exact DSO path");std::free(actual);std::free(wanted);
    }
#endif
    const std::string group=argc>1?argv[1]:"all";
    require(group=="all"||group=="wrap"||group=="nowrap"||group=="atomic","known group");
    if(group=="all"||group=="wrap") {scenario="wrap auto";Fixture f;f.row();}
    if(group=="all"||group=="nowrap") {scenario="nowrap control";Fixture f("#inner{flex-wrap:nowrap}");f.row();}
    if(group=="all"||group=="atomic") {scenario="atomic inline consumer";Fixture f("",cards,true);f.row();near(f.rect("atomic").width,300,"atomic width");}
    if(group=="all") {
        inline_height_regressions();
        collapsed_height_regressions();
        baseline_height_regressions();
        preserved_height_regressions();
        {scenario="explicit width";Fixture f("#inner{width:300px}");f.row();}
        for(int outer:{150,80}) {scenario="automatic minimum "+std::to_string(outer);Fixture f("#outer{width:"+std::to_string(outer)+"px}#inner{flex:0 1 auto}");f.lines(outer==80?100:150);}
        {scenario="empty";Fixture f("","");f.width(0);}
        {scenario="singleton";Fixture f("","<div id='a'></div>");f.width(100);}
        for(int m:{5,-5}) {scenario="signed margin "+std::to_string(m);Fixture f("#inner>div{margin-left:"+std::to_string(m)+"px;margin-right:"+std::to_string(m)+"px}");f.row(300+6*m,0,20,float(m));}
        {scenario="content box child decoration";Fixture f("#inner>div{padding:2px;border:1px solid black;box-sizing:content-box}");f.row(318,0,20,0,6);}
        {scenario="border box child decoration";Fixture f("#inner>div{padding:2px;border:1px solid black;box-sizing:border-box}");f.row();}
        {scenario="parent decoration once";Fixture f("#inner{padding:3px;border:2px solid black}");f.width(310);near(f.rect().height,30,"decorated height");int i=0;for(const char* id:{"a","b","c"}){float x[]={5,125,215};near(f.rect(id).x-f.rect().x,x[i++],"decorated x");near(f.rect(id).y-f.rect().y,5,"decorated y");}}
        {scenario="axis-specific gaps";Fixture f("#inner{row-gap:7px;column-gap:20px}");f.row();}
        {scenario="percentage gap";Fixture f("#inner{column-gap:10%}");f.width(260);}
        {scenario="calc gap";Fixture f("#inner{column-gap:calc(5px + 10%)}");f.width(270);}
        {scenario="zero gap";Fixture f("#inner{gap:0}");f.row(260,0,0);}
        {scenario="excluded children";Fixture f("",std::string(cards)+"<div id='hidden' style='display:none;width:999px'></div><div id='abs' style='position:absolute;width:999px'></div><div id='fixed' style='position:fixed;width:999px'></div>");f.row();require(!aether_layout_result_find_primary_box(f.result,aether_doc_get_element_by_id(f.doc,"hidden")),"hidden has no box");f.box("abs");f.box("fixed");}
        {scenario="max width clamp";Fixture f("#inner{max-width:150px}");f.lines(150);}
        {scenario="min width clamp";Fixture f("#inner{min-width:350px}");f.row(350);}
        {scenario="result/style lifecycle";Fixture f;f.row();auto first=f.result;f.layout();f.row();
         f.set("#inner{flex-wrap:nowrap;gap:10px}");f.layout();f.row(280,0,10);
         f.set("");f.layout();f.row();for(const char* id:{"inner","a","b","c"}){auto a=f.rect(id,first),b=f.rect(id);near(a.x,b.x,"snapshot x");near(a.y,b.y,"snapshot y");near(a.width,b.width,"snapshot width");near(a.height,b.height,"snapshot height");}}
    }
    std::printf("flex-intrinsic-width: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
