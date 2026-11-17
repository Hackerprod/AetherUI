#include <aether/aether.h>
#include "../rendering/native_frame.h"
#include "../rendering/preparation/native_command_source.h"
#include "../rendering/software/software_renderer.h"
#include <cstring>
#include "../rendering/html_ltr/session.h"
#include "../rendering/html_ltr/admission.h"
#include "../rendering/html_ltr/raster_coordinate.h"
#include <memory>
#include <new>
#ifdef AETHER_SURFACE_TESTING
#include "surface_test_hooks.h"
#define SURFACE_STAGE(name) aether::surface_test::stage(aether::surface_test::Stage::name)
#else
#define SURFACE_STAGE(name) ((void)0)
#endif
namespace r = aether::rendering;
namespace p = aether::rendering::preparation;
struct aether_surface_s {
#ifdef AETHER_SURFACE_TESTING
    r::FrameStore store{[] { SURFACE_STAGE(allocation); }};
#else
    r::FrameStore store;
#endif
};
namespace {
aether_status_t frame_status(r::FrameStatus s) noexcept {
    switch (s) {
    case r::FrameStatus::ok: return AETHER_OK;
    case r::FrameStatus::invalid_viewport: return AETHER_ERROR_INVALID_ARGUMENT;
    case r::FrameStatus::suspended: return AETHER_ERROR_SUSPENDED;
    case r::FrameStatus::allocation_failed: return AETHER_ERROR_OUT_OF_MEMORY;
    case r::FrameStatus::wrong_thread: return AETHER_ERROR_WRONG_THREAD;
    case r::FrameStatus::exhausted: return AETHER_ERROR_EXHAUSTED;
    default: return AETHER_ERROR_INTERNAL;
    }
}
aether_status_t preparation_status(p::ErrorCode c) noexcept {
    using E = p::ErrorCode;
    switch (c) {
    case E::OUT_OF_MEMORY: return AETHER_ERROR_OUT_OF_MEMORY;
    case E::COMMAND_COUNT: case E::LIMIT_EXCEEDED: return AETHER_ERROR_LIMIT_EXCEEDED;
    case E::UNKNOWN_TAG: case E::UNSUPPORTED_TAG: case E::UnsupportedValidationDomain: return AETHER_ERROR_UNSUPPORTED;
    case E::INVALID_LIST: case E::INVALID_PAYLOAD: case E::INVALID_UTF8:
    case E::STACK_UNDERFLOW: case E::STACK_MISMATCH: case E::CLIP_TRANSFORM_CROSSING:
    case E::UNCLOSED_SCOPE: case E::NUMERIC_RANGE: return AETHER_ERROR_INVALID_ARGUMENT;
    default: return AETHER_ERROR_INTERNAL;
    }
}
aether_status_t rendering_status(r::software::Result v) noexcept {
    using S = r::software::Status;
    switch (v.status) {
    case S::ok: return AETHER_OK;
    case S::unsupported: case S::missing_mask: case S::invalid_mask: return AETHER_ERROR_UNSUPPORTED;
    case S::limit_exceeded: return AETHER_ERROR_LIMIT_EXCEEDED;
    case S::allocation_failed: return AETHER_ERROR_OUT_OF_MEMORY;
    case S::numeric_error: return AETHER_ERROR_INVALID_ARGUMENT;
    case S::frame_failed: return frame_status(v.frame_status);
    default: return AETHER_ERROR_INTERNAL;
    }
}
}
#define SURFACE_CATCH catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; } catch (...) { return AETHER_ERROR_INTERNAL; }
extern "C" {
AETHER_API size_t aether_html_ltr_options_size(void) { return sizeof(aether_html_ltr_options); }
AETHER_API uint32_t aether_html_ltr_profile_limitations(uint32_t profile) {
    return profile == AETHER_HTML_LTR_PROFILE_1 ? 3u :
        profile == AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2 ? 15u : UINT32_MAX;
}
AETHER_API aether_status_t aether_surface_render_html_ltr(aether_surface_t surface,
        const char* html, size_t length, const aether_html_ltr_options* options) {
    try {
        if (!surface) return AETHER_ERROR_NULL_ARG;
        r::Viewport viewport;
        auto status = frame_status(surface->store.current_viewport(viewport));
        if (status != AETHER_OK) return status;
        if (!html || !options) return AETHER_ERROR_NULL_ARG;
        if (options->struct_size != sizeof(*options) || (options->profile != 1 && options->profile != 2) ||
            !options->regular_font || !options->regular_font_size ||
            (bool(options->bold_font) != bool(options->bold_font_size)))
            return AETHER_ERROR_INVALID_ARGUMENT;
        for (auto reserved : options->reserved) if (reserved) return AETHER_ERROR_INVALID_ARGUMENT;
        if (!viewport.width || !viewport.height) return AETHER_ERROR_SUSPENDED;
        if (!length) return AETHER_ERROR_INVALID_ARGUMENT;
        constexpr size_t font_limit = 16u * 1024u * 1024u;
        if (viewport.width > 2048 || viewport.height > 2048 || viewport.dpi != 1 ||
            length > 65536 || options->regular_font_size > font_limit ||
            options->bold_font_size > font_limit - options->regular_font_size)
            return AETHER_ERROR_LIMIT_EXCEEDED;
        HTML_LTR_STAGE(html_copy);
        std::string owned_html(html, length);
        HTML_LTR_STAGE(font_copy);
        std::vector<uint8_t> regular(options->regular_font, options->regular_font + options->regular_font_size);
        std::vector<uint8_t> bold;
        if (options->bold_font_size) bold.assign(options->bold_font, options->bold_font + options->bold_font_size);
        r::html_ltr::HtmlLtrSession session(std::move(owned_html), std::move(regular), std::move(bold), viewport, options->profile);
        const auto& prepared = session.prepare();
        HTML_LTR_STAGE(candidate);
        const auto rendered = r::software::render_a8(surface->store, prepared, session.masks(), nullptr);
        if (rendered.status == r::software::Status::missing_mask || rendered.status == r::software::Status::invalid_mask)
            return AETHER_ERROR_INTERNAL;
        return rendering_status(rendered);
    } catch (const r::html_ltr::Error& e) { return e.status; }
      catch (const r::html_ltr::BoundaryError& e) { return e.status; }
      catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INTERNAL; }
}
AETHER_API size_t aether_surface_frame_info_size(void) { return sizeof(aether_surface_frame_info); }
AETHER_API aether_status_t aether_surface_create(int32_t width, int32_t height, aether_surface_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
    *out = nullptr;
    try {
        // Validate before allocating; this is the same physical limit as FrameStore.
        if (width < 0 || height < 0 || width > 8192 || height > 8192 ||
            uint64_t(width) * uint64_t(height) * 4 > 64ULL * 1024 * 1024)
            return AETHER_ERROR_INVALID_ARGUMENT;
        SURFACE_STAGE(create);
        auto value = std::make_unique<aether_surface_s>();
        auto status = frame_status(value->store.resize({width, height, 1}));
        if (status != AETHER_OK) return status;
        *out = value.release();
        return AETHER_OK;
    } SURFACE_CATCH
}
AETHER_API aether_status_t aether_surface_destroy(aether_surface_t surface) {
    try {
        if (!surface) return AETHER_OK;
        r::Viewport viewport;
        auto status = frame_status(surface->store.current_viewport(viewport));
        if (status != AETHER_OK) return status;
        delete surface;
        return AETHER_OK;
    } SURFACE_CATCH
}
AETHER_API aether_status_t aether_surface_resize(aether_surface_t surface, int32_t width, int32_t height) {
    try {
        if (!surface) return AETHER_ERROR_NULL_ARG;
        return frame_status(surface->store.resize({width, height, 1}));
    } SURFACE_CATCH
}
AETHER_API aether_status_t aether_surface_render(aether_surface_t surface, aether_display_list_t list) {
    try {
        if (!surface) return AETHER_ERROR_NULL_ARG;
        r::Viewport viewport;
        auto status = frame_status(surface->store.current_viewport(viewport));
        if (status != AETHER_OK) return status;
        if (!list) return AETHER_ERROR_NULL_ARG;
        if (!viewport.width || !viewport.height) return AETHER_ERROR_SUSPENDED;
        SURFACE_STAGE(unexpected);
        SURFACE_STAGE(snapshot);
        auto captured = p::snapshot_native_commands(list, nullptr);
        if (auto* error = std::get_if<p::Error>(&captured)) return preparation_status(error->code);
        SURFACE_STAGE(prepare);
        auto prepared = p::prepare(std::get<p::Snapshot>(captured), nullptr);
        if (auto* error = std::get_if<p::Error>(&prepared)) return preparation_status(error->code);
        SURFACE_STAGE(candidate);
        return rendering_status(r::software::render(surface->store, std::get<p::PreparedFrame>(prepared), nullptr));
    } SURFACE_CATCH
}
AETHER_API aether_status_t aether_surface_copy_frame(aether_surface_t surface,
        aether_surface_frame_info* info, uint8_t* destination, size_t capacity) {
    try {
        if (!surface) return AETHER_ERROR_NULL_ARG;
        r::Viewport viewport;
        auto status = frame_status(surface->store.current_viewport(viewport));
        if (status != AETHER_OK) return status;
        if (!info) return AETHER_ERROR_NULL_ARG;
        if (info->struct_size != sizeof(*info)) return AETHER_ERROR_INVALID_ARGUMENT;
        if (!destination && capacity) return AETHER_ERROR_NULL_ARG;
        SURFACE_STAGE(copy);
        std::optional<r::Frame> frame;
        status = frame_status(surface->store.copy_confirmed(frame));
        if (status != AETHER_OK) return status;
        if (!frame) return AETHER_ERROR_NO_FRAME;
        const auto width = frame->viewport.width, height = frame->viewport.height;
        const uint64_t stride = frame->stride, bytes = frame->pixels.size(), sequence = frame->sequence;
        info->struct_size = sizeof(*info);
        info->width = width; info->height = height;
        info->format = AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED;
        info->stride = stride; info->byte_count = bytes; info->sequence = sequence;
        if (destination && capacity < bytes) return AETHER_ERROR_BUFFER_TOO_SMALL;
        if (destination) std::memcpy(destination, frame->pixels.data(), frame->pixels.size());
        return AETHER_OK;
    } SURFACE_CATCH
}
}
#ifdef AETHER_SURFACE_TESTING
namespace aether::rendering {
struct FrameStoreTestAccess {
    static void set(FrameStore& store, bool sequence, uint64_t value) {
        if (sequence) store.sequence_ = value;
        else store.issued_ = value;
    }
};
}
namespace aether::surface_test {
void set_counter(aether_surface_t surface, bool sequence, uint64_t value) {
    r::FrameStoreTestAccess::set(surface->store, sequence, value);
}
}
#endif
