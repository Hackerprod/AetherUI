#include "snapshot_test_support.h"
#include <aether/aether_contextual_transitions.h>
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

struct State {
    Fixture* fixture;
    aether_display_list_builder_t builder{};
    bool drop = false, mutate = false, edit_dom = false, text_gradient = false;
    int calls = 0;
};
static void nested_then_drop(State& s) {
    auto& f = *s.fixture;
    aether_display_list_t sentinel{};
    need(aether_display_list_build_with_styles(s.builder, f.layout, f.map, f.target, &sentinel)
        == AETHER_ERROR_INVALID_STATE, "same-builder reentry rejected");
    need(!sentinel, "same-builder reentry preserves output");
    aether_display_list_builder_t other{};
    ok(aether_display_list_builder_create(nullptr, nullptr, &other), "nested painter");
    ok(aether_display_list_build_with_styles(other, f.layout, f.map, nullptr, &sentinel), "other-builder reentry independent");
    need(aether_display_list_get_count(sentinel) > 0, "nested commands present");
    aether_display_list_destroy(sentinel); aether_display_list_builder_destroy(other);
    aether_style_map_destroy(f.map); f.map = nullptr;
    aether_layout_result_destroy(f.layout); f.layout = nullptr;
}
static aether_text_metrics_t caret(const char*, const aether_computed_style_t* style, float, void* data) {
    auto& s = *static_cast<State*>(data); ++s.calls;
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
        // Nested descriptors may legally be rebound through a mutable layer.
        // Do not free or retain the callback's buffers. Original ledger owns them.
        for (int i = 0; i < style->background_layers.count; ++i) {
            auto& layer = style->background_layers.items[i];
            layer.image_source = nullptr;
            layer.gradient.stops = nullptr;
            layer.gradient.stop_count = 999;
        }
    }
    if (s.drop && s.calls == 1) nested_then_drop(s);
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
static uint64_t caret_case(bool destructive, bool contextual, bool external, bool text_gradient) {
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
    State s{&f}; s.drop = destructive; s.mutate = destructive; s.text_gradient = text_gradient;
    ok(aether_display_list_builder_create(caret, &s, &s.builder), "caret builder");
    if (contextual) aether_display_list_builder_set_contextual_text_measurer(s.builder, contextual_caret, &s);
    aether_display_list_t output{};
    ok(aether_display_list_build_with_styles(s.builder, f.layout, external ? f.map : nullptr, f.target, &output), "caret pin paint");
    need(s.calls > 0, "caret callback executed");
    if (destructive) need(!f.map && !f.layout, "external owners released in callback");
    auto h = revision_handle(f.revision, f.target); Flat retained(h); aether_computed_style_handle_destroy(h);
    need(style_equal(original.value, retained.value), "callback payload/rebind did not mutate revision");
    const auto result = complete_commands(output);
    aether_display_list_destroy(output); aether_display_list_builder_destroy(s.builder);
    return result;
}
static uint64_t image_case(bool destructive, bool contextual, bool external) {
    Fixture f(false, contextual); f.build();
    State s{&f}; s.drop = destructive;
    ok(aether_display_list_builder_create(nullptr, nullptr, &s.builder), "image builder");
    aether_display_list_builder_set_image_dimensions_callback(s.builder, image, &s);
    aether_display_list_t output{};
    ok(aether_display_list_build_with_styles(s.builder, f.layout, external ? f.map : nullptr, nullptr, &output), "image pin paint");
    need(s.calls > 0, "image callback executed");
    if (destructive) need(!f.map && !f.layout, "image external owners released");
    const auto result = complete_commands(output);
    aether_display_list_destroy(output); aether_display_list_builder_destroy(s.builder);
    return result;
}
static void mutation_failure(bool image_callback) {
    Fixture f(!image_callback); f.build(); State s{&f};
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
            need(caret_case(false, contextual, external, text_gradient) == caret_case(true, contextual, external, text_gradient),
                "caret callback isolation command equivalence for every payload family");
        need(image_case(false, contextual, external) == image_case(true, contextual, external), "image pin command equivalence");
    }
    aether_doc_t empty_doc{}; aether_layout_engine_t empty_engine{}; aether_layout_result_t empty_layout{};
    ok(aether_doc_create(&empty_doc), "empty-map doc");
    ok(aether_doc_parse_html(empty_doc, "<html><body><div>empty map text</div></body></html>"), "empty-map markup");
    ok(aether_layout_create(nullptr, nullptr, &empty_engine), "empty-map engine");
    ok(aether_layout_layout(empty_engine, empty_doc, nullptr, 300, 200, nullptr, nullptr, &empty_layout), "layout without map");
    ok(aether_contextual_transition_finalize_layout(empty_layout, nullptr), "valid empty-base finalize is not NULL_ARG");
    aether_layout_result_destroy(empty_layout); aether_layout_destroy(empty_engine); aether_doc_destroy(empty_doc);
    mutation_failure(false); mutation_failure(true);
    no_map_document_destruction(false); no_map_document_destruction(true);
    partial_map_document_destruction(false); partial_map_document_destruction(true);
    std::puts("frozen paint callback pins: PASS");
}
