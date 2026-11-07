#include "snapshot_test_support.h"
#include <aether/aether_contextual_transitions.h>
#include <climits>
#include "../src/dom/dom_types.h"
using namespace snapshot_test;

static uint64_t complete_commands(aether_display_list_t list) {
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
        case AETHER_PAINT_PUSH_CLIP:d.rect(c.data.push_clip.rect);d.radii(c.data.push_clip.radii);break;
        case AETHER_PAINT_POP_CLIP:case AETHER_PAINT_POP_TRANSFORM:case AETHER_PAINT_POP_SCROLL_OFFSET:break;
        case AETHER_PAINT_PUSH_SCROLL_OFFSET:d.scalar(c.data.push_scroll_offset.scroll_container_id);break;
        case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:d.scalar(c.data.draw_scrollbar_chrome.scroll_container_id);break;
        case AETHER_PAINT_BEGIN_PAINT_CHUNK:case AETHER_PAINT_END_PAINT_CHUNK:d.scalar(c.data.paint_chunk.style_node_id);break;
        case AETHER_PAINT_STROKE_ROUNDED_RECT:{auto& v=c.data.stroke_rounded_rect;d.rect(v.rect);d.radii(v.radii);d.scalar(v.stroke_width);d.color(v.color);break;}
        case AETHER_PAINT_FILL_BORDER_SIDE:{auto& v=c.data.fill_border_side;d.rect(v.outer_rect);d.radii(v.outer_radii);d.rect(v.inner_rect);d.radii(v.inner_radii);d.scalar(v.side);d.scalar(v.style);d.color(v.color);break;}
        default:fail("unexpected paint tag in callback fixture");
        }
        aether_paint_command_free(&c);
    }return d.hash;
}

