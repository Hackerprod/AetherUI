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
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

// Route nothrow and array allocations through the same fault allocator. This
// keeps ASan allocation families matched instead of suppressing its checks.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

#include "../src/animations/contextual_transitions.cpp"
#include "../src/rendering/transform_matrix.h"
#include "../src/css/frozen_style.h"
#include "../src/css/style_handle_export.h"
#include "../src/dom/dom_types.h"
#include <cassert>
#include <iostream>
#include <functional>
#include <bit>
#include <cstdint>

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
// Owners survive both adaptation and the complete internal sample operation.
static aether::style::FrozenStyle freeze_sample_style(const Style& source) {
    using namespace aether::style;
    const auto& s = source.h.style;
    REQUIRE(s.transforms.count >= 0);
    REQUIRE(!s.transforms.count || s.transforms.items);
    StyleScalars scalars{};
    scalars.opacity = s.opacity;
    scalars.transform_origin = s.transform_origin;
    auto transforms = ImmutableArray<aether_transform_operation>::generate(
        static_cast<size_t>(s.transforms.count), [&](size_t i) { return s.transforms.items[i]; });
    auto payload = std::make_shared<const FrozenStylePayload>(
        ImmutableArray<FrozenBackgroundLayer>{}, ImmutableArray<aether_backdrop_filter>{},
        FrozenGradient{}, std::move(transforms), ImmutableArray<aether_shadow>{},
        ImmutableArray<aether_shadow>{});
    return FrozenStyle(scalars, std::move(payload));
}
static void same_sample(const aether_contextual_transition_sample& a,
                        const aether_contextual_transition_sample& b) {
    const auto bits=[](float value){return std::bit_cast<uint32_t>(value);};
    REQUIRE(bits(a.matrix.m11) == bits(b.matrix.m11)); REQUIRE(bits(a.matrix.m12) == bits(b.matrix.m12));
    REQUIRE(bits(a.matrix.m21) == bits(b.matrix.m21)); REQUIRE(bits(a.matrix.m22) == bits(b.matrix.m22));
    REQUIRE(bits(a.matrix.dx) == bits(b.matrix.dx)); REQUIRE(bits(a.matrix.dy) == bits(b.matrix.dy));
    REQUIRE(bits(a.opacity) == bits(b.opacity)); REQUIRE(a.has_transform == b.has_transform);
    REQUIRE(a.active == b.active);
    REQUIRE(a.presentation_timestamp_ms == b.presentation_timestamp_ms);
}
static aether_contextual_transition_sample sample(aether_contextual_transition_batch_t b,
    Style& s, float w=200, float h=100, float x=0, float y=0) {
    auto frozen = freeze_sample_style(s);
    aether_contextual_transition_sample public_value{}, internal_value{};
    const auto public_status = aether_contextual_transition_sample_box(b, &s.h, {x,y,w,h}, &public_value);
    const auto internal_status = sample_box_read(b,
        sample_inputs(frozen.view(), s.h.document_identity, s.h.node_identity), {x,y,w,h}, &internal_value);
    REQUIRE(public_status == internal_status); REQUIRE(public_status == AETHER_OK);
    same_sample(public_value, internal_value);
    return public_value;
}
static void invalid_sample(aether_contextual_transition_batch_t batch, Style& s,
                           aether_rectf rect, bool representable_frozen=true) {
    aether_contextual_transition_sample public_value{}, internal_value{};
    public_value.matrix = {11,12,13,14,15,16}; public_value.opacity = .37f;
    public_value.has_transform = AETHER_TRUE; public_value.active = AETHER_TRUE;
    public_value.presentation_timestamp_ms = -987654321;
    internal_value = public_value;
    const auto sentinel = public_value;
    const auto public_status = aether_contextual_transition_sample_box(batch, &s.h, rect, &public_value);
    aether_status_t internal_status;
    if (representable_frozen) {
        auto frozen = freeze_sample_style(s);
        internal_status = sample_box_read(batch,
            sample_inputs(frozen.view(), s.h.document_identity, s.h.node_identity), rect, &internal_value);
    } else {
        // Negative count and null nonempty C lists cannot exist in ImmutableArray.
        internal_status = sample_box_read(batch, sample_inputs(s.h), rect, &internal_value);
    }
    REQUIRE(public_status == AETHER_ERROR_INVALID_STATE);
    REQUIRE(internal_status == public_status);
    same_sample(public_value, sentinel); same_sample(internal_value, sentinel);
}
static void sampler_input_contract() {
    Style from(0), to(50);
    aether_contextual_transition_runtime_t runtime = nullptr;
    REQUIRE(aether_contextual_transition_create(&runtime) == AETHER_OK);
    aether_contextual_transition_event event{1,0,reinterpret_cast<aether_node_t>(1),&from.h,&to.h};
    aether_contextual_transition_batch_t active_batch = nullptr;
    REQUIRE(aether_contextual_transition_prepare(runtime,&event,1,500,&active_batch) == AETHER_OK);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // Validate current even when the active transform channel will replace it.
    for (auto batch : {aether_contextual_transition_batch_t(nullptr), active_batch}) {
        auto reject = [&](const std::function<void(Style&)>& change, bool frozen=true) {
            Style bad(50); change(bad); invalid_sample(batch,bad,{0,0,200,100},frozen);
        };
        reject([](Style& s){s.h.style.transforms.count=-1;},false);
        reject([](Style& s){s.h.style.transforms.items=nullptr;},false);
        reject([](Style& s){s.op.kind=-1;});
        reject([&](Style& s){s.op.x.value=nan;});
        reject([&](Style& s){s.op.y.value=inf;});
        reject([](Style& s){s.op.x.unit=AETHER_CSS_UNIT_REM;});
        reject([](Style& s){s.op.x.unit=AETHER_CSS_UNIT_LINEAR;s.op.x.linear_em=1;});
        reject([&](Style& s){s.op.x.unit=AETHER_CSS_UNIT_LINEAR;s.op.x.linear_percent=nan;});
        reject([&](Style& s){s.op.kind=AETHER_TRANSFORM_SCALE;s.op.scalar_x=inf;});
        reject([&](Style& s){s.op.kind=AETHER_TRANSFORM_ROTATE;s.op.angle_radians=nan;});
        reject([&](Style& s){s.op.kind=AETHER_TRANSFORM_MATRIX;s.op.matrix_f=inf;});
        reject([&](Style& s){s.h.style.opacity=nan;});
        reject([](Style& s){s.h.style.transform_origin.x.unit=AETHER_CSS_UNIT_EM;});
        reject([&](Style& s){s.h.style.transform_origin.y.value=inf;});
        reject([](Style& s){s.h.document_identity=0;});
        reject([](Style& s){s.h.node_identity=0;});
        for (auto rect : {aether_rectf{nan,0,200,100}, aether_rectf{0,inf,200,100},
            aether_rectf{0,0,nan,100},aether_rectf{0,0,200,inf},
            aether_rectf{0,0,-1,100},aether_rectf{0,0,200,-1}})
            invalid_sample(batch,to,rect);
    }
    // Multiple separately-constructed immutable slots exercise indexed reverse composition.
    Style multi(0);
    aether_transform_operation operations[3]{};
    operations[0].kind=AETHER_TRANSFORM_ROTATE; operations[0].angle_radians=.7f;
    operations[1].kind=AETHER_TRANSFORM_TRANSLATE; operations[1].x=pct(25); operations[1].y=px(13);
    operations[2].kind=AETHER_TRANSFORM_SCALE; operations[2].scalar_x=2; operations[2].scalar_y=3;
    multi.h.style.transforms={operations,3};
    sample(nullptr,multi,300,150,-17,24);
    for (int kind : {AETHER_TRANSFORM_SKEW_X,AETHER_TRANSFORM_SKEW_Y,AETHER_TRANSFORM_MATRIX}) {
        Style single(0,kind); single.op.angle_radians=.2f;
        single.op.matrix_a=1; single.op.matrix_d=2; single.op.matrix_e=7; single.op.matrix_f=9;
        sample(nullptr,single,250,70,19,-8);
    }
    Style empty(0); empty.h.style.transforms={nullptr,0};
    REQUIRE(!sample(nullptr,empty,0,0).has_transform);
    empty.h.style.opacity=1.5f; near(sample(nullptr,empty).opacity,1.5);
    // Distinct IDs must use current, not the batch's animated entry.
    to.h.document_identity=2; REQUIRE(!sample(active_batch,to).active);
    to.h.document_identity=1; to.h.node_identity=2; REQUIRE(!sample(active_batch,to).active);
    aether_contextual_transition_sample sentinel{}; sentinel.matrix.dx=123;
    REQUIRE(aether_contextual_transition_sample_box(nullptr,nullptr,{0,0,1,1},&sentinel)==AETHER_ERROR_NULL_ARG);
    REQUIRE(sentinel.matrix.dx==123);
    REQUIRE(aether_contextual_transition_sample_box(nullptr,&to.h,{0,0,1,1},nullptr)==AETHER_ERROR_NULL_ARG);
    auto frozen=freeze_sample_style(to);
    REQUIRE(sample_box_read(nullptr,sample_inputs(frozen.view(),1,2),{0,0,1,1},nullptr)==AETHER_ERROR_NULL_ARG);
    // Build the immutable owner before arming faults. Only kernel endpoint work
    // is measured here, not fixture setup or payload construction.
    Style fault_style(50);
    auto fault_frozen=freeze_sample_style(fault_style);
    int oom=0, successful=0;
    for (int ordinal=0;ordinal<12;++ordinal) {
        aether_contextual_transition_sample public_value{{11,12,13,14,15,16},.37f,
            AETHER_TRUE,AETHER_TRUE,-987654321};
        auto internal_value=public_value;
        const auto old=public_value;
        fail_allocation_after=ordinal;
        const auto public_status=aether_contextual_transition_sample_box(active_batch,
            &fault_style.h,{0,0,200,100},&public_value);
        fail_allocation_after=-1;
        fail_allocation_after=ordinal;
        const auto internal_status=sample_box_read(active_batch,
            sample_inputs(fault_frozen.view(),1,1),{0,0,200,100},&internal_value);
        fail_allocation_after=-1;
        REQUIRE(public_status==internal_status);
        if(public_status==AETHER_ERROR_OUT_OF_MEMORY) {
            ++oom; same_sample(public_value,old); same_sample(internal_value,old);
        } else {
            REQUIRE(public_status==AETHER_OK); ++successful;
            same_sample(public_value,internal_value);
        }
    }
    REQUIRE(oom>0 && successful>0);
    aether_contextual_transition_batch_destroy(active_batch);
    aether_contextual_transition_destroy(runtime);
}

