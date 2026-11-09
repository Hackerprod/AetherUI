#include "style_runtime/state_semantics.h"
#include "style_runtime/frozen_runtime_metadata.h"
#include "dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
using namespace aether::style;
static void need(bool b,const char* label){if(!b){std::fprintf(stderr,"FAIL: %s\n",label);std::abort();}}
static FrozenStyleRef style(StyleScalars scalars={}) {
    return std::make_shared<const FrozenStyle>(scalars,std::make_shared<const FrozenStylePayload>(
        ImmutableArray<FrozenBackgroundLayer>{},ImmutableArray<aether_backdrop_filter>{},FrozenGradient{},
        ImmutableArray<aether_transform_operation>{},ImmutableArray<aether_shadow>{},ImmutableArray<aether_shadow>{}));
}
static FrozenMeasurePayloadRef measure(MeasureScalarValues values={}) {
    return std::make_shared<const FrozenMeasurePayload>(values,ImmutableArray<MeasureBackgroundPosition>{},
        ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
}
static FrozenColorRef colors(bool background=false,std::array<bool,4> borders={}) {
    return std::make_shared<const FrozenColorMetadata>(borders,ImmutableArray<uint8_t>{},ImmutableArray<uint8_t>{},background);
}
static FrozenNodePayloadRef freeze(NodePayloadInput in) {
    FrozenNodePayloadRef out;need(freeze_node_payload(in,out)==AETHER_OK,"freeze payload");return out;
}
int main(){
    aether_node_s node;
    NodePayloadInput in;in.node=&node;in.style=style();in.measures=measure();in.color_references=colors();
    need(in.parent_dependencies==ParentDependencies::all,"unknown defaults to all");
    const auto base=freeze(in);
    auto expect=[&](NodePayloadInput input,bool semantic,uint32_t propagation,const char* label){
        const auto delta=compare_node_semantics(*base,*freeze(input));
        need(delta.semantic==semantic&&delta.propagation==propagation,label);
    };
    auto copy=in;copy.style=style();copy.measures=measure();copy.color_references=colors();
    expect(copy,false,0,"new owners same complete content");
    copy=in;copy.color_references=colors(true);expect(copy,true,ParentDependencies::background_color,"currentColor background same RGBA");
    copy=in;copy.color_references=colors(false,{true,false,false,false});expect(copy,true,ParentDependencies::border_colors,"currentColor border same RGBA");
    copy=in;copy.color_references=std::make_shared<const FrozenColorMetadata>(std::array<bool,4>{},
        ImmutableArray<uint8_t>::generate(1,[](size_t){return uint8_t{1};}),ImmutableArray<uint8_t>{},false);
    expect(copy,true,ParentDependencies::all,"text shadow currentColor metadata");
    copy=in;copy.color_references=std::make_shared<const FrozenColorMetadata>(std::array<bool,4>{},
        ImmutableArray<uint8_t>{},ImmutableArray<uint8_t>::generate(1,[](size_t){return uint8_t{1};}),false);
    expect(copy,true,ParentDependencies::all,"box shadow currentColor metadata");
    copy=in;copy.style=std::make_shared<const FrozenStyle>(StyleScalars{},std::make_shared<const FrozenStylePayload>(
        ImmutableArray<FrozenBackgroundLayer>{},ImmutableArray<aether_backdrop_filter>{},FrozenGradient{1,0,{}},
        ImmutableArray<aether_transform_operation>{},ImmutableArray<aether_shadow>{},ImmutableArray<aether_shadow>{}));
    expect(copy,true,ParentDependencies::all,"inactive gradient payload with zero background layers");
    copy=in;copy.measures=std::make_shared<const FrozenMeasurePayload>(MeasureScalarValues{},
        ImmutableArray<MeasureBackgroundPosition>{},ImmutableArray<MeasureBackgroundSize>{},
        ImmutableArray<FrozenPropertyDependency>::generate(1,[](size_t){return FrozenPropertyDependency{{"height",7}};}));
    expect(copy,true,ParentDependencies::all,"property dependency metadata");
    copy=in;auto values=MeasureScalarValues{};values.line_height_kind=FrozenLineHeightKind::number;copy.measures=measure(values);
    expect(copy,true,ParentDependencies::all,"line height kind same resolved value");
    copy=in;values={};values.line_height_value=1.5f;copy.measures=measure(values);
    expect(copy,true,ParentDependencies::all,"line height symbolic value");
    copy=in;copy.parent_dependencies=ParentDependencies::other;expect(copy,true,0,"dependency-only delta");
    copy=in;copy.declared_impact=123;expect(copy,true,0,"historic impact-only delta");
    copy=in;auto scalars=StyleScalars{};scalars.background_color.r=1;copy.style=style(scalars);
    expect(copy,true,ParentDependencies::background_color,"background with zero layers");
    scalars.border_colors.left.b=1;copy.style=style(scalars);
    expect(copy,true,ParentDependencies::background_color|ParentDependencies::border_colors,"both local colors");
    scalars.line_height=2;copy.style=style(scalars);expect(copy,true,ParentDependencies::all,"other style field");
    FrozenScopeRef scope1,scope2,scope3;
    CustomPropertyInput input;input.emplace("--x",computed_custom_property_s{custom_property_status::valid,"red"});
    need(freeze_custom_scope(input,{},scope1)==AETHER_OK,"scope1");
    need(freeze_custom_scope(input,{},scope2)==AETHER_OK,"scope2");
    input.at("--x").status=custom_property_status::work_exhausted;
    need(freeze_custom_scope(input,{},scope3)==AETHER_OK,"scope3");
    in.custom_properties=scope1;auto first=freeze(in);in.custom_properties=scope2;
    need(node_semantically_equal(*first,*freeze(in)),"structurally equal scopes");
    in.custom_properties=scope3;
    need(compare_node_semantics(*first,*freeze(in)).propagation==ParentDependencies::all,"scope status differs");
    in.custom_properties=scope1;in.backdrop_custom_properties=scope1;first=freeze(in);in.backdrop_custom_properties=scope2;
    need(node_semantically_equal(*first,*freeze(in)),"structurally equal backdrop scopes");
    in.backdrop_custom_properties=scope3;
    need(compare_node_semantics(*first,*freeze(in)).propagation==ParentDependencies::all,"backdrop scope status differs");
    // Canonicalization belongs only to resolver output, never import factories
    // or the comparator. Same runtime identity/context, only bookkeeping differs.
    aether_doc_s document;
    auto imported=in;imported.origin=RecordOrigin::runtime;
    imported.node_lifetime=node.identity;imported.node_identity=aether_node_identity(&node);
    imported.document_lifetime=document.identity;imported.document_identity=aether_node_identity(&document);
    imported.inherited_change_impact=43;auto imported43=freeze(imported);
    imported.inherited_change_impact=91;auto imported91=freeze(imported);
    need(imported43->inherited_change_impact()==43&&imported91->inherited_change_impact()==91,"runtime imports preserve noncanonical values");
    const auto imported_delta=compare_node_semantics(*imported43,*imported91);
    need(imported_delta.semantic&&imported_delta.propagation==0,"same-context imports distinguish only inherited impact");
    std::puts("state semantics passed");
}
