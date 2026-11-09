#include "content_hit_contract.h"
#include "../src/layout/hit_acceleration.h"
#include <aether/aether_contextual_transitions.h>
#include <new>
#include <random>
#include <limits>
#include <stdexcept>

// Linked with actual core objects: fault injection covers shared-pointer control
// blocks, range storage and the shared paint-order helper, not a test-only model.
static long fail_after = -1;
static bool allocation_failed = false;
static bool observe_allocations = false;
static uint64_t observed_allocations = 0, observed_bytes = 0;
void* operator new(std::size_t size) {
    if (observe_allocations) { ++observed_allocations; observed_bytes += size; }
    if (fail_after >= 0 && fail_after-- == 0) {
        fail_after = -1; allocation_failed = true; throw std::bad_alloc();
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }


using namespace content_hit_contract;
using aether::layout::HitFallback;
#ifdef AETHERUI_ENABLE_HIT_ACCELERATION
static constexpr bool acceleration = true;
#else
static constexpr bool acceleration = false;
#endif
static bool counters = false;
static aether_hit_work_counters_v1_t work() {
    aether_hit_work_counters_v1_t result{}; ok(aether_hit_work_counters_get_v1(&result)); return result;
}
static aether_node_t oracle(aether_layout_result_t layout, aether_style_map_t map, float x, float y,
    aether_scroll_offset_fn cb=nullptr, void* data=nullptr) {
    StyleOperationPin pin(layout->assets, aether::style::pin_style_map(map), map != nullptr);
    if (!pin.structure_valid() || !pin.accepts_override()) return nullptr;
    return layout->hit_test(x,y,pin.lookup(),map ? BoxStylePolicy::effective_map : BoxStylePolicy::snapshot,cb,data);
}
static void compare(aether_layout_result_t layout, aether_style_map_t map, float x, float y) {
    const auto expected = oracle(layout,map,x,y);
    const auto actual = aether_layout_result_hit_test_with_styles_and_scroll_offsets(layout,map,x,y,nullptr,nullptr);
    check(actual == expected, "accelerated and original traversal agree");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(layout,map,x,y,nullptr,nullptr) == expected,
        "repeated exact point agrees, including misses");
}
static void matrix() {
    const char* scenes[] = {
        "", "#container{overflow:hidden;width:100px;height:60px}#b{margin-left:-25px}",
        "#container{overflow-x:hidden;overflow-y:visible}#b{margin-top:-75px}",
        "#container{overflow-x:visible;overflow-y:hidden}#b{margin-left:-175px}",
        "#container{visibility:hidden}#b{visibility:visible}",
        "#container{pointer-events:none}#b{pointer-events:auto}",
        "#a{position:relative;z-index:1}#b{position:relative;z-index:2;margin-top:-25px}",
        "#a{position:relative;z-index:1}#b{position:relative;z-index:-2;margin-top:-25px}",
        "#b{width:0;height:0}", "#b{width:0;height:30px}", "#b{width:30px;height:0}",
        "#b{margin-left:-175px}", "#b{margin-top:-75px}",
        "#b::before{content:'hi';display:block;height:12px}",
        "#b{transform:translate(17px,23px);transform-origin:5px 9px}",
        "#b{transform:rotate(25deg);transform-origin:10px 4px}",
        "#b{transform:scale(0)}",
        "#container{transform:rotate(10deg)}#b{transform:translate(20px,13px)}"
    };
    std::mt19937 random(20261109);
    std::uniform_real_distribution<float> points(-200,600);
    for (const auto* css:scenes) {
        context=css; Fixture f(css);
        ok(aether_contextual_transition_finalize_layout(f.layout,nullptr));
        for (auto map:{aether_style_map_t(nullptr),f.styles}) {
            for(int n=0;n<250;++n) compare(f.layout,map,points(random),points(random));
            compare(f.layout,map,+0.f,-0.f); compare(f.layout,map,-0.f,+0.f);
            for(auto* box:f.layout->paint_order) {
                const auto r=box->border_box;
                for(float x:{r.x,r.x+r.width}) for(float y:{r.y,r.y+r.height}) {
                    compare(f.layout,map,x,y);
                    compare(f.layout,map,std::nextafter(x,-INFINITY),y);
                    compare(f.layout,map,std::nextafter(x,INFINITY),y);
                    compare(f.layout,map,x,std::nextafter(y,-INFINITY));
                    compare(f.layout,map,x,std::nextafter(y,INFINITY));
                }
            }
        }
    }
    for(const char* html:{"<body><div id='container'><div id='a'></div><button id='b'>hi</button></div></body>",
        "<body><div id='container'><div id='a'></div><div id='b'>inline fragments wrap at small widths</div></div></body>"}) {
        context="atomic/text/inline ownership"; Fixture f("#b{width:55px}",html);
        for(int n=0;n<500;++n) compare(f.layout,f.styles,points(random),points(random));
    }
}
static void cache_and_lifetime() {
    context="cache validity and lifetime"; Fixture f("");
    compare(f.layout,f.styles,75,75);
    check(f.layout->references==1,"cache does not self-pin layout");
    aether_work_counters_reset(); compare(f.layout,f.styles,75,75);
    if(counters && acceleration) check(work().cache_hits==2 && work().builds==0,"exact cache reuse");
    compare(f.layout,f.styles,5000,5000);
    aether_work_counters_reset(); compare(f.layout,f.styles,5000,5000);
    if(counters && acceleration) check(work().cache_hits==2,"exact miss reuse");
    aether_element_set_state(f.node("a"),AETHER_STATE_HOVER);
    aether_work_counters_reset(); compare(f.layout,f.styles,5000,5000);
    if(counters && acceleration) check(work().cache_misses==1 && work().builds==1,"mutation invalidates old certificate");
    const auto before=f.layout->hit_presentation_serial;
    ok(aether_contextual_transition_finalize_layout(f.layout,nullptr));
    check(f.layout->hit_presentation_serial>before && !f.layout->hit_acceleration,"finalize invalidates before write");
    compare(f.layout,f.styles,75,75);
    const auto before_failure=f.layout->hit_presentation_serial;
    allocation_failed=false; fail_after=0;
    const auto failed_finalize=aether_contextual_transition_finalize_layout(f.layout,nullptr);
    fail_after=-1;
    check(allocation_failed && failed_finalize==AETHER_ERROR_OUT_OF_MEMORY,"finalize fault injected");
    check(f.layout->hit_presentation_serial>before_failure && !f.layout->hit_acceleration,"failed finalize invalidates old cache");
    compare(f.layout,f.styles,75,75);
    aether_layout_result_t clone=nullptr; ok(aether_layout_clone_snapshot(f.layout,&clone));
    check(!clone->hit_acceleration,"clone never shares raw box index"); compare(clone,f.styles,75,75);
    aether_layout_result_destroy(clone);
    aether_style_map_t alternative=nullptr; ok(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&alternative));
    std::weak_ptr<const aether::style::MapVersion> pin=alternative->current;
    compare(f.layout,alternative,75,75);
    aether_style_map_destroy(alternative);
    if(acceleration) check(!pin.expired(),"current selected map pin retained");
    compare(f.layout,f.styles,75,75);
    check(pin.expired(),"switch map releases previous selected map pin");
    f.layout->hit_presentation_serial=std::numeric_limits<uint64_t>::max();
    f.layout->invalidate_hit_acceleration();
    check(f.layout->hit_acceleration_disabled,"serial saturation disables reuse");
    aether_work_counters_reset(); compare(f.layout,f.styles,75,75);
    if(counters && acceleration) check(work().fallbacks==2 && !f.layout->hit_acceleration,"saturated fallback, never wrapped");
    auto* detached=f.node("a");
    ok(aether_node_remove_child(f.node("container"),detached));
    check(aether_layout_result_hit_test(f.layout,75,75)==nullptr,"retained geometry rejected after topology mutation");
    delete detached; // Test uses the real private node destructor, after disconnection.
}
struct ScrollCalls {
    int count=0; bool fail=false; aether_node_t order[64]{};
    aether_layout_result_t release=nullptr;
    aether_node_t mutate=nullptr;
};
static aether_scroll_offset_t offset_callback(aether_node_t node, void* raw) {
    auto& calls=*static_cast<ScrollCalls*>(raw);
    if(calls.count<64) calls.order[calls.count]=node;
    ++calls.count;
    if(calls.release) { auto* release=calls.release; calls.release=nullptr; aether_layout_result_destroy(release); }
    if(calls.mutate) { aether_element_set_state(calls.mutate,AETHER_STATE_HOVER); calls.mutate=nullptr; }
    if(calls.fail) throw std::runtime_error("synthetic callback failure");
    return {10,10};
}
static void changed_map_same_point() {
    context="same point different overlay map";
    Fixture f("#a:hover{pointer-events:none}");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,nullptr,nullptr)==f.node("a"),"old overlay hits A");
    aether_element_set_state(f.node("a"),AETHER_STATE_HOVER);
    aether_style_update_t* updates=nullptr;
    const auto count=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&updates);
    check(count>0,"new hovered style prepared"); aether_style_updates_free(updates,count);
    aether_style_map_t next=nullptr; ok(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&next));
    compare(f.layout,next,75,75);
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,next,75,75,nullptr,nullptr)==f.node("container"),"new overlay changes target despite identical border box");
    compare(f.layout,f.styles,75,75);
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,nullptr,nullptr)==f.node("a"),"retained old overlay remains independent");
    aether_style_map_destroy(next);
}
static void callbacks_and_unknowns() {
    context="callback/unknown fallback"; Fixture f("#container{overflow:auto;width:100px;height:60px}");
    ScrollCalls expected, actual;
    auto* hit=oracle(f.layout,f.styles,75,75,offset_callback,&expected);
    aether_work_counters_reset();
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,offset_callback,&actual)==hit,
        "external callback result unchanged");
    check(expected.count==actual.count && actual.count>0,"external callback count retained");
    check(std::equal(expected.order,expected.order+std::min(expected.count,64),actual.order),"external callback order retained");
    if(counters && acceleration) check(work().fallbacks==1 && work().builds==0 && work().cache_hits==0,"callback full oracle");
    actual.fail=true;
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,offset_callback,&actual)==nullptr,
        "callback exception rejected");
    actual.fail=false;
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,offset_callback,&actual)==hit,
        "callback error was not cached as miss");
    actual.mutate=f.node("a");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,offset_callback,&actual)==nullptr,
        "callback mutation rejected by current post guards");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,offset_callback,&actual)==hit,
        "callback invalidation was not cached as miss");
    aether_layout_result_t retained=nullptr; ok(aether_layout_clone_snapshot(f.layout,&retained));
    actual.release=retained;
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(retained,f.styles,75,75,offset_callback,&actual)==hit,
        "query operation pins layout after callback releases caller handle");
    check(!actual.release,"callback release exercised");
    aether_style_map_s empty_map;
    aether_work_counters_reset(); compare(f.layout,&empty_map,75,75);
    if(counters && acceleration) check(work().fallbacks==2 && work().builds==0,"unknown map provenance fallback");
    aether_style_map_t foreign=nullptr;
    { Fixture other(""); foreign=other.styles; other.styles=nullptr; }
    aether_work_counters_reset(); compare(f.layout,foreign,75,75);
    if(counters && acceleration) check(work().fallbacks==2 && work().builds==0,"dead foreign legacy provenance preserves full oracle");
    aether_style_map_destroy(foreign);


    for(float value:{NAN,INFINITY,-INFINITY}) { compare(f.layout,f.styles,value,75); compare(f.layout,f.styles,75,value); }
    auto* box=f.layout->find_primary_box(f.node("b"));
    ok(aether_contextual_transition_finalize_layout(f.layout,nullptr));
    for(auto matrix:{aether_transform_matrix{0,0,0,0,0,0},aether_transform_matrix{NAN,0,0,1,0,0},
        aether_transform_matrix{1,0,0,1,INFINITY,0},aether_transform_matrix{1,0,0,1,12,8}}) {
        f.layout->invalidate_hit_acceleration(); box->visual_has_transform=true;box->visual_transform=matrix;
        for(float x:{60.f,70.f,75.f,110.f,190.f}) for(float y:{60.f,70.f,75.f,110.f,190.f}) compare(f.layout,f.styles,x,y);
    }
    f.layout->invalidate_hit_acceleration(); box->border_box.width=-1;
    compare(f.layout,f.styles,75,125);
    f.layout->invalidate_hit_acceleration(); box->border_box.width=NAN;
    compare(f.layout,f.styles,75,125);
}
static void failures_and_work() {
    if(!acceleration) return;
    context="OOM staged build and release"; Fixture f("#a,#b{position:relative;z-index:1}");
    const auto expected=oracle(f.layout,f.styles,75,75);
    int covered=0;
    for(long n=0;n<200;++n) {
        f.layout->invalidate_hit_acceleration(); aether_work_counters_reset();
        allocation_failed=false; fail_after=n;
        const auto result=aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,f.styles,75,75,nullptr,nullptr);
        fail_after=-1;
        check(result==expected,"OOM uses full exact fallback");
        if(!allocation_failed) break;
        ++covered;
        // libstdc++ stable_sort can recover from its optional scratch allocation
        // failure using an in-place algorithm. A completed equivalent index is
        // valid; only an actually aborted build must remain unpublished.
        if (!f.layout->hit_acceleration) {
            if(counters) check(work().fallbacks==1 && work().build_attempts==1,"aborted build work counted");
        } else if(counters) check(work().builds==1 && work().fallbacks==0,"recovered internal allocation completed whole index");
    }
    check(covered>0,"allocation failure sweep actually ran");
    std::printf("hit E1 allocation failures covered: %d\n",covered);
    aether_style_map_t map_a=nullptr,map_b=nullptr;
    ok(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&map_a));
    ok(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&map_b));
    std::weak_ptr<const aether::style::MapVersion> a=map_a->current,b=map_b->current;
    compare(f.layout,map_a,75,75); aether_style_map_destroy(map_a);
    allocation_failed=false; fail_after=0;
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,map_b,75,75,nullptr,nullptr)==expected,"B build failure fallback");
    fail_after=-1; aether_style_map_destroy(map_b);
    check(allocation_failed && a.expired() && b.expired(),"failed B build retains neither A nor B maps");
    for(int cards:{100,500,1000}) for(bool stacked:{false,true}) {
        context="catalog bounded query work";
        std::string html="<body><div id='container'>";
        for(int n=0;n<cards;++n) html+="<div class='card' id='c"+std::to_string(n)+"'></div>";
        html+="</div></body>";
        Fixture catalog(std::string("#container{position:static;width:200px;height:auto}.card{width:80px;height:20px}")+
            (stacked?".card{position:relative;z-index:1}":""),html.c_str());
        const auto expected_card=catalog.node("c0"); // Resolve fixture identity outside measured query.
        aether_work_counters_reset();
        observed_allocations=observed_bytes=0; observe_allocations=true;
        const auto first=aether_layout_result_hit_test_with_styles_and_scroll_offsets(catalog.layout,catalog.styles,1,1,nullptr,nullptr);
        observe_allocations=false;
        check(first==expected_card,"first cold card query");
        const auto build=work();
        const auto cold_allocations=observed_allocations,cold_bytes=observed_bytes;
        aether_work_counters_reset();
        observed_allocations=observed_bytes=0; observe_allocations=true;
        for(int n=0;n<50;++n)
            check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(catalog.layout,catalog.styles,float(1+(n%2)),1,nullptr,nullptr)==expected_card,"first card exact");
        observe_allocations=false;
        const auto warm=work();
        check(observed_allocations==0,"warm indexed queries allocate nothing including library scratch");
        if(counters) {
            check(build.build_boxes>=static_cast<uint64_t>(cards),"index construction work exposed");
            check(warm.build_attempts==0 && warm.build_boxes==0 && warm.fallbacks==0,"no warm hidden rebuild or fallback");
            aether_work_counters_v1_t total{}; ok(aether_work_counters_get_v1(&total));
            check(total.paint_order_children_classified==0,"no warm paint classification/sort");
            check(warm.exact_boxes<=50*8 && warm.query_range_nodes<=50*32,"bounded warm query visits");
        }
        std::printf("E1 cards=%d stacked=%d build_boxes=%llu build_ranges=%llu storage_requests=%llu storage_bytes=%llu all_new_requests=%llu all_new_bytes=%llu warm_exact=%llu warm_ranges=%llu cache_hits=%llu warm_new_requests=%llu\n",
            cards,stacked,(unsigned long long)build.build_boxes,(unsigned long long)build.build_range_nodes,
            (unsigned long long)build.allocation_requests,(unsigned long long)build.requested_bytes,
            (unsigned long long)cold_allocations,(unsigned long long)cold_bytes,
            (unsigned long long)warm.exact_boxes,(unsigned long long)warm.query_range_nodes,(unsigned long long)warm.cache_hits,
            (unsigned long long)observed_allocations);
    }
}
int main() {
    counters=aether_work_counters_set_enabled(AETHER_TRUE)!=AETHER_FALSE;
    run(); // Existing public contract suite, including input chrome.
    check(aether_hit_work_counters_get_v1(nullptr)==AETHER_ERROR_NULL_ARG,"new counter ABI rejects null");
    aether_work_counters_set_enabled(AETHER_FALSE); aether_work_counters_reset();
    aether::diagnostics::count_work(aether::diagnostics::WorkCounter::hit_cache_hits);
    check(work().cache_hits==0,"disabled hit counters remain zero");
    aether_work_counters_set_enabled(AETHER_TRUE);
    matrix(); cache_and_lifetime(); changed_map_same_point(); callbacks_and_unknowns(); failures_and_work();
    std::printf("E1 ordinary hit differential: %d assertions, %d failures, acceleration=%d counters=%d\n",assertions,failures,acceleration,counters);
    return failures?1:0;
}
