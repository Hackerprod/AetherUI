#include "css_types.h"
#include <aether/aether_contextual_style.h>
#include "style_handle_export.h"
#include "style_c_export.h"
#include "../dom/dom_types.h"
#include "computed_style_internal.h"
#include "../abi/aether_abi.h"

AETHER_API aether_status_t aether_css_resolve(
    aether_doc_t doc, aether_stylesheet_t author_sheet,
    float viewport_width, float viewport_height,
    aether_style_map_t* out_map) {
    (void)doc; (void)author_sheet; (void)viewport_width; (void)viewport_height;
    if (!out_map) return AETHER_ERROR_NULL_ARG;
    *out_map = new aether_style_map_s();
    return AETHER_OK;
}

AETHER_API void aether_style_map_destroy(aether_style_map_t map) {
    delete map;
}

namespace aether::style {
aether_status_t export_style_handle(const MapVersion& binding, const FrozenNodeRef& record,
    aether_computed_style_handle_t* out) noexcept {
    if (!out) return AETHER_ERROR_NULL_ARG;
    if (!record || !binding.backing()) return AETHER_ERROR_INVALID_STATE;
    const auto member = binding.backing()->find_owner(record->node());
    if (member != record || member.owner_before(record) || record.owner_before(member))
        return AETHER_ERROR_INVALID_STATE;
    try {
        auto result = std::make_unique<aether_computed_style_handle_s>();
        const auto status = export_owned_flat(record->style_view(), &result->style);
        if (status != AETHER_OK) return status;
        // These owner copies cannot allocate or throw after flat allocation.
        result->record = record; result->measures = record->measures();
        result->revision = binding.revision();
        result->document_identity = binding.document_identity();
        result->document_lifetime = binding.document_lifetime();
        result->node_lifetime = record->node_lifetime(); result->node_identity = record->node_identity();
        *out = result.release(); return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
}

AETHER_API aether_computed_style_handle_t aether_style_map_get(aether_style_map_t map, aether_node_t element) {
    if (!map || !element) return nullptr;
    const auto version = aether::style::pin_style_map(map);
    if (!version) return nullptr;
    const auto record = version->backing()->find_owner(element);
    if (!record) return nullptr;
    aether_computed_style_handle_t result = nullptr;
    return aether::style::export_style_handle(*version, record, &result) == AETHER_OK ? result : nullptr;
}

AETHER_API aether_status_t aether_computed_style_copy(aether_computed_style_handle_t handle, aether_computed_style_t* out) {
    if (!handle || !out) return AETHER_ERROR_NULL_ARG;
    return computed_style_deep_copy(handle->style, out);
}

AETHER_API void aether_computed_style_handle_destroy(aether_computed_style_handle_t handle) {
    if (!handle) return;
    aether_computed_style_free(&handle->style);
    delete handle;
}

AETHER_API aether_status_t aether_computed_style_handle_clone(
    aether_computed_style_handle_t source, aether_computed_style_handle_t* out) {
    if (out) *out = nullptr;
    if (!source || !out) return AETHER_ERROR_NULL_ARG;
    try {
        auto result = std::make_unique<aether_computed_style_handle_s>();
        if (computed_style_deep_copy(source->style, &result->style) != AETHER_OK) return AETHER_ERROR_OUT_OF_MEMORY;
        result->measures = source->measures; result->record = source->record; result->revision = source->revision;
        result->document_identity = source->document_identity; result->node_identity = source->node_identity;
        result->node_lifetime = source->node_lifetime; result->document_lifetime = source->document_lifetime;
        *out = result.release(); return AETHER_OK;
    } catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API aether_status_t aether_computed_style_contextual_metrics(
    aether_computed_style_handle_t style, float* out_line_height,
    aether_css_length* out_row_gap, aether_css_length* out_column_gap,
    uint64_t* out_generation, uint64_t* out_document_identity, uint64_t* out_node_identity) {
    if (!style) return AETHER_ERROR_NULL_ARG;
    if (!style->measures) return AETHER_ERROR_INVALID_STATE;
    const auto& values = style->measures->scalars();
    if (out_line_height) *out_line_height = values.line_height_kind == aether::style::FrozenLineHeightKind::absolute_css_px ?
        values.line_height_value : values.self_font_size * (values.line_height_kind == aether::style::FrozenLineHeightKind::normal ? 1.2f : values.line_height_value);
    if (out_row_gap) *out_row_gap = values.row_gap;
    if (out_column_gap) *out_column_gap = values.column_gap;
    if (out_generation) *out_generation = style->measures->document().values().generation;
    if (out_document_identity) *out_document_identity = style->document_identity;
    if (out_node_identity) *out_node_identity = style->node_identity;
    return AETHER_OK;
}
