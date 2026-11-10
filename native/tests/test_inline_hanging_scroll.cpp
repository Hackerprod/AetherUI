// OOM transaction checks through public ABI; shared-core operator-new interposition.
#include <new>
#include <cstdlib>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(std::size_t n){if(fail_after>=0&&fail_after--==0){fault_delivered=true;throw std::bad_alloc();}if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void*p,const std::nothrow_t&) noexcept {std::free(p);}
#include "fault_inject_aligned.h"
// Hanging scroll uses a private horizontal proxy; physical text stays intact.
#define main hanging_geometry_suite
#include "test_inline_hanging.cpp"
#undef main
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_transitions.h>
#include "../src/layout/layout_internal.h"
#include "../src/layout/layout_scroll_bounds.h"
#include "../src/layout/inline_line_resolver.h"
#include <algorithm>
#include <array>
#include <limits>

static void exact(double actual, double expected, const char* message) {
    ++checks;
    if (actual != expected) {
        ++failures;
        std::fprintf(stderr,"FAIL [%s] %s: actual=%.17g expected=%.17g\n",
            scenario.c_str(),message,actual,expected);
    }
}
static aether_layout_box_t outer(Fixture& f) {
    auto box=aether_layout_result_find_primary_box(f.result,aether_doc_get_element_by_id(f.doc,"outer"));
    need(box,"outer box");return box;
}
static aether_style_map_t revision_map(Fixture& f) {
    aether_style_transaction_t tx{};aether_style_revision_t revision{};aether_style_map_t map{};
    need(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&tx)==AETHER_OK,"prepare revision");
    need(aether_style_runtime_commit_revision(f.runtime,tx,&revision)==AETHER_OK,"commit revision");
    aether_style_transaction_destroy(tx);
    need(aether_style_revision_create_map(revision,&map)==AETHER_OK,"revision map");
    aether_style_revision_destroy(revision);return map;
}
static void contextual(Fixture& f) {
    auto map=revision_map(f);aether_style_map_destroy(f.map);f.map=map;f.layout();
}
static void unchanged_runs(const std::vector<Run>& a, const std::vector<Run>& b) {
    check(a.size()==b.size(),"unchanged physical fragment count");
    for(size_t i=0;i<a.size()&&i<b.size();++i) {
        check(a[i].text==b[i].text,"unchanged physical text");
        near(a[i].rect.x,b[i].rect.x,"unchanged physical x");
        near(a[i].rect.y,b[i].rect.y,"unchanged physical y");
        near(a[i].rect.width,b[i].rect.width,"unchanged physical width");
        near(a[i].rect.height,b[i].rect.height,"unchanged physical height");
    }
}
static void basic_cases() {
    struct Case {const char* text;const char* css;float overflow,height;};
    for(auto c:std::vector<Case>{
        {"aa    bb","",0,40},{"aa    ","",0,20},{"aa    <br>","",0,20},
        {"aa    \nbb","",0,40},{"aa  ","",0,20},{"       ","",0,20},
        {"<span>aa </span><span>   </span><span>bb</span>","",0,40},
        {"abcdefgh    bb","",30,40},{"abcdefgh    ","",30,20},
        {"a a    bb","#host{width:70px;text-align:justify}",0,40},
        {"aa    ","#host{white-space:pre}",10,20},
        {"aa    bb","#host{white-space:nowrap}",0,20},
        {"aa    bb","#host{text-align:center}",0,40},
        {"aa    bb","#host{text-align:right}",0,40}}) {
        for(bool ctx:{false,true}) {
            scenario=std::string("scroll contract ")+c.text+c.css+(ctx?" contextual":" legacy");
            Fixture f(c.text,std::string("#host{overflow:auto}")+c.css);
            auto saved=f.runs();auto callbacks=f.meter.calls;
            if(ctx) contextual(f);
            near(aether_layout_box_get_scroll_metrics(f.box()).scrollable_overflow_x,c.overflow,"scroll extent");
            near(f.rect().height,c.height,"physical line height");
            unchanged_runs(saved,f.runs());
            if(!ctx)check(callbacks==f.meter.calls,"scroll reads add no callback");
        }
    }
    scenario="physical hit ownership survives proxy";
    Fixture f("aa    bb","#host{overflow:visible}");
    auto host=aether_doc_get_element_by_id(f.doc,"host");
    check(aether_layout_result_hit_test(f.result,55,5)==host,"hanging physical region still hits");
    f.expect({{"aa",{0,0,20,20}},{"    ",{20,0,40,20}},{"bb",{0,20,20,20}}});
}
struct OracleRect {double l,t,r,b;};
// Independent corner enumeration, deliberately not using production helpers.
static OracleRect oracle_project(OracleRect box, aether_transform_matrix m) {
    OracleRect out{INFINITY,INFINITY,-INFINITY,-INFINITY};
    const std::array<std::array<double,2>,4> corners{{{box.l,box.t},{box.r,box.t},{box.r,box.b},{box.l,box.b}}};
    for(auto p:corners){const double x=m.m11*p[0]+m.m21*p[1]+m.dx,y=m.m12*p[0]+m.m22*p[1]+m.dy;
        out.l=std::min(out.l,x);out.t=std::min(out.t,y);out.r=std::max(out.r,x);out.b=std::max(out.b,y);}
    return out;
}
static void public_projections() {
    struct Transform {const char* css; aether_transform_matrix matrix;};
    for(auto t:std::vector<Transform>{
        {"matrix(1,0,0,1,0,0)",{1,0,0,1,0,0}},
        {"matrix(1,0,0,1,7,11)",{1,0,0,1,7,11}},
        {"matrix(2,0,0,.5,0,0)",{2,0,0,.5f,0,0}},
        {"matrix(-2,0,0,-1,100,50)",{-2,0,0,-1,100,50}},
        {"matrix(0,0,0,0,80,60)",{0,0,0,0,80,60}},
        {"matrix(0,1,-1,0,100,0)",{0,1,-1,0,100,0}},
        {"matrix(.70710678,.70710678,-.70710678,.70710678,10,0)",{.70710678f,.70710678f,-.70710678f,.70710678f,10,0}},
        {"matrix(1,0,1,1,0,0)",{1,0,1,1,0,0}}}) {
        for(bool clip:{false,true}) {
            scenario=std::string("public corner oracle ")+t.css+(clip?" clipped":" visible");
            Fixture f("aa    bb",std::string("#outer{height:40px;overflow:auto}#host{transform-origin:0 0;transform:")+t.css+(clip?";overflow:hidden}":"}"));
            auto saved=f.runs();contextual(f);
            auto projected=oracle_project({0,0,50,40},t.matrix);
            const auto metrics=aether_layout_box_get_scroll_metrics(outer(f));
            near(metrics.scrollable_overflow_x,float(std::max(50.,projected.r)-50),"independent projected right");
            near(metrics.scrollable_overflow_y,float(std::max(40.,projected.b)-40),"independent projected bottom");
            unchanged_runs(saved,f.runs());
            for(int n=0;n<3;++n){need(aether_contextual_transition_finalize_layout(f.result,nullptr)==AETHER_OK,"repeat finalize");
                near(aether_layout_box_get_scroll_metrics(outer(f)).scrollable_overflow_x,metrics.scrollable_overflow_x,"no double transform x");
                near(aether_layout_box_get_scroll_metrics(outer(f)).scrollable_overflow_y,metrics.scrollable_overflow_y,"no double transform y");}
        }
    }
}
static void set_proxy(aether_layout_box_s* box,double l,double r) {
    box->scroll_proxy.reference_content_x=box->content_box.x;
    box->scroll_proxy.left=l;box->scroll_proxy.right=r;box->scroll_proxy.valid=true;
}
static void check_bounds(const layout_scroll_bounds_s& b,OracleRect expected,const char* what) {
    check(!b.empty,what);exact(b.left,expected.l,"proxy left");exact(b.top,expected.t,"proxy top");
    exact(b.right,expected.r,"proxy right");exact(b.bottom,expected.b,"proxy bottom");
}

