#include <aether/aether.h>
#include "computed_style_internal.h"

#include <cstdlib>
#include <cstring>

AETHER_API aether_computed_style_t aether_computed_style_default(void) {
    aether_computed_style_t s{};
    s.display = AETHER_DISPLAY_INLINE;
    s.flex_direction = AETHER_FLEX_DIRECTION_ROW;
    s.justify_content = AETHER_JUSTIFY_START;
    s.align_items = AETHER_ALIGN_STRETCH;
    s.align_self = AETHER_ALIGN_SELF_AUTO;
    s.flex_shrink = 1.0f;
    s.flex_basis = { AETHER_FLEX_BASIS_AUTO, aether_css_length_auto() };
    s.position = AETHER_POSITION_STATIC;
    s.inset = { aether_css_length_auto(), aether_css_length_auto(),
                aether_css_length_auto(), aether_css_length_auto() };
    s.margin = aether_css_edges_zero();
    s.padding = aether_css_edges_zero();
    s.border_width = aether_css_edges_zero();
    s.box_sizing = AETHER_BOX_SIZING_CONTENT_BOX;
    s.color = {0, 0, 0, 1};
    // CSS Color: each border color initially resolves to currentColor.
    // The style runtime preserves that symbolic relationship until its
    // post-cascade color-resolution phase; direct default callers receive the
    // equivalent concrete initial color.
    s.border_color = s.color;
    s.border_colors = {s.border_color, s.border_color, s.border_color, s.border_color};
    s.border_styles = {AETHER_BORDER_STYLE_NONE, AETHER_BORDER_STYLE_NONE,
                       AETHER_BORDER_STYLE_NONE, AETHER_BORDER_STYLE_NONE};
    s.opacity = 1.0f;
    s.transform_origin = {aether_css_length{50.0f, AETHER_CSS_UNIT_PERCENT},
                          aether_css_length{50.0f, AETHER_CSS_UNIT_PERCENT}};
    s.font_size = 16.0f;
    s.font_weight = AETHER_FONT_WEIGHT_NORMAL;
    s.line_height = 1.25f;
    s.white_space = AETHER_WHITE_SPACE_NORMAL;
    s.overflow_x = AETHER_OVERFLOW_VISIBLE;
    s.overflow_y = AETHER_OVERFLOW_VISIBLE;
    s.pointer_events = AETHER_POINTER_EVENTS_AUTO;
    s.visible = AETHER_TRUE;
    s.z_index = 0;
    s.z_index_kind = AETHER_Z_INDEX_AUTO;
    s.transition_properties = 0;
    s.transition_duration = 0.0f;
    s.transition_delay = 0.0f;
    s.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    const auto zero_radius = aether_corner_radius{aether_css_length_zero(), aether_css_length_zero()};
    s.border_radii = {zero_radius, zero_radius, zero_radius, zero_radius};
    strncpy(s.font_family, "Segoe UI", sizeof(s.font_family) - 1);
    return s;
}

