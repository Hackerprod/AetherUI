// Public-header-only regression. All checks remain active under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
static aether_text_metrics_t measure(const char*t,const aether_computed_style_t*,float,void*) {
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