struct PaintList {
    aether_display_list_t value{};
    explicit PaintList(aether_display_list_t v):value(v){}
    ~PaintList(){aether_display_list_destroy(value);}
    PaintList(const PaintList&)=delete;
};
static bool text_equal(const char* a,const char* b) {return bool(a)==bool(b)&&(!a||std::strcmp(a,b)==0);}
static bool command_equal(const aether_paint_command_t& a,const aether_paint_command_t& b) {
    if(a.tag!=b.tag)return false;
    const auto shadow_equal=[](const aether_shadow& x,const aether_shadow& y){return x.offset_x==y.offset_x&&
        x.offset_y==y.offset_y&&x.blur_radius==y.blur_radius&&x.spread_radius==y.spread_radius&&eq(x.color,y.color)&&x.inset==y.inset;};
#define LEAVES(name) const auto& x=a.data.name;const auto& y=b.data.name
    switch(a.tag) {
    case AETHER_PAINT_FILL_ROUNDED_RECT:{LEAVES(fill_rounded_rect);return eq(x.rect,y.rect)&&eq(x.radii,y.radii)&&eq(x.color,y.color);}
    case AETHER_PAINT_DRAW_BOX_SHADOW:{LEAVES(draw_box_shadow);return eq(x.rect,y.rect)&&eq(x.radii,y.radii)&&shadow_equal(x.shadow,y.shadow);}
    case AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT:{LEAVES(fill_rounded_rect_gradient);return eq(x.rect,y.rect)&&eq(x.radii,y.radii)&&eq(x.gradient_rect,y.gradient_rect)&&gradient_equal(x.gradient,y.gradient)&&x.opacity==y.opacity;}
    case AETHER_PAINT_DRAW_TEXT:{LEAVES(draw_text);return text_equal(x.text,y.text)&&eq(x.rect,y.rect)&&std::memcmp(x.font_family,y.font_family,sizeof(x.font_family))==0&&x.font_size==y.font_size&&x.font_weight==y.font_weight&&x.text_align==y.text_align&&eq(x.color,y.color)&&x.wrap==y.wrap;}
    case AETHER_PAINT_DRAW_TEXT_GRADIENT:{LEAVES(draw_text_gradient);return text_equal(x.text,y.text)&&eq(x.rect,y.rect)&&std::memcmp(x.font_family,y.font_family,sizeof(x.font_family))==0&&x.font_size==y.font_size&&x.font_weight==y.font_weight&&x.text_align==y.text_align&&gradient_equal(x.gradient,y.gradient)&&x.opacity==y.opacity;}
    case AETHER_PAINT_DRAW_TEXT_SHADOW:{LEAVES(draw_text_shadow);return text_equal(x.text,y.text)&&eq(x.rect,y.rect)&&std::memcmp(x.font_family,y.font_family,sizeof(x.font_family))==0&&x.font_size==y.font_size&&x.font_weight==y.font_weight&&x.text_align==y.text_align&&shadow_equal(x.shadow,y.shadow)&&x.wrap==y.wrap;}
    case AETHER_PAINT_DRAW_BACKGROUND_IMAGE:{LEAVES(draw_background_image);return text_equal(x.source,y.source)&&eq(x.paint_rect,y.paint_rect)&&eq(x.position_x,y.position_x)&&eq(x.position_y,y.position_y)&&x.size_kind==y.size_kind&&eq(x.size_x,y.size_x)&&eq(x.size_y,y.size_y)&&x.repeat_x==y.repeat_x&&x.repeat_y==y.repeat_y&&x.opacity==y.opacity&&eq(x.source_rect,y.source_rect)&&eq(x.destination_rect,y.destination_rect)&&x.has_resolved_geometry==y.has_resolved_geometry;}
    case AETHER_PAINT_APPLY_BACKDROP_FILTER:{LEAVES(apply_backdrop_filter);
        if(!eq(x.rect,y.rect)||!eq(x.radii,y.radii)||x.filters.count!=y.filters.count)return false;
        for(int i=0;i<x.filters.count;++i){const auto& u=x.filters.items[i];const auto& v=y.filters.items[i];
            if(u.kind!=v.kind||!eq(u.length_amount,v.length_amount)||u.scalar_amount!=v.scalar_amount)return false;}return true;}
    case AETHER_PAINT_PUSH_TRANSFORM:{const auto& x=a.data.push_transform.matrix;const auto& y=b.data.push_transform.matrix;
        return x.m11==y.m11&&x.m12==y.m12&&x.m21==y.m21&&x.m22==y.m22&&x.dx==y.dx&&x.dy==y.dy;}
    case AETHER_PAINT_PUSH_CLIP:{LEAVES(push_clip);return eq(x.rect,y.rect)&&eq(x.radii,y.radii);}
    case AETHER_PAINT_POP_CLIP:case AETHER_PAINT_POP_TRANSFORM:case AETHER_PAINT_POP_SCROLL_OFFSET:return true;
    case AETHER_PAINT_PUSH_SCROLL_OFFSET:return a.data.push_scroll_offset.scroll_container_id==b.data.push_scroll_offset.scroll_container_id;
    case AETHER_PAINT_DRAW_SCROLLBAR_CHROME:return a.data.draw_scrollbar_chrome.scroll_container_id==b.data.draw_scrollbar_chrome.scroll_container_id;
    case AETHER_PAINT_BEGIN_PAINT_CHUNK:case AETHER_PAINT_END_PAINT_CHUNK:return a.data.paint_chunk.style_node_id==b.data.paint_chunk.style_node_id;
    case AETHER_PAINT_STROKE_ROUNDED_RECT:{LEAVES(stroke_rounded_rect);return eq(x.rect,y.rect)&&eq(x.radii,y.radii)&&x.stroke_width==y.stroke_width&&eq(x.color,y.color);}
    case AETHER_PAINT_FILL_BORDER_SIDE:{LEAVES(fill_border_side);return eq(x.outer_rect,y.outer_rect)&&eq(x.outer_radii,y.outer_radii)&&eq(x.inner_rect,y.inner_rect)&&eq(x.inner_radii,y.inner_radii)&&x.side==y.side&&x.style==y.style&&eq(x.color,y.color);}
    default:fail("unexpected command in direct equality");
    }
#undef LEAVES
}
static bool commands_equal(const PaintList& a,const PaintList& b) {
    const int count=aether_display_list_get_count(a.value);
    if(count!=aether_display_list_get_count(b.value))return false;
    for(int i=0;i<count;++i) {
        aether_paint_command_t x{},y{};
        ok(aether_display_list_get_command(a.value,i,&x),"left complete command");
        ok(aether_display_list_get_command(b.value,i,&y),"right complete command");
        const bool same=command_equal(x,y);aether_paint_command_free(&x);aether_paint_command_free(&y);
        if(!same)return false;
    }return true;
}

