#ifndef AETHER_TRANSITION_TIME_CASES_H
#define AETHER_TRANSITION_TIME_CASES_H
#include <cfenv>
#include <limits>

// Public-API regression cases; every check remains live under NDEBUG.
namespace transition_time_cases {
constexpr int opacity = AETHER_TRANSITION_PROPERTY_OPACITY;
constexpr int transform = AETHER_TRANSITION_PROPERTY_TRANSFORM;
constexpr int both = opacity | transform;
constexpr int64_t lo = std::numeric_limits<int64_t>::min();
constexpr int64_t hi = std::numeric_limits<int64_t>::max();
constexpr float inf = std::numeric_limits<float>::infinity();
constexpr float nan = std::numeric_limits<float>::quiet_NaN();
// Adjacent binary32 seconds, with exact double products respectively
// 9223371401199616000 and 9223372474941440000. The latter is above 2^63;
// all IEEE rounding modes produce a float product >= 2^63. Thus it is rejected in every mode. No counterexample
// to a 2^63 exact-product prefilter exists among these or any binary32 inputs.
constexpr float boundary_low = 0x1.0624dcp53f;
constexpr float boundary_high = 0x1.0624dep53f;

struct Style {
    aether_computed_style_t value = aether_computed_style_default();
    Style(int properties, float progress, float duration = 1.f, float delay = 0.f) {
        value.opacity = progress;
        value.transforms = make_scale_list(1.f + progress);
        value.transition_properties = properties;
        value.transition_duration = duration;
        value.transition_delay = delay;
        value.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
static void check(aether_transition_runtime_t r, aether_node_t n, int props,
                  float progress, bool present = true) {
    float o = -9.f;
    aether_transform_matrix m{};
    const bool has_o = aether_transition_runtime_opacity_override(r,n,&o) == AETHER_TRUE;
    const bool has_t = aether_transition_runtime_transform_override(r,n,&m) == AETHER_TRUE;
    require(has_o == (present && (props & opacity)), "time: opacity presence");
    require(has_t == (present && (props & transform)), "time: transform presence");
    if (has_o) require(near(o,progress,1e-6f), "time: opacity progress");
    if (has_t) require(near(m.m11,1.f+progress,1e-6f) && near(m.m22,1.f+progress,1e-6f),
        "time: scale progress (including exact 1ms distinction)");
}
static void active(aether_transition_runtime_t r, bool expected) {
    require(aether_transition_runtime_has_active(r) == (expected ? AETHER_TRUE : AETHER_FALSE),
        "time: active state");
}
static void tick(aether_transition_runtime_t r, int64_t now, bool expected) {
    require(aether_transition_runtime_tick(r,now) == (expected ? AETHER_TRUE : AETHER_FALSE),
        "time: tick activity");
    active(r,expected);
}
static void install(aether_transition_runtime_t r, aether_node_t n, const Style& a,
                    const Style& b, int64_t now, aether_status_t status = AETHER_OK) {
    require(aether_transition_runtime_observe(r,n,&a.value,&b.value,now) == status,
        "time: observe status");
}
static void ordinary(aether_node_t n, int props) {
    for (int64_t epoch : {int64_t{0},lo,hi-1000}) {
        RuntimeFixture f; Style a(props,0), b(props,1);
        install(f.runtime,n,a,b,epoch); active(f.runtime,true); check(f.runtime,n,props,0);
        for (int64_t offset : {int64_t{1},int64_t{500},int64_t{999},int64_t{1000}}) {
            tick(f.runtime,epoch+offset,offset<1000);
            check(f.runtime,n,props,static_cast<float>(offset)/1000.f);
        }
        tick(f.runtime,lo,false); check(f.runtime,n,props,1);
    }
    for (float duration : {std::numeric_limits<float>::denorm_min(),
            std::nextafter(0.f,inf),0.0005f,0.125f,1.f}) {
        const int64_t end = duration == 1.f ? 1000 : duration == .125f ? 125 : 1;
        RuntimeFixture f; Style a(props,0), b(props,1,duration);
        install(f.runtime,n,a,b,0); active(f.runtime,true); check(f.runtime,n,props,0);
        tick(f.runtime,end-1,true); tick(f.runtime,end,false); check(f.runtime,n,props,1);
    }
}
struct RoundingGuard {
    int original = std::fegetround();
    RoundingGuard() { require(original != -1,"time: fegetround support"); }
    ~RoundingGuard() { require(std::fesetround(original)==0,"time: restore rounding mode"); }
};
static void rounding(aether_node_t n, int props) {
    RoundingGuard guard;
    require(std::nextafter(boundary_low,inf)==boundary_high,"time: boundary inputs are adjacent");
    for (int mode : {FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO}) {
        require(std::fesetround(mode)==0 && std::fegetround()==mode,"time: rounding mode supported");
        // Volatile loads make the input runtime data, not folded arithmetic.
        volatile float stored = 0x1.666666p-1f;
        RuntimeFixture f; Style a(props,0), b(props,1,stored);
        install(f.runtime,n,a,b,0);
        const bool seven_hundred = mode==FE_TONEAREST || mode==FE_UPWARD;
        tick(f.runtime,698,true);
        check(f.runtime,n,props,seven_hundred ? 698.f/700.f : 698.f/699.f);
        tick(f.runtime,699,seven_hundred);
        check(f.runtime,n,props,seven_hundred ? 699.f/700.f : 1.f);
        tick(f.runtime,700,false); check(f.runtime,n,props,1);
        for (bool high : {false,true}) {
            volatile float boundary = high ? boundary_high : boundary_low;
            RuntimeFixture g; Style c(props,0), d(props,1,boundary);
            install(g.runtime,n,c,d,0,high ? AETHER_ERROR_INVALID_STATE : AETHER_OK);
            active(g.runtime,!high);
            check(g.runtime,n,props,0,!high);
            if (!high) { tick(g.runtime,500,true); check(g.runtime,n,props,0); }
        }
        std::printf("transition time: rounding mode %d supported, properties %d\n",mode,props);
    }
}
static void extremes(aether_node_t n, int props) {
    struct Case { int64_t epoch, now; float delay, initial, expected; bool running; };
    const Case cases[] = {
        {lo,hi,0,0,1,false}, {hi,lo,0,0,0,true},
        {hi-500,hi,1,0,0,true}, {lo+100,lo+200,-.25f,.25f,.35f,true},
        {lo,lo,-2,1,1,false}, {1000,750,-.5f,.5f,.25f,true},
        {1000,500,-.5f,.5f,0,true}, {1000,499,-.5f,.5f,0,true},
        {lo,hi,-.5f,.5f,1,false}, {hi,lo,-.5f,.5f,0,true},
        {1000,999,.125f,0,0,true}
    };
    for (const auto& c : cases) {
        RuntimeFixture f; Style a(props,0),b(props,1,1,c.delay);
        install(f.runtime,n,a,b,c.epoch); active(f.runtime,c.initial<1); check(f.runtime,n,props,c.initial);
        tick(f.runtime,c.now,c.running); check(f.runtime,n,props,c.expected);
    }
    // Exact double delay truncates toward zero, including fractional negatives.
    struct Delay { float seconds; int64_t ms; };
    for (const auto& d : {Delay{0,0},Delay{-0.f,0},Delay{.125f,125},Delay{-.125f,-125},
            Delay{.0005f,0},Delay{-.0005f,0},Delay{.0015f,1},Delay{-.0015f,-1}}) {
        RuntimeFixture f; Style a(props,0),b(props,1,1,d.seconds);
        install(f.runtime,n,a,b,0);
        tick(f.runtime,500,true); check(f.runtime,n,props,static_cast<float>(500-d.ms)/1000.f);
        tick(f.runtime,1000+d.ms,false); check(f.runtime,n,props,1);
    }
    for (float delay : {boundary_low,-boundary_low}) {
        RuntimeFixture f; Style a(props,0),b(props,1,1,delay);
        install(f.runtime,n,a,b,0); active(f.runtime,delay>0); check(f.runtime,n,props,delay>0?0:1);
    }
    // -2^63 itself cannot be produced: -2^63/1000 has a factor 125 in
    // its reduced denominator. Adjacent reachable float seconds are tested.
}
static void rejection_and_atomicity(aether_node_t n, aether_node_t other) {
    struct Bad { float duration,delay; };
    const Bad bad[]={{inf,0},{1e20f,0},{std::numeric_limits<float>::max(),0},{boundary_high,0},
        {1,nan},{1,inf},{1,-inf},{1,std::numeric_limits<float>::max()},
        {1,-std::numeric_limits<float>::max()},{1,boundary_high},{1,-boundary_high}};
    for (const auto& v : bad) for (int props : {opacity,transform,both}) {
        for (int phase : {0,1,2}) {
            RuntimeFixture f; Style start(both,0), end(both,1);
            // Another node is finished before n starts, so its retained state
            // cannot be mistaken for n's activity or changed by preflight.
            install(f.runtime,other,start,end,-2000); tick(f.runtime,-1000,false);
            if (phase) { install(f.runtime,n,start,end,0); tick(f.runtime,phase==1?100:1000,phase==1); }
            Style target(props,.75f,v.duration,v.delay);
            install(f.runtime,n,end,target,500,AETHER_ERROR_INVALID_STATE);
            active(f.runtime,phase==1);
            check(f.runtime,n,both,phase==1?.1f:1.f,phase!=0);
            check(f.runtime,other,both,1);
            if (phase==1) { tick(f.runtime,750,true); check(f.runtime,n,both,.75f); }
        }
    }
}
static void unused_and_recovery(aether_node_t n, int props) {
    for (bool retained : {false,true}) for (bool changed : {false,true}) {
        for (float duration : {0.f,-0.f,-1.f,nan,-inf}) {
            RuntimeFixture f; Style a(props,0),b(props,1);
            install(f.runtime,n,a,b,0); tick(f.runtime,retained?1000:100, !retained);
            Style c(props,changed?.5f:1.f,duration,nan);
            install(f.runtime,n,b,c,250); active(f.runtime,false); check(f.runtime,n,props,0,false);
            Style unchanged(props,c.value.opacity);
            install(f.runtime,n,unchanged,unchanged,500); active(f.runtime,false);
            check(f.runtime,n,props,0,false);
            Style next(props,.8f); install(f.runtime,n,unchanged,next,600); active(f.runtime,true);
        }
        RuntimeFixture f; Style a(props,0),b(props,1);
        install(f.runtime,n,a,b,0); tick(f.runtime,retained?1000:100,!retained);
        Style disabled(0,changed?.5f:1.f,inf,nan);
        install(f.runtime,n,b,disabled,250); active(f.runtime,false); check(f.runtime,n,props,0,false);
    }
    for (bool retained : {false,true}) for (float duration : {inf,1e20f,1.f}) {
        RuntimeFixture f; Style a(props,0),b(props,1),unchanged(props,1,duration,nan);
        install(f.runtime,n,a,b,0); tick(f.runtime,retained?1000:100,!retained);
        install(f.runtime,n,b,unchanged,250); active(f.runtime,!retained);
        check(f.runtime,n,props,retained?1.f:.1f);
        if (!retained) { tick(f.runtime,500,true); check(f.runtime,n,props,.5f); }
    }
}
static void interruption(aether_node_t n, int props) {
    for (int64_t epoch : {lo,hi-2000}) for (float delay : {0.f,.125f,-.125f,-2.f}) {
        RuntimeFixture f; Style a(props,0),b(props,1),c(props,0,1,delay);
        install(f.runtime,n,a,b,epoch);
        // No tick: interruption must sample at +250, not cached origin.
        install(f.runtime,n,b,c,epoch+250);
        const float initial = delay == -2.f ? 0.f : delay == -.125f ? .21875f : .25f;
        active(f.runtime,delay!=-2.f); check(f.runtime,n,props,initial);
        if (delay!=-2.f) {
            tick(f.runtime,epoch,true); check(f.runtime,n,props,.25f);
            tick(f.runtime,epoch+750,true);
            const float expected = delay==.125f ? .15625f : delay==-.125f ? .09375f : .125f;
            check(f.runtime,n,props,expected);
        }
    }
    // Transform has a distinct no-install rule when sampled origin equals target.
    RuntimeFixture f; Style a(both,0),b(both,1),target(transform,.5f,inf,nan);
    install(f.runtime,n,a,b,0);
    install(f.runtime,n,b,target,500); active(f.runtime,false); check(f.runtime,n,both,0,false);
    RuntimeFixture g; install(g.runtime,n,a,b,0);
    Style mixed(both,.5f,inf,nan);
    install(g.runtime,n,b,mixed,500,AETHER_ERROR_INVALID_STATE);
    active(g.runtime,true); check(g.runtime,n,both,0);
    // Opacity does install when sampled value equals target, unlike transform.
    RuntimeFixture h; Style oa(opacity,0),ob(opacity,1),oc(opacity,.5f,inf,nan);
    install(h.runtime,n,oa,ob,0);
    install(h.runtime,n,ob,oc,500,AETHER_ERROR_INVALID_STATE);
    check(h.runtime,n,opacity,0); active(h.runtime,true);
}
struct PaintSummary {
    int pushes = 0, pops = 0, depth = 0;
    float scale = 0;
    opacity_paint_sample owner{}, child{};
};
static PaintSummary summarize(aether_display_list_t list) {
    PaintSummary result;
    result.owner = opacity_paint_find(list,false);
    result.child = opacity_paint_find(list,true);
    for (int32_t i=0; i<aether_display_list_get_count(list); ++i) {
        aether_paint_command_t c{};
        require(aether_display_list_get_command(list,i,&c)==AETHER_OK,"time paint: command");
        if (c.tag==AETHER_PAINT_PUSH_TRANSFORM) {
            ++result.pushes; ++result.depth;
            result.scale = c.data.push_transform.matrix.m11;
        } else if (c.tag==AETHER_PAINT_POP_TRANSFORM) {
            ++result.pops; --result.depth;
            require(result.depth>=0,"time paint: scope underflow");
        }
        aether_paint_command_free(&c);
    }
    require(result.depth==0 && result.pushes==result.pops,"time paint: balanced scopes");
    return result;
}
static void paint_extreme_and_rejection() {
    aether_doc_t doc{}; aether_style_runtime_t sr{}; aether_style_map_t styles{};
    aether_layout_engine_t engine{}; aether_layout_result_t layout{};
    aether_display_list_builder_t builder{};
    require(aether_doc_create(&doc)==AETHER_OK,"time paint: doc");
    require(aether_doc_parse_html(doc,
        "<body><div id='owner'><div id='child'></div></div></body>")==AETHER_OK,"time paint: html");
    opacity_paint_resolve(doc,
        "body{display:block;margin:0}"
        "#owner{display:block;width:100px;height:60px;background:red}"
        "#child{display:block;width:30px;height:20px;background:blue}",&sr,&styles);
    auto owner=aether_doc_get_element_by_id(doc,"owner");
    require(owner!=nullptr,"time paint: owner");
    require(aether_layout_create(measure_text,nullptr,&engine)==AETHER_OK,"time paint: engine");
    require(aether_layout_layout(engine,doc,styles,240,160,nullptr,nullptr,&layout)==AETHER_OK,"time paint: layout");
    require(aether_display_list_builder_create(measure_text,nullptr,&builder)==AETHER_OK,"time paint: builder");
    RuntimeFixture f;
    aether_display_list_builder_set_transition_runtime(builder,f.runtime);
    Style a(both,0),b(both,1),bad(opacity,.75f,inf);
    install(f.runtime,owner,a,b,lo);
    tick(f.runtime,lo+500,true); check(f.runtime,owner,both,.5f);
    aether_display_list_t prior{},rejected{},finished{};
    require(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&prior)==AETHER_OK,"time paint: prior");
    const auto first=summarize(prior);
    require(first.pushes==1 && near(first.scale,1.5f,1e-6f),"time paint: active transform scope");
    require(first.owner.found && first.child.found && near(first.owner.color.a,.5f,1e-6f) &&
        near(first.child.color.a,.5f,1e-6f),"time paint: inherited opacity override");
    // Opacity installation rejects; the other channel would have been canceled.
    install(f.runtime,owner,b,bad,lo+750,AETHER_ERROR_INVALID_STATE);
    check(f.runtime,owner,both,.5f); active(f.runtime,true);
    require(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&rejected)==AETHER_OK,"time paint: rejection");
    const auto second=summarize(rejected);
    require(first.pushes==second.pushes && first.pops==second.pops && first.scale==second.scale,
        "time paint: rejection preserved transform scopes");
    opacity_paint_compare(second.owner,first.owner); opacity_paint_compare(second.child,first.child);
    tick(f.runtime,hi,false); check(f.runtime,owner,both,1);
    tick(f.runtime,lo,false); check(f.runtime,owner,both,1);
    require(aether_display_list_build_with_styles(builder,layout,styles,nullptr,&finished)==AETHER_OK,"time paint: retained");
    const auto last=summarize(finished);
    require(last.pushes==1 && near(last.scale,2.f,1e-6f),"time paint: retained transform");
    require(last.owner.found && near(last.owner.color.a,1.f,1e-6f),"time paint: retained opacity");
    aether_display_list_destroy(prior); aether_display_list_destroy(rejected); aether_display_list_destroy(finished);
    aether_display_list_builder_destroy(builder); aether_layout_result_destroy(layout); aether_layout_destroy(engine);
    aether_style_map_destroy(styles); aether_style_runtime_destroy(sr); aether_doc_destroy(doc);
}
static void run(aether_node_t node, aether_node_t other) {
    for (int props : {opacity,transform}) {
        ordinary(node,props); rounding(node,props); extremes(node,props);
        unused_and_recovery(node,props); interruption(node,props);
    }
    rejection_and_atomicity(node,other);
    paint_extreme_and_rejection();
}
} // namespace transition_time_cases
#endif
