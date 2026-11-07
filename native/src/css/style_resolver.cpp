#include "css_types.h"
#include <aether/aether_contextual_style.h>
#include "measure_state.h"
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

AETHER_API aether_computed_style_handle_t aether_style_map_get(aether_style_map_t map, aether_node_t element) {
    if (!map || !element) return nullptr;
    auto found = map->styles.find(element); if (found == map->styles.end()) return nullptr;
    try {
        auto result = std::make_unique<aether_computed_style_handle_s>();
        if (computed_style_deep_copy(found->second, &result->style) != AETHER_OK) return nullptr;
        auto measures = map->measures.find(element);
        if (measures != map->measures.end()) result->measures = measures->second;
        result->revision = map->revision; result->document_identity = map->document_identity;
        result->document_lifetime = map->document_lifetime;
        auto token = map->node_lifetimes.find(element);
        if (token != map->node_lifetimes.end()) {
            result->node_lifetime = token->second;
            result->node_identity = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(token->second.get()));
        }
        return result.release();
    } catch (...) { return nullptr; }
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
        result->measures = source->measures; result->revision = source->revision;
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
    if (!style->measures || !style->measures->document) return AETHER_ERROR_INVALID_STATE;
    if (out_line_height) *out_line_height = measure_used_line_height(style->measures.get(), style->style);
    if (out_row_gap) *out_row_gap = style->measures->row_gap;
    if (out_column_gap) *out_column_gap = style->measures->column_gap;
    if (out_generation) *out_generation = style->measures->document->generation;
    if (out_document_identity) *out_document_identity = style->document_identity;
    if (out_node_identity) *out_node_identity = style->node_identity;
    return AETHER_OK;
}