aether_status_t computed_style_inherit_checked(const aether_computed_style_t* parent,
                                                aether_computed_style_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
    aether_computed_style_t s = aether_computed_style_default();
    if (parent) {
        s.color = parent->color;
        strncpy(s.font_family, parent->font_family, sizeof(s.font_family) - 1);
        s.font_size = parent->font_size;
        s.font_weight = parent->font_weight;
        s.line_height = parent->line_height;
        s.text_align = parent->text_align;
        s.text_transform = parent->text_transform;
        s.white_space = parent->white_space;
        s.cursor = parent->cursor;
        s.pointer_events = parent->pointer_events;
        s.visible = parent->visible;
        // text-shadow is inherited. Keep an owned copy so parent and child
        // style handles never alias list storage.
        if (!aether_shadow_list_copy(&parent->text_shadows, &s.text_shadows)) {
            aether_computed_style_free(&s);
            *out = {};
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
    }
    *out = s;
    return AETHER_OK;
}

AETHER_API aether_computed_style_t aether_computed_style_inherit_from(const aether_computed_style_t* parent) {
    aether_computed_style_t result{};
    if (computed_style_inherit_checked(parent, &result) != AETHER_OK)
        return aether_computed_style_default(); // Legacy ABI has no status channel.
    return result;
}

AETHER_API void aether_computed_style_free(aether_computed_style_t* s) {
    if (s) {
        aether_background_layer_list_free(&s->background_layers);
        aether_backdrop_filter_list_free(&s->backdrop_filters);
        aether_transform_list_free(&s->transforms);
        aether_linear_gradient_free(&s->text_gradient);
        aether_shadow_list_free(&s->text_shadows);
        aether_shadow_list_free(&s->box_shadows);
    }
}

namespace {

bool copy_gradient(const aether_linear_gradient& source, aether_linear_gradient& destination) {
    destination = source;
    destination.stops = nullptr;
    if (source.stop_count < 0 || (source.stop_count > 0 && !source.stops)) return false;
    if (source.stop_count == 0) return true;
    const size_t bytes = sizeof(aether_gradient_stop) * static_cast<size_t>(source.stop_count);
    destination.stops = static_cast<aether_gradient_stop*>(std::malloc(bytes));
    if (!destination.stops) return false;
    std::memcpy(destination.stops, source.stops, bytes);
    return true;
}

} // namespace

aether_status_t computed_style_deep_copy(const aether_computed_style_t& src,
                                         aether_computed_style_t* dst) {
    if (!dst) return AETHER_ERROR_NULL_ARG;
    aether_computed_style_t copy = src;
    copy.background_layers.items = nullptr;
    copy.background_layers.count = 0;
    copy.backdrop_filters.items = nullptr;
    copy.backdrop_filters.count = 0;
    copy.transforms.items = nullptr;
    copy.transforms.count = 0;
    copy.text_gradient.stops = nullptr;
    copy.text_shadows.items = nullptr;
    copy.text_shadows.count = 0;
    copy.box_shadows.items = nullptr;
    copy.box_shadows.count = 0;
    if (aether_background_layer_list_copy(&src.background_layers, &copy.background_layers) != AETHER_TRUE ||
        aether_backdrop_filter_list_copy(&src.backdrop_filters, &copy.backdrop_filters) != AETHER_TRUE ||
        aether_transform_list_copy(&src.transforms, &copy.transforms) != AETHER_TRUE ||
        !copy_gradient(src.text_gradient, copy.text_gradient) ||
        aether_shadow_list_copy(&src.text_shadows, &copy.text_shadows) != AETHER_TRUE ||
        aether_shadow_list_copy(&src.box_shadows, &copy.box_shadows) != AETHER_TRUE) {
        aether_computed_style_free(&copy);
        return AETHER_ERROR_OUT_OF_MEMORY;
    }
    *dst = copy;
    return AETHER_OK;
}


#include "frozen_style.h"
#include <limits>
#include <stdexcept>
#include <new>
namespace {
template<class T> T* copy_frozen_array(aether::style::ReadOnlyArrayView<T> source) {
    if (source.size() > static_cast<size_t>(INT32_MAX) || source.size() > SIZE_MAX / sizeof(T)) throw std::length_error("builder count");
    if (source.empty()) return nullptr;
    auto* p = static_cast<T*>(std::malloc(source.size() * sizeof(T)));
    if (!p) throw std::bad_alloc();
    for (size_t i=0;i<source.size();++i) p[i]=source[i];
    return p;
}
aether_linear_gradient copy_frozen_gradient(aether::style::GradientReadView source) {
    aether_linear_gradient result{};
    result.direction_x=source.direction_x; result.direction_y=source.direction_y;
    result.stops=copy_frozen_array(source.stops); result.stop_count=static_cast<int32_t>(source.stops.size());
    return result;
}
struct builder_copy_guard {
    aether_computed_style_t value{};
    ~builder_copy_guard() { aether_computed_style_free(&value); }
    void release(aether_computed_style_t* out) noexcept { *out=value; value={}; }
};
}
namespace aether::style {
aether_status_t copy_builder_backgrounds(ComputedStyleReadView source, aether_background_layer_list* out) noexcept {
    if (!out) return AETHER_ERROR_NULL_ARG;
    builder_copy_guard g;
    try {
        const auto layers=source.background_layers();
        if (layers.size()>static_cast<size_t>(INT32_MAX) || layers.size()>SIZE_MAX/sizeof(aether_background_layer)) throw std::length_error("builder layers");
        if (!layers.empty()) {
            auto* p=static_cast<aether_background_layer*>(std::calloc(layers.size(),sizeof(aether_background_layer)));
            if (!p) throw std::bad_alloc();
            g.value.background_layers={p,static_cast<int32_t>(layers.size())};
            for(size_t i=0;i<layers.size();++i) {
                const auto view=layers[i].view(); auto& dst=p[i];
                view.scalars.copy_to(dst); dst.gradient=copy_frozen_gradient(view.gradient);
                if(view.image_source.present()) {
                    if(view.image_source.size()==SIZE_MAX) throw std::length_error("builder string");
                    dst.image_source=static_cast<char*>(std::malloc(view.image_source.size()+1));
                    if(!dst.image_source) throw std::bad_alloc();
                    for(size_t j=0;j<view.image_source.size();++j) dst.image_source[j]=view.image_source[j];
                    dst.image_source[view.image_source.size()]='\0';
                }
            }
        }
        *out=g.value.background_layers; g.value.background_layers={}; return AETHER_OK;
    } catch(const std::bad_alloc&) {return AETHER_ERROR_OUT_OF_MEMORY;} catch(...) {return AETHER_ERROR_INVALID_STATE;}
}
aether_status_t copy_builder_shadows(ReadOnlyArrayView<aether_shadow> src,aether_shadow_list* out) noexcept {
    if(!out)return AETHER_ERROR_NULL_ARG;
    try { auto* p=copy_frozen_array(src); *out={p,static_cast<int32_t>(src.size())}; return AETHER_OK; }
    catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
aether_status_t copy_builder_transforms(ReadOnlyArrayView<aether_transform_operation> src,aether_transform_list* out) noexcept {
    if(!out)return AETHER_ERROR_NULL_ARG;
    try { auto* p=copy_frozen_array(src); *out={p,static_cast<int32_t>(src.size())}; return AETHER_OK; }
    catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
}
aether_status_t computed_style_deep_copy(aether::style::ComputedStyleReadView src,aether_computed_style_t* out) noexcept {
    if(!out)return AETHER_ERROR_NULL_ARG;
    builder_copy_guard g;
    try {
        src.scalars().copy_to(g.value);
        auto status=aether::style::copy_builder_backgrounds(src,&g.value.background_layers);
        if(status!=AETHER_OK)return status;
        g.value.backdrop_filters.items=copy_frozen_array(src.backdrop_filters()); g.value.backdrop_filters.count=static_cast<int32_t>(src.backdrop_filters().size());
        g.value.transforms.items=copy_frozen_array(src.transforms()); g.value.transforms.count=static_cast<int32_t>(src.transforms().size());
        g.value.text_gradient=copy_frozen_gradient(src.text_gradient());
        g.value.text_shadows.items=copy_frozen_array(src.text_shadows()); g.value.text_shadows.count=static_cast<int32_t>(src.text_shadows().size());
        g.value.box_shadows.items=copy_frozen_array(src.box_shadows()); g.value.box_shadows.count=static_cast<int32_t>(src.box_shadows().size());
        g.release(out); return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
aether_status_t computed_style_inherit_checked(const aether::style::ComputedStyleReadView* parent,aether_computed_style_t* out) noexcept {
    if(!out)return AETHER_ERROR_NULL_ARG;
    builder_copy_guard g; g.value=aether_computed_style_default();
    if(parent) {
        const auto& p=parent->scalars(); auto& s=g.value;
        s.color=p.color; std::strncpy(s.font_family,p.font_family.data(),sizeof(s.font_family)-1);
        s.font_size=p.font_size; s.font_weight=p.font_weight; s.line_height=p.line_height;
        s.text_align=p.text_align; s.text_transform=p.text_transform; s.white_space=p.white_space;
        s.cursor=p.cursor; s.pointer_events=p.pointer_events; s.visible=p.visible;
        const auto status=aether::style::copy_builder_shadows(parent->text_shadows(),&s.text_shadows);
        if(status!=AETHER_OK)return status;
    }
    g.release(out); return AETHER_OK;
}
