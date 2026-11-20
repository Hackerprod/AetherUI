#ifndef AETHER_B0_SOURCE_CONTRACT_H
#define AETHER_B0_SOURCE_CONTRACT_H
#include <aether/aether.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* B0 static source session, version 1. See docs/b0-source.md. */
typedef struct aether_b0_source_s* aether_b0_source_t;
#define AETHER_B0_SOURCE_VERSION 1u
#define AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS 1u
#define AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE 2u
#define AETHER_B0_SOURCE_STATIC_INLINE_WRAP 4u
#define AETHER_B0_SOURCE_STATIC_ROUNDED_BOXES 8u
#define AETHER_B0_NO_INDEX UINT64_MAX
#define AETHER_B0_STAGE_NONE 0u
#define AETHER_B0_STAGE_INPUT 1u
#define AETHER_B0_STAGE_DOM 2u
#define AETHER_B0_STAGE_CSS 3u
#define AETHER_B0_STAGE_STYLE 4u
#define AETHER_B0_STAGE_TEXT 5u
#define AETHER_B0_STAGE_LAYOUT 6u
#define AETHER_B0_STAGE_TRANSPORT 7u
#define AETHER_B0_STAGE_PREPARATION 8u
#define AETHER_B0_STAGE_RASTER 9u
#define AETHER_B0_REASON_NONE 0u
#define AETHER_B0_REASON_ARGUMENT 1u
#define AETHER_B0_REASON_LIMIT 2u
#define AETHER_B0_REASON_UNSUPPORTED 3u
#define AETHER_B0_REASON_CSS_LOSS 4u
#define AETHER_B0_REASON_GLYPH_MISSING 5u
#define AETHER_B0_REASON_ALLOCATION 6u
#define AETHER_B0_REASON_INVARIANT 7u
typedef struct {
    uint32_t struct_size, version;
    const uint8_t* regular_font;
    size_t regular_font_size;
    const uint8_t* bold_font;
    size_t bold_font_size;
    uint32_t reserved[4];
} aether_b0_source_options_v1;
typedef struct {
    uint32_t struct_size, version;
    int32_t status;
    uint32_t stage, reason, reserved;
    uint64_t node_ordinal, command_index, source_byte_offset;
} aether_b0_source_diagnostic_v1;
/* Imagen de entrada para create_v2 (ciclo 01, §2.8): la clave `source` debe
 * coincidir con el url('...') del CSS; los bytes son PNG/JPEG/GIF. */
typedef struct {
    const char* source;
    const uint8_t* bytes;
    size_t byte_count;
} aether_b0_source_image_v1;
typedef struct {
    uint32_t struct_size, version;
    const uint8_t* regular_font;
    size_t regular_font_size;
    const uint8_t* bold_font;
    size_t bold_font_size;
    const aether_b0_source_image_v1* images;
    size_t image_count;
    uint32_t reserved[4];
} aether_b0_source_options_v2;
typedef struct {
    uint32_t struct_size, version;
    int32_t width, height;
    uint32_t format, reserved;
    uint64_t stride, byte_count, sequence;
} aether_b0_source_frame_info_v1;
AETHER_API uint32_t aether_b0_source_capabilities(void);
AETHER_API size_t aether_b0_source_options_v1_size(void);
AETHER_API size_t aether_b0_source_options_v2_size(void);
AETHER_API size_t aether_b0_source_diagnostic_v1_size(void);
AETHER_API size_t aether_b0_source_frame_info_v1_size(void);
AETHER_API aether_status_t aether_b0_source_create(
    int32_t width, int32_t height, const char* html, size_t html_length,
    const aether_b0_source_options_v1* options,
    aether_b0_source_t* out_session,
    aether_b0_source_diagnostic_v1* diagnostic);
/* Como create(), pero con imágenes de entrada (options_v2, version == 2).
 * Las imágenes se registran antes de prepare(); un decode fallido se reporta
 * en etapa INPUT: LIMIT_EXCEEDED si excede presupuesto, UNSUPPORTED si no
 * decodifica. */
AETHER_API aether_status_t aether_b0_source_create_v2(
    int32_t width, int32_t height, const char* html, size_t html_length,
    const aether_b0_source_options_v2* options,
    aether_b0_source_t* out_session,
    aether_b0_source_diagnostic_v1* diagnostic);
AETHER_API aether_status_t aether_b0_source_render(
    aether_b0_source_t session,
    aether_b0_source_diagnostic_v1* diagnostic);
AETHER_API aether_status_t aether_b0_source_copy_frame(
    aether_b0_source_t session, aether_b0_source_frame_info_v1* info,
    uint8_t* destination, size_t capacity);
AETHER_API aether_status_t aether_b0_source_destroy(aether_b0_source_t session);
#ifdef __cplusplus
}
#endif
#endif