// Authentic DOM controls and factory-created versions test metadata adaptation;
// legacy partial allows the binding document to differ from the record document.
static void sampler_binding_contract() {
    using namespace aether::style;
    aether_doc_s source_document, selected_document;
    auto leaf=std::make_shared<aether_node_s>();
    leaf->is_generated=true; leaf->snapshot_owned=true;
    Style source(25);
    NodeRecordInput record_input;
    record_input.node=leaf.get(); record_input.generated_leaf=leaf;
    record_input.node_lifetime=leaf->identity;
    record_input.node_identity=aether_node_identity(leaf.get());
    record_input.document_lifetime=source_document.identity;
    record_input.document_identity=aether_node_identity(&source_document);
    record_input.style=std::make_shared<const FrozenStyle>(freeze_sample_style(source));
    FrozenNodeRef record;
    REQUIRE(freeze_node_record(record_input,record)==AETHER_OK);
    BackingBuildInput backing_input;
    backing_input.entries.emplace(leaf.get(),record);
    MapVersionInput version_input;
    REQUIRE(freeze_backing(backing_input,version_input.backing)==AETHER_OK);
    version_input.document_lifetime=selected_document.identity;
    version_input.document_identity=aether_node_identity(&selected_document);
    MapVersionRef selected_version;
    REQUIRE(make_map_version(version_input,selected_version)==AETHER_OK);
    REQUIRE(selected_version->mode()==MapMode::legacy_partial);
    REQUIRE(selected_version->document_identity()!=record->document_identity());
    const auto inputs=sample_inputs(*selected_version,BoundNodeView::from_legacy(record));
    REQUIRE(inputs.document_identity==selected_version->document_identity());
    REQUIRE(inputs.node_identity==record->node_identity());
    aether_computed_style_handle_t owned=nullptr;
    REQUIRE(export_style_handle(*selected_version,record,&owned)==AETHER_OK);
    REQUIRE(owned->document_identity==inputs.document_identity);
    REQUIRE(owned->node_identity==inputs.node_identity);
    aether_contextual_transition_sample public_value{},internal_value{};
    REQUIRE(aether_contextual_transition_sample_box(nullptr,owned,{9,-7,200,100},&public_value)==AETHER_OK);
    REQUIRE(sample_box_read(nullptr,inputs,{9,-7,200,100},&internal_value)==AETHER_OK);
    same_sample(public_value,internal_value);
    auto reject_member=[&](const FrozenNodeRef& nonmember) {
        auto unchanged=owned;
        REQUIRE(export_style_handle(*selected_version,nonmember,&unchanged)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(unchanged==owned);
        bool rejected=false;
        try { (void)sample_inputs(*selected_version,BoundNodeView::from_legacy(nonmember)); }
        catch(const std::invalid_argument&) { rejected=true; }
        REQUIRE(rejected);
    };
    FrozenNodeRef different_record;
    REQUIRE(freeze_node_record(record_input,different_record)==AETHER_OK);
    reject_member(different_record);
    // Same pointer is deliberately wrapped in a foreign control block only to
    // prove rejection; the genuine record owner remains alive throughout.
    FrozenNodeRef foreign_control(record.get(),[](const FrozenNodeRecord*){});
    REQUIRE(foreign_control.get()==record.get());
    REQUIRE(foreign_control.owner_before(record)||record.owner_before(foreign_control));
    reject_member(foreign_control);
    reject_member(FrozenNodeRef{});
    aether_computed_style_handle_destroy(owned); owned=nullptr;
    // A partial version with no global identity must not inherit record identity.
    version_input.document_lifetime.reset(); version_input.document_identity=0;
    MapVersionRef legacy_version;
    REQUIRE(make_map_version(version_input,legacy_version)==AETHER_OK);
    REQUIRE(record->document_identity()!=0);
    const auto legacy_inputs=sample_inputs(*legacy_version,BoundNodeView::from_legacy(record));
    REQUIRE(legacy_inputs.document_identity==0);
    REQUIRE(legacy_inputs.node_identity==record->node_identity());
    REQUIRE(export_style_handle(*legacy_version,record,&owned)==AETHER_OK);
    REQUIRE(owned->document_identity==0);
    public_value={{11,12,13,14,15,16},.37f,AETHER_TRUE,AETHER_TRUE,-987654321};
    internal_value=public_value;
    const auto sentinel=public_value;
    REQUIRE(aether_contextual_transition_sample_box(nullptr,owned,{0,0,200,100},&public_value)==AETHER_ERROR_INVALID_STATE);
    REQUIRE(sample_box_read(nullptr,legacy_inputs,{0,0,200,100},&internal_value)==AETHER_ERROR_INVALID_STATE);
    same_sample(public_value,sentinel); same_sample(internal_value,sentinel);
    aether_computed_style_handle_destroy(owned);
}

static void sampler_timing_contract() {
    struct TimingCase { int timing; double midpoint; };
    // CSS timing-function midpoint reference values, independent of the kernel.
    const TimingCase cases[]={
        {AETHER_TRANSITION_TIMING_LINEAR,.5},
        {AETHER_TRANSITION_TIMING_EASE,.8024033876},
        {AETHER_TRANSITION_TIMING_EASE_IN,.3153568126},
        {AETHER_TRANSITION_TIMING_EASE_OUT,.6846431874},
        {AETHER_TRANSITION_TIMING_EASE_IN_OUT,.5}
    };
    for(const auto& timing:cases) {
        aether_contextual_transition_runtime_t runtime=nullptr;
        REQUIRE(aether_contextual_transition_create(&runtime)==AETHER_OK);
        Style from(0),to(50);
        from.h.style.opacity=.2f;to.h.style.opacity=.8f;
        to.h.style.transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM|AETHER_TRANSITION_PROPERTY_OPACITY;
        to.h.style.transition_timing=timing.timing;
        to.h.style.transition_delay=.25f; // Exactly representable: start is 350 ms.
        aether_contextual_transition_event event{1,100,reinterpret_cast<aether_node_t>(1),&from.h,&to.h};
        for(int64_t timestamp:{int64_t(200),int64_t(350),int64_t(850),int64_t(1350)}) {
            aether_contextual_transition_batch_t batch=nullptr;
            REQUIRE(aether_contextual_transition_prepare(runtime,&event,1,timestamp,&batch)==AETHER_OK);
            // sample compares every public/frozen float bit and every other field.
            const auto value=sample(batch,to);
            const double progress=timestamp<=350?0:timestamp==850?timing.midpoint:1;
            near(value.matrix.dx,100*progress);
            near(value.opacity,.2+.6*progress);
            REQUIRE(value.has_transform);
            REQUIRE(value.active==(timestamp<1350?AETHER_TRUE:AETHER_FALSE));
            REQUIRE(value.presentation_timestamp_ms==timestamp);
            aether_contextual_transition_batch_destroy(batch);
        }
        aether_contextual_transition_destroy(runtime);
    }
}

static void opacity_percentage_endpoint() {
    aether_doc_t doc{}; aether_style_runtime_t styles{};
    REQUIRE(aether_doc_create(&doc)==AETHER_OK);
    REQUIRE(aether_doc_parse_html(doc,"<html><body><div id='opacity' style='opacity:50%'></div></body></html>")==AETHER_OK);
    aether_style_runtime_options_t options{}; aether_style_environment_t env{200,100,1,16,0,0,0};
    REQUIRE(aether_style_runtime_create(&options,&env,nullptr,nullptr,&styles)==AETHER_OK);
    aether_style_update_t* updates{};
    const int count=aether_style_runtime_resolve_subtree(styles,aether_doc_get_document_element(doc),&updates);
    REQUIRE(count>0); aether_style_updates_free(updates,count);
    aether_style_map_t map{};
    REQUIRE(aether_style_runtime_create_resolved_style_map(styles,doc,&map)==AETHER_OK);
    auto handle=aether_style_map_get(map,aether_doc_get_element_by_id(doc,"opacity")); REQUIRE(handle);
    aether_computed_style_t parsed{}; REQUIRE(aether_computed_style_copy(handle,&parsed)==AETHER_OK);
    REQUIRE(parsed.opacity==.5f);
    Style from(0),to(0); from.h.style.opacity=0; to.h.style.opacity=parsed.opacity;
    to.h.style.transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;
    aether_contextual_transition_runtime_t runtime{}; REQUIRE(aether_contextual_transition_create(&runtime)==AETHER_OK);
    aether_contextual_transition_event event{1,0,reinterpret_cast<aether_node_t>(1),&from.h,&to.h};
    for (int time : {500,1000}) {
        aether_contextual_transition_batch_t batch{};
        REQUIRE(aether_contextual_transition_prepare(runtime,&event,1,time,&batch)==AETHER_OK);
        REQUIRE(sample(batch,to).opacity==(time==500?.25f:.5f));
        aether_contextual_transition_batch_destroy(batch);
    }
    aether_contextual_transition_destroy(runtime); aether_computed_style_free(&parsed);
    aether_computed_style_handle_destroy(handle); aether_style_map_destroy(map);
    aether_style_runtime_destroy(styles); aether_doc_destroy(doc);
}

int main(){
    opacity_percentage_endpoint();
    sampler_input_contract();
    sampler_timing_contract();
    sampler_binding_contract();
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
