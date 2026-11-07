#pragma once
// Public-ABI fixtures only: intentionally no private core headers.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace snapshot_test {
[[noreturn]] inline void fail(const char* message) {
    std::fprintf(stderr, "FAIL snapshot: %s\n", message); std::fflush(stderr); std::exit(1);
}
inline void need(bool condition, const char* message) { if (!condition) fail(message); }
inline void ok(aether_status_t status, const char* message) {
    if (status != AETHER_OK) { std::fprintf(stderr, "status=%d: ", int(status)); fail(message); }
}
inline bool eq(float a, float b) { return a == b; }
inline bool eq(aether_color_rgba a, aether_color_rgba b) {
    return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}
inline bool eq(aether_css_length a, aether_css_length b) {
    return a.value==b.value && a.unit==b.unit && a.linear_percent==b.linear_percent &&
        a.linear_em==b.linear_em && a.linear_rem==b.linear_rem && a.linear_vw==b.linear_vw && a.linear_vh==b.linear_vh;
}
inline bool eq(aether_css_edges a, aether_css_edges b) {
    return eq(a.left,b.left)&&eq(a.top,b.top)&&eq(a.right,b.right)&&eq(a.bottom,b.bottom);
}
inline bool eq(aether_rectf a, aether_rectf b) { return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height; }
inline bool eq(aether_corner_radius a, aether_corner_radius b) { return eq(a.horizontal,b.horizontal)&&eq(a.vertical,b.vertical); }
inline bool eq(aether_border_radii a,aether_border_radii b) {
    return eq(a.top_left,b.top_left)&&eq(a.top_right,b.top_right)&&eq(a.bottom_left,b.bottom_left)&&eq(a.bottom_right,b.bottom_right);
}
inline bool eq(aether_corner_radii a,aether_corner_radii b) {
    return a.top_left_x==b.top_left_x&&a.top_left_y==b.top_left_y&&a.top_right_x==b.top_right_x&&
        a.top_right_y==b.top_right_y&&a.bottom_right_x==b.bottom_right_x&&a.bottom_right_y==b.bottom_right_y&&
        a.bottom_left_x==b.bottom_left_x&&a.bottom_left_y==b.bottom_left_y;
}
inline bool gradient_equal(const aether_linear_gradient& a, const aether_linear_gradient& b) {
    if (a.direction_x!=b.direction_x || a.direction_y!=b.direction_y || a.stop_count!=b.stop_count) return false;
    for(int i=0;i<a.stop_count;++i) {
        const auto& x=a.stops[i]; const auto& y=b.stops[i];
        if(!eq(x.color,y.color)||!eq(x.position,y.position)||x.offset!=y.offset||x.has_position!=y.has_position) return false;
    }
    return true;
}
inline bool scalar_style_equal(const aether_computed_style_t& a,const aether_computed_style_t& b) {
#define SAME(f) if(a.f!=b.f) return false
#define VALUE(f) if(!eq(a.f,b.f)) return false
    SAME(display); SAME(flex_direction); SAME(justify_content); SAME(align_items); SAME(align_self);
    SAME(flex_grow); SAME(flex_shrink); SAME(flex_basis.kind); VALUE(flex_basis.length); SAME(flex_wrap); SAME(gap);
    SAME(position); VALUE(inset); VALUE(width); VALUE(height); VALUE(min_width); VALUE(min_height); VALUE(max_width); VALUE(max_height);
    VALUE(margin); VALUE(padding); VALUE(border_width); SAME(box_sizing); VALUE(color); VALUE(background_color); VALUE(border_color);
    VALUE(border_colors.left); VALUE(border_colors.top); VALUE(border_colors.right); VALUE(border_colors.bottom);
    SAME(border_styles.left); SAME(border_styles.top); SAME(border_styles.right); SAME(border_styles.bottom);
    SAME(has_text_gradient); VALUE(transform_origin.x); VALUE(transform_origin.y); VALUE(border_radii); SAME(opacity);
    if(std::strcmp(a.font_family,b.font_family)) return false;
    SAME(font_size); SAME(font_weight); SAME(line_height); SAME(text_align); SAME(text_transform); SAME(white_space);
    SAME(overflow_x); SAME(overflow_y); SAME(cursor); SAME(pointer_events); SAME(visible); SAME(z_index); SAME(z_index_kind);
    SAME(transition_properties); SAME(transition_duration); SAME(transition_delay); SAME(transition_timing);
#undef SAME
#undef VALUE
    return true;
}
inline bool style_equal(const aether_computed_style_t& a,const aether_computed_style_t& b) {
    return scalar_style_equal(a,b) && aether_background_layer_list_equal(&a.background_layers,&b.background_layers) &&
        aether_backdrop_filter_list_equal(&a.backdrop_filters,&b.backdrop_filters) &&
        gradient_equal(a.text_gradient,b.text_gradient) && aether_transform_list_equal(&a.transforms,&b.transforms) &&
        aether_shadow_list_equal(&a.text_shadows,&b.text_shadows) && aether_shadow_list_equal(&a.box_shadows,&b.box_shadows);
}
// Export-copy comparison includes inactive payload fields and every font byte.
inline bool complete_style_equal(const aether_computed_style_t& a,const aether_computed_style_t& b) {
    if(!scalar_style_equal(a,b)||std::memcmp(a.font_family,b.font_family,sizeof(a.font_family))||
        a.background_layers.count!=b.background_layers.count||a.backdrop_filters.count!=b.backdrop_filters.count||
        a.transforms.count!=b.transforms.count||a.text_shadows.count!=b.text_shadows.count||
        a.box_shadows.count!=b.box_shadows.count||!gradient_equal(a.text_gradient,b.text_gradient))return false;
    for(int i=0;i<a.background_layers.count;++i) {
        const auto& x=a.background_layers.items[i];const auto& y=b.background_layers.items[i];
        if(x.kind!=y.kind||!gradient_equal(x.gradient,y.gradient)||bool(x.image_source)!=bool(y.image_source)||
            (x.image_source&&std::strcmp(x.image_source,y.image_source))||!eq(x.position_x,y.position_x)||
            !eq(x.position_y,y.position_y)||!eq(x.size_x,y.size_x)||!eq(x.size_y,y.size_y)||
            x.size_kind!=y.size_kind||x.repeat_x!=y.repeat_x||x.repeat_y!=y.repeat_y)return false;
    }
    for(int i=0;i<a.backdrop_filters.count;++i) {
        const auto& x=a.backdrop_filters.items[i];const auto& y=b.backdrop_filters.items[i];
        if(x.kind!=y.kind||!eq(x.length_amount,y.length_amount)||x.scalar_amount!=y.scalar_amount)return false;
    }
    for(int i=0;i<a.transforms.count;++i) {
        const auto& x=a.transforms.items[i];const auto& y=b.transforms.items[i];
        if(x.kind!=y.kind||!eq(x.x,y.x)||!eq(x.y,y.y)||x.scalar_x!=y.scalar_x||x.scalar_y!=y.scalar_y||
            x.angle_radians!=y.angle_radians||x.matrix_a!=y.matrix_a||x.matrix_b!=y.matrix_b||
            x.matrix_c!=y.matrix_c||x.matrix_d!=y.matrix_d||x.matrix_e!=y.matrix_e||x.matrix_f!=y.matrix_f)return false;
    }
    const auto shadows_equal=[](const aether_shadow_list& x,const aether_shadow_list& y) {
        for(int i=0;i<x.count;++i) {
            const auto& u=x.items[i];const auto& v=y.items[i];
            if(u.offset_x!=v.offset_x||u.offset_y!=v.offset_y||u.blur_radius!=v.blur_radius||
                u.spread_radius!=v.spread_radius||!eq(u.color,v.color)||u.inset!=v.inset)return false;
        }return true;
    };
    return shadows_equal(a.text_shadows,b.text_shadows)&&shadows_equal(a.box_shadows,b.box_shadows);
}
// A shallow descriptor snapshot is used only while the original loan is alive.
inline bool same_payload_addresses(const aether_computed_style_t& a,const aether_computed_style_t& b) {
    return a.background_layers.items==b.background_layers.items&&a.backdrop_filters.items==b.backdrop_filters.items&&
        a.text_gradient.stops==b.text_gradient.stops&&a.transforms.items==b.transforms.items&&
        a.text_shadows.items==b.text_shadows.items&&a.box_shadows.items==b.box_shadows.items;
}
struct Flat {
    aether_computed_style_t value{};
    Flat()=default;
    explicit Flat(aether_computed_style_handle_t handle) { need(handle!=nullptr,"style handle exists"); ok(aether_computed_style_copy(handle,&value),"copy owned public flat"); }
    Flat(const Flat&)=delete; Flat& operator=(const Flat&)=delete;
    Flat(Flat&& other) noexcept : value(other.value) { other.value={}; }
    ~Flat() { aether_computed_style_free(&value); }
};
inline Flat map_flat(aether_style_map_t map,aether_node_t node) {
    auto handle=aether_style_map_get(map,node); Flat flat(handle); aether_computed_style_handle_destroy(handle); return flat;
}
inline aether_computed_style_handle_t revision_handle(aether_style_revision_t revision,aether_node_t target,bool changes=false) {
    const int count=changes ? aether_style_revision_change_count(revision) : aether_style_revision_style_count(revision);
    for(int i=0;i<count;++i) {
        aether_node_t node{}; aether_computed_style_handle_t handle{};
        ok(changes ? aether_style_revision_get_change(revision,i,&node,&handle,nullptr) :
            aether_style_revision_get_style(revision,i,&node,&handle,nullptr),"read revision entry");
        if(node==target) { need(handle!=nullptr,"nonremoved revision target"); return handle; }
        aether_computed_style_handle_destroy(handle);
    }
    fail("target present in public revision enumeration");
}
inline void disjoint(const aether_computed_style_t& a,const aether_computed_style_t& b) {
#define SEPARATE(field) need(!a.field || a.field!=b.field,"independent allocation: " #field)
    SEPARATE(background_layers.items); SEPARATE(backdrop_filters.items); SEPARATE(text_gradient.stops);
    SEPARATE(transforms.items); SEPARATE(text_shadows.items); SEPARATE(box_shadows.items);
#undef SEPARATE
    need(a.background_layers.count==b.background_layers.count,"same background layer coverage");
    for(int i=0;i<a.background_layers.count;++i) {
        const auto& x=a.background_layers.items[i];const auto& y=b.background_layers.items[i];
        need(!x.image_source||x.image_source!=y.image_source,"independent nested URL storage");
        need(!x.gradient.stops||x.gradient.stops!=y.gradient.stops,"independent nested gradient storage");
    }
}
inline void rich(const aether_computed_style_t& s) {
    need(s.background_layers.count==2,"fixture has both background layers");
    bool image=false,gradient=false;
    for(int i=0;i<s.background_layers.count;++i) {
        const auto& layer=s.background_layers.items[i];image|=layer.image_source&&*layer.image_source;
        gradient|=layer.gradient.stop_count>=2&&layer.gradient.stops;
    }
    need(image&&gradient,"fixture has URL bytes and nested stops");
    need(s.backdrop_filters.count>0,"fixture has filters");need(s.transforms.count>0,"fixture has transforms");
    need(s.text_shadows.count>0&&s.box_shadows.count>0,"fixture has both shadows");
    need(s.has_text_gradient&&s.text_gradient.stop_count>=2,"fixture has independent text-gradient stops");
}
// Legal writes through the nested mutable pointers. No const_cast and no free
// of a callback loan; descriptor rebinding is handled separately by each test.
inline void mutate_payload(const aether_computed_style_t& s,float marker=31.f) {
    for(int i=0;i<s.background_layers.count;++i) {
        auto& l=s.background_layers.items[i];l.position_x.value+=marker;l.repeat_x=AETHER_BACKGROUND_NO_REPEAT;
        if(l.image_source&&l.image_source[0]) l.image_source[0]='X';
        for(int j=0;j<l.gradient.stop_count;++j) { l.gradient.stops[j].color={.125f,.25f,.375f,1};l.gradient.stops[j].offset+=marker; }
    }
    for(int i=0;i<s.backdrop_filters.count;++i) s.backdrop_filters.items[i].length_amount.value+=marker;
    for(int i=0;i<s.transforms.count;++i) s.transforms.items[i].x.value+=marker;
    for(int i=0;i<s.text_shadows.count;++i) s.text_shadows.items[i].offset_x+=marker;
    for(int i=0;i<s.box_shadows.count;++i) s.box_shadows.items[i].offset_y+=marker;
    for(int i=0;i<s.text_gradient.stop_count;++i) s.text_gradient.stops[i].color={.5f,.25f,.125f,1};
}
inline constexpr const char* rich_css=
    "html,body{display:block;margin:0;padding:0}#target{display:block;width:180px;height:40px;"
    "font-size:20px;line-height:1.5;color:#123456;background-color:#abcdef;"
    "background-image:linear-gradient(90deg,red 0%,blue 100%),url(asset://snapshot);"
    "background-size:20px 30px;background-repeat:no-repeat;background-clip:text;"
    "backdrop-filter:blur(3px);transform:translate(2px,3px);"
    "text-shadow:1px 2px 3px red;box-shadow:4px 5px 6px blue}";
