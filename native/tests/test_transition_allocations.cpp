#include <cstdio>
#if defined(AETHER_DOM_ALLOCATIONS_UNAVAILABLE)
int main() { std::puts("UNAVAILABLE transition allocator sweep: unsupported product/static-runtime linkage"); return 77; }
#else
#include "fault_allocator.h"
#include <aether/aether_transition_sync.h>
#include "animations/transition_runtime.h"
#include "dom/dom_types.h"
#include <array>
#include <cmath>

#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"FAIL transition allocator line %d: %s\n",__LINE__,#x); std::abort(); } } while(0)
#include "dom_lifetime_check.h"
static void ok(aether_status_t status) { CHECK(status==AETHER_OK); }
static aether_transition_token_t token(aether_transition_runtime_t r) {
    aether_transition_token_t result{}; ok(aether_transition_runtime_read_token(r,&result)); return result;
}
constexpr size_t node_count=8;
struct Image {
    aether_bool_t active=0;
    std::array<aether_bool_t,node_count> opacity_present{},matrix_present{},owner{};
    std::array<float,node_count> opacity{};
    std::array<aether_transform_matrix,node_count> matrix{};
};
struct Fixture {
    aether_doc_t doc=nullptr;
    std::array<aether_node_t,node_count> nodes{};
    aether_transition_runtime_t subject=nullptr,twin=nullptr;
    std::array<aether_transform_operation,4> scales{};
    std::array<aether_computed_style_t,node_count> before{},after{},next{};
    std::array<std::weak_ptr<EditableNodeStorage>,node_count+1> weak;
    Fixture() {
        ok(aether_doc_create(&doc));
        ok(aether_doc_parse_html(doc,"<body><div id='a'></div><div id='b'></div><div id='c'></div><div id='d'></div><div id='e'></div><div id='f'></div><div id='g'></div><div id='h'></div></body>"));
        const char* ids[]={"a","b","c","d","e","f","g","h"};
        for(size_t i=0;i<node_count;++i) {
            nodes[i]=aether_doc_get_element_by_id(doc,ids[i]); CHECK(nodes[i]);
            weak[i]=aether_editable_lease(nodes[i]); CHECK(!weak[i].expired());
        }
        weak[node_count]=aether_editable_lease(doc);
        for(size_t i=0;i<scales.size();++i) {
            scales[i].kind=AETHER_TRANSFORM_SCALE;
            scales[i].x=scales[i].y=aether_css_length_zero();
            scales[i].scalar_x=scales[i].scalar_y=float(i+1);
        }
        for(size_t i=0;i<node_count;++i) {
            before[i]=aether_computed_style_default();
            before[i].transforms={&scales[0],1};
            after[i]=before[i]; after[i].opacity=.2f+float(i)*.05f;
            after[i].transforms={&scales[1],1};
            after[i].transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY|AETHER_TRANSITION_PROPERTY_TRANSFORM;
            after[i].transition_duration=2; after[i].transition_timing=AETHER_TRANSITION_TIMING_LINEAR;
        }
        after[0].transition_delay=.2f;      // Active at the 400 ms seed sample.
        after[1].transition_delay=1;        // Still delayed at the seed sample.
        after[2].transition_duration=.1f;   // Both finished.
        after[3].transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;
        after[3].transition_duration=3;
        after[4].transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM;
        after[4].transition_duration=3; after[4].transition_delay=-.1f;
        after[5].transition_properties=AETHER_TRANSITION_PROPERTY_OPACITY;
        after[5].transition_duration=1; after[5].transition_delay=-2; // Instantly finished.
        after[7].transition_duration=4;
        next=after;
        next[0].opacity=.8f; next[0].transforms={&scales[2],1};
        next[1].transition_properties=0; // Cancel delayed channels.
        next[6].opacity=.7f; next[6].transforms={&scales[3],1};
        next[6].transition_delay=-2; next[6].transition_duration=1;
        next[7].transition_properties=AETHER_TRANSITION_PROPERTY_TRANSFORM; // Cancel only opacity.
        ok(aether_transition_runtime_create(&subject)); ok(aether_transition_runtime_create(&twin));
        for(auto r:{subject,twin}) {
            for(size_t i=0;i<node_count;++i) if(i!=6)
                ok(aether_transition_runtime_observe(r,nodes[i],&before[i],&after[i],0));
            aether_bool_t active=0; ok(aether_transition_runtime_try_tick(r,400,&active)); CHECK(active);
        }
    }
    ~Fixture() {
        aether_transition_runtime_destroy(subject); aether_transition_runtime_destroy(twin); aether_doc_destroy(doc);
    }
    Image read(aether_transition_runtime_t r) const {
        CHECK(!fault_alloc::depth); Image image;
        image.active=aether_transition_runtime_has_active(r);
        const auto owners=aether_active_transform_owners(r);
        size_t recognized=0;
        for(size_t i=0;i<node_count;++i) {
            image.opacity_present[i]=aether_transition_runtime_opacity_override(r,nodes[i],&image.opacity[i]);
            image.matrix_present[i]=aether_transition_runtime_transform_override(r,nodes[i],&image.matrix[i]);
            image.owner[i]=owners.count(nodes[i])!=0; recognized+=image.owner[i]!=0;
        }
        CHECK(recognized==owners.size()); return image;
    }
};
static bool same_float(float a,float b) { return std::memcmp(&a,&b,sizeof(a))==0; }
static void equal(const Image& a,const Image& b) {
    CHECK(a.active==b.active && a.opacity_present==b.opacity_present && a.matrix_present==b.matrix_present && a.owner==b.owner);
    for(size_t i=0;i<node_count;++i) {
        CHECK(same_float(a.opacity[i],b.opacity[i]));
        CHECK(std::memcmp(&a.matrix[i],&b.matrix[i],sizeof(aether_transform_matrix))==0);
    }
}
static void twins(Fixture& f,uint64_t status_offset) {
    equal(f.read(f.subject),f.read(f.twin));
    const auto a=token(f.subject),b=token(f.twin);
    CHECK(a.instance_id!=b.instance_id && a.mutation_serial==b.mutation_serial);
    CHECK(a.tick_error==AETHER_OK && b.tick_error==AETHER_OK && a.status_revision==b.status_revision+status_offset);
}
enum class Operation { batch, batch_new_active, retire, cancel, observe_retarget, observe_install,
    observe_cancel, tick_middle, tick_final, tick_backward, legacy_tick, noop };
