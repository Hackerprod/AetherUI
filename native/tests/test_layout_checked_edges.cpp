// Direct-source target, --wrap=aether_css_length_resolve. No production hooks.
#include "../src/layout/layout_internal.h"
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); std::abort(); } } while(false)
struct Call { aether_css_length value; float reference,font,root,width,height; };
static std::array<Call,16> calls{};
static unsigned count=0, expected_calls=4, cases=0;
static bool counting=false;
extern "C" float __real_aether_css_length_resolve(aether_css_length,float,float,float,float,float);
extern "C" float __wrap_aether_css_length_resolve(aether_css_length v,float r,float f,float root,float w,float h) {
    if(counting) { CHECK(count<calls.size()); calls[count++]={v,r,f,root,w,h}; }
    return __real_aether_css_length_resolve(v,r,f,root,w,h);
}
static uint32_t bits(float f) { return std::bit_cast<uint32_t>(f); }
static bool same(aether_css_length a,aether_css_length b) {
    return a.unit==b.unit && bits(a.value)==bits(b.value) && bits(a.linear_percent)==bits(b.linear_percent) &&
        bits(a.linear_em)==bits(b.linear_em) && bits(a.linear_rem)==bits(b.linear_rem) &&
        bits(a.linear_vw)==bits(b.linear_vw) && bits(a.linear_vh)==bits(b.linear_vh);
}
static aether_css_length length(float v,int unit=AETHER_CSS_UNIT_PX) { aether_css_length l{};l.value=v;l.unit=static_cast<aether_css_unit_t>(unit);return l; }
static aether_css_length& side(aether_css_edges& e,unsigned n) {
    switch(n) {case 0:return e.left;case 1:return e.top;case 2:return e.right;default:return e.bottom;}
}
static std::array<float,4> fields(aether_thicknessf t) { return {t.left,t.top,t.right,t.bottom}; }
static aether_thicknessf actual(int kind,const aether_layout_engine_s* e,aether_css_edges edges,float ref,BoxStyleSnapshot& s) {
    if(kind==0)return resolve_margin_edges(e,edges,ref,s);
    if(kind==1)return resolve_padding_edges(e,edges,ref,s);
    s.border_width=edges;return resolve_border_edges(e,s,ref);
}
// Historical two-pass contract: authentic scalar validation then untouched legacy helper.
static aether_thicknessf historical(int kind,const aether_layout_engine_s* e,aether_css_edges edges,float ref,BoxStyleSnapshot& s) {
    for(auto v:{edges.left,edges.top,edges.right,edges.bottom}) {
        const auto used=__real_aether_css_length_resolve(v,ref,s.font_size,e?e->root_font_size:16.f,e?e->viewport_width:0.f,e?e->viewport_height:0.f);
        if(v.unit!=AETHER_CSS_UNIT_AUTO && !std::isfinite(used)) throw std::overflow_error("Non-finite used CSS length");
    }
    if(kind==2) { s.border_width=edges; return aether_resolve_border_edges(s,ref,e?e->root_font_size:16.f,e?e->viewport_width:0.f,e?e->viewport_height:0.f); }
    return aether_resolve_box_edges(edges,ref,s.font_size,e?e->root_font_size:16.f,e?e->viewport_width:0.f,e?e->viewport_height:0.f,kind==0);
}
static void check(int kind,const aether_layout_engine_s* engine,aether_css_edges edges,float ref,BoxStyleSnapshot s) {
    aether_thicknessf oracle{},result{};bool invalid=false;
    try {oracle=historical(kind,engine,edges,ref,s);} catch(const std::overflow_error& ex) {CHECK(std::strcmp(ex.what(),"Non-finite used CSS length")==0);invalid=true;}
    unsigned invalid_side=0;
    if(invalid) for(unsigned i=0;i<4;++i) {auto v=side(edges,i);auto f=__real_aether_css_length_resolve(v,ref,s.font_size,engine?engine->root_font_size:16.f,engine?engine->viewport_width:0.f,engine?engine->viewport_height:0.f);if(v.unit!=AETHER_CSS_UNIT_AUTO&&!std::isfinite(f)){invalid_side=i+1;break;}}
    count=0;counting=true;bool threw=false;
    try {result=actual(kind,engine,edges,ref,s);} catch(const std::overflow_error& ex) {counting=false;CHECK(std::strcmp(ex.what(),"Non-finite used CSS length")==0);threw=true;}
    counting=false;CHECK(threw==invalid);CHECK(count==(invalid?invalid_side:expected_calls));
    for(unsigned i=0;i<count;++i) {const auto& c=calls[i];CHECK(same(c.value,side(edges,i%4)));CHECK(bits(c.reference)==bits(ref));CHECK(bits(c.font)==bits(s.font_size));CHECK(bits(c.root)==bits(engine?engine->root_font_size:16.f));CHECK(bits(c.width)==bits(engine?engine->viewport_width:0.f));CHECK(bits(c.height)==bits(engine?engine->viewport_height:0.f));}
    if(!invalid) {auto a=fields(result),b=fields(oracle);for(unsigned i=0;i<4;++i)CHECK(bits(a[i])==bits(b[i]));}
    ++cases;
}
int main(int argc,char** argv) {
    if(argc==2 && std::strcmp(argv[1],"--expect-eight")==0)expected_calls=8;
    else CHECK(argc==1);
    BoxStyleSnapshot s;s.font_size=13;s.border_styles={AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_SOLID};
    aether_layout_engine_s engine;engine.root_font_size=19;engine.viewport_width=997;engine.viewport_height=613;
    auto edges=aether_css_edges{length(-3),length(7),length(11),length(17)};
    counting=true;const auto direct=aether_css_length_resolve(length(7),200,13,19,997,613);counting=false;CHECK(count==1&&direct==7);
    count=0;counting=true;(void)aether_resolve_box_edges(edges,200,13,19,997,613,true);counting=false;CHECK(count==4);
    const float max=std::numeric_limits<float>::max(),inf=std::numeric_limits<float>::infinity(),nan=std::numeric_limits<float>::quiet_NaN();
    const float values[]={0.f,-0.f,1.25f,-2.75f,max,-max,std::numeric_limits<float>::denorm_min(),inf,-inf,nan};
    for(int kind=0;kind<3;++kind)for(const auto* e:{&engine,static_cast<aether_layout_engine_s*>(nullptr)})for(int unit=AETHER_CSS_UNIT_AUTO;unit<=AETHER_CSS_UNIT_LINEAR;++unit)for(float value:values)for(unsigned n=0;n<4;++n) {
        auto input=edges;auto v=length(value,unit);if(unit==AETHER_CSS_UNIT_LINEAR){v.linear_percent=-17;v.linear_em=2;v.linear_rem=-3;v.linear_vw=11;v.linear_vh=-13;}side(input,n)=v;check(kind,e,input,200,s);
    }
    for(int kind=0;kind<3;++kind)for(float ref:{0.f,-0.f,-200.f,max,inf,nan})for(float font:{0.f,13.f,max,inf,nan}) {auto local=s;local.font_size=font;auto input=edges;input.top=length(0,AETHER_CSS_UNIT_LINEAR);input.left=length(100,AETHER_CSS_UNIT_PERCENT);check(kind,&engine,input,ref,local);}
    // Suppression must never mask overflow; vary every side and every CSS border style.
    for(int style=AETHER_BORDER_STYLE_NONE;style<=AETHER_BORDER_STYLE_OUTSET;++style)for(unsigned n=0;n<4;++n)for(float v:{-max,max,inf,-inf,nan,-0.f,3.f}) {
        auto local=s;local.border_styles={style,style,style,style};auto input=edges;side(input,n)=length(v,AETHER_CSS_UNIT_EM);check(2,&engine,input,200,local);
    }
    for(int kind=0;kind<3;++kind)for(int axis=0;axis<3;++axis)for(float base:{0.f,-0.f,-17.f,max,inf,nan}) {
        auto e=engine;if(axis==0)e.root_font_size=base;else if(axis==1)e.viewport_width=base;else e.viewport_height=base;
        auto input=edges;input.left=length(0,AETHER_CSS_UNIT_LINEAR);input.top=length(13,AETHER_CSS_UNIT_REM);input.right=length(17,AETHER_CSS_UNIT_VW);input.bottom=length(19,AETHER_CSS_UNIT_VH);check(kind,&e,input,200,s);
    }
    {auto local=s;local.border_styles.left=AETHER_BORDER_STYLE_NONE;local.border_styles.top=AETHER_BORDER_STYLE_SOLID;local.border_styles.right=AETHER_BORDER_STYLE_HIDDEN;local.border_styles.bottom=AETHER_BORDER_STYLE_DOUBLE;check(2,&engine,edges,200,local);}
    // Double intermediates and affine cancellation; do not reassociate the authentic kernel.
    for(int kind=0;kind<3;++kind) {auto input=edges;input.left=length(50,AETHER_CSS_UNIT_PERCENT);input.top=length(max,AETHER_CSS_UNIT_LINEAR);input.top.linear_em=-max;input.top.linear_rem=max;input.top.linear_vw=-max;auto local=s;local.font_size=1;auto e=engine;e.root_font_size=1;e.viewport_width=100;check(kind,&e,input,max,local);input.right=length(-0.f);input.bottom=length(nan,AETHER_CSS_UNIT_AUTO);input.bottom.linear_em=inf;check(kind,nullptr,input,0,local);}
    std::printf("checked edges: %u cases PASS; direct=1 legacy=4 checked=%u; real resolver/order/bits/errors\n",cases,expected_calls);
}
