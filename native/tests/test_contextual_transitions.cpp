#include <cstdio>
#include <new>
#include <cstdlib>
static int fail_allocation_after = -1;
void* operator new(std::size_t size) {
    if (fail_allocation_after == 0) throw std::bad_alloc();
    if (fail_allocation_after > 0) --fail_allocation_after;
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
#include "../src/animations/contextual_transitions.cpp"
#include "../src/rendering/transform_matrix.h"
#include <cassert>
#include <iostream>
#include <functional>

static void require_test(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "requirement failed at line %d\n", line);
        std::abort();
    }
}
#define REQUIRE(condition) require_test(bool(condition), __LINE__)

static aether_css_length px(float x){aether_css_length v{};v.unit=AETHER_CSS_UNIT_PX;v.value=x;return v;}
static aether_css_length pct(float x){auto v=px(x);v.unit=AETHER_CSS_UNIT_PERCENT;return v;}
static auto test_token=std::make_shared<node_identity_s>();
static auto test_document=std::make_shared<node_identity_s>();
struct Style {aether_computed_style_handle_s h; aether_transform_operation op{};Style(float percentage,int kind=AETHER_TRANSFORM_TRANSLATE){h.node_identity=1;h.document_identity=1;test_token->owner=reinterpret_cast<void*>(1);h.node_lifetime=test_token;h.document_lifetime=test_document;h.style.opacity=1;h.style.transform_origin={pct(50),pct(50)};h.style.transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM;h.style.transition_duration=1;h.style.transition_timing=AETHER_TRANSITION_TIMING_LINEAR;op.kind=kind;op.x=pct(percentage);op.y=px(0);op.scalar_x=op.scalar_y=1;h.style.transforms={&op,1};}};
static void near(double a,double b){REQUIRE(std::abs(a-b)<.002);}
static aether_contextual_transition_sample sample(aether_contextual_transition_batch_t b,Style&s,float w=200,float h=100,float x=0,float y=0){aether_contextual_transition_sample o{};REQUIRE(aether_contextual_transition_sample_box(b,&s.h,{x,y,w,h},&o)==AETHER_OK);return o;}
int main(){
    static_assert(sizeof(ContextualAffine)==10*sizeof(double));
    aether_contextual_transition_runtime_t r=nullptr;REQUIRE(aether_contextual_transition_create(&r)==AETHER_OK);
    Style a(0),b(50);aether_contextual_transition_event event{1,100,reinterpret_cast<aether_node_t>(1),&a.h,&b.h};
    aether_contextual_transition_batch_t candidate=nullptr;
    b.h.style.transition_delay=.2f;
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,200,&candidate)==AETHER_OK);near(sample(candidate,b,400).matrix.dx,0);REQUIRE(aether_contextual_transition_invalidation(candidate)==6);aether_contextual_transition_batch_destroy(candidate);
    // Failed frame retry retains original event clock and delay, not retry time.
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,800,&candidate)==AETHER_OK);near(sample(candidate,b,200).matrix.dx,50);near(sample(candidate,b,400).matrix.dx,100);REQUIRE(r->state->consumed==0);REQUIRE(aether_contextual_transition_commit(r,candidate)==AETHER_OK);REQUIRE(r->state->consumed==1);aether_contextual_transition_batch_destroy(candidate);
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,1301,&candidate)==AETHER_OK);near(sample(candidate,b,400).matrix.dx,200);REQUIRE(!sample(candidate,b).active);REQUIRE(aether_contextual_transition_commit(r,candidate)==AETHER_OK);aether_contextual_transition_batch_destroy(candidate);
    REQUIRE(aether_contextual_transition_prepare(r,nullptr,0,2000,&candidate)==AETHER_OK);near(sample(candidate,b,600).matrix.dx,300);aether_contextual_transition_batch_destroy(candidate);
    // Current origin is composed once in world coordinates.
    Style rotate(0,AETHER_TRANSFORM_ROTATE);rotate.op.angle_radians=float(3.141592653589793/2);auto s=sample(nullptr,rotate,200,100,500,300);near(s.matrix.dx,950);near(s.matrix.dy,-250);
    // Incompatible operation_form interpolation oracle: copy_endpoint matrices at real box.
    Style scale(0,AETHER_TRANSFORM_SCALE);scale.op.scalar_x=2;scale.op.scalar_y=3;b.h.style.transition_delay=0;
    aether_contextual_transition_destroy(r);aether_contextual_transition_create(&r);event={1,0,reinterpret_cast<aether_node_t>(1),&b.h,&scale.h};
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,500,&candidate)==AETHER_OK);
    auto mixed=sample(candidate,scale,400);near(mixed.matrix.m11,1.5);near(mixed.matrix.dx,0); // tx100 + origin200*(1-1.5)
    REQUIRE(aether_contextual_transition_commit(r,candidate)==AETHER_OK);aether_contextual_transition_batch_destroy(candidate);
    // Every interruption keeps ten canonical doubles, no historical handles.
    for(uint64_t i=2;i<10002;++i){Style next(float(i+1));event={i,int64_t(i*2+500),reinterpret_cast<aether_node_t>(1),&scale.h,&next.h};
        REQUIRE(aether_contextual_transition_prepare(r,&event,1,event.change_timestamp_ms,&candidate)==AETHER_OK);
        REQUIRE(candidate->state->entries.size()==1);const auto&start=candidate->state->entries[0].transform.from;REQUIRE(start.canonical);REQUIRE(start.ops.empty());REQUIRE(start.form.size()==10);
        sample(candidate,next,float(200+i%701),float(100+i%367));REQUIRE(aether_contextual_transition_commit(r,candidate)==AETHER_OK);aether_contextual_transition_batch_destroy(candidate);
    }
    // An independent uncompacted oracle resolves original operation lists at
    // each requested box, then blends six matrices recursively. It deliberately
    // retains history only in this short test, never in the runtime.
    ContextualAffine canonical = identity();
    using MatrixValues = std::array<double, 6>;
    std::function<MatrixValues(float, float)> oracle = [](float, float) {
        return MatrixValues{1, 0, 0, 1, 0, 0};
    };
    for (int i = 0; i < 15; ++i) {
        std::vector<aether_transform_operation> operations(2);
        operations[0].kind = AETHER_TRANSFORM_ROTATE;
        operations[0].angle_radians = float(i) / 7;
        operations[1].kind = AETHER_TRANSFORM_TRANSLATE;
        operations[1].x = pct(float(3 * i + 7));
        operations[1].y = pct(float(2 * i + 5));
        auto style = aether_computed_style_t{};
        style.transforms = {operations.data(), 2};
        auto target = copy_endpoint(style).form;
        double p = .13 + i * .03;
        auto old = oracle;
        oracle = [old, operations, p](float width, float height) {
            aether_transform_list list{const_cast<aether_transform_operation*>(operations.data()), 2};
            auto matrix = aether_transform_matrix_resolve(list, {px(0), px(0)},
                {0, 0, width, height}, 0, 0, {0, 0});
            MatrixValues result{matrix.m11, matrix.m12, matrix.m21, matrix.m22, matrix.dx, matrix.dy};
            const auto previous = old(width, height);
            for (size_t j = 0; j < result.size(); ++j)
                result[j] = previous[j] * (1 - p) + result[j] * p;
            return result;
        };
        canonical = blend_coefficients(canonical, target, p);
        for (float width : {20.f, 200.f, 700.f}) {
            const float height = width / 2 + 17;
            auto expected = oracle(width, height);
            for (int j = 0; j < 4; ++j) near(canonical[j], expected[j]);
            near(canonical[4] + canonical[5] * width + canonical[6] * height, expected[4]);
            near(canonical[7] + canonical[8] * width + canonical[9] * height, expected[5]);
        }
    }
    // Invalid prepare leaves prior consumed state and output unchanged.
    auto previous=r->state->consumed;Style invalid(0);invalid.op.x.unit=AETHER_CSS_UNIT_REM;event={10002,99999,reinterpret_cast<aether_node_t>(1),&scale.h,&invalid.h};candidate=nullptr;REQUIRE(aether_contextual_transition_prepare(r,&event,1,99999,&candidate)==AETHER_ERROR_INVALID_STATE);REQUIRE(!candidate&&r->state->consumed==previous);
    // Opacity-only does not invalidate layout.
    aether_contextual_transition_destroy(r);aether_contextual_transition_create(&r);Style o1(0),o2(0);o1.h.style.opacity=.2f;o2.h.style.opacity=.8f;o2.h.style.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;event={1,0,reinterpret_cast<aether_node_t>(1),&o1.h,&o2.h};REQUIRE(aether_contextual_transition_prepare(r,&event,1,500,&candidate)==AETHER_OK);REQUIRE(aether_contextual_transition_invalidation(candidate)==4);near(sample(candidate,o2).opacity,.5);aether_contextual_transition_batch_destroy(candidate);
    // A committed snapshot stays sampleable and stale siblings cannot commit.
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,600,&candidate)==AETHER_OK);
    aether_contextual_transition_batch_t stale=nullptr;
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,700,&stale)==AETHER_OK);
    REQUIRE(aether_contextual_transition_commit(r,candidate)==AETHER_OK);
    near(sample(candidate,o2).opacity,.56);
    REQUIRE(aether_contextual_transition_commit(r,stale)==AETHER_ERROR_INVALID_STATE);
    aether_contextual_transition_batch_destroy(stale);
    aether_contextual_transition_destroy(r);r=nullptr;
    near(sample(candidate,o2).opacity,.56);
    aether_contextual_transition_batch_destroy(candidate);
    // Compatible rotations interpolate_endpoint angles, not affine coefficients.
    aether_contextual_transition_create(&r);
    Style turn0(0,AETHER_TRANSFORM_ROTATE),turn1(0,AETHER_TRANSFORM_ROTATE);
    turn1.op.angle_radians=float(3.141592653589793);
    event={1,0,reinterpret_cast<aether_node_t>(1),&turn0.h,&turn1.h};
    REQUIRE(aether_contextual_transition_prepare(r,&event,1,500,&candidate)==AETHER_OK);
    near(sample(candidate,turn1).matrix.m12,1);
    near(sample(candidate,turn1).matrix.m11,0);
    aether_contextual_transition_batch_destroy(candidate);
    // Allocation failure at every prepare reservation never publishes state.
    event={2,600,reinterpret_cast<aether_node_t>(1),&turn1.h,&b.h};
    const auto old_generation=r->generation;
    const auto old_consumed=r->state->consumed;
    int failures=0, successes=0;
    for(int allocation=0;allocation<40;++allocation){
        candidate=nullptr;
        fail_allocation_after=allocation;
        const auto status=aether_contextual_transition_prepare(r,&event,1,700,&candidate);
        fail_allocation_after=-1;
        REQUIRE(r->generation==old_generation && r->state->consumed==old_consumed);
        if(status==AETHER_ERROR_OUT_OF_MEMORY){++failures;REQUIRE(candidate==nullptr);}
        else {REQUIRE(status==AETHER_OK);++successes;aether_contextual_transition_batch_destroy(candidate);}
    }
    REQUIRE(failures>3 && successes>0);
    // Unordered unconsumed commands must reject, never silently lose an event.
    aether_contextual_transition_event unordered[] = {
        {3, 600, reinterpret_cast<aether_node_t>(1), &turn1.h, &b.h},
        {2, 600, reinterpret_cast<aether_node_t>(1), &turn1.h, &b.h}
    };
    candidate = reinterpret_cast<aether_contextual_transition_batch_t>(1);
    REQUIRE(aether_contextual_transition_prepare(r, unordered, 2, 700, &candidate) == AETHER_ERROR_INVALID_STATE);
    REQUIRE(candidate == nullptr && r->state->consumed == old_consumed);
    aether_contextual_transition_sample unchanged{};
    unchanged.matrix.dx = 77;
    REQUIRE(aether_contextual_transition_sample_box(nullptr, &b.h,
        {0, 0, -1, 100}, &unchanged) == AETHER_ERROR_INVALID_STATE);
    REQUIRE(unchanged.matrix.dx == 77);
    // Computed viewport and root units are copy_endpoint pixels, not sampled again.
    Style viewport(0);viewport.op.x=px(80);near(sample(nullptr,viewport,200).matrix.dx,80);
    viewport.op.x=px(32);near(sample(nullptr,viewport,700).matrix.dx,32);
    aether_contextual_transition_destroy(r);std::cout<<"contextual transitions: retry, resize, origin, affine oracle, 10000 interruptions PASS\n";
}