static void real_fragmented_extremes() {
    struct ExtremeMeter {
        float width;int calls=0;
        static aether_text_metrics_t measure(const char* text,const aether_computed_style_t*,float,void* data) {
            auto& meter=*static_cast<ExtremeMeter*>(data);++meter.calls;
            return {text[0]==' '?meter.width:20.f,20,15};
        }
    };
    for(float width:{1.e9f,1.e38f})for(bool ctx:{false,true})for(int count:{1,2})
    for(const char* align:{"start","center","right"})for(int ending:{0,1,2}) {
        const bool soft=ending==0;const double edge=soft?(std::string(align)=="center"?35:std::string(align)=="right"?50:20):50;
        scenario="real huge tail "+std::to_string(width)+" count"+std::to_string(count)+align+" ending"+std::to_string(ending)+(ctx?" contextual":" legacy");
        std::string body="aa";for(int i=0;i<count;++i)body+="<span> </span>";body+=soft?"bb":ending==1?"<br>":"";
        Fixture f(body,std::string("#host{overflow:auto;text-align:")+align+"}");
        ExtremeMeter meter{width};aether_layout_destroy(f.engine);f.engine=nullptr;
        need(aether_layout_create(ExtremeMeter::measure,&meter,&f.engine)==AETHER_OK,"extreme measurement engine");
        if(ctx)contextual(f);else f.layout();
        check(meter.calls==1+count+int(soft),"huge fragments measured once in source order");
        near(f.rect().height,soft?40:20,"huge tail creates no phantom line");
        auto* host=f.box();need(host->children.size()==size_t(1+count+int(soft)),"huge tail kept as separate fragments");
        for(int i=1;i<=count;++i) {
            auto* text=host->children.at(i);check(text->scroll_proxy.valid,"real huge tail has proxy");
            check_bounds(aether_scroll_own_bounds(text),{!soft&&i==1?20:edge,0,edge,20},"real huge tail preserves small prefix edge");
        }
        near(aether_layout_box_get_scroll_metrics(host).scrollable_overflow_x,0,"real huge tail excluded from scroll");
        if(ctx){need(aether_contextual_transition_finalize_layout(f.result,nullptr)==AETHER_OK,"huge tail repeated finalization");
            exact(aether_scroll_own_bounds(host->children.at(count)).right,edge,"huge tail repeated exact edge");}
        aether_layout_result_t clone{};need(aether_layout_clone_snapshot(f.result,&clone)==AETHER_OK,"real huge tail clone");
        auto* copied=aether_layout_result_find_primary_box(clone,aether_doc_get_element_by_id(f.doc,"host"));
        exact(aether_scroll_own_bounds(copied->children.at(count)).right,edge,"real huge tail clone exact edge");
        aether_layout_result_destroy(clone);
    }
}

