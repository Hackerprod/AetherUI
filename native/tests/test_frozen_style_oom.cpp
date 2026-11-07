// Compile the helper .cpp files (including frozen_c_string.cpp) into THIS executable and link with
// -Wl,--wrap=malloc,--wrap=free. Wrapping a precompiled DSO is insufficient.
#include "frozen_style_test_support.h"
#include <new>
#include <cstdio>
#include <cstdlib>
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
static size_t ordinary_test_calls=0, ordinary_test_failures=0;
namespace fault {
enum class Mode { none, cpp, ordinary, aligned, c };
const char* label(Mode m) { return m==Mode::cpp?"combined-new":m==Mode::ordinary?"ordinary-new":m==Mode::aligned?"aligned-new":"malloc"; }
long ordinary_calls=0,aligned_calls=0,malloc_calls=0;
Mode mode=Mode::none;
long remaining=-1,calls=0,cpp_alloc=0,cpp_free=0,c_alloc=0,c_free=0;
bool hit=false;
bool reject(Mode selected) noexcept {
    if(mode==Mode::none)return false;
    if(selected==Mode::ordinary)++ordinary_calls;else if(selected==Mode::aligned)++aligned_calls;else ++malloc_calls;
    if(mode!=selected && !(mode==Mode::cpp && selected!=Mode::c))return false;
    ++calls;
    if(remaining--==0){hit=true;return true;}
    return false;
}
void arm(Mode selected,long index){remaining=index;ordinary_calls=aligned_calls=malloc_calls=0;calls=cpp_alloc=cpp_free=c_alloc=c_free=0;hit=false;mode=selected;}
void disarm(){mode=Mode::none;}
void* cpp_allocate(size_t size) {
    ++ordinary_test_calls;
    if(reject(Mode::ordinary)){++ordinary_test_failures;throw std::bad_alloc();}
    void* p=__real_malloc(size?size:1);
    if(!p)throw std::bad_alloc();
    if(mode!=Mode::none)++cpp_alloc;
    return p;
}
void cpp_delete(void* p) noexcept {if(p&&mode!=Mode::none)++cpp_free;__real_free(p);}
}
extern "C" void* __wrap_malloc(size_t n) noexcept {
    if(fault::reject(fault::Mode::c))return nullptr;
    void* p=__real_malloc(n);if(p&&fault::mode!=fault::Mode::none)++fault::c_alloc;return p;
}
extern "C" void __wrap_free(void* p) noexcept {if(p&&fault::mode!=fault::Mode::none)++fault::c_free;__real_free(p);}
void* operator new(size_t n){return fault::cpp_allocate(n);}
void* operator new[](size_t n){return fault::cpp_allocate(n);}
void operator delete(void* p) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p) noexcept {fault::cpp_delete(p);}
void operator delete(void* p,size_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,size_t) noexcept {fault::cpp_delete(p);}
static long& fail_after=fault::remaining;
static bool& fault_delivered=fault::hit;
#define AETHER_TEST_ALIGNED_REJECT() fault::reject(fault::Mode::aligned)
#define AETHER_TEST_ALIGNED_ALLOCATED() do { if(fault::mode!=fault::Mode::none)++fault::cpp_alloc; } while(false)
#define AETHER_TEST_ALIGNED_FREED(p) do { if((p)&&fault::mode!=fault::Mode::none)++fault::cpp_free; } while(false)
#define AETHER_TEST_ALIGNED_SYSTEM_FREE(p) __real_free(p)
#include "fault_inject_aligned.h"
#include "fault_inject_pmr_contract.h"
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return fault::cpp_allocate(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return fault::cpp_allocate(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {fault::cpp_delete(p);}
using namespace frozen_test;
using namespace aether::style;
void balanced_failure() {
    need(fault::cpp_alloc==fault::cpp_free,"failure releases every successful new allocation");
    need(fault::c_alloc==fault::c_free,"failure releases every successful C allocation including ledger insertion failure");
}
void freeze_sweep(const Builder& builder, fault::Mode mode) {
    FrozenStyleRef previous;ok(freeze_style(builder.flat,previous),"baseline freeze");
    for(long index=0;index<1000;++index) {
        auto output=previous;
        fault::arm(mode,index);auto status=freeze_style(builder.flat,output);fault::disarm();
        if(fault::hit) {
            need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"freeze OOM preserves exact owner");balanced_failure();
            ok(freeze_style(builder.flat,output),"freeze clean retry");
            snapshot_test::Flat exported;ok(export_owned_flat(output->view(),&exported.value),"retry export");equal(builder.flat,exported.value);
        } else {ok(status,"uninjected freeze success");need(index==fault::calls,"all new sites swept");std::printf("freeze %s sites=%ld\n",fault::label(mode),index);return;}
    }need(false,"freeze sweep bound");
}
void export_sweep(const FrozenStyleRef& source, fault::Mode mode,bool callback) {
    snapshot_test::Flat expected;ok(export_owned_flat(source->view(),&expected.value),"expected export");
    bool registration_failed=false;
    for(long index=0;index<1000;++index) {
        bool failed=false;long sites=0;
        fault::arm(mode,index);
        {
            snapshot_test::Flat output;
            output.value.opacity=123;std::strcpy(output.value.font_family,"output sentinel");
            const auto sentinel=output.value;
            CallbackStyleCopy copy;int callbacks=0;
            const auto status=callback?copy.prepare(source->view()):export_owned_flat(source->view(),&output.value);
            if(status==AETHER_OK&&callback)++callbacks;
            failed=fault::hit;sites=fault::calls;
            if(failed) {
                need(status==AETHER_ERROR_OUT_OF_MEMORY,"every injected allocation failure reported");
                need(!copy.get() && callbacks==0,"no partial loan or callback on OOM");
                equal(output.value,sentinel);
                // A first ledger registration fails after malloc succeeded.
                if(callback && (mode==fault::Mode::ordinary||mode==fault::Mode::cpp) && index==0)
                    registration_failed=fault::c_alloc==1 && fault::c_free==1;
                // Retained vector capacity is released at frame destruction, not prepare failure.
                fault::remaining=-1;
                if(callback){ok(copy.prepare(source->view()),"same frame clean retry");}
                else ok(export_owned_flat(source->view(),&output.value),"export clean retry");
            } else ok(status,"uninjected export success");
            equal(callback?*copy.get():output.value,expected.value);
            if(callback) {
                const auto* loan=copy.get();
                need(copy.prepare(source->view())==AETHER_ERROR_INVALID_STATE && copy.get()==loan,"ready frame refuses replacement");
                equal(*loan,expected.value);
            }
        }
        fault::disarm();balanced_failure();
        snapshot_test::Flat intact;ok(export_owned_flat(source->view(),&intact.value),"source after OOM/retry");equal(intact.value,expected.value);
        if(!failed) {
            need(index==sites,"all selected allocation sites swept");
            if(callback && (mode==fault::Mode::ordinary||mode==fault::Mode::cpp))
                need(registration_failed,"malloc success followed by ledger registration OOM covered");
            std::printf("%s %s sites=%ld\n",callback?"callback":"export",fault::label(mode),index);return;
        }
    }need(false,"export sweep bound");
}
void empty_frames() {
    FrozenStyleRef empty; aether_computed_style_t flat{};
    ok(freeze_style(flat,empty),"freeze truly empty source outside counters");
    for(auto mode:{fault::Mode::cpp,fault::Mode::ordinary,fault::Mode::aligned,fault::Mode::c}) {
        fault::arm(mode,0);
        {
            CallbackStyleCopy frame;
            need(!frame.get(),"fresh empty frame null");
            ok(frame.prepare(empty->view()),"empty frame ignores first-allocation fault");
            equal(*frame.get(),flat);
        }
        fault::disarm();
        need(!fault::hit&&fault::ordinary_calls==0&&fault::aligned_calls==0&&fault::malloc_calls==0,
            "empty frame prepare/use/destruct has zero exporter allocations in every family");
        balanced_failure();
    }
    aether_background_layer layer{};char text[]="";layer.image_source=text;
    flat.background_layers={&layer,1};FrozenStyleRef present;
    ok(freeze_style(flat,present),"freeze present-empty outside counters");
    for(auto mode:{fault::Mode::cpp,fault::Mode::ordinary,fault::Mode::aligned,fault::Mode::c})export_sweep(present,mode,true);
    fault::arm(fault::Mode::c,-1);
    {CallbackStyleCopy frame;ok(frame.prepare(present->view()),"present-empty allocation");
     need(frame.get()->background_layers.items[0].image_source && !*frame.get()->background_layers.items[0].image_source,"owned NUL byte");}
    fault::disarm();balanced_failure();need(fault::c_alloc==2,"layer and present-empty terminator have separate malloc ownership");
}
void measure_sweep(fault::Mode mode) {
    std::shared_ptr<const FrozenDocumentMeasureContext> document;
    ok(freeze_document({800,600,16,19,7,11},document),"document setup");
    MeasureScalarValues scalar{};scalar.dependencies=3;
    MeasureBackgroundPosition positions[2]{};MeasureBackgroundSize sizes[2]{};
    PropertyDependencyInput deps[]={{"font-size",1},{"width",2}};
    std::shared_ptr<const FrozenMeasures> previous;
    ok(freeze_measures(scalar,document,positions,sizes,deps,previous),"metadata setup");
    for(long i=0;i<1000;++i) {
        auto output=previous;
        fault::arm(mode,i);auto status=freeze_measures(scalar,document,positions,sizes,deps,output);fault::disarm();
        if(fault::hit){need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"metadata output unchanged");balanced_failure();ok(freeze_measures(scalar,document,positions,sizes,deps,output),"metadata retry");}
        else {ok(status,"metadata success");need(i==fault::calls,"metadata complete sweep");std::printf("measures %s sites=%ld\n",fault::label(mode),i);return;}
    }need(false,"metadata sweep bound");
}
void document_sweep() {
    std::shared_ptr<const FrozenDocumentMeasureContext> previous;
    ok(freeze_document({800,600,16,19,7,11},previous),"document initial");
    for(long i=0;i<1000;++i) {
        auto output=previous;
        fault::arm(fault::Mode::cpp,i);auto status=freeze_document({900,700,17,20,8,12},output);fault::disarm();
        if(fault::hit){need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"document owner unchanged");balanced_failure();ok(freeze_document({900,700,17,20,8,12},output),"document retry");}
        else{ok(status,"document success");need(i==fault::calls,"document complete sweep");std::printf("document new sites=%ld\n",i);return;}
    }need(false,"document sweep bound");
}
void callback_cleanup(const FrozenStyleRef& source) {
    char alien[]="alien";aether_gradient_stop stops[1]{};
    fault::arm(fault::Mode::c,-1);
    { CallbackStyleCopy outer,inner;
    auto s1=outer.prepare(source->view());
    auto s2=inner.prepare(source->view());
    if(s1!=AETHER_OK||s2!=AETHER_OK){fault::disarm();need(false,"callback ledger setup");}
    for(int i=0;i<outer.get()->background_layers.count;++i){auto& l=outer.get()->background_layers.items[i];l.image_source=alien;l.gradient.stops=stops;l.gradient.stop_count=INT32_MAX;}
    } fault::disarm();
    balanced_failure();need(fault::c_alloc>0&&fault::cpp_alloc>0,"ledger success allocations actually tracked");
}
void string_sweep() {
    for(auto text:{std::string_view(""),std::string_view("a\0z",3)}) {
        auto previous=std::make_shared<const ImmutableUtf8>("sentinel");
        for(auto mode:{fault::Mode::cpp,fault::Mode::ordinary,fault::Mode::aligned,fault::Mode::c}) {
            bool complete=false;
            for(long i=0;i<100;++i) {
                auto out=previous;bool failed=false;
                fault::arm(mode,i);
                try {out=std::make_shared<const ImmutableUtf8>(text);}catch(const std::bad_alloc&){failed=true;}
                fault::disarm();
                if(fault::hit) {
                    need(failed&&out==previous&&out->view().equals("sentinel"),"string OOM atomic owner");balanced_failure();
                    out=std::make_shared<const ImmutableUtf8>(text);need(out->view().equals(text),"string clean retry");
                } else {
                    need(!failed&&fault::calls==i&&out->view().equals(text)&&out->view().c_str()[text.size()]==0,"string terminal success");
                    std::printf("string size=%zu %s sites=%ld\n",text.size(),fault::label(mode),i);complete=true;break;
                }
            }
            need(complete,"string sweep complete");
        }
    }
}
void noalloc_reads(const FrozenStyleRef& source) {
    ImmutableUtf8 binary(std::string_view("a\0z",3)), empty("");
    std::shared_ptr<const FrozenDocumentMeasureContext> document;
    ok(freeze_document({},document),"noalloc document");
    MeasureScalarValues scalars{};scalars.dependencies=1;
    MeasureBackgroundPosition position{};MeasureBackgroundSize size{};
    PropertyDependencyInput dependency{std::string_view("x\0y",3),1};
    std::shared_ptr<const FrozenMeasures> measures;
    ok(freeze_measures(scalars,document,{&position,1},{&size,1},{&dependency,1},measures),"noalloc measures");
    const auto* pointer=binary.view().c_str();
    fault::arm(fault::Mode::cpp,-1);
    for(int i=0;i<1000;++i) {
        ImmutableUtf8 copy(binary),moved(std::move(copy));
        auto v=moved.view();
        need(v.present()&&v.size()==3&&v[2]=='z'&&v.c_str()==pointer&&v.bytes().data()==pointer&&v.equals(std::string_view("a\0z",3)),"binary reads");
        need(empty.view().c_str()[0]==0&&measures->dependency(dependency.name)==1,"empty and dependency reads");
        for(const auto& layer:source->view().background_layers()) {
            auto text=layer.view().image_source;
            need(text.bytes().size()==text.size(),"image span extent");
            if(text.present())need(text.c_str()[text.size()]==0,"image termination");
        }
    }
    fault::disarm();
    need(fault::ordinary_calls==0&&fault::aligned_calls==0&&fault::malloc_calls==0,"all reader allocation families zero");
}
int main() {
    fault::arm(fault::Mode::cpp,-1);
    const auto pmr_family=pmr_fault_test::qualify(need);
    fault::disarm();
    const pmr_fault_test::Counters sweep_before;
    Builder builder;
    for(auto mode:{fault::Mode::cpp,fault::Mode::ordinary,fault::Mode::aligned,fault::Mode::c}) {freeze_sweep(builder,mode);measure_sweep(mode);}
    document_sweep();string_sweep();
    pmr_fault_test::require_sweep_coverage(pmr_family,sweep_before,need);
    empty_frames();
    FrozenStyleRef frozen;ok(freeze_style(builder.flat,frozen),"shared source");
    callback_cleanup(frozen);noalloc_reads(frozen);
    for(auto mode:{fault::Mode::cpp,fault::Mode::ordinary,fault::Mode::aligned,fault::Mode::c})for(bool callback:{false,true})export_sweep(frozen,mode,callback);
    std::puts("PASS frozen OOM: exhaustive first-uninjected-success, independent new/malloc, atomic owners/output and balanced partial cleanup");
}