struct State {
    Fixture* fixture;
    aether_display_list_builder_t builder{};
    bool drop = false, mutate = false, edit_dom = false, text_gradient = false;
    int calls = 0;
    bool contextual = false, external = false, nest = false;
    int invalidation = -1;
    const aether_computed_style_t* expected = nullptr;
    const aether_computed_style_t* mutated = nullptr;
    const aether_computed_style_t* outer = nullptr;
    const aether_computed_style_t* original_buffers = nullptr;
};
static aether_text_metrics_t caret(const char*,const aether_computed_style_t*,float,void*);
static aether_text_metrics_t contextual_caret(const char*,const aether_computed_style_t*,float,float,void*);
static void nested_then_drop(State& s,const aether_computed_style_t* outer = nullptr) {
    auto& f = *s.fixture;
    aether_display_list_t sentinel{};
    need(aether_display_list_build_with_styles(s.builder, f.layout, f.map, f.target, &sentinel)
        == AETHER_ERROR_INVALID_STATE, "same-builder reentry rejected");
    need(!sentinel, "same-builder reentry preserves output");
    aether_display_list_builder_t other{};
    State inner{&f};inner.text_gradient=s.text_gradient;inner.contextual=s.contextual;inner.external=s.external;
    inner.expected=s.expected;inner.outer=outer;inner.original_buffers=s.original_buffers;
    ok(aether_display_list_builder_create(outer?caret:nullptr, &inner, &other), "nested painter");
    inner.builder=other;
    if(outer&&s.contextual)aether_display_list_builder_set_contextual_text_measurer(other,contextual_caret,&inner);
    ok(aether_display_list_build_with_styles(other, f.layout, s.external?f.map:nullptr, outer?f.target:nullptr, &sentinel), "other-builder reentry independent");
    if(outer)need(inner.calls==1,"nested focused caret really invokes a second live style frame");
    need(aether_display_list_get_count(sentinel) > 0, "nested commands present");
    aether_display_list_destroy(sentinel); aether_display_list_builder_destroy(other);
    if(s.drop) {
        aether_style_map_destroy(f.map); f.map = nullptr;
        aether_layout_result_destroy(f.layout); f.layout = nullptr;
    }
}
static aether_text_metrics_t caret(const char*, const aether_computed_style_t* style, float, void* data) {
    auto& s = *static_cast<State*>(data); ++s.calls;
    need(s.expected&&complete_style_equal(*style,*s.expected),"complete callback bytes match pinned source before mutation");
    if(s.outer){need(style!=s.outer,"nested roots disjoint");disjoint(*style,*s.outer);disjoint(*style,*s.original_buffers);}
    const auto descriptors=*style;
    std::vector<aether_background_layer> mutated_layers;
    need(style->backdrop_filters.count > 0, "callback fixture has filters");
    need(style->transforms.count > 0, "callback fixture has transforms");
    need(style->text_shadows.count > 0 && style->box_shadows.count > 0, "callback fixture has both shadows");
    if (s.text_gradient) {
        need(style->background_layers.count == 0, "single gradient moved from background to text");
        need(style->has_text_gradient && style->text_gradient.stop_count >= 2 && style->text_gradient.stops,
            "callback fixture has independent text-gradient stops");
    } else {
        need(style->background_layers.count == 2, "callback fixture has both background layers");
        bool url = false, gradient = false;
        for (int i = 0; i < style->background_layers.count; ++i) {
            const auto& layer = style->background_layers.items[i];
            url |= layer.image_source && *layer.image_source;
            gradient |= layer.gradient.stop_count >= 2 && layer.gradient.stops;
        }
        need(url && gradient, "callback fixture has nested URL and gradient stops");
        need(!style->has_text_gradient && style->text_gradient.stop_count == 0,
            "multi-layer clip preserves backgrounds without inventing text gradient");
    }
    if (s.mutate) {
        mutate_payload(*style);
        need(s.mutated&&complete_style_equal(*style,*s.mutated),"all legal payload mutations observed");
        if(style->background_layers.count)mutated_layers.assign(style->background_layers.items,
            style->background_layers.items+style->background_layers.count);
        // Nested descriptors may legally be rebound through a mutable layer.
        // Do not free or retain the callback's buffers. Original ledger owns them.
        for (int i = 0; i < style->background_layers.count; ++i) {
            auto& layer = style->background_layers.items[i];
            layer.image_source = nullptr;
            layer.gradient.stops = nullptr;
            layer.gradient.stop_count = 999;
        }
    }
    if ((s.drop||s.nest) && s.calls == 1) {
        auto original_buffers=descriptors;original_buffers.background_layers.items=mutated_layers.data();
        s.original_buffers=&original_buffers;
        nested_then_drop(s,style);
        s.original_buffers=nullptr;
        need(same_payload_addresses(*style,descriptors),"outer payload addresses survive inner paint and owner teardown");
        for(int i=0;i<style->background_layers.count;++i) {
            auto& l=style->background_layers.items[i];
            need(!l.image_source&&!l.gradient.stops&&l.gradient.stop_count==999,"outer legal rebind survives nested callback");
            l=mutated_layers[size_t(i)];
        }
        need(complete_style_equal(*style,*s.mutated),"outer mutated bytes survive inner destruction");
        auto handle=revision_handle(s.fixture->revision,s.fixture->target);Flat retained(handle);aether_computed_style_handle_destroy(handle);
        need(complete_style_equal(retained.value,*s.expected),"source intact after nested callback and teardown");
        for(int i=0;i<style->background_layers.count;++i) {
            auto& l=style->background_layers.items[i];l.image_source=nullptr;l.gradient.stops=nullptr;l.gradient.stop_count=INT32_MAX;
        }
    }
    switch(s.invalidation) {
    case 0:++s.fixture->doc->identity->mutation_epoch;break;
    case 1:++s.fixture->doc->identity->style_tree_epoch;break;
    case 2:++s.fixture->doc->identity->structure_epoch;break;
    case 3:ok(aether_node_remove_child(s.fixture->target->parent,s.fixture->target),"retire measured input during callback");break;
    }
    if (s.edit_dom) ok(aether_element_set_attribute(s.fixture->target, "data-mutated", "yes"), "mutate DOM in caret");
    return {30, 24, 18};
}
static aether_text_metrics_t contextual_caret(const char* text, const aether_computed_style_t* style,
    float line_height, float max_width, void* data) {
    need(line_height == 30, "contextual caret uses frozen line-height metadata");
    return caret(text, style, max_width, data);
}
static aether_bool_t image(const char* source, aether_sizef* size, void* data) {
    auto& s = *static_cast<State*>(data); ++s.calls;
    need(source && std::strcmp(source, "asset://snapshot") == 0, "pinned image URL before callback release");
    if (s.drop && s.calls == 1) nested_then_drop(s);
    need(std::strcmp(source, "asset://snapshot") == 0, "pinned image URL after wrapper/layout release");
    if (s.edit_dom) ok(aether_element_set_attribute(s.fixture->target, "data-mutated", "yes"), "mutate DOM in image");
    *size = {20, 30}; return AETHER_TRUE;
}
static PaintList caret_case(bool destructive, bool contextual, bool external, bool text_gradient) {
    // The public clip implementation moves exactly one gradient into text_gradient.
    // Exercise that payload in a separate real CSS fixture, not an impossible
    // assumption that a two-layer gradient+URL declaration also creates it.
    std::string css = rich_css;
    if (text_gradient) {
        const auto url = css.find(",url(asset://snapshot)");
        need(url != std::string::npos, "text-gradient fixture derived from rich CSS");
        css.erase(url, std::strlen(",url(asset://snapshot)"));
    }
    Fixture f(true, contextual, css.c_str()); f.build();
    auto original = map_flat(f.map, f.target);
    auto mutated=map_flat(f.map,f.target);mutate_payload(mutated.value);
    State s{&f}; s.drop = destructive; s.mutate = destructive; s.text_gradient = text_gradient;
    s.contextual=contextual;s.external=external;s.expected=&original.value;s.mutated=&mutated.value;
    ok(aether_display_list_builder_create(caret, &s, &s.builder), "caret builder");
    if (contextual) aether_display_list_builder_set_contextual_text_measurer(s.builder, contextual_caret, &s);
    aether_display_list_t output{};
    ok(aether_display_list_build_with_styles(s.builder, f.layout, external ? f.map : nullptr, f.target, &output), "caret pin paint");
    need(s.calls > 0, "caret callback executed");
    if (destructive) need(!f.map && !f.layout, "external owners released in callback");
    auto h = revision_handle(f.revision, f.target); Flat retained(h); aether_computed_style_handle_destroy(h);
    need(style_equal(original.value, retained.value), "callback payload/rebind did not mutate revision");
    aether_display_list_builder_destroy(s.builder);
    return PaintList(output);
}
static PaintList image_case(bool destructive, bool contextual, bool external) {
    Fixture f(false, contextual); f.build();
    State s{&f}; s.drop = destructive;
    ok(aether_display_list_builder_create(nullptr, nullptr, &s.builder), "image builder");
    aether_display_list_builder_set_image_dimensions_callback(s.builder, image, &s);
    aether_display_list_t output{};
    ok(aether_display_list_build_with_styles(s.builder, f.layout, external ? f.map : nullptr, nullptr, &output), "image pin paint");
    need(s.calls > 0, "image callback executed");
    if (destructive) need(!f.map && !f.layout, "image external owners released");
    aether_display_list_builder_destroy(s.builder);
    return PaintList(output);
}
static void mutation_failure(bool image_callback) {
    Fixture f(!image_callback); f.build(); State s{&f};
    auto expected=map_flat(f.map,f.target);s.expected=&expected.value;
    ok(aether_display_list_builder_create(nullptr, nullptr, &s.builder), "prior builder");
    aether_display_list_t output{};
    ok(aether_display_list_build_with_styles(s.builder, f.layout, f.map, nullptr, &output), "prior output");
    const auto previous = output; const auto digest = complete_commands(previous);
    aether_display_list_builder_destroy(s.builder);
    s.edit_dom = true;
    ok(aether_display_list_builder_create(image_callback ? nullptr : caret, &s, &s.builder), "mutation builder");
    if (image_callback) aether_display_list_builder_set_image_dimensions_callback(s.builder, image, &s);
    need(aether_display_list_build_with_styles(s.builder, f.layout, f.map, f.target, &output)
        == AETHER_ERROR_INVALID_STATE, "callback DOM change rejects paint");
    need(output == previous && complete_commands(output) == digest, "callback failure preserves prior owned output");
    need(s.calls == 1, "no further callbacks after invalidation");
    aether_display_list_destroy(output); aether_display_list_builder_destroy(s.builder);
}