static void stable_alignment_summaries() {
    auto word=[](float width){return inline_line_metrics{width,20,15,true,false,AETHER_WHITE_SPACE_PRE_WRAP};};
    auto space=[](float width){return inline_line_metrics{width,20,15,true,true,AETHER_WHITE_SPACE_PRE_WRAP};};
    struct Case{float available;std::vector<inline_line_metrics> fragments;float soft,hard;};
    auto atom=word(5);atom.text=false;auto pre=space(5);pre.white_space=AETHER_WHITE_SPACE_PRE;auto nowrap=space(5);nowrap.white_space=AETHER_WHITE_SPACE_NOWRAP;
    std::vector<Case> cases{{50,{word(20),space(30)},20,50},{50,{word(20),space(31)},20,50},
        {0,{word(20),space(10)},20,20},{50,{space(60)},0,50},{50,{word(80),space(10)},80,80},
        {50,{word(20),space(10),atom,space(30)},35,50},
        {50,{word(20),space(30),pre},55,55},{50,{word(20),space(30),nowrap},55,55}};
    for(float huge:{1.e9f,1.e38f}){cases.push_back({50,{word(20),space(huge)},20,50});cases.push_back({50,{word(20),space(huge),space(huge)},20,50});}
    for(const auto& c:cases)for(auto ending:{inline_line_end::soft,inline_line_end::forced,inline_line_end::paragraph_end}) {
        scenario="stable retained-prefix line summary";inline_line_resolver resolver(c.available,20);float physical=0;
        for(auto fragment:c.fragments){resolver.append(fragment);physical+=fragment.width;}
        auto summary=resolver.close(ending);exact(summary.alignment_width,ending==inline_line_end::soft?c.soft:c.hard,"A survives cancellation without clipping word");
        exact(summary.physical_width,physical,"P keeps original physical accumulation");
        near(summary.height,20,"summary height unchanged");
    }
}
static void anchored_extremes_and_clone() {
    for(float x:{1.e9f,1.e38f,-1.e9f,-1.e38f}) {
        scenario="anchored extreme "+std::to_string(x);
        Fixture f("aa    bb");auto* text=f.box()->children.at(1);
        text->content_box={x,0,40,20};set_proxy(text,20,20);
        check_bounds(aether_scroll_own_bounds(text),{20,0,20,20},"initial extreme retains small edge");
        const auto reference=text->scroll_proxy;
        for(float delta:{1.f,128.f,-128.f}) {
            text->content_box.x=x+delta;text->content_box.y=-delta;
            const double dx=double(text->content_box.x)-double(x),dy=double(text->content_box.y);
            check_bounds(aether_scroll_own_bounds(text),{20+dx,dy,20+dx,20+dy},"observable offset");
            exact(text->scroll_proxy.reference_content_x,reference.reference_content_x,"reference x never reanchors");
        }
        aether_layout_result_t clone{};need(aether_layout_clone_snapshot(f.result,&clone)==AETHER_OK,"extreme clone");
        auto* copied=aether_layout_result_find_primary_box(clone,aether_doc_get_element_by_id(f.doc,"host"))->children.at(1);
        check(copied!=text,"clone owns distinct text");check(copied->scroll_proxy.valid,"clone preserves proxy validity");
        const auto a=aether_scroll_own_bounds(text),b=aether_scroll_own_bounds(copied);
        check_bounds(b,{a.left,a.top,a.right,a.bottom},"clone retains exact anchor and proxy");
        exact(copied->scroll_proxy.reference_content_x,reference.reference_content_x,"clone preserves original x reference");
        aether_layout_result_destroy(clone);
    }
}
// Exercise both consumers with a real result-owned box; restore tree membership
// before destruction. No test-only allocations participate in the accumulators.
static void segment_projection_and_clips(float initial_y = 0) {
    Fixture f("aa    bb");auto* parent=f.box();auto* text=parent->children.at(1);
    const auto children=parent->children;parent->children={text};
    text->content_box=text->margin_box=text->border_box=text->padding_box={1.e9f,0,40,20};
    text->content_box.y=initial_y;set_proxy(text,20,20);
    text->content_box.y-=initial_y;
    for(auto m:std::vector<aether_transform_matrix>{{1,0,0,1,0,0},{1,0,0,1,7,11},{-2,0,0,-1,80,60},{0,0,0,0,8,9},{0,1,-1,0,100,0},{.70710678f,.70710678f,-.70710678f,.70710678f,20,0},{1,0,1,1,0,0}}) {
        scenario="degenerate four-corner projection";
        parent->visual_sample_valid=true;text->visual_sample_valid=true;text->visual_has_transform=true;text->visual_transform=m;
        float right=-1000,bottom=-1000;accumulate_scrollable_descendant_extent(parent,true,true,right,bottom);
        auto projected=oracle_project({20,0,20,20},m);
        near(right,float(std::max(20.,projected.r)),"segment projected right union");
        near(bottom,float(std::max(20.,projected.b)),"segment projected bottom union");
        parent->visual_sample_valid=false;right=bottom=-1000;accumulate_scrollable_descendant_extent(parent,true,true,right,bottom);
        near(right,20,"legacy uses proxy not physical text");near(bottom,20,"legacy preserves segment height");
    }
    parent->children=children;
    // Use host as a clipping owner inside outer. Its own zero-size rectangle
    // sits at the origin, so it cannot manufacture a positive oracle result.
    auto* grand=outer(f);const auto grand_children=grand->children;grand->children={parent};
    parent->children={text};parent->margin_box=parent->border_box={0,0,0,0};
    parent->visual_sample_valid=true;parent->visual_has_transform=false;
    grand->visual_sample_valid=true;text->visual_has_transform=false;
    parent->style.overflow_x=AETHER_OVERFLOW_HIDDEN;parent->style.overflow_y=AETHER_OVERFLOW_HIDDEN;
    for(auto c:std::vector<std::pair<aether_rectf,bool>>{{{0,0,20,20},true},{{20,0,0,20},true},{{21,0,10,20},false},{{0,0,19,20},false},{{0,21,30,5},false},{{0,-10,30,9},false}}) {
        scenario=c.second?"clip exact segment edge":"clip disjoint segment";
        parent->padding_box=c.first;float right=-1000,bottom=-1000;
        accumulate_scrollable_descendant_extent(grand,true,true,right,bottom);
        near(right,c.second?20:0,"clip does not drop exact edge or bridge disjoint");
        near(bottom,c.second?20:0,"clip vertical extent");
    }

    scenario="nested transforms preserve segment and owner clip";
    text->visual_has_transform=true;text->visual_transform={1,0,1,1,0,0};
    parent->visual_has_transform=true;parent->visual_transform={0,1,-1,0,100,0};
    parent->padding_box={0,0,30,20};
    float right=-1000,bottom=-1000;accumulate_scrollable_descendant_extent(grand,true,true,right,bottom);
    // Child [20,0,20,20] plus skew becomes [20,0,40,20], clips to
    // [20,0,30,20], unions owner's [0,0,0,0], then rotates.
    auto nested=oracle_project({0,0,30,20},parent->visual_transform);
    near(right,float(std::max(30.,nested.r)),"nested projected right");
    near(bottom,float(std::max(20.,nested.b)),"nested projected bottom");
    scenario="zero-height and width clip retains point";
    text->visual_has_transform=false;parent->visual_has_transform=false;parent->padding_box={20,10,0,0};
    right=bottom=-1000;accumulate_scrollable_descendant_extent(grand,true,true,right,bottom);
    near(right,20,"point clip right");near(bottom,10,"point clip bottom");
    scenario="opposite disjoint proxies cannot bridge clip";
    auto* sibling=children.at(0);sibling->content_box={-10,0,0,20};
    set_proxy(sibling,-10,-10);sibling->visual_has_transform=false;
    set_proxy(text,30,30);parent->children={sibling,text};parent->padding_box={0,0,20,20};
    right=bottom=-1000;accumulate_scrollable_descendant_extent(grand,true,true,right,bottom);
    near(right,0,"separate clipped contributions never bridge horizontally");near(bottom,0,"disjoint proxies never retain phantom height");
    grand->children=grand_children;parent->children=children;
}
static void physical_proxy_coverage() {
    for(auto body:std::vector<std::string>{"aa    bb","<span>aa </span><span>   </span><span>bb</span>","       ","abcdefgh    bb"}) {
        scenario="proxy emission coverage "+body;Fixture f(body);
        bool saw=false;
        for(auto* box:f.box()->children) {
            if(box->kind!=LAYOUT_BOX_TEXT)continue;
            if(box->content_box.y==f.rect().y){check(box->scroll_proxy.valid,"all text on hanging line gets proxy");saw=true;}
            else check(!box->scroll_proxy.valid,"H=0 line has no proxy");
        }
        check(saw,"at least one hanging fragment");
    }

    scenario="partial EOF proxy";
    {Fixture f("aa    ");auto* space=f.box()->children.at(1);check_bounds(aether_scroll_own_bounds(space),{20,0,50,20},"partial tail retains fitting portion");}
    scenario="soft all-spaces line proxy";
    {Fixture f("      aa");auto* space=f.box()->children.at(0);check_bounds(aether_scroll_own_bounds(space),{0,0,0,20},"all-spaces soft line retains vertical segment");}
    for(auto c:std::vector<std::pair<const char*,double>>{{"center",35},{"right",50}}){
        scenario=std::string("aligned proxy edge ")+c.first;Fixture f("aa    bb",std::string("#host{text-align:")+c.first+"}");
        check_bounds(aether_scroll_own_bounds(f.box()->children.at(1)),{c.second,0,c.second,20},"aligned tail is anchored to real edge");}
    scenario="baseline-height proxy";
    {Fixture f("aa<span style='font-size:38px'>    </span>bb","",true);auto* space=f.box()->children.at(1);
        check_bounds(aether_scroll_own_bounds(space),{20,16,20,36},"hanging retains full shifted baseline height");}
    scenario="atomic has no proxy";
    {Fixture f("aa    <span id='atom' style='display:inline-block;width:20px;height:20px'></span>");
        auto* atom=aether_layout_result_find_primary_box(f.result,aether_doc_get_element_by_id(f.doc,"atom"));need(atom,"atomic box");
        check(!atom->scroll_proxy.valid,"atomic never receives text proxy");}
    scenario="no proxy for fallback or H0";
    for(auto css:{"#host{white-space:pre}","#host{white-space:nowrap}","#host{white-space:normal}"}) {
        Fixture f("aa    bb",css);for(auto* box:f.box()->children)check(!box->scroll_proxy.valid,"fallback proxy invalid");
    }
    Fixture f("aa  ");for(auto* box:f.box()->children)check(!box->scroll_proxy.valid,"short EOF H0 proxy invalid");
}

