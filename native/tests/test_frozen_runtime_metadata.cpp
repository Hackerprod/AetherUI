#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/style_runtime/style_property_registry_internal.h"
#include "../src/css/frozen_measure_read.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
namespace fault {
long remaining=-1, live=0, calls=0;
long scalar_calls=0,array_calls=0,aligned_calls=0,nothrow_calls=0;
bool hit=false;
void* alloc(size_t n, size_t align=0) {
    ++calls;
    if(remaining>=0 && remaining--==0) { hit=true; throw std::bad_alloc(); }
    void* p=nullptr;
    if(align) { if(posix_memalign(&p, align<sizeof(void*)?sizeof(void*):align, n?n:1)) p=nullptr; }
    else p=std::malloc(n?n:1);
    if(!p) throw std::bad_alloc();
    ++live; return p;
}
void release(void* p) noexcept { if(p) {--live; std::free(p);} }
}
void* operator new(size_t n){++fault::scalar_calls;return fault::alloc(n);}
void* operator new[](size_t n){++fault::array_calls;return fault::alloc(n);}
void operator delete(void* p) noexcept {fault::release(p);}
void operator delete[](void* p) noexcept {fault::release(p);}
void operator delete(void* p,size_t) noexcept {fault::release(p);}
void operator delete[](void* p,size_t) noexcept {fault::release(p);}
void* operator new(size_t n,std::align_val_t a){++fault::aligned_calls;return fault::alloc(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){++fault::aligned_calls;return fault::alloc(n,size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept {fault::release(p);}
void operator delete[](void* p,std::align_val_t) noexcept {fault::release(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {fault::release(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {fault::release(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {++fault::nothrow_calls;try{return fault::alloc(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {++fault::nothrow_calls;try{return fault::alloc(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {fault::release(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {fault::release(p);}
void* operator new(size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{++fault::aligned_calls;return fault::alloc(n,size_t(a));}catch(...){return nullptr;}}
void* operator new[](size_t n,std::align_val_t a,const std::nothrow_t&) noexcept {try{++fault::aligned_calls;return fault::alloc(n,size_t(a));}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t,const std::nothrow_t&) noexcept {fault::release(p);}
void operator delete[](void* p,std::align_val_t,const std::nothrow_t&) noexcept {fault::release(p);}
using namespace aether::style;
void need(bool value,const char* label){if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::abort();}}
template<class Ref,class Factory> void sweep(Ref old,Factory factory,const char* label) {
    bool success=false;
    for(long i=0;i<10000;++i) {
        Ref out=old;
        const long before=fault::live;
        const long calls_before=fault::calls;
        fault::remaining=i;fault::hit=false;
        const auto status=factory(out);
        fault::remaining=-1;
        if(fault::hit) {
            need(status==AETHER_ERROR_OUT_OF_MEMORY,label);
            need(out==old,"OOM output atomicity");
            need(fault::live==before,"OOM allocations balanced");
            need(factory(out)==AETHER_OK,"clean retry after every injected failure");
        } else {need(status==AETHER_OK,label);need(fault::calls-calls_before==i,"terminal sweep index matches calls");success=true;std::printf("%s: %ld injected failures then success\n",label,i);break;}
    }
    need(success,"sweep reached clean success");
}
void scopes() {
    CustomPropertyInput input{{"--a",{custom_property_status::valid,"hello"}},
        {"--bad",{custom_property_status::invalid,""}},
        {std::string("--x\0y",5),{custom_property_status::work_exhausted,std::string("v\0z",3)}}};
    FrozenScopeRef old;
    need(freeze_custom_scope({}, {},old)==AETHER_OK,"empty scope");
    sweep(old,[&](auto& out){return freeze_custom_scope(input,old,out);},"scope sweep");
    FrozenScopeRef result;
    auto& retained=input.at("--a");
    need(freeze_custom_scope(input,old,result)==AETHER_OK,"scope freeze");
    retained.value="changed";input.clear();
    need(result->find_local("--a")->value()=="hello","scope input alias isolation");
    need(result->find_local("--bad")->status()==custom_property_status::invalid,"invalid masks parent");
    auto* binary=result->find_local(std::string_view("--x\0y",5));
    need(binary && binary->value()==std::string_view("v\0z",3) && binary->status()==custom_property_status::work_exhausted,"binary and exhausted status");
    need(!result->find_local("--x") && result->parent()==old.get(),"full key equality and parent");
    long before=fault::live;
    FrozenScopeRef chain,branch;
    for(int i=0;i<200000;++i) {
        FrozenScopeRef next;need(freeze_custom_scope({},chain,next)==AETHER_OK,"deep create");chain=std::move(next);
        if(i==99999)branch=chain;
    }
    std::weak_ptr<const FrozenCustomPropertyScope> weak=chain;
    const long calls=fault::calls;
    fault::remaining=0;fault::hit=false;
    chain.reset();need(weak.expired(),"deep weak expiry");branch.reset();weak.reset();
    fault::remaining=-1;
    need(!fault::hit && fault::calls==calls,"deep drainer no allocations");
    need(fault::live==before,"deep shared weak balanced");
}
void colors() {
    color_resolution_state_s input;
    input.border_uses_current_color[1]=false;input.border_uses_current_color[3]=false;
    input.text_shadow_uses_current_color={true,false,true};input.box_shadow_uses_current_color={false,true};
    FrozenColorRef old;need(freeze_color_metadata({},old)==AETHER_OK,"empty colors");
    sweep(old,[&](auto& out){return freeze_color_metadata(input,out);},"color sweep");
    FrozenColorRef result;need(freeze_color_metadata(input,result)==AETHER_OK,"color freeze");
    input.text_shadow_uses_current_color.clear();input.border_uses_current_color[0]=false;
    ColorMetadataReadView view(*result);
    need(view.border(0)&&!view.border(1)&&view.border(2)&&!view.border(3),"four border flags");
    need(view.text_shadow_count()==3&&view.text_shadow_at(0)&&!view.text_shadow_at(1)&&view.text_shadow_at(2),"exact text flags");
    need(view.box_shadow_count()==2&&!view.box_shadow_at(0)&&view.box_shadow_at(1),"exact box flags");
}
void measures() {
    FrozenDocumentRef document;need(freeze_document({900,700,18,0,91,712},document)==AETHER_OK,"document");
    measure_state_s input;auto mutable_doc=std::make_shared<document_measure_context_s>();input.document=mutable_doc;
    input.self_font_size=0;input.parent_font_size=19;input.line_height={measure_line_height_kind_e::number,2.5f};
    input.row_gap={1,AETHER_CSS_UNIT_LINEAR,2,3,4,5,6};input.column_gap={7,AETHER_CSS_UNIT_LINEAR,8,9,10,11,12};
    input.background_positions.push_back({input.row_gap,input.column_gap});
    input.background_sizes.push_back({AETHER_BACKGROUND_SIZE_AUTO,input.column_gap,input.row_gap});
    input.property_dependencies={{"width",MEASURE_PERCENTAGE},{"height",MEASURE_ROOT_FONT},{"zero",0}};
    input.dependencies=MEASURE_PERCENTAGE|MEASURE_ROOT_FONT;
    FrozenMeasuresRef old;need(freeze_runtime_measures(input,document,old)==AETHER_OK,"measures seed");
    sweep(old,[&](auto& out){return freeze_runtime_measures(input,document,out);},"measure sweep");
    auto& retained=input.property_dependencies.at("width");
    FrozenMeasuresRef result;need(freeze_runtime_measures(input,document,result)==AETHER_OK,"measure freeze");
    retained=0;input.background_positions[0].x.value=99;mutable_doc->root_font_size=88;
    need(result->document().values().identity==712&&result->document().values().generation==91&&result->document().values().root_font_size==0,"explicit final document");
    MeasuresReadView frozen(*result), builder(input);
    measure_property_delta_s delta;delta.self_font_size=23;delta.property_dependencies=MEASURE_VIEWPORT;
    std::string key="width";MeasuresReadView overlay(measure_read_view_s(input,delta,key));
    const long calls=fault::calls;
    need(frozen.dependency(key)==MEASURE_PERCENTAGE&&overlay.dependency(key)==MEASURE_VIEWPORT,"dependencies including cancelled zero");
    need(overlay.self_font_size()==23&&builder.self_font_size()==0&&frozen.inputs().root_font_size==0,"builder delta and root self zero");
    need(frozen.position_count()==2&&frozen.position_at(0).x.value==0&&frozen.size_count()==2,"lists independent by value");
    need(frozen.size_at(0).kind==AETHER_BACKGROUND_SIZE_AUTO,"size kind");
    const auto equal_length=[](aether_css_length a,aether_css_length b){return a.value==b.value&&a.unit==b.unit&&a.linear_percent==b.linear_percent&&a.linear_em==b.linear_em&&a.linear_rem==b.linear_rem&&a.linear_vw==b.linear_vw&&a.linear_vh==b.linear_vh;};
    need(equal_length(frozen.row_gap(),input.row_gap)&&equal_length(frozen.column_gap(),input.column_gap),"all gap components");
    need(equal_length(frozen.position_at(1).x,input.row_gap)&&equal_length(frozen.position_at(1).y,input.column_gap)&&equal_length(frozen.size_at(1).x,input.column_gap)&&equal_length(frozen.size_at(1).y,input.row_gap),"all longhand components");
    const auto numeric=frozen.inputs_with_self_font(31);
    need(numeric.self_font_size==31&&numeric.viewport_width==900&&numeric.viewport_height==700&&numeric.initial_font_size==18&&numeric.root_font_size==0&&frozen.parent_font_size()==19,"all numeric inputs");
    need(result->property_dependencies().size()==2,"zero dependency canonical absence");
    need(fault::calls==calls,"reads no allocations");
    StyleScalars scalars{};scalars.font_size=10;scalars.line_height=0;
    need(measure_used_line_height(&frozen,scalars)==0&&measure_used_line_height(nullptr,scalars)==12,"line height fallback and zero");
    need(measure_has_percentage(&frozen,key,aether_css_length_zero()),"cancelled percent retains flag");
    for(auto kind:{measure_line_height_kind_e::normal,measure_line_height_kind_e::number,measure_line_height_kind_e::absolute_css_px}) {
        input.line_height={kind,7};input.dependencies=MEASURE_ROOT_FONT;
        need(freeze_runtime_measures(input,document,result)==AETHER_OK,"enum freeze");
        need(MeasuresReadView(*result).line_height().kind==kind,"enum exact");
    }
    auto saved=result;input.dependencies=99;
    need(freeze_runtime_measures(input,document,result)==AETHER_ERROR_INVALID_STATE&&result==saved,"aggregate mismatch atomic");input.dependencies=MEASURE_ROOT_FONT;
    input.line_height.kind=static_cast<measure_line_height_kind_e>(99);
    need(freeze_runtime_measures(input,document,result)==AETHER_ERROR_INVALID_STATE&&result==saved,"invalid enum atomic");
    input.line_height.kind=measure_line_height_kind_e::normal;input.background_sizes.clear();
    need(freeze_runtime_measures(input,document,result)==AETHER_ERROR_INVALID_STATE&&result==saved,"empty lists not silently repaired");
}
void binary_measure_snapshot() {
    FrozenDocumentRef document;need(freeze_document({},document)==AETHER_OK,"binary document");
    measure_state_s input;
    const std::string key("width\0suffix",12);
    input.property_dependencies.emplace(key,MEASURE_PERCENTAGE);input.dependencies=MEASURE_PERCENTAGE;
    FrozenMeasuresRef result;need(freeze_runtime_measures(input,document,result)==AETHER_OK,"binary runtime measures");
    auto pin=result;const auto name=result->property_dependencies()[0].name();const auto* pointer=name.c_str();
    input.property_dependencies.clear();result.reset();
    const long calls=fault::calls;
    need(name.equals(key)&&name.size()==12&&name.c_str()==pointer&&pointer[12]==0&&name.bytes().data()==pointer,"binary name pinned after source destruction");
    need(pin->dependency(key)==MEASURE_PERCENTAGE&&pin->dependency("width")==0,"binary full key lookup");
    need(fault::calls==calls,"binary snapshot read no allocations");
}
int main(){const auto before=fault::live;scopes();colors();measures();binary_measure_snapshot();need(fault::live==before,"all owners balanced");std::printf("allocation families: new=%ld new[]=%ld aligned=%ld nothrow=%ld\n",fault::scalar_calls,fault::array_calls,fault::aligned_calls,fault::nothrow_calls);std::puts("frozen runtime metadata PASS (200000 scopes, OOM sweeps, noalloc reads)");}