static const char* name(Operation operation) {
    switch(operation) {
    case Operation::batch:return "prepare mixed retarget/cancel/instant-finish/retire";
    case Operation::batch_new_active:return "prepare new delayed active channels";
    case Operation::retire:return "prepare retire active+finished";
    case Operation::cancel:return "prepare cancel delayed+opacity";
    case Operation::observe_retarget:return "Observe retarget two properties";
    case Operation::observe_install:return "Observe install instant finished";
    case Operation::observe_cancel:return "Observe cancel delayed channels";
    case Operation::tick_middle:return "TryTick intermediate samples";
    case Operation::tick_final:return "TryTick migrate many finished";
    case Operation::tick_backward:return "TryTick backward clock";
    case Operation::legacy_tick:return "legacy Tick migrate many finished";
    default:return "prepare no-op shared state";
    }
}
static bool tick_operation(Operation op) { return op>=Operation::tick_middle && op<=Operation::legacy_tick; }
static int64_t operation_time(Operation op) {
    if(op==Operation::tick_final || op==Operation::legacy_tick) return 10000;
    if(op==Operation::tick_backward) return -500;
    return 650;
}
struct Outcome { aether_status_t status=AETHER_OK; aether_transition_candidate_t candidate=nullptr; aether_bool_t active=77; };
static Outcome execute(Fixture& f,aether_transition_runtime_t r,Operation op) {
    Outcome out; const auto now=operation_time(op);
    std::array<aether_transition_observation_t,4> observations{{
        {f.nodes[0],&f.after[0],&f.next[0]}, {f.nodes[1],&f.after[1],&f.next[1]},
        {f.nodes[6],&f.before[6],&f.next[6]}, {f.nodes[7],&f.after[7],&f.next[7]}}};
    const aether_node_t retire[]={f.nodes[2],f.nodes[4]};
    switch(op) {
    case Operation::batch:
        out.status=aether_transition_runtime_prepare(r,observations.data(),observations.size(),retire,2,now,&out.candidate); break;
    case Operation::batch_new_active: {
        auto delayed=f.next[6]; delayed.transition_delay=.8f;
        aether_transition_observation_t observation{f.nodes[6],&f.before[6],&delayed};
        out.status=aether_transition_runtime_prepare(r,&observation,1,nullptr,0,now,&out.candidate); break;
    }
    case Operation::retire:out.status=aether_transition_runtime_prepare(r,nullptr,0,retire,2,now,&out.candidate);break;
    case Operation::cancel: {
        const aether_transition_observation_t cancel[]={observations[1],observations[3]};
        out.status=aether_transition_runtime_prepare(r,cancel,2,nullptr,0,now,&out.candidate);break;
    }
    case Operation::observe_retarget:out.status=aether_transition_runtime_observe(r,f.nodes[0],&f.after[0],&f.next[0],now);break;
    case Operation::observe_install:out.status=aether_transition_runtime_observe(r,f.nodes[6],&f.before[6],&f.next[6],now);break;
    case Operation::observe_cancel:out.status=aether_transition_runtime_observe(r,f.nodes[1],&f.after[1],&f.next[1],now);break;
    case Operation::legacy_tick: {
        out.active=aether_transition_runtime_tick(r,now);
        // Pure certificate query is deliberately outside the injection interval.
        break;
    }
    case Operation::tick_middle:case Operation::tick_final:case Operation::tick_backward:
        out.status=aether_transition_runtime_try_tick(r,now,&out.active);break;
    case Operation::noop:out.status=aether_transition_runtime_prepare(r,nullptr,0,nullptr,0,now,&out.candidate);break;
    }
    return out;
}
static void publish(Outcome& out) {
    ok(out.status);
    if(out.candidate) {
        ok(aether_transition_candidate_validate(out.candidate));
        ok(aether_transition_candidate_commit(out.candidate,nullptr));
        aether_transition_candidate_destroy(out.candidate); out.candidate=nullptr;
    }
}
static void future_samples(Fixture& f,uint64_t status_offset) {
    // Backward samples distinguish timestamps/delays from an endpoint-only copy.
    for(int64_t now:{int64_t(-100),int64_t(650),int64_t(1100),int64_t(1800),int64_t(6000),int64_t(-5000)}) {
        aether_bool_t a=0,b=0;
        ok(aether_transition_runtime_try_tick(f.subject,now,&a));
        ok(aether_transition_runtime_try_tick(f.twin,now,&b)); CHECK(a==b); twins(f,status_offset);
    }
}
static uint64_t failures=0;
static void one(Operation op,uint64_t ordinal,uint64_t expected_requests) {
    const DomLifetimeCheck instances;
    std::array<std::weak_ptr<EditableNodeStorage>,node_count+1> weak;
    {
        Fixture f; weak=f.weak; twins(f,0);
        const auto initial=f.read(f.subject);
        const auto before=token(f.subject);
        aether_transition_candidate_t earlier=nullptr;
        ok(aether_transition_runtime_prepare(f.subject,nullptr,0,nullptr,0,400,&earlier));
        fault_alloc::reset(ordinal); Outcome out;
        { fault_alloc::Scope scope; out=execute(f,f.subject,op); }
        CHECK(fault_alloc::valid()); const auto ledger=fault_alloc::counts;
        if(op==Operation::legacy_tick) out.status=token(f.subject).tick_error;
        uint64_t status_offset=0;
        if(ordinal<expected_requests) {
            CHECK(ledger.injected_failures==1 && ledger.failed_ordinal==ordinal && ledger.null_results==1);
            CHECK(out.status==AETHER_ERROR_OUT_OF_MEMORY && !out.candidate); ++failures;
            equal(initial,f.read(f.subject)); equal(initial,f.read(f.twin));
            const auto after=token(f.subject);
            CHECK(after.instance_id==before.instance_id && after.mutation_serial==before.mutation_serial);
            if(tick_operation(op)) {
                CHECK(after.tick_error==AETHER_ERROR_OUT_OF_MEMORY && after.status_revision==before.status_revision+1);
                CHECK(aether_transition_candidate_validate(earlier)==AETHER_ERROR_INVALID_STATE);
                if(op==Operation::legacy_tick) CHECK(out.active==initial.active);
                else CHECK(out.active==AETHER_FALSE);
                // An unrelated unchanged Observe must not clear the TickError.
                ok(aether_transition_runtime_observe(f.subject,f.nodes[0],&f.after[0],&f.after[0],400));
                const auto unrelated=token(f.subject);
                CHECK(unrelated.status_revision==after.status_revision && unrelated.tick_error==after.tick_error &&
                    unrelated.mutation_serial==after.mutation_serial);
                status_offset=2; // One failure and acknowledgment, never restored.
            } else {
                CHECK(after.tick_error==before.tick_error && after.status_revision==before.status_revision);
                ok(aether_transition_candidate_validate(earlier));
            }
            out=execute(f,f.subject,op);
            if(op==Operation::legacy_tick) out.status=token(f.subject).tick_error;
        } else {
            CHECK(ordinal==expected_requests && ledger.injected_failures==0 && ledger.requests==expected_requests);
        }
        publish(out);
        auto reference=execute(f,f.twin,op); publish(reference);
        twins(f,status_offset);
        if(tick_operation(op) && ordinal<expected_requests)
            CHECK(aether_transition_candidate_validate(earlier)==AETHER_ERROR_INVALID_STATE); // No ABA after acknowledgment.
        aether_transition_candidate_destroy(earlier);
        future_samples(f,status_offset);
    }
    for(const auto& lease:weak) CHECK(lease.expired());
}
static void sweep(Operation op) {
    const DomLifetimeCheck instances;
    uint64_t requests=0;
    {
        Fixture f; fault_alloc::reset(); Outcome out;
        { fault_alloc::Scope scope; out=execute(f,f.subject,op); }
        CHECK(fault_alloc::valid() && !fault_alloc::counts.injected_failures);
        requests=fault_alloc::counts.requests; CHECK(requests>0);
        publish(out);
    }
    for(uint64_t ordinal=0;ordinal<=requests;++ordinal) one(op,ordinal,requests);
    std::printf("transition allocator: %s: %llu successful-path requests, %llu fresh injected fixtures + final success\n",
        name(op),static_cast<unsigned long long>(requests),static_cast<unsigned long long>(requests));
}
int main() {
    const DomLifetimeCheck instances;
    fault_alloc::positive_controls();
    for(auto operation:{Operation::batch,Operation::batch_new_active,Operation::retire,Operation::cancel,
        Operation::observe_retarget,Operation::observe_install,Operation::observe_cancel,
        Operation::tick_middle,Operation::tick_final,Operation::tick_backward,Operation::legacy_tick,Operation::noop}) sweep(operation);
    std::printf("PASS real transition allocator sweep: %llu injected ordinals; all public samples/owners + future timestamps + separate status history. Private-vector bitwise equality, additional exception-path ordinals, libc-private/DSO/CLR and global leak freedom NOT claimed.\n",
        static_cast<unsigned long long>(failures));
}
#endif