static void clone_faults_and_numeric_rollback() {
    scenario="proxy clone allocation rollback";
    Fixture f("aa    bb","#outer{height:40px;overflow:auto}#host{overflow:auto}");contextual(f);
    auto* text=f.box()->children.at(1);const auto proxy=text->scroll_proxy;
    auto saved=f.runs();int delivered=0;bool finished=false;
    for(long ordinal=0;ordinal<1000;++ordinal) {
        auto output=f.result;fault_delivered=false;fail_after=ordinal;
        auto status=aether_layout_clone_snapshot(f.result,&output);fail_after=-1;
        if(!fault_delivered){need(status==AETHER_OK,"clone eventual success");aether_layout_result_destroy(output);finished=true;break;}
        ++delivered;check(status==AETHER_ERROR_OUT_OF_MEMORY,"clone OOM status");check(output==f.result,"clone failure preserves output");
        check(text->scroll_proxy.valid==proxy.valid,"clone failure preserves proxy validity");
        exact(text->scroll_proxy.right,proxy.right,"clone failure preserves proxy right");
        exact(text->scroll_proxy.reference_content_x,proxy.reference_content_x,"clone failure preserves reference");
        unchanged_runs(saved,f.runs());
    }
    check(finished&&delivered>10,"clone sweep reaches success after allocations");
    std::printf("hanging clone OOM: %d failures injected\n",delivered);
    struct SavedVisual {aether_layout_box_s* box;bool valid,has; aether_transform_matrix matrix;float opacity;int64_t time;aether_scroll_metrics_t scroll;};
    const auto content=text->content_box;
    for(int invalid=0;invalid<9;++invalid) {
        scenario="invalid proxy transaction "+std::to_string(invalid);
        text->scroll_proxy=proxy;text->content_box=content;
        if(invalid==0)text->scroll_proxy.right=std::numeric_limits<double>::infinity();
        if(invalid==1)text->scroll_proxy.left=proxy.right+1;
        if(invalid==2)text->scroll_proxy.reference_content_x=std::numeric_limits<float>::quiet_NaN();
        if(invalid==3)text->content_box.y=std::numeric_limits<float>::infinity();
        if(invalid==4)text->content_box.y=std::numeric_limits<float>::quiet_NaN();
        if(invalid==5)text->content_box.height=std::numeric_limits<float>::infinity();
        if(invalid==6)text->content_box.height=std::numeric_limits<float>::quiet_NaN();
        if(invalid==7)text->content_box.height=-1;
        if(invalid==8){text->content_box.y=std::numeric_limits<float>::max();text->content_box.height=std::numeric_limits<float>::max();}
        std::vector<SavedVisual> states;
        for(auto* box:f.result->paint_order)states.push_back({box,box->visual_sample_valid,box->visual_has_transform,box->visual_transform,box->visual_opacity,box->visual_timestamp,box->scroll});
        check(aether_contextual_transition_finalize_layout(f.result,nullptr)==AETHER_ERROR_INVALID_STATE,"invalid proxy rejects finalize");
        for(const auto& old:states){auto* box=old.box;
            check(box->visual_sample_valid==old.valid&&box->visual_has_transform==old.has,"visual flags roll back");
            check(std::memcmp(&box->visual_transform,&old.matrix,sizeof(old.matrix))==0,"transform rolls back");
            exact(box->visual_opacity,old.opacity,"opacity rolls back");check(box->visual_timestamp==old.time,"timestamp rolls back");
            check(std::memcmp(&box->scroll,&old.scroll,sizeof(old.scroll))==0,"scroll metrics roll back");}
        // Invalid data remains untouched, rather than being silently reanchored.
        if(invalid==0)check(std::isinf(text->scroll_proxy.right),"failed read never repairs proxy");
        text->content_box=content;unchanged_runs(saved,f.runs());
    }
    text->scroll_proxy=proxy;need(aether_contextual_transition_finalize_layout(f.result,nullptr)==AETHER_OK,"retry after proxy numeric rejection");
}
static void transition_reuse() {
    scenario="same snapshot identity animation identity";
    Fixture f("aa    bb","#outer{height:40px;overflow:auto}#host{transition:transform 1s linear;transform-origin:0 0}");
    contextual(f);auto* host=aether_doc_get_element_by_id(f.doc,"host");
    auto before=aether_style_map_get(f.map,host);need(before,"transition before style");
    need(aether_element_set_attribute(host,"style","transform:translateX(100px)")==AETHER_OK,"transition target");
    auto next_map=revision_map(f);auto after=aether_style_map_get(next_map,host);need(after,"transition after style");
    aether_contextual_transition_runtime_t runtime{};need(aether_contextual_transition_create(&runtime)==AETHER_OK,"transition runtime");
    aether_contextual_transition_event event{1,0,host,before,after};
    aether_contextual_transition_batch_t half{},three_quarters{};
    need(aether_contextual_transition_prepare(runtime,&event,1,500,&half)==AETHER_OK,"half batch");
    need(aether_contextual_transition_prepare(runtime,&event,1,750,&three_quarters)==AETHER_OK,"three-quarter batch");
    auto original=f.result;auto saved=f.runs();const auto proxy=f.box()->children.at(1)->scroll_proxy;
    const auto measure_calls=f.meter.calls;
    aether_layout_destroy(f.engine);f.engine=nullptr;
    aether_style_runtime_destroy(f.runtime);f.runtime=nullptr;
    for(auto step:std::vector<std::pair<aether_contextual_transition_batch_t,float>>{{nullptr,0},{half,50},{half,50},{three_quarters,75},{nullptr,0},{half,50},{nullptr,0}}) {
        need(aether_contextual_transition_finalize_layout(f.result,step.first)==AETHER_OK,"same-result finalization");
        near(aether_layout_box_get_scroll_metrics(outer(f)).scrollable_overflow_x,step.second,"animated scroll extent without physical tail");
        check(f.result==original,"same layout result retained");unchanged_runs(saved,f.runs());
        auto* text=f.box()->children.at(1);exact(text->scroll_proxy.reference_content_x,proxy.reference_content_x,"animation preserves anchor");
        exact(text->scroll_proxy.right,proxy.right,"animation preserves proxy edge");
    }
    check(f.meter.calls==measure_calls,"repeated contextual projection invokes no measurement");
    aether_contextual_transition_batch_destroy(half);aether_contextual_transition_batch_destroy(three_quarters);aether_contextual_transition_destroy(runtime);
    aether_computed_style_handle_destroy(before);aether_computed_style_handle_destroy(after);aether_style_map_destroy(next_map);
}
static void shifted_layout_and_scroll_input() {
    for(auto css:{"#host{position:relative;left:13px;top:9px}","#outer{display:flex;flex-direction:column;align-items:center;width:100px}#host{width:50px}"}) {
        scenario=std::string("placement proxy ")+css;Fixture f("aa    bb",css);
        for(auto* text:f.box()->children)if(text->scroll_proxy.valid){
            auto b=aether_scroll_own_bounds(text);
            near(float(b.top),text->content_box.y,"vertical proxy follows actual placement");
            near(float(b.bottom),text->content_box.y+text->content_box.height,"proxy height follows actual placement");
            check(b.right<=double(f.box()->content_box.x)+f.box()->content_box.width,"shifted proxy remains within line bound");
        }
    }
    scenario="deferred table cell placement";
    {
        Fixture table("<table id='table'><tr><td id='cell'>aa    bb</td><td style='height:100px'>z</td></tr></table>","#table{display:table;width:100px}tr{display:table-row}td{display:table-cell}#cell{width:50px;white-space:pre-wrap;vertical-align:middle}");
        auto* cell=aether_layout_result_find_primary_box(table.result,aether_doc_get_element_by_id(table.doc,"cell"));need(cell,"table cell box");
        int proxies=0;for(auto* text:cell->children)if(text->scroll_proxy.valid){++proxies;auto b=aether_scroll_own_bounds(text);
            near(float(b.top),text->content_box.y,"table proxy follows vertical alignment");
            near(float(b.bottom),text->content_box.y+text->content_box.height,"table proxy preserves physical height");}
        check(proxies>=2,"table cell emitted hanging proxies");
    }
    scenario="scroll input clamp follows new extent";Fixture f("aa    bb","#host{overflow:auto}");
    auto offsets=+[](aether_node_t,void*)->aether_scroll_offset_t{return {100,100};};
    aether_layout_result_t result{};need(aether_layout_layout(f.engine,f.doc,f.map,600,400,offsets,nullptr,&result)==AETHER_OK,"layout scroll callback");
    auto host=aether_layout_result_find_primary_box(result,aether_doc_get_element_by_id(f.doc,"host"));
    near(aether_layout_box_get_scroll_metrics(host).scroll_offset_x,0,"hanging-only scroll request clamped");
    near(aether_layout_box_get_scroll_metrics(host).scroll_offset_y,0,"vertical scroll request clamped");
    aether_layout_result_destroy(result);
}

