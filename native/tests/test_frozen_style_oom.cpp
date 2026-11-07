// Compile the three additive .cpp files into THIS executable and link with
// -Wl,--wrap=malloc,--wrap=free. Wrapping a precompiled DSO is insufficient.
#include "frozen_style_test_support.h"
#include <new>
#include <cstdio>
#include <cstdlib>
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
namespace fault {
enum class Mode { none, cpp, c };
Mode mode=Mode::none;
long remaining=-1,calls=0,cpp_alloc=0,cpp_free=0,c_alloc=0,c_free=0;
bool hit=false;
bool reject(Mode selected) noexcept {
    if(mode!=selected)return false;
    ++calls;
    if(remaining--==0){hit=true;return true;}
    return false;
}
void arm(Mode selected,long index){remaining=index;calls=cpp_alloc=cpp_free=c_alloc=c_free=0;hit=false;mode=selected;}
void disarm(){mode=Mode::none;}
void* cpp_allocate(size_t size,size_t align=0) {
    if(reject(Mode::cpp))throw std::bad_alloc();
    void* p=nullptr;
    if(align) { if(posix_memalign(&p,align,size?size:1))p=nullptr; }
    else p=__real_malloc(size?size:1);
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
void* operator new(size_t n,std::align_val_t a){return fault::cpp_allocate(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){return fault::cpp_allocate(n,size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {fault::cpp_delete(p);}
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
void freeze_sweep(const Builder& builder) {
    FrozenStyleRef previous;ok(freeze_style(builder.flat,previous),"baseline freeze");
    for(long index=0;index<1000;++index) {
        auto output=previous;
        fault::arm(fault::Mode::cpp,index);auto status=freeze_style(builder.flat,output);fault::disarm();
        if(fault::hit) {
            need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"freeze OOM preserves exact owner");balanced_failure();
            ok(freeze_style(builder.flat,output),"freeze clean retry");
            snapshot_test::Flat exported;ok(export_owned_flat(output->view(),&exported.value),"retry export");equal(builder.flat,exported.value);
        } else {ok(status,"uninjected freeze success");need(index==fault::calls&&index>0,"all new sites swept");std::printf("freeze new sites=%ld\n",index);return;}
    }need(false,"freeze sweep bound");
}
void export_sweep(const FrozenStyleRef& source, fault::Mode mode,bool callback) {
    snapshot_test::Flat expected;ok(export_owned_flat(source->view(),&expected.value),"expected export");
    for(long index=0;index<1000;++index) {
        snapshot_test::Flat output;
        output.value.opacity=123;std::strcpy(output.value.font_family,"output sentinel");
        auto sentinel=output.value;
        std::unique_ptr<CallbackStyleCopy> copy;
        if(callback)ok(CallbackStyleCopy::create(source->view(),copy),"prior callback owner");
        const auto* previous=copy.get();
        fault::arm(mode,index);
        auto status=callback?CallbackStyleCopy::create(source->view(),copy):export_owned_flat(source->view(),&output.value);
        fault::disarm();
        if(fault::hit) {
            need(status==AETHER_ERROR_OUT_OF_MEMORY,"every injected allocation failure reported");
            need(copy.get()==previous,"callback output owner unchanged");
            equal(output.value,sentinel);balanced_failure();
            if(callback){ok(CallbackStyleCopy::create(source->view(),copy),"callback clean retry");}
            else ok(export_owned_flat(source->view(),&output.value),"export clean retry");
            equal(callback?*copy->get():output.value,expected.value);
        } else {
            ok(status,"uninjected export success");equal(callback?*copy->get():output.value,expected.value);need(index==fault::calls&&index>0,"all selected allocation sites swept");
            std::printf("%s %s sites=%ld\n",callback?"callback":"export",mode==fault::Mode::cpp?"new":"malloc",index);return;
        }
    }need(false,"export sweep bound");
}
void measure_sweep() {
    std::shared_ptr<const FrozenDocumentMeasureContext> document;
    ok(freeze_document({800,600,16,19,7,11},document),"document setup");
    MeasureScalarValues scalar{};scalar.dependencies=3;
    MeasureBackgroundPosition positions[2]{};MeasureBackgroundSize sizes[2]{};
    PropertyDependencyInput deps[]={{"font-size",1},{"width",2}};
    std::shared_ptr<const FrozenMeasures> previous;
    ok(freeze_measures(scalar,document,positions,sizes,deps,previous),"metadata setup");
    for(long i=0;i<1000;++i) {
        auto output=previous;
        fault::arm(fault::Mode::cpp,i);auto status=freeze_measures(scalar,document,positions,sizes,deps,output);fault::disarm();
        if(fault::hit){need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"metadata output unchanged");balanced_failure();ok(freeze_measures(scalar,document,positions,sizes,deps,output),"metadata retry");}
        else {ok(status,"metadata success");need(i==fault::calls&&i>0,"metadata complete sweep");std::printf("measures new sites=%ld\n",i);return;}
    }need(false,"metadata sweep bound");
}
void document_sweep() {
    std::shared_ptr<const FrozenDocumentMeasureContext> previous;
    ok(freeze_document({800,600,16,19,7,11},previous),"document initial");
    for(long i=0;i<1000;++i) {
        auto output=previous;
        fault::arm(fault::Mode::cpp,i);auto status=freeze_document({900,700,17,20,8,12},output);fault::disarm();
        if(fault::hit){need(status==AETHER_ERROR_OUT_OF_MEMORY&&output==previous,"document owner unchanged");balanced_failure();ok(freeze_document({900,700,17,20,8,12},output),"document retry");}
        else{ok(status,"document success");need(i==fault::calls&&i>0,"document complete sweep");std::printf("document new sites=%ld\n",i);return;}
    }need(false,"document sweep bound");
}
void callback_cleanup(const FrozenStyleRef& source) {
    char alien[]="alien";aether_gradient_stop stops[1]{};
    fault::arm(fault::Mode::c,-1);
    std::unique_ptr<CallbackStyleCopy> outer,inner;
    auto s1=CallbackStyleCopy::create(source->view(),outer);
    auto s2=CallbackStyleCopy::create(source->view(),inner);
    if(s1!=AETHER_OK||s2!=AETHER_OK){fault::disarm();need(false,"callback ledger setup");}
    for(int i=0;i<outer->get()->background_layers.count;++i){auto& l=outer->get()->background_layers.items[i];l.image_source=alien;l.gradient.stops=stops;l.gradient.stop_count=INT32_MAX;}
    inner.reset();outer.reset();fault::disarm();
    balanced_failure();need(fault::c_alloc>0&&fault::cpp_alloc>0,"ledger success allocations actually tracked");
}
int main() {
    Builder builder;freeze_sweep(builder);measure_sweep();document_sweep();
    FrozenStyleRef frozen;ok(freeze_style(builder.flat,frozen),"shared source");
    callback_cleanup(frozen);
    for(auto mode:{fault::Mode::cpp,fault::Mode::c})for(bool callback:{false,true})export_sweep(frozen,mode,callback);
    std::puts("PASS frozen OOM: exhaustive first-uninjected-success, independent new/malloc, atomic owners/output and balanced partial cleanup");
}
