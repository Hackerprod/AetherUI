// Public layout failure boundaries with authentic direct core objects.
#define main historical_geometry_suite
#include "test_contextual_used_geometry.cpp"
#undef main
#include "../src/layout/layout_internal.h"
#include "../src/css/style_freeze.h"
#include <new>
#include <bit>
#include <cstdint>
#include <limits>
#include <cstring>
static int family=-1;
static long ordinal=-1,calls=0;
static bool injected=false;
static bool fail(int f) {if(family!=f)return false;const auto n=calls++;if(n!=ordinal)return false;injected=true;return true;}
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n) {return fail(1)?nullptr:__real_malloc(n);}
extern "C" void* __wrap_calloc(size_t n,size_t s) {return fail(2)?nullptr:__real_calloc(n,s);}
extern "C" void* __wrap_realloc(void* p,size_t n) {return fail(3)?nullptr:__real_realloc(p,n);}
void* operator new(size_t n) {if(fail(0))throw std::bad_alloc();if(auto p=__real_malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n) {return ::operator new(n);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p) noexcept {std::free(p);} void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);} void operator delete[](void* p,size_t) noexcept {std::free(p);}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);} void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
// Field-wise snapshot avoids struct padding and owns no borrowed payload.
static std::vector<uint64_t> snapshot(aether_layout_result_t result) {
    std::vector<uint64_t> v;
    auto f=[&](float x){v.push_back(std::bit_cast<uint32_t>(x));};
    auto rect=[&](aether_rectf r){f(r.x);f(r.y);f(r.width);f(r.height);};
    v.push_back(reinterpret_cast<uintptr_t>(result->root));
    for(auto* b:result->paint_order)v.push_back(reinterpret_cast<uintptr_t>(b));
    for(const auto& [node,box]:result->primary_boxes){v.push_back(reinterpret_cast<uintptr_t>(node));v.push_back(reinterpret_cast<uintptr_t>(box));}
    v.push_back(result->live_box_count);v.push_back(result->paint_order.size());v.push_back(result->primary_boxes.size());
    for(auto* b:result->allocated_boxes) {
        v.push_back(reinterpret_cast<uintptr_t>(b));v.push_back(reinterpret_cast<uintptr_t>(b->element));
        rect(b->margin_box);rect(b->border_box);rect(b->padding_box);rect(b->content_box);
        f(b->used_border.left);f(b->used_border.top);f(b->used_border.right);f(b->used_border.bottom);
        f(b->collapsed_margin_before);f(b->collapsed_margin_after);f(b->own_relative_dx);f(b->own_relative_dy);
        f(b->scroll.scrollable_overflow_x);f(b->scroll.scrollable_overflow_y);f(b->scroll.scroll_offset_x);f(b->scroll.scroll_offset_y);
        v.push_back(b->visual_sample_valid);v.push_back(b->visual_has_transform);v.push_back(b->visual_timestamp);
        const auto identity=b->visual_identity.lock();v.push_back(reinterpret_cast<uintptr_t>(identity.get()));
        f(b->visual_opacity);f(b->visual_transform.m11);f(b->visual_transform.m12);f(b->visual_transform.m21);f(b->visual_transform.m22);f(b->visual_transform.dx);f(b->visual_transform.dy);
        v.push_back(b->children.size());for(auto* c:b->children)v.push_back(reinterpret_cast<uintptr_t>(c));
    }
    return v;
}
static void overflow_atomic() {
    fixture good("#c{width:20px;height:10px}");const auto before=snapshot(good.layout);
    for(const char* group:{"margin","padding"})for(const char* side:{"left","top","right","bottom"}) {
        std::string property=std::string(group)+"-"+side;if(std::strcmp(group,"border")==0)property+="-width";
        for(const char* border:{"solid","none","hidden"}) {
            const std::string css="#p{width:1000px;height:1000px}#c{border-style:"+std::string(border)+";"+property+":3e38%}";
            // The established parser accepts these length inputs; checked edges reject the used overflow.
            fixture bad(css.c_str(),"<div id='c'></div>",false);
            auto output=good.layout;
            REQUIRE(aether_layout_layout(bad.engine,bad.doc,bad.map,800,600,nullptr,nullptr,&output)==AETHER_ERROR_INVALID_STATE);
            REQUIRE(output==good.layout);REQUIRE(snapshot(good.layout)==before);
        }
    }
    // CSS rejects percentage border widths, so exercise authentic public layout
    // against a legacy partial map with finite symbolic EM widths that overflow
    // only at used-value resolution. No immutable payload is mutated.
    for(unsigned side=0;side<4;++side)for(int border:{AETHER_BORDER_STYLE_SOLID,AETHER_BORDER_STYLE_NONE,AETHER_BORDER_STYLE_HIDDEN})for(float sign:{1.f,-1.f}) {
        using namespace aether::style;
        auto flat=aether_computed_style_default();flat.font_size=13;flat.display=AETHER_DISPLAY_BLOCK;
        flat.border_styles={border,border,border,border};
        aether_css_length width{};width.unit=AETHER_CSS_UNIT_EM;width.value=sign*3e38f;
        if(side==0)flat.border_width.left=width;else if(side==1)flat.border_width.top=width;else if(side==2)flat.border_width.right=width;else flat.border_width.bottom=width;
        FrozenStyleRef frozen;REQUIRE(freeze_style(flat,frozen)==AETHER_OK);
        auto* node=aether_doc_get_element_by_id(good.doc,"c");
        NodeRecordInput input;input.node=node;input.style=frozen;input.node_lifetime=node->identity;input.node_identity=aether_node_identity(node);
        FrozenNodeRef record;REQUIRE(freeze_node_record(input,record)==AETHER_OK);
        BackingBuildInput backing;backing.entries=good.map->current->backing()->entries();backing.entries[node]=record;
        MapVersionInput version;REQUIRE(freeze_backing(backing,version.backing)==AETHER_OK);
        aether_style_map_s map;REQUIRE(make_map_version(version,map.current)==AETHER_OK);
        auto output=good.layout;const auto status=aether_layout_layout(good.engine,good.doc,&map,800,600,nullptr,nullptr,&output);if(status!=AETHER_ERROR_INVALID_STATE)std::fprintf(stderr,"border overflow side=%u style=%d sign=%g status=%d\n",side,border,sign,status);REQUIRE(status==AETHER_ERROR_INVALID_STATE);
        REQUIRE(output==good.layout);REQUIRE(snapshot(good.layout)==before);
        aether_layout_result_t retry{};REQUIRE(aether_layout_layout(good.engine,good.doc,good.map,800,600,nullptr,nullptr,&retry)==AETHER_OK);aether_layout_result_destroy(retry);
    }
    fixture sum("#c{width:3e38px;padding-left:3e38px}","<div id='c'></div>",false);
    auto output=good.layout;REQUIRE(aether_layout_layout(sum.engine,sum.doc,sum.map,800,600,nullptr,nullptr,&output)==AETHER_ERROR_INVALID_STATE);REQUIRE(output==good.layout);REQUIRE(snapshot(good.layout)==before);
}
static void oom_sweep() {
    // Fresh result per call; retained baseline is never the operation's mutable candidate.
    fixture f("#p{display:flex;width:200px;padding:2%;border:1px solid red;transform:translateX(0)}#c{margin:-3px;padding:1rem;border:2px hidden blue;width:20px;height:10px;text-shadow:1px 2px red}#fixed{position:fixed;left:10%;width:20px;height:10px}","anonymous text<div id='c'>edge text</div><div id='fixed'></div>");
    aether_layout_set_contextual_text_measurer(f.engine,[](const char*,const aether_computed_style_t*,float h,float,void*)->aether_text_metrics_t{return {20,h,h*.8f};},nullptr);
    const auto before=snapshot(f.layout);const auto generation=f.map->current->generation();
    for(int kind=0;kind<4;++kind) {
        long failures=0,reachable=0;
        for(long i=0;i<10000;++i) {
            auto output=f.layout;family=kind;ordinal=i;calls=0;injected=false;
            const auto status=aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&output);
            family=-1;reachable=calls;
            REQUIRE(snapshot(f.layout)==before);REQUIRE(f.map->current->generation()==generation);
            if(!injected) {REQUIRE(status==AETHER_OK);aether_layout_result_destroy(output);break;}
            REQUIRE(status==AETHER_ERROR_OUT_OF_MEMORY);REQUIRE(output==f.layout);++failures;
            aether_layout_result_t retry{};REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&retry)==AETHER_OK);aether_layout_result_destroy(retry);
            REQUIRE(i<9999);
        }
        if(kind==0||kind==1)REQUIRE(failures>0);
        std::printf("edge layout OOM family=%d failures=%ld reachable=%ld%s\n",kind,failures,reachable,reachable?"":" (unreachable in fixture)");
    }
}
int main() { REQUIRE(historical_geometry_suite()==0);std::fprintf(stderr,"historical geometry complete\n");overflow_atomic();oom_sweep();std::puts("checked edges public atomic PASS"); }