static void public_vertical_cancellation() {
    // Exercise production relative positioning/offset_box, without private mutation.
    for(const char* css:{
        "#host{margin-top:100000000000000000000000000000000000000px;position:relative;top:-100000000000000000000000000000000000000px}",
        "#outer{padding-top:100000000000000000000000000000000000000px}#host{position:relative;top:-100000000000000000000000000000000000000px}"})for(bool ctx:{false,true}) {
        scenario="public vertical placement cancellation";Fixture f("aa    ",css);
        if(ctx)contextual(f);
        for(auto* text:f.box()->children)if(text->scroll_proxy.valid){
            auto b=aether_scroll_own_bounds(text);
            exact(text->content_box.y,0,"production placement canceled huge y");
            exact(b.top,0,"public placement proxy top");exact(b.bottom,20,"public placement preserves height");
        }
    }
}

static void vertical_cancellation_regression() {
    for(float y:{1.e38f,-1.e38f,1.e9f,-1.e9f})for(float height:{0.f,20.f}) {
        scenario="vertical cancellation retains current physical height";
        Fixture f("aa    bb");auto* text=f.box()->children.at(1);
        text->content_box=text->margin_box=text->border_box=text->padding_box={20,y,40,height};
        set_proxy(text,20,20);
        for(auto* rect:{&text->content_box,&text->margin_box,&text->border_box,&text->padding_box})rect->y-=y;
        check_bounds(aether_scroll_own_bounds(text),{20,0,20,height},"cancel huge y to zero");
        for(auto* rect:{&text->content_box,&text->margin_box,&text->border_box,&text->padding_box}){rect->x+=7;rect->y+=9;}
        check_bounds(aether_scroll_own_bounds(text),{27,9,27,9+height},"small offset after cancellation");
        aether_layout_result_t clone{};
        need(aether_layout_clone_snapshot(f.result,&clone)==AETHER_OK,"vertical cancellation clone");
        auto* copied=aether_layout_result_find_primary_box(clone,aether_doc_get_element_by_id(f.doc,"host"))->children.at(1);
        check_bounds(aether_scroll_own_bounds(copied),{27,9,27,9+height},"clone retains restored height");
        aether_layout_result_destroy(clone);
    }
}

int main() {
    static_assert(sizeof(layout_scroll_proxy_s)==24,"horizontal proxy stays compact");
    public_vertical_cancellation();vertical_cancellation_regression();basic_cases();public_projections();physical_proxy_coverage();stable_alignment_summaries();real_fragmented_extremes();anchored_extremes_and_clone();segment_projection_and_clips();segment_projection_and_clips(1.e38f);segment_projection_and_clips(-1.e38f);clone_faults_and_numeric_rollback();transition_reuse();shifted_layout_and_scroll_input();
    std::printf("sizeof(layout_box)=%zu sizeof(scroll_proxy)=%zu\n",sizeof(aether_layout_box_s),sizeof(layout_scroll_proxy_s));
    std::printf("inline-hanging-scroll: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