static void nested_epoch_abort_matrix() {
    for(bool contextual:{false,true})for(bool external:{false,true})for(bool text_gradient:{false,true})
    for(int invalidation=0;invalidation<4;++invalidation) {
        std::string css=rich_css;
        if(text_gradient)css.erase(css.find(",url(asset://snapshot)"),std::strlen(",url(asset://snapshot)"));
        Fixture f(true,contextual,css.c_str());f.build();
        auto expected=map_flat(f.map,f.target),mutated=map_flat(f.map,f.target);mutate_payload(mutated.value);
        State s{&f};s.expected=&expected.value;s.mutated=&mutated.value;s.contextual=contextual;
        s.external=external;s.text_gradient=text_gradient;s.nest=true;s.mutate=true;s.invalidation=invalidation;
        ok(aether_display_list_builder_create(nullptr,nullptr,&s.builder),"epoch prior painter");
        aether_display_list_t output{};
        ok(aether_display_list_build_with_styles(s.builder,f.layout,external?f.map:nullptr,nullptr,&output),"epoch prior output");
        const auto previous=output;const auto digest=complete_commands(output);
        aether_display_list_builder_destroy(s.builder);
        ok(aether_display_list_builder_create(caret,&s,&s.builder),"epoch callback painter");
        if(contextual)aether_display_list_builder_set_contextual_text_measurer(s.builder,contextual_caret,&s);
        auto* parent=f.target->parent;
        need(aether_display_list_build_with_styles(s.builder,f.layout,external?f.map:nullptr,f.target,&output)
            ==AETHER_ERROR_INVALID_STATE,"nested callback followed by epoch/node invalidation aborts");
        need(s.calls==1&&output==previous&&complete_commands(output)==digest,"nested epoch abort preserves previous complete commands");
        if(invalidation==3)ok(aether_node_append_child(parent,f.target),"restore detached input ownership for cleanup");
        aether_display_list_destroy(output);aether_display_list_builder_destroy(s.builder);
    }
}

