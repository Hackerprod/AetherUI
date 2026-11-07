#pragma once
#include "css/style_freeze.h"
#include "css/style_c_export.h"
#include "snapshot_test_support.h"
#include <array>
namespace frozen_test {
using snapshot_test::need;
using snapshot_test::ok;
inline void seed(float& x) { x = 7.25f; }
inline void seed(int32_t& x) { x = 3; }
inline void seed(aether_css_length& x) { x = {7, AETHER_CSS_UNIT_LINEAR, 2, 3, 4, 5, 6}; }
inline void seed(aether_css_edges& x) { seed(x.left); seed(x.top); seed(x.right); seed(x.bottom); }
inline void seed(aether_color_rgba& x) { x = {.1f, .2f, .3f, .4f}; }
inline void seed(aether_color_edges& x) { seed(x.left); seed(x.top); seed(x.right); seed(x.bottom); }
inline void seed(aether_border_style_edges& x) { seed(x.left); seed(x.top); seed(x.right); seed(x.bottom); }
inline void seed(aether_flex_basis& x) { seed(x.kind); seed(x.length); }
inline void seed(aether_transform_origin& x) { seed(x.x); seed(x.y); }
inline void seed(aether_corner_radius& x) { seed(x.horizontal); seed(x.vertical); }
inline void seed(aether_border_radii& x) { seed(x.top_left); seed(x.top_right); seed(x.bottom_left); seed(x.bottom_right); }
// All payloads belong to this mutable builder, never to the flat ABI destructor.
struct Builder {
    aether_computed_style_t flat{};
    std::array<aether_background_layer, 3> layers{};
    std::array<aether_gradient_stop, 3> stops{}, text_stops{};
    std::array<aether_backdrop_filter, 2> filters{};
    std::array<aether_transform_operation, 2> transforms{};
    std::array<aether_shadow, 2> text{}, boxes{};
    char image[24] = "asset://immutable";
    char empty[1] = "";
    Builder() {
#define SEED(type, name) seed(flat.name);
        AETHER_STYLE_SCALAR_FIELDS(SEED)
#undef SEED
        std::strcpy(flat.font_family, "Frozen fixture");
        for (size_t i=0;i<stops.size();++i) {
            seed(stops[i].color); seed(stops[i].position); stops[i].offset=float(i)/2; stops[i].has_position=1;
            text_stops[i]=stops[i]; text_stops[i].color.r=.9f;
        }
        for (auto& layer: layers) {
            layer.kind=AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT;
            layer.gradient={.25f,.75f,stops.data(),int32_t(stops.size())};
            seed(layer.position_x); seed(layer.position_y); seed(layer.size_x); seed(layer.size_y);
            layer.size_kind=AETHER_BACKGROUND_SIZE_EXPLICIT; layer.repeat_x=AETHER_BACKGROUND_SPACE; layer.repeat_y=AETHER_BACKGROUND_ROUND;
        }
        layers[1].image_source=image; layers[1].kind=AETHER_BACKGROUND_LAYER_IMAGE_URL;
        layers[2].image_source=empty;
        for(auto& f: filters) { f.kind=AETHER_BACKDROP_FILTER_BLUR; seed(f.length_amount); f.scalar_amount=.5f; }
        for(auto& t: transforms) {
            t.kind=AETHER_TRANSFORM_MATRIX; seed(t.x); seed(t.y); t.scalar_x=2; t.scalar_y=3; t.angle_radians=.75f;
            t.matrix_a=1;t.matrix_b=2;t.matrix_c=3;t.matrix_d=4;t.matrix_e=5;t.matrix_f=6;
        }
        for(auto& s: text) { s.offset_x=1;s.offset_y=2;s.blur_radius=3;s.spread_radius=4;seed(s.color);s.inset=1; }
        boxes=text;
        flat.background_layers={layers.data(),int32_t(layers.size())};
        flat.backdrop_filters={filters.data(),int32_t(filters.size())};
        flat.transforms={transforms.data(),int32_t(transforms.size())};
        flat.text_shadows={text.data(),int32_t(text.size())};flat.box_shadows={boxes.data(),int32_t(boxes.size())};
        flat.text_gradient={.6f,.8f,text_stops.data(),int32_t(text_stops.size())};
    }
    Builder(const Builder&)=delete;
};
inline void equal(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    need(snapshot_test::complete_style_equal(a,b), "complete fieldwise style equality");
    need(std::memcmp(a.font_family,b.font_family,64)==0,"all inline font bytes preserved");
    // Public semantic layer equality may ignore inactive union-like fields;
    // frozen export must preserve even these observable descriptors.
    need(a.background_layers.count==b.background_layers.count,"all layers");
    for(int i=0;i<a.background_layers.count;++i) {
        auto& x=a.background_layers.items[i];auto& y=b.background_layers.items[i];
        need(snapshot_test::gradient_equal(x.gradient,y.gradient),"all nested gradient fields including image layers");
        need(bool(x.image_source)==bool(y.image_source),"null/empty URL distinction");
        if(x.image_source) need(std::strcmp(x.image_source,y.image_source)==0,"URL contents");
    }
}
inline void disjoint_source(aether::style::ComputedStyleReadView source,const aether_computed_style_t& copy) {
    if(!source.background_layers().empty())need(static_cast<const void*>(&source.background_layers()[0])!=copy.background_layers.items,"layer array differs from frozen");
    for(size_t i=0;i<source.background_layers().size();++i) {
        const auto layer=source.background_layers()[i].view();
        if(layer.image_source.present())need(layer.image_source.c_str()!=copy.background_layers.items[i].image_source,"URL differs from frozen");
        if(!layer.gradient.stops.empty())need(&layer.gradient.stops[0]!=copy.background_layers.items[i].gradient.stops,"nested stops differ from frozen");
    }
#define DISJOINT(field, pointer) if(!source.field().empty())need(&source.field()[0]!=copy.pointer,"payload differs from frozen: " #field)
    DISJOINT(backdrop_filters,backdrop_filters.items);DISJOINT(transforms,transforms.items);
    DISJOINT(text_shadows,text_shadows.items);DISJOINT(box_shadows,box_shadows.items);
#undef DISJOINT
    if(!source.text_gradient().stops.empty())need(&source.text_gradient().stops[0]!=copy.text_gradient.stops,"text stops differ from frozen");
}
}
