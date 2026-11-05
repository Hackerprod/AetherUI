#include <aether/aether.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool ok, const std::string& message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message.c_str()); ++failures; }
}
static void require(bool ok) { if (!ok) { std::fprintf(stderr,"fixture setup failed\n"); std::exit(2); } }
static bool same(float a,float b) { return std::isfinite(a) && std::fabs(a-b)<.02f; }
using rects = std::array<aether_rectf,4>;
struct fixture {
    std::string name, markup, css;
    std::vector<std::string> ids;
    std::vector<float> dx,dy;
};
static std::vector<rects> layout(const fixture& f, bool moved) {
    aether_doc_t doc=nullptr; require(aether_doc_create(&doc)==AETHER_OK);
    require(aether_doc_parse_html(doc, ("<html><body>"+f.markup+"</body></html>").c_str())==AETHER_OK);
    aether_style_runtime_options_t options{};
    aether_style_environment_t env{360,340,1,16,0,0,0};
    aether_style_runtime_t runtime=nullptr; require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
    std::string css="html,body,div{display:block;margin:0;padding:0;border:0}body{width:360px;height:340px}div{font-size:16px;line-height:0}#outer{width:240px}#item{width:60px;height:40px}#leaf{width:10px;height:10px}"+f.css;
    if (!moved) css += f.name.rfind("P1-",0)==0 ? "#leaf{top:0px;bottom:auto;left:0px;right:auto}" : "#item,#leaf{top:0px;bottom:auto;left:0px;right:auto}";
    int64_t sheet=0;int32_t rules=0;
    require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"relative-percentage.css",&sheet,&rules)==AETHER_OK);
    aether_style_update_t* updates=nullptr;
    const auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
    require(count>0);aether_style_updates_free(updates,count);
    aether_style_map_t styles=nullptr;require(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);
    aether_layout_engine_t engine=nullptr;require(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
    aether_layout_result_t result=nullptr;require(aether_layout_layout(engine,doc,styles,360,340,nullptr,nullptr,&result)==AETHER_OK);
    std::vector<rects> out;
    for (const auto& id:f.ids) {
        const auto node=aether_doc_get_element_by_id(doc,id.c_str());require(node!=nullptr);
        const auto box=aether_layout_result_find_primary_box(result,node);require(box!=nullptr);
        out.push_back({aether_layout_box_get_margin_box(box),aether_layout_box_get_border_box(box),aether_layout_box_get_padding_box(box),aether_layout_box_get_content_box(box)});
    }
    aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    return out;
}
int main() {
    std::vector<fixture> cases;
    const std::string simple="<div id='outer'><div id='item'><div id='leaf'></div></div><div id='after'></div></div>";
    for(const auto display:{"block","inline-block"}) {
        const auto add=[&](std::string name,std::string parent,std::string inset,float dy) {
            cases.push_back({name+"-"+display,simple,"#outer{"+parent+"}#item{display:"+display+";vertical-align:top;position:relative;left:30%;"+inset+"}",{"outer","item","leaf","after"},{0,72,72,0},{0,dy,dy,0}});
        };
        add("V0-zero","height:0px;","top:0%;bottom:7px;",0);
        add("V0-auto","height:auto;","top:0%;bottom:7px;",-7);
        add("V1-top","height:auto;","top:30%;bottom:7px;",-7);
        add("V1-bottom","height:auto;","top:9px;bottom:30%;",9);
        add("V1-linear-percent","height:auto;","top:calc(30% + 2px);bottom:7px;",-7);
        add("V2-px","height:auto;","top:9px;",9);
        add("V2-em","height:auto;","top:2em;",32);
        add("V2-rem","height:auto;","top:2rem;",32);
        add("V2-vh","height:auto;","top:10vh;",34);
        add("V2-vw","height:auto;","top:10vw;",36);
        add("V2-linear","height:auto;","top:calc(9px + 1em);",25);
        add("V3-bottom","height:200px;","top:auto;bottom:30%;",-60);
        add("V3-priority","height:200px;","top:30%;bottom:7px;",60);
        add("D1","width:260px;height:220px;padding:8px;border:2px solid black;box-sizing:border-box;","top:30%;",60);
        add("D2-max","height:200px;max-height:100px;","top:30%;",30);
        add("D2-min","height:200px;min-height:300px;","top:30%;",90);
        add("D3","height:auto;min-height:200px;","top:30%;",0);
        cases.push_back({std::string("D4-")+display,simple,std::string("#outer{height:200px}#item{display:")+display+";vertical-align:top;height:50%;}#leaf{position:relative;top:30%;}",{"outer","item","leaf","after"},{0,0,0,0},{0,0,30,0}});
    }
    const std::string flex="<div id='outer'><div id='item'><div id='leaf'></div><div id='tail'></div></div><div id='sibling'></div></div>";
    const auto flexcase=[&](std::string name,std::string css,float item,float leaf) {
        cases.push_back({name,flex,"#outer{display:flex;}#item{height:auto;}#leaf{position:relative;top:30%}"+css,{"outer","item","leaf"},{0,0,0},{0,item,leaf}});
    };
    flexcase("F1","#outer{height:200px}#item{position:relative;top:30%}",60,120);
    flexcase("F2","#outer{height:auto}#item{position:relative;top:30%}#tail{height:30px}#sibling{height:80px}",0,24);
    flexcase("F3","#outer{height:200px;flex-direction:column}#item{flex-grow:1;flex-basis:0px;min-height:0}#sibling{display:none}",0,60);
    flexcase("F4-auto","#outer{height:auto;flex-direction:column}#item{flex-basis:auto;flex-shrink:0}#tail{height:70px}#sibling{display:none}",0,0);
    flexcase("F4-length","#outer{height:auto;flex-direction:column}#item{flex-basis:100px;flex-shrink:0}#tail{height:70px}#sibling{display:none}",0,30);
    flexcase("F4-auto-transfer","#outer{height:auto;flex-direction:column}#item{height:100px;flex-basis:auto;flex-shrink:0}#tail{height:70px}#sibling{display:none}",0,30);
    for(const bool definite:{false,true}) cases.push_back({definite?"T1-defined":"T1-auto","<div id='outer'><div id='row'><div id='item'><div id='leaf'></div><div id='tail'></div></div></div></div>",std::string("#outer{display:table}#row{display:table-row}#item{display:table-cell;height:")+(definite?"100px":"auto")+"}#tail{height:70px}#leaf{position:relative;top:30%}",{"outer","item","leaf"},{0,0,0},{0,0,definite?30.f:0.f}});
    for(const bool fixed:{false,true}) cases.push_back({fixed?"P1-fixed":"P1-absolute",simple,std::string("#outer{height:200px;position:relative}#item{height:auto;position:")+(fixed?"fixed":"absolute")+";top:10px;bottom:10px}#leaf{position:relative;top:30%}",{"outer","item","leaf","after"},{0,0,0,0},{0,0,fixed?96.f:54.f,0}});
    for (const auto& f:cases) {
        const auto zero=layout(f,false), actual=layout(f,true);
        // Certify the finite reference sizes themselves, independently of deltas.
        float expected_item_height = -1.f;
        if (f.name.rfind("D4-",0)==0 || f.name=="F4-length" || f.name=="F4-auto-transfer" || f.name=="T1-defined") expected_item_height=100;
        else if(f.name=="F1" || f.name=="F3") expected_item_height=200;
        else if(f.name=="F2" || f.name=="F4-auto" || f.name=="T1-auto") expected_item_height=80;
        else if(f.name=="P1-fixed") expected_item_height=320;
        else if(f.name=="P1-absolute") expected_item_height=180;
        if (expected_item_height>=0) check(same(zero[1][1].height,expected_item_height) &&
            same(actual[1][1].height,expected_item_height), f.name+" used height");
        for(size_t n=0;n<f.ids.size();++n) for(size_t r=0;r<4;++r) {
            const auto a=actual[n][r],z=zero[n][r];
            const bool ok=same(a.x-z.x,f.dx[n])&&same(a.y-z.y,f.dy[n])&&same(a.width,z.width)&&same(a.height,z.height);
            if(!ok) std::fprintf(stderr,"%s %s rect%zu actual(%g,%g,%g,%g) zero(%g,%g,%g,%g) expected delta(%g,%g)\n",f.name.c_str(),f.ids[n].c_str(),r,a.x,a.y,a.width,a.height,z.x,z.y,z.width,z.height,f.dx[n],f.dy[n]);
            check(ok,f.name+" "+f.ids[n]+" four-rect rigid translation");
        }
        std::printf("CASE %s\n",f.name.c_str());
    }
    std::printf("relative percentage guards: %zu cases, failures=%d\n",cases.size(),failures);
    return failures?1:0;
}
