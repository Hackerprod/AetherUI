#include "frozen_style_test_support.h"
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>
using namespace aether::style;
using namespace frozen_test;
static_assert(!std::is_convertible_v<ComputedStyleReadView, aether_computed_style_t>);
static_assert(!std::is_convertible_v<ComputedStyleReadView, const aether_computed_style_t*>);
static_assert(!std::is_assignable_v<decltype(std::declval<ComputedStyleReadView>().scalars().opacity),float>);
static_assert(std::is_same_v<decltype(std::declval<ComputedStyleReadView>().transforms()[0]),const aether_transform_operation&>);
static_assert(std::is_same_v<decltype(std::declval<GradientReadView>().stops[0]),const aether_gradient_stop&>);
static_assert(std::is_same_v<decltype(std::declval<Utf8ReadView>()[0]),const char&>);
static_assert(std::forward_iterator<ReadOnlyArrayView<int>::const_iterator>);
template<class T> concept MutableData = requires(T x) { x.data(); };
template<class T> concept Release = requires(T x) { x.release(); };
static_assert(!MutableData<ImmutableArray<int>> && !Release<ImmutableArray<int>>);
struct alignas(256) Counted {
    static inline int alive=0, destroyed=0;
    const int value;
    explicit Counted(int x):value(x) { ++alive; }
    Counted(const Counted&)=delete;
    ~Counted() noexcept { --alive; ++destroyed; }
};
void arrays() {
    {
        auto a=ImmutableArray<Counted>::generate(4,[](size_t i){return Counted(int(i));});
        need(Counted::alive==4,"real noncopyable const objects constructed once");
        auto b=a; a={};
        for(size_t i=0;i<b.size();++i) {
            need(b.view()[i].value==int(i),"slot access");
            need(reinterpret_cast<uintptr_t>(&b.view()[i])%alignof(Counted)==0,"overalignment");
        }
        bool bounds=false;try{(void)b.view().at(4);}catch(const std::out_of_range&){bounds=true;}
        need(bounds,"checked access rejects boundary");
    }
    need(Counted::alive==0&&Counted::destroyed==4,"typed reverse destruction exactly once");
    try { (void)ImmutableArray<Counted>::generate(5,[](size_t i){if(i==3)throw 42;return Counted(int(i));});need(false,"generator throws"); }
    catch(int x){need(x==42,"original failure retained");}
    need(Counted::alive==0&&Counted::destroyed==7,"partial construction cleaned");
    bool overflow=false;try{(void)ImmutableArray<Counted>::generate(std::numeric_limits<size_t>::max(),[](size_t){return Counted(0);});}
    catch(const std::length_error&){overflow=true;}need(overflow,"overflow rejected before allocation");
}
void measures() {
    DocumentMeasureValues document{800,600,16,20,42,93};
    std::shared_ptr<const FrozenDocumentMeasureContext> frozen_document;
    ok(freeze_document(document,frozen_document),"freeze document");document.root_font_size=99;
    MeasureScalarValues scalars{};scalars.dependencies=3;
    MeasureBackgroundPosition position{};position.x.value=17;
    MeasureBackgroundSize size{};size.x.value=29;
    std::string key="font-size";
    PropertyDependencyInput deps[]={{key,1},{"width",2},{"zero",0}};
    std::shared_ptr<const FrozenMeasures> result;
    ok(freeze_measures(scalars,frozen_document,{&position,1},{&size,1},deps,result),"freeze metadata");
    key[0]='X';position.x.value=0;size.x.value=0;scalars.dependencies=0;frozen_document.reset();
    need(result->document().values().root_font_size==20&&result->document().values().generation==42&&result->document().values().identity==93,"independent final document");
    need(result->background_positions()[0].x.value==17&&result->background_sizes()[0].x.value==29,"independent lists");
    need(result->property_dependencies().size()==2&&result->dependency("font-size")==1&&result->dependency("width")==2&&result->scalars().dependencies==3,"independent exact nonzero dependency inventory");
    auto before=result;need(freeze_measures(scalars,nullptr,{},{},{},result)!=AETHER_OK&&result==before,"bad metadata preserves output");
}
int main() {
    arrays();measures();
    FrozenStyleRef frozen;
    snapshot_test::Flat first,second;
    {
        Builder builder;
        ok(freeze_style(builder.flat,frozen),"freeze every field");
        ok(export_owned_flat(frozen->view(),&first.value),"export first");
        ok(export_owned_flat(frozen->view(),&second.value),"export second");
        equal(builder.flat,first.value);equal(builder.flat,second.value);
        snapshot_test::disjoint(builder.flat,first.value);snapshot_test::disjoint(first.value,second.value);
        need(!frozen->view().background_layers()[0].view().image_source.present(),"null retained");
        need(frozen->view().background_layers()[2].view().image_source.present()&&frozen->view().background_layers()[2].view().image_source.empty(),"empty retained");
        for(auto& c:builder.flat.font_family)c='X';builder.flat.opacity=99;snapshot_test::mutate_payload(builder.flat);
    }
    snapshot_test::Flat after;ok(export_owned_flat(frozen->view(),&after.value),"export after builder destruction");equal(first.value,after.value);
    snapshot_test::mutate_payload(first.value);equal(second.value,after.value);
    std::unique_ptr<CallbackStyleCopy> outer,inner;
    ok(CallbackStyleCopy::create(frozen->view(),outer),"outer callback copy");
    snapshot_test::mutate_payload(*outer->get());
    ok(CallbackStyleCopy::create(frozen->view(),inner),"nested callback independent copy");
    equal(*inner->get(),second.value);snapshot_test::disjoint(*outer->get(),*inner->get());
    aether_gradient_stop alien_stops[2]{};char alien_url[]="borrowed external";
    // The descriptor root is const; layer descriptors and all payloads are
    // mutable through the public ABI. Never free any loan or foreign storage.
    for(int i=0;i<outer->get()->background_layers.count;++i) {
        auto& layer=outer->get()->background_layers.items[i];
        layer.gradient.stops=alien_stops;layer.gradient.stop_count=INT32_MAX;layer.image_source=alien_url;
    }
    outer.reset();inner.reset();need(std::strcmp(alien_url,"borrowed external")==0,"foreign rebinding not freed or changed");
    snapshot_test::Flat last;ok(export_owned_flat(frozen->view(),&last.value),"export after callbacks");equal(last.value,second.value);
    auto old=frozen;Builder invalid;invalid.flat.transforms.count=-1;
    need(freeze_style(invalid.flat,frozen)==AETHER_ERROR_INVALID_STATE&&frozen==old,"invalid input preserves owner");
    invalid.flat.transforms={nullptr,1};need(freeze_style(invalid.flat,frozen)==AETHER_ERROR_INVALID_STATE&&frozen==old,"missing list rejected");
    need(export_owned_flat(frozen->view(),nullptr)==AETHER_ERROR_NULL_ARG,"null output");
    std::puts("PASS frozen style: const slots, full fields, aliases, metadata, callback ledger");
}
