#include "frozen_style_test_support.h"
#include "../src/style_runtime/style_diff.h"
#include "../src/style_runtime/style_property_registry_internal.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/css/frozen_measure_read.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
using namespace aether::style;
using frozen_test::need;
using frozen_test::ok;
static bool counting = false;
static size_t allocation_calls = 0, checks = 0;
#ifdef AETHER_DIFF_WRAP_MALLOC
extern "C" void* __real_malloc(size_t);
extern "C" void* __wrap_malloc(size_t n) noexcept { if(counting) ++allocation_calls; return __real_malloc(n); }
#endif
void* operator new(size_t n) { if(counting) ++allocation_calls; void* p=std::malloc(n?n:1); if(!p)throw std::bad_alloc(); return p; }
void* operator new[](size_t n) {return ::operator new(n);}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {std::free(p);}
// Match the existing platform-specific aligned allocation helper and count all
// PMR/overaligned paths, not only ordinary operator new.
static long fail_after = -1;
static bool fault_delivered = false;
#define AETHER_TEST_ALIGNED_REJECT() (counting ? (++allocation_calls, false) : false)
#include "fault_inject_aligned.h"

struct Fixture {
    frozen_test::Builder builder;
    measure_state_s measures;
    color_resolution_state_s colors;
    FrozenStyleRef frozen;
    FrozenMeasuresRef frozen_measures;
    FrozenColorRef frozen_colors;
    Fixture() {
        measures.document=std::make_shared<document_measure_context_s>();
        colors.text_shadow_uses_current_color={true,false};
        colors.box_shadow_uses_current_color={false,true};
        freeze();
    }
    void freeze() {
        measure_recompute_dependencies(measures);
        ok(freeze_style(builder.flat,frozen),"freeze style");
        const auto& d=*measures.document;
        FrozenDocumentRef document;
        ok(freeze_document({d.viewport_width,d.viewport_height,d.initial_font_size,d.root_font_size,d.generation,d.identity},document),"freeze document");
        ok(freeze_runtime_measures(measures,document,frozen_measures),"freeze measures");
        ok(freeze_color_metadata(colors,frozen_colors),"freeze colors");
    }
    StyleDiff compare() {
        allocation_calls=0; counting=true;
        auto result=classify_style_diff(frozen->view(),*frozen_measures,*frozen_colors,builder.flat,measures,colors);
        counting=false;
        need(allocation_calls==0,"classifier allocates zero, including first call");
        ++checks;
        return result;
    }
    void expect(uint32_t own, uint32_t inherited=0, bool storage=false, bool metadata=false) {
        auto r=compare();
        if(r.own_impact!=own || r.inherited_value_impact!=inherited || r.style_storage_equivalent!=storage || r.metadata_changed!=metadata || r.unclassified) {
            std::fprintf(stderr,"own %u/%u inherited %u/%u storage %d/%d metadata %d/%d unknown %d\n",r.own_impact,own,r.inherited_value_impact,inherited,r.style_storage_equivalent,storage,r.metadata_changed,metadata,r.unclassified);
            need(false,"classifier exact policy");
        }
    }
};
// Test expectations are independent constants, never read from production policy.
uint32_t expected(StyleField f) {
    switch(f) {
#define CASES7(name) case StyleField::name:
    CASES7(display) CASES7(position) CASES7(inset) CASES7(width) CASES7(height) CASES7(min_width) CASES7(min_height)
    CASES7(max_width) CASES7(max_height) CASES7(margin) CASES7(padding) CASES7(border_width) CASES7(overflow_x) CASES7(overflow_y) return 7;
#undef CASES7
    case StyleField::box_sizing: case StyleField::flex_direction: case StyleField::justify_content: case StyleField::align_items:
    case StyleField::align_self: case StyleField::flex_grow: case StyleField::flex_shrink: case StyleField::flex_basis:
    case StyleField::flex_wrap: case StyleField::gap: return 1;
    case StyleField::border_styles:return 3;
    case StyleField::color:return 66;
    case StyleField::background_color: case StyleField::border_color: case StyleField::border_colors: case StyleField::has_text_gradient:
    case StyleField::transition_properties:case StyleField::transition_duration:case StyleField::transition_delay:case StyleField::transition_timing:return 2;
    case StyleField::border_radii:case StyleField::z_index:case StyleField::z_index_kind:return 6;
    case StyleField::opacity:case StyleField::transform_origin:return 12;
    case StyleField::font_size:return 83;
    case StyleField::font_family:case StyleField::font_weight:case StyleField::text_align:case StyleField::text_transform:return 82;
    case StyleField::line_height:case StyleField::white_space:return 81;
    case StyleField::cursor:return 96;
    case StyleField::pointer_events:return 68;
    case StyleField::visible:return 70;
    default:need(false,"new scalar needs test expectation");return 0;
    }
}
bool inherited(StyleField f) {
    switch(f) {
    case StyleField::color:case StyleField::font_size:case StyleField::font_family:case StyleField::font_weight:
    case StyleField::text_align:case StyleField::text_transform:case StyleField::line_height:case StyleField::white_space:
    case StyleField::cursor:case StyleField::pointer_events:case StyleField::visible:return true;
    default:return false;
    }
}
template<class T,class F> void leaves(T& v,F fn){fn(v);}
template<class F> void leaves(aether_css_length& v,F fn){fn(v.value);fn(v.unit);fn(v.linear_percent);fn(v.linear_em);fn(v.linear_rem);fn(v.linear_vw);fn(v.linear_vh);}
template<class F> void leaves(aether_color_rgba& v,F fn){fn(v.r);fn(v.g);fn(v.b);fn(v.a);}
template<class F> void leaves(aether_css_edges& v,F fn){leaves(v.top,fn);leaves(v.right,fn);leaves(v.bottom,fn);leaves(v.left,fn);}
template<class F> void leaves(aether_color_edges& v,F fn){leaves(v.top,fn);leaves(v.right,fn);leaves(v.bottom,fn);leaves(v.left,fn);}
template<class F> void leaves(aether_border_style_edges& v,F fn){fn(v.top);fn(v.right);fn(v.bottom);fn(v.left);}
template<class F> void leaves(aether_flex_basis& v,F fn){fn(v.kind);leaves(v.length,fn);}
template<class F> void leaves(aether_corner_radius& v,F fn){leaves(v.horizontal,fn);leaves(v.vertical,fn);}
template<class F> void leaves(aether_border_radii& v,F fn){leaves(v.top_left,fn);leaves(v.top_right,fn);leaves(v.bottom_left,fn);leaves(v.bottom_right,fn);}
template<class F> void leaves(aether_transform_origin& v,F fn){leaves(v.x,fn);leaves(v.y,fn);}
template<class F> void leaves(aether_gradient_stop& v,F fn){leaves(v.color,fn);leaves(v.position,fn);fn(v.offset);fn(v.has_position);}
template<class F> void leaves(aether_shadow& v,F fn){fn(v.offset_x);fn(v.offset_y);fn(v.blur_radius);fn(v.spread_radius);leaves(v.color,fn);fn(v.inset);}
template<class T> void mutations(Fixture& f,T& value,uint32_t mask,bool inherit=false,bool storage=false,bool metadata=false) {
    leaves(value,[&](auto& leaf){const auto old=leaf;
        if constexpr(std::is_same_v<std::remove_reference_t<decltype(leaf)>,aether_css_unit_t>) leaf=leaf==AETHER_CSS_UNIT_PX?AETHER_CSS_UNIT_EM:AETHER_CSS_UNIT_PX;
        else leaf=static_cast<std::remove_reference_t<decltype(leaf)>>(leaf+1);f.expect(mask,inherit?mask:0,storage,metadata);leaf=old;f.expect(0,0,true);});
}
void scalars() {
    Fixture f;f.expect(0,0,true);
#define MUTATE(type,name) mutations(f,f.builder.flat.name,expected(StyleField::name),inherited(StyleField::name));
    AETHER_STYLE_SCALAR_FIELDS(MUTATE)
#undef MUTATE
    mutations(f,f.builder.flat.font_family[0],82,true);
    f.builder.flat.font_family[63]='X';f.expect(0,0,false);f.builder.flat.font_family[63]=0;
    // Full 64-byte nonterminated arrays are compared within their bounds.
    for(auto& c:f.builder.flat.font_family)c='A'; f.freeze();
    f.builder.flat.font_family[63]='B';f.expect(82,82);f.builder.flat.font_family[63]='A';f.expect(0,0,true);
    f.builder.flat.opacity=0;f.freeze();f.builder.flat.opacity=-0.f;f.expect(0,0,true);
    f.builder.flat.opacity=std::numeric_limits<float>::quiet_NaN();f.freeze();f.expect(12);
}
void payloads() {
    Fixture f;
    for(auto& t:f.builder.transforms) {
        // Every defined operation member, including inactive slots.
        auto kind=t.kind;t.kind=AETHER_TRANSFORM_SCALE;f.expect(12);t.kind=kind;
        mutations(f,t.x,12);mutations(f,t.y,12);
        for(auto member:{&aether_transform_operation::scalar_x,&aether_transform_operation::scalar_y,&aether_transform_operation::angle_radians,
            &aether_transform_operation::matrix_a,&aether_transform_operation::matrix_b,&aether_transform_operation::matrix_c,
            &aether_transform_operation::matrix_d,&aether_transform_operation::matrix_e,&aether_transform_operation::matrix_f}) mutations(f,t.*member,12);
    }
    for(auto& filter:f.builder.filters){mutations(f,filter.length_amount,10);mutations(f,filter.scalar_amount,10);mutations(f,filter.kind,10);}
    for(auto& shadow:f.builder.text)mutations(f,shadow,66,true);
    for(auto& shadow:f.builder.boxes)mutations(f,shadow,2);
    for(auto& stop:f.builder.text_stops)mutations(f,stop,2);
    mutations(f,f.builder.flat.text_gradient.direction_x,2);mutations(f,f.builder.flat.text_gradient.direction_y,2);
    --f.builder.flat.text_gradient.stop_count;f.expect(2);++f.builder.flat.text_gradient.stop_count;
    for(auto& layer:f.builder.layers) {
        mutations(f,layer.position_x,2);mutations(f,layer.position_y,2);mutations(f,layer.size_x,2);mutations(f,layer.size_y,2);
        mutations(f,layer.size_kind,2);mutations(f,layer.repeat_x,2);mutations(f,layer.repeat_y,2);
    }
    for(auto& stop:f.builder.stops)mutations(f,stop,2);
    mutations(f,f.builder.layers[0].gradient.direction_x,2);mutations(f,f.builder.layers[0].gradient.direction_y,2);
    mutations(f,f.builder.image[0],2);
    f.builder.layers[0].image_source=f.builder.image;f.expect(0,0,false);f.builder.layers[0].image_source=nullptr;
    f.builder.layers[1].gradient.direction_x+=1;f.expect(0,0,false);f.builder.layers[1].gradient.direction_x-=1;
    f.builder.layers[1].image_source=nullptr;f.expect(2);f.builder.layers[1].image_source=f.builder.image;
    --f.builder.layers[1].gradient.stop_count;f.expect(0,0,false);++f.builder.layers[1].gradient.stop_count;
    --f.builder.flat.background_layers.count;f.expect(2);++f.builder.flat.background_layers.count;
    --f.builder.flat.text_shadows.count;f.expect(66,66);++f.builder.flat.text_shadows.count;
    --f.builder.flat.box_shadows.count;f.expect(2);++f.builder.flat.box_shadows.count;
    --f.builder.flat.transforms.count;f.expect(12);++f.builder.flat.transforms.count;
    --f.builder.flat.backdrop_filters.count;f.expect(10);++f.builder.flat.backdrop_filters.count;
    std::swap(f.builder.layers[0],f.builder.layers[1]);f.expect(2);std::swap(f.builder.layers[0],f.builder.layers[1]);
    f.builder.flat.transforms.count=0;f.expect(12);f.builder.flat.backdrop_filters.count=0;f.expect(15);
    f.builder.flat.transforms.count=2;f.builder.flat.backdrop_filters.count=2;f.expect(0,0,true);
    f.builder.transforms[0].kind=999;auto r=f.compare();need(r.unclassified&&r.own_impact==127&&!r.style_storage_equivalent,"unknown operation conservative");
    f.builder.transforms[0].kind=AETHER_TRANSFORM_MATRIX;
    f.builder.filters[0].kind=999;r=f.compare();need(r.unclassified&&r.own_impact==127,"unknown filter conservative");
    f.builder.filters[0].kind=AETHER_BACKDROP_FILTER_BLUR;
    f.builder.layers[0].kind=999;r=f.compare();need(r.unclassified&&r.own_impact==127,"unknown background conservative");
}
// Independent policy checks plus member mutations below cover the complete
// sidecar inventory. A new inventory entry requires an explicit test decision.
static_assert(static_cast<size_t>(StyleSidecar::count) == 18);
static_assert(static_cast<size_t>(StylePayload::count) == 6);
#define CHECK_SIDECAR(name, expected_kind, mask, inherit) \
    static_assert(StyleSidecarPolicy<StyleSidecar::name>::kind == SidecarPolicyKind::expected_kind); \
    static_assert(StyleSidecarPolicy<StyleSidecar::name>::impact == mask); \
    static_assert(StyleSidecarPolicy<StyleSidecar::name>::inherited == inherit);
