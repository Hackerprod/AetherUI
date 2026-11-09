#include "state_semantics.h"
#include "frozen_runtime_metadata.h"
namespace aether::style {
namespace {
template<class T> bool eq(const T& a,const T& b) noexcept { return a==b; }
static bool eq(const aether_color_rgba& a, const aether_color_rgba& b) noexcept {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static bool eq(const aether_css_length& a, const aether_css_length& b) noexcept {
    return a.value == b.value &&
           a.unit == b.unit &&
           a.linear_percent == b.linear_percent &&
           a.linear_em == b.linear_em &&
           a.linear_rem == b.linear_rem &&
           a.linear_vw == b.linear_vw &&
           a.linear_vh == b.linear_vh;
}

static bool eq(const aether_css_edges& a, const aether_css_edges& b) noexcept {
    return eq(a.left, b.left) &&
           eq(a.top, b.top) &&
           eq(a.right, b.right) &&
           eq(a.bottom, b.bottom);
}

static bool eq(const aether_color_edges& a, const aether_color_edges& b) noexcept {
    return eq(a.left, b.left) &&
           eq(a.top, b.top) &&
           eq(a.right, b.right) &&
           eq(a.bottom, b.bottom);
}

static bool eq(const aether_border_style_edges& a, const aether_border_style_edges& b) noexcept {
    return a.left == b.left &&
           a.top == b.top &&
           a.right == b.right &&
           a.bottom == b.bottom;
}

static bool eq(const aether_flex_basis& a, const aether_flex_basis& b) noexcept {
    return a.kind == b.kind && eq(a.length, b.length);
}

static bool eq(const aether_corner_radius& a, const aether_corner_radius& b) noexcept {
    return eq(a.horizontal, b.horizontal) &&
           eq(a.vertical, b.vertical);
}

static bool eq(const aether_border_radii& a, const aether_border_radii& b) noexcept {
    return eq(a.top_left, b.top_left) &&
           eq(a.top_right, b.top_right) &&
           eq(a.bottom_right, b.bottom_right) &&
           eq(a.bottom_left, b.bottom_left);
}

static bool eq(const aether_transform_origin& a, const aether_transform_origin& b) noexcept {
    return eq(a.x, b.x) && eq(a.y, b.y);
}

static bool eq(const aether_gradient_stop& a, const aether_gradient_stop& b) noexcept {
    return eq(a.color, b.color) &&
           eq(a.position, b.position) &&
           a.offset == b.offset &&
           a.has_position == b.has_position;
}

bool eq(Utf8ReadView a,Utf8ReadView b) noexcept {
    if(a.present()!=b.present()||a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i]!=b[i])return false;
    return true;
}
bool eq(const aether_shadow& a,const aether_shadow& b) noexcept { return eq(a.offset_x,b.offset_x)&&eq(a.offset_y,b.offset_y)&&eq(a.blur_radius,b.blur_radius)&&eq(a.spread_radius,b.spread_radius)&&eq(a.color,b.color)&&eq(a.inset,b.inset); }
bool eq(const aether_backdrop_filter& a,const aether_backdrop_filter& b) noexcept { return eq(a.kind,b.kind)&&eq(a.length_amount,b.length_amount)&&eq(a.scalar_amount,b.scalar_amount); }
bool eq(const aether_transform_operation& a,const aether_transform_operation& b) noexcept { return eq(a.kind,b.kind)&&eq(a.x,b.x)&&eq(a.y,b.y)&&eq(a.scalar_x,b.scalar_x)&&eq(a.scalar_y,b.scalar_y)&&eq(a.angle_radians,b.angle_radians)&&eq(a.matrix_a,b.matrix_a)&&eq(a.matrix_b,b.matrix_b)&&eq(a.matrix_c,b.matrix_c)&&eq(a.matrix_d,b.matrix_d)&&eq(a.matrix_e,b.matrix_e)&&eq(a.matrix_f,b.matrix_f); }
bool eq(const BackgroundLayerScalars& a,const BackgroundLayerScalars& b) noexcept { return eq(a.kind,b.kind)&&eq(a.position_x,b.position_x)&&eq(a.position_y,b.position_y)&&eq(a.size_kind,b.size_kind)&&eq(a.size_x,b.size_x)&&eq(a.size_y,b.size_y)&&eq(a.repeat_x,b.repeat_x)&&eq(a.repeat_y,b.repeat_y); }
bool eq(const MeasureBackgroundPosition& a,const MeasureBackgroundPosition& b) noexcept { return eq(a.x,b.x)&&eq(a.y,b.y); }
bool eq(const MeasureBackgroundSize& a,const MeasureBackgroundSize& b) noexcept { return eq(a.kind,b.kind)&&eq(a.x,b.x)&&eq(a.y,b.y); }
bool eq(const MeasureScalarValues& a,const MeasureScalarValues& b) noexcept { return eq(a.parent_font_size,b.parent_font_size)&&eq(a.self_font_size,b.self_font_size)&&eq(a.line_height_kind,b.line_height_kind)&&eq(a.line_height_value,b.line_height_value)&&eq(a.row_gap,b.row_gap)&&eq(a.column_gap,b.column_gap)&&eq(a.dependencies,b.dependencies); }
bool eq(const FrozenPropertyDependency& a,const FrozenPropertyDependency& b) noexcept {return eq(a.name(),b.name())&&a.flags()==b.flags();}
bool eq(const FrozenBackgroundLayer&,const FrozenBackgroundLayer&) noexcept;
template<class T> bool arrays_equal(ReadOnlyArrayView<T> a,ReadOnlyArrayView<T> b) noexcept {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(!eq(a[i],b[i]))return false;
    return true;
}
bool eq(GradientReadView a,GradientReadView b) noexcept {
    return eq(a.direction_x,b.direction_x)&&eq(a.direction_y,b.direction_y)&&arrays_equal(a.stops,b.stops);
}
bool eq(const FrozenBackgroundLayer& a,const FrozenBackgroundLayer& b) noexcept {
    const auto x=a.view(),y=b.view();
    return eq(x.scalars,y.scalars)&&eq(x.gradient,y.gradient)&&eq(x.image_source,y.image_source);
}
bool style_equal(const FrozenStyle& a,const FrozenStyle& b,bool ignore_local_colors) noexcept {
    if(&a==&b)return true;
    const auto av=a.view(),bv=b.view();
    auto x=av.scalars(),y=bv.scalars();
    if(ignore_local_colors){x.background_color=y.background_color;x.border_color=y.border_color;x.border_colors=y.border_colors;}
#define SAME_FIELD(type,name) if(!eq(x.name,y.name))return false;
    AETHER_STYLE_SCALAR_FIELDS(SAME_FIELD)
#undef SAME_FIELD
    if(x.font_family!=y.font_family)return false;
    // Always compare payload, even with zero background layers or disabled text gradients.
    return arrays_equal(av.background_layers(),bv.background_layers())&&
        arrays_equal(av.backdrop_filters(),bv.backdrop_filters())&&eq(av.text_gradient(),bv.text_gradient())&&
        arrays_equal(av.transforms(),bv.transforms())&&arrays_equal(av.text_shadows(),bv.text_shadows())&&
        arrays_equal(av.box_shadows(),bv.box_shadows());
}
bool scopes_equal(const FrozenCustomPropertyScope* a,const FrozenCustomPropertyScope* b)noexcept{
    while(a!=b){
        if(!a||!b||a->local_count()!=b->local_count())return false;
        for(const auto& entry:a->local_entries()) {const auto* other=b->find_local(entry.name());
            if(!other||entry.status()!=other->status()||entry.value()!=other->value())return false;}
        a=a->parent();b=b->parent();
    }
    return true;
}
bool colors_equal(const FrozenColorMetadata& a,const FrozenColorMetadata& b)noexcept{
    if(a.background()!=b.background()||a.text_shadow_count()!=b.text_shadow_count()||a.box_shadow_count()!=b.box_shadow_count())return false;
    for(size_t i=0;i<4;++i)if(a.border(i)!=b.border(i))return false;
    for(size_t i=0;i<a.text_shadow_count();++i)if(a.text_shadow_at(i)!=b.text_shadow_at(i))return false;
    for(size_t i=0;i<a.box_shadow_count();++i)if(a.box_shadow_at(i)!=b.box_shadow_at(i))return false;
    return true;
}
bool measures_equal(const FrozenMeasurePayload& a,const FrozenMeasurePayload& b) noexcept {
    return eq(a.scalars(),b.scalars())&&arrays_equal(a.background_positions(),b.background_positions())&&
        arrays_equal(a.background_sizes(),b.background_sizes())&&arrays_equal(a.property_dependencies(),b.property_dependencies());
}
template<class T,class F> bool owners_equal(const std::shared_ptr<const T>& a,const std::shared_ptr<const T>& b,F equal) noexcept {
    return a==b||(a&&b&&equal(*a,*b));
}
template<class T> bool same_owner(const std::shared_ptr<T>& a,const std::shared_ptr<T>& b) noexcept {
    return a==b&&!a.owner_before(b)&&!b.owner_before(a);
}
bool identity_equal(const FrozenNodePayload& a,const FrozenNodePayload& b) noexcept {
    return a.origin()==b.origin()&&a.node()==b.node()&&a.node_identity()==b.node_identity()&&
        a.document_identity()==b.document_identity()&&same_owner(a.node_lifetime(),b.node_lifetime())&&
        same_owner(a.document_lifetime(),b.document_lifetime())&&same_owner(a.generated_leaf(),b.generated_leaf());
}
}
StateSemanticDelta compare_node_semantics(const FrozenNodePayload& a,const FrozenNodePayload& b) noexcept {
    if(&a==&b)return {};
    const bool context_same=identity_equal(a,b)&&
        owners_equal(a.measures(),b.measures(),measures_equal)&&
        scopes_equal(a.custom_properties().get(),b.custom_properties().get())&&
        scopes_equal(a.backdrop_custom_properties().get(),b.backdrop_custom_properties().get())&&
        owners_equal(a.color_references(),b.color_references(),colors_equal);
    // Historical impact fields are observable through records and must prevent
    // equivalence/reuse. They are not parent inputs and never trigger propagation.
    const bool metadata_same=a.parent_dependencies()==b.parent_dependencies()&&
        a.declared_impact()==b.declared_impact()&&a.declared_inherited_impact()==b.declared_inherited_impact()&&
        a.inherited_change_impact()==b.inherited_change_impact();
    if(!context_same||!a.style()||!b.style())return {true,ParentDependencies::all};
    if(style_equal(*a.style(),*b.style(),false))return {!metadata_same,0};
    if(!style_equal(*a.style(),*b.style(),true))return {true,ParentDependencies::all};
    const auto& x=a.style_view().scalars();const auto& y=b.style_view().scalars();
    uint32_t propagation=0;
    if(!eq(x.background_color,y.background_color))propagation|=ParentDependencies::background_color;
    if(!eq(x.border_color,y.border_color)||!eq(x.border_colors,y.border_colors))propagation|=ParentDependencies::border_colors;
    return {true,propagation};
}
bool node_projection_equal(const FrozenNodePayload& a,const FrozenNodePayload& b) noexcept { return style_equal(*a.style(),*b.style(),false); }
bool node_semantically_equal(const FrozenNodePayload& a,const FrozenNodePayload& b) noexcept {
    return !compare_node_semantics(a,b).semantic;
}
}