struct DestroyDocument {
    aether_doc_t* doc;
    int calls = 0;
};
static aether_text_metrics_t destroy_document_caret(const char*, const aether_computed_style_t*, float, void* data) {
    auto& state = *static_cast<DestroyDocument*>(data);
    ++state.calls;
    aether_doc_destroy(*state.doc); *state.doc = nullptr;
    return {30, 24, 18};
}
static aether_bool_t destroy_document_image(const char* source, aether_sizef* size, void* data) {
    auto& state = *static_cast<DestroyDocument*>(data);
    ++state.calls;
    need(source && std::strcmp(source, "asset://snapshot") == 0, "unbound map image source");
    aether_doc_destroy(*state.doc); *state.doc = nullptr;
    need(std::strcmp(source, "asset://snapshot") == 0, "image leaf survives destroyed DOM");
    *size = {20, 30}; return AETHER_TRUE;
}
static void no_map_document_destruction(bool empty_wrapper) {
    aether_doc_t doc{}; aether_layout_engine_t engine{}; aether_layout_result_t layout{};
    aether_style_map_t map{}; aether_display_list_builder_t builder{}; aether_display_list_t output{};
    ok(aether_doc_create(&doc), "unbound document");
    ok(aether_doc_parse_html(doc, "<html><body><input id='target' value='text'></body></html>"), "unbound input");
    auto* target = aether_doc_get_element_by_id(doc, "target");
    if (empty_wrapper) ok(aether_css_resolve(doc, nullptr, 300, 200, &map), "public empty wrapper");
    ok(aether_layout_create(nullptr, nullptr, &engine), "unbound engine");
    ok(aether_layout_layout(engine, doc, map, 300, 200, nullptr, nullptr, &layout), "unbound layout");
    ok(aether_display_list_builder_create(nullptr, nullptr, &builder), "unbound prior painter");
    ok(aether_display_list_build_with_styles(builder, layout, map, nullptr, &output), "unbound prior paint");
    const auto previous = output; const auto digest = complete_commands(output);
    aether_display_list_builder_destroy(builder);
    DestroyDocument state{&doc};
    ok(aether_display_list_builder_create(destroy_document_caret, &state, &builder), "unbound destructive painter");
    need(aether_display_list_build_with_styles(builder, layout, map, target, &output) == AETHER_ERROR_INVALID_STATE,
        "no-map document destruction aborts before raw paint read");
    need(state.calls == 1 && !doc, "unbound destructive caret invoked exactly once");
    need(output == previous && complete_commands(output) == digest, "unbound destruction preserves prior output");
    need(aether_contextual_transition_finalize_layout(layout, nullptr) == AETHER_ERROR_INVALID_STATE,
        "empty-base finalize rejects destroyed document before node traversal");
    need(aether_display_list_build_with_styles(builder, layout, map, nullptr, &output) == AETHER_ERROR_INVALID_STATE,
        "empty-base paint rejects destroyed document before traversal");
    aether_display_list_destroy(output); aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout); aether_layout_destroy(engine); aether_style_map_destroy(map);
}
static void partial_map_document_destruction(bool external) {
    Fixture f(false, false);
    aether_style_map_destroy(f.map); f.map = nullptr;
    ok(aether_css_resolve(f.doc, nullptr, 800, 600, &f.map), "empty map for partial update");
    const aether_node_t nodes[]{aether_doc_get_document_element(f.doc), aether_doc_get_body(f.doc), f.target};
    ok(aether_style_runtime_update_resolved_style_map(f.runtime, f.map, nodes, 3), "populate unbound partial map");
    f.build();
    aether_display_list_builder_t builder{}; aether_display_list_t output{};
    ok(aether_display_list_builder_create(nullptr, nullptr, &builder), "partial prior painter");
    ok(aether_display_list_build_with_styles(builder, f.layout, external ? f.map : nullptr, nullptr, &output), "partial prior paint");
    const auto previous = output; const auto digest = complete_commands(output);
    DestroyDocument state{&f.doc};
    aether_display_list_builder_set_image_dimensions_callback(builder, destroy_document_image, &state);
    need(aether_display_list_build_with_styles(builder, f.layout, external ? f.map : nullptr, nullptr, &output)
        == AETHER_ERROR_INVALID_STATE, "partial unbound document destruction aborts paint");
    need(state.calls == 1 && !f.doc, "partial image destroys document once");
    need(output == previous && complete_commands(output) == digest, "partial failed paint preserves prior output");
    need(aether_contextual_transition_finalize_layout(f.layout, nullptr) == AETHER_ERROR_INVALID_STATE,
        "partial finalize rejects destroyed document before node traversal");
    aether_display_list_destroy(output); aether_display_list_builder_destroy(builder);
}
int main() {
    for (bool contextual : {false, true}) for (bool external : {false, true}) {
        for (bool text_gradient : {false, true})
            need(commands_equal(caret_case(false, contextual, external, text_gradient), caret_case(true, contextual, external, text_gradient)),
                "caret callback isolation command equivalence for every payload family");
        need(commands_equal(image_case(false, contextual, external), image_case(true, contextual, external)), "image pin command equivalence");
    }
    aether_doc_t empty_doc{}; aether_layout_engine_t empty_engine{}; aether_layout_result_t empty_layout{};
    ok(aether_doc_create(&empty_doc), "empty-map doc");
    ok(aether_doc_parse_html(empty_doc, "<html><body><div>empty map text</div></body></html>"), "empty-map markup");
    ok(aether_layout_create(nullptr, nullptr, &empty_engine), "empty-map engine");
    ok(aether_layout_layout(empty_engine, empty_doc, nullptr, 300, 200, nullptr, nullptr, &empty_layout), "layout without map");
    ok(aether_contextual_transition_finalize_layout(empty_layout, nullptr), "valid empty-base finalize is not NULL_ARG");
    aether_layout_result_destroy(empty_layout); aether_layout_destroy(empty_engine); aether_doc_destroy(empty_doc);
    nested_epoch_abort_matrix();mutation_failure(false); mutation_failure(true);
    no_map_document_destruction(false); no_map_document_destruction(true);
    partial_map_document_destruction(false); partial_map_document_destruction(true);
    std::puts("frozen paint callback pins: PASS");
}