CHECK_SIDECAR(property_dependencies, percentage_consumers, 0, false)
CHECK_SIDECAR(contextual_lengths, value, 7, false)
CHECK_SIDECAR(intrinsic_keywords, value, 7, false)
CHECK_SIDECAR(combined_dependencies, metadata, 0, false)
CHECK_SIDECAR(parent_font_size, metadata, 0, false)
CHECK_SIDECAR(self_font_size, metadata, 0, false)
CHECK_SIDECAR(line_height, value, 81, true)
CHECK_SIDECAR(flex_order, value, 7, false)
CHECK_SIDECAR(direction_rtl, value, 7, true)
CHECK_SIDECAR(row_gap, value, 17, false)
CHECK_SIDECAR(column_gap, value, 17, false)
CHECK_SIDECAR(background_positions, metadata, 0, false)
CHECK_SIDECAR(background_sizes, metadata, 0, false)
CHECK_SIDECAR(document_inputs, metadata, 0, false)
CHECK_SIDECAR(background_color_reference, metadata, 0, false)
CHECK_SIDECAR(border_color_references, metadata, 0, false)
CHECK_SIDECAR(text_shadow_references, inherited_metadata, 0, false)
CHECK_SIDECAR(box_shadow_references, metadata, 0, false)
#undef CHECK_SIDECAR
void sidecars() {
    Fixture f;
    for(const auto& policy:style_dependency_catalog) {
        // Every generable key is classified, even aliases and no-op properties.
        f.measures.property_dependencies[std::string(policy.name)]=MEASURE_ROOT_FONT;
        measure_recompute_dependencies(f.measures);f.expect(0,0,true,true);
        f.measures.property_dependencies.clear();measure_recompute_dependencies(f.measures);f.expect(0,0,true);
    }
    // Remove the fixture's geometric percentage coefficients before testing
    // dependency-only category changes on otherwise identical flat state.
    f.builder.flat.width=f.builder.flat.height=f.builder.flat.min_width=f.builder.flat.min_height=
        f.builder.flat.max_width=f.builder.flat.max_height=f.builder.flat.flex_basis.length=aether_css_length_zero();f.freeze();
    int percentages=0;
    for(const auto& p:style_dependency_catalog) {
        f.measures.property_dependencies[std::string(p.name)]=MEASURE_PERCENTAGE;measure_recompute_dependencies(f.measures);
        uint32_t mask=0;
        if(p.name=="width"||p.name=="height"||p.name=="min-width"||p.name=="min-height"||p.name=="max-width"||p.name=="max-height") mask=7;
        else if(p.name=="flex-basis")mask=1;
        else if(p.name=="row-gap"||p.name=="column-gap")mask=17;
        if(mask)++percentages;
        f.expect(mask,0,true,true);f.measures.property_dependencies.clear();measure_recompute_dependencies(f.measures);f.expect(0,0,true);
    }
    need(percentages==9,"only nine visual percentage keys");
    // H8 tag changes are visual even when the public scalar projection is identical.
    for (auto property : {"width", "min-width", "max-width", "flex-basis"}) {
        for (auto keyword : {aether::css::intrinsic_size_keyword::min_content,
                             aether::css::intrinsic_size_keyword::max_content,
                             aether::css::intrinsic_size_keyword::fit_content}) {
            f.measures.intrinsic_keywords[property] = keyword; f.expect(7,0,true,true);
            f.freeze(); f.expect(0,0,true,false);
            f.measures.intrinsic_keywords.clear(); f.expect(7,0,true,true);
            f.freeze(); f.expect(0,0,true,false);
        }
    }
    f.measures.direction_rtl = true; f.expect(7,7,true,true); f.measures.direction_rtl = false;
    f.measures.flex_order = -2; f.expect(7,0,true,true); f.measures.flex_order = 0;
    mutations(f,f.measures.row_gap,17,false,true,true);mutations(f,f.measures.column_gap,17,false,true,true);
    f.measures.line_height.kind=measure_line_height_kind_e::number;f.expect(81,81,true,true);
    f.measures.line_height.kind=measure_line_height_kind_e::normal;
    f.measures.line_height.value+=1;f.expect(81,81,true,true);f.measures.line_height.value-=1;
    f.measures.parent_font_size+=1;f.expect(0,0,true,true);f.measures.parent_font_size-=1;
    f.measures.self_font_size+=1;f.expect(81,81,true,true);f.measures.self_font_size-=1;
    mutations(f,f.measures.background_positions[0].x,0,false,true,true);mutations(f,f.measures.background_positions[0].y,0,false,true,true);
    mutations(f,f.measures.background_sizes[0].x,0,false,true,true);mutations(f,f.measures.background_sizes[0].y,0,false,true,true);
    mutations(f,f.measures.background_sizes[0].kind,0,false,true,true);
    f.measures.background_positions.push_back({});f.expect(0,0,true,true);f.measures.background_positions.pop_back();
    f.measures.background_sizes.push_back({});f.expect(0,0,true,true);f.measures.background_sizes.pop_back();
    auto document=std::make_shared<document_measure_context_s>(*f.measures.document);f.measures.document=document;
    for(auto member:{&document_measure_context_s::viewport_width,&document_measure_context_s::viewport_height,&document_measure_context_s::initial_font_size,&document_measure_context_s::root_font_size}) mutations(f,document.get()->*member,0,false,true,true);
    mutations(f,document->generation,0,false,true,true);mutations(f,document->identity,0,false,true,true);
    f.colors.background_uses_current_color=true;f.expect(0,0,true,true);f.colors.background_uses_current_color=false;
    for(auto& flag:f.colors.border_uses_current_color){flag=false;f.expect(0,0,true,true);flag=true;}
    f.colors.text_shadow_uses_current_color[0]=false;auto r=f.compare();need(r.own_impact==0&&r.inherited_metadata_changed&&r.metadata_changed&&r.style_storage_equivalent,"text shadow relation metadata");
    f.colors.text_shadow_uses_current_color[0]=true;
    f.colors.box_shadow_uses_current_color[0]=true;f.expect(0,0,true,true);f.colors.box_shadow_uses_current_color[0]=false;
    f.colors.text_shadow_uses_current_color.push_back(true);r=f.compare();need(r.inherited_metadata_changed&&r.metadata_changed&&r.own_impact==0,"symbolic text list count");f.colors.text_shadow_uses_current_color.pop_back();
    f.colors.box_shadow_uses_current_color.push_back(true);f.expect(0,0,true,true);f.colors.box_shadow_uses_current_color.pop_back();
    f.measures.dependencies=MEASURE_ROOT_FONT;f.expect(0,0,true,true);f.measures.dependencies=0;
    f.measures.property_dependencies["future-property"]=MEASURE_PARENT;r=f.compare();need(r.unclassified&&r.own_impact==127&&!r.style_storage_equivalent,"unknown dependency conservative");
}
int main(){scalars();payloads();sidecars();std::printf("style diff classifier: %zu exact comparisons, zero allocations PASS\n",checks);}