struct Fixture {
    aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};
    aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_node_t target{};
    explicit Fixture(bool input=false,bool contextual=true,const char* css=rich_css) {
        ok(aether_doc_create(&doc),"create doc");
        ok(aether_doc_parse_html(doc,input?"<html><body><input id='target' value='snapshot'></body></html>":
            "<html><body><div id='target'>snapshot words</div></body></html>"),"parse fixture");
        target=aether_doc_get_element_by_id(doc,"target");need(target!=nullptr,"fixture target");
        aether_style_environment_t env{800,600,1,16,0,0,0};
        ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"create runtime");
        int64_t sheet{};ok(aether_style_runtime_add_stylesheet(runtime,css,0,"snapshot-public-test",&sheet,nullptr),"add sheet");
        revision=resolve();
        if(contextual)ok(aether_style_revision_create_map(revision,&map),"revision map");
        else ok(aether_style_runtime_create_resolved_style_map(runtime,doc,&map),"legacy map");
        ok(aether_layout_create(nullptr,nullptr,&engine),"create engine");
    }
    aether_style_revision_t resolve() {
        aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),nullptr,&tx),"prepare revision");
        aether_style_revision_t r{};ok(aether_style_runtime_commit_revision(runtime,tx,&r),"commit revision");
        aether_style_transaction_destroy(tx);return r;
    }
    void build() { need(!layout,"layout starts empty");ok(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&layout),"layout fixture"); }
    ~Fixture() {
        aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);
        aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
    }
};
// Canonical, fieldwise command signature, not object padding or pointer values.
struct Digest {
    uint64_t hash=1469598103934665603ULL;
    template<class T> void scalar(T value) { const auto* p=reinterpret_cast<const unsigned char*>(&value);for(size_t i=0;i<sizeof(T);++i){hash^=p[i];hash*=1099511628211ULL;} }
    void text(const char* s){scalar(bool(s));if(s){while(*s)scalar(*s++);scalar(char(0));}}
    void rect(aether_rectf r){scalar(r.x);scalar(r.y);scalar(r.width);scalar(r.height);}
    void color(aether_color_rgba c){scalar(c.r);scalar(c.g);scalar(c.b);scalar(c.a);}
    void length(aether_css_length l){scalar(l.value);scalar(l.unit);scalar(l.linear_percent);scalar(l.linear_em);scalar(l.linear_rem);scalar(l.linear_vw);scalar(l.linear_vh);}
    void radii(aether_corner_radii r){scalar(r.top_left_x);scalar(r.top_left_y);scalar(r.top_right_x);scalar(r.top_right_y);scalar(r.bottom_right_x);scalar(r.bottom_right_y);scalar(r.bottom_left_x);scalar(r.bottom_left_y);}
    void gradient(const aether_linear_gradient& g){scalar(g.direction_x);scalar(g.direction_y);scalar(g.stop_count);for(int i=0;i<g.stop_count;++i){const auto& s=g.stops[i];color(s.color);length(s.position);scalar(s.offset);scalar(s.has_position);}}
    void shadow(aether_shadow s){scalar(s.offset_x);scalar(s.offset_y);scalar(s.blur_radius);scalar(s.spread_radius);color(s.color);scalar(s.inset);}
};
inline uint64_t commands(aether_display_list_t list) {
    Digest d;d.scalar(aether_display_list_get_count(list));
    for(int i=0;i<aether_display_list_get_count(list);++i){
        aether_paint_command_t c{};ok(aether_display_list_get_command(list,i,&c),"export paint command");d.scalar(c.tag);
        switch(c.tag){
        case AETHER_PAINT_FILL_ROUNDED_RECT:{auto& v=c.data.fill_rounded_rect;d.rect(v.rect);d.radii(v.radii);d.color(v.color);break;}
        case AETHER_PAINT_DRAW_BOX_SHADOW:{auto& v=c.data.draw_box_shadow;d.rect(v.rect);d.radii(v.radii);d.shadow(v.shadow);break;}
        case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:{auto& v=c.data.fill_rounded_rect_gradient;d.rect(v.rect);d.radii(v.radii);d.rect(v.gradient_rect);d.gradient(v.gradient);d.scalar(v.opacity);break;}
        case AETHER_PAINT_DRAW_TEXT:{auto& v=c.data.draw_text;d.text(v.text);d.rect(v.rect);d.text(v.font_family);d.scalar(v.font_size);d.scalar(v.font_weight);d.scalar(v.text_align);d.color(v.color);d.scalar(v.wrap);break;}
        case AETHER_PAINT_DRAW_TEXT_GRADIENT:{auto& v=c.data.draw_text_gradient;d.text(v.text);d.rect(v.rect);d.text(v.font_family);d.scalar(v.font_size);d.scalar(v.font_weight);d.scalar(v.text_align);d.gradient(v.gradient);d.scalar(v.opacity);break;}
        case AETHER_PAINT_DRAW_TEXT_SHADOW:{auto& v=c.data.draw_text_shadow;d.text(v.text);d.rect(v.rect);d.text(v.font_family);d.scalar(v.font_size);d.scalar(v.font_weight);d.scalar(v.text_align);d.shadow(v.shadow);d.scalar(v.wrap);break;}
        case AETHER_PAINT_DRAW_BACKGROUND_IMAGE:{auto& v=c.data.draw_background_image;d.text(v.source);d.rect(v.paint_rect);d.length(v.position_x);d.length(v.position_y);d.scalar(v.size_kind);d.length(v.size_x);d.length(v.size_y);d.scalar(v.repeat_x);d.scalar(v.repeat_y);d.scalar(v.opacity);d.rect(v.source_rect);d.rect(v.destination_rect);d.scalar(v.has_resolved_geometry);break;}
        case AETHER_PAINT_APPLY_BACKDROP_FILTER:{auto& v=c.data.apply_backdrop_filter;d.rect(v.rect);d.radii(v.radii);d.scalar(v.filters.count);for(int j=0;j<v.filters.count;++j){auto f=v.filters.items[j];d.scalar(f.kind);d.length(f.length_amount);d.scalar(f.scalar_amount);}break;}
        case AETHER_PAINT_PUSH_TRANSFORM:{auto m=c.data.push_transform.matrix;d.scalar(m.m11);d.scalar(m.m12);d.scalar(m.m21);d.scalar(m.m22);d.scalar(m.dx);d.scalar(m.dy);break;}
        case AETHER_PAINT_PUSH_CLIP:d.rect(c.data.push_clip.rect);break;
        case AETHER_PAINT_POP_CLIP:case AETHER_PAINT_POP_TRANSFORM:case AETHER_PAINT_POP_SCROLL_OFFSET:break;
        case AETHER_PAINT_PUSH_SCROLL_OFFSET:d.scalar(c.data.push_scroll_offset.scroll_container_id);break;
        case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:d.scalar(c.data.draw_scrollbar_chrome.scroll_container_id);break;
        case AETHER_PAINT_BEGIN_PAINT_CHUNK:case AETHER_PAINT_END_PAINT_CHUNK:d.scalar(c.data.paint_chunk.style_node_id);break;
        default:fail("unexpected paint tag in borderless fixture: extend fieldwise oracle");
        }
        aether_paint_command_free(&c);
    }return d.hash;
}
inline uint64_t paint(aether_layout_result_t layout,aether_style_map_t map=nullptr,aether_node_t focus=nullptr) {
    aether_display_list_builder_t builder{};ok(aether_display_list_builder_create(nullptr,nullptr,&builder),"create painter");
    aether_display_list_t list{};ok(aether_display_list_build_with_styles(builder,layout,map,focus,&list),"paint snapshot");
    auto d=commands(list);aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);return d;
}
inline void require_sanitized_hazards() {
#if !defined(__SANITIZE_ADDRESS__)
# if defined(__has_feature)
#  if __has_feature(address_sanitizer)
    return;
#  endif
# endif
    fail("hazard cases require an ASan+UBSan-instrumented executable AND core; do not run on the unsanitized reference");
#endif
}
} // namespace snapshot_test
