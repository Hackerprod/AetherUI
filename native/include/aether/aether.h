#ifndef AETHER_AETHER_H
#define AETHER_AETHER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
    #ifdef AETHER_EXPORTS
        #define AETHER_API __declspec(dllexport)
    #else
        #define AETHER_API __declspec(dllimport)
    #endif
#else
    #define AETHER_API __attribute__((visibility("default")))
#endif

/* ── Status & Lifecycle ────────────────────────────────────────────── */

/* ABI introspection
 *
 * The packed version uses 8 bits for major, 8 bits for minor, and 16 bits for
 * patch. Before ABI 1.0, minor releases may contain breaking changes. After
 * ABI 1.0, a major-version change denotes an incompatible ABI.
 */
#define AETHER_ABI_VERSION_MAJOR 0u
#define AETHER_ABI_VERSION_MINOR 1u
#define AETHER_ABI_VERSION_PATCH 0u

#define AETHER_ABI_VERSION_MAKE(major, minor, patch) \
    ((((uint32_t)(major) & UINT32_C(0xff)) << 24) | \
     (((uint32_t)(minor) & UINT32_C(0xff)) << 16) | \
      ((uint32_t)(patch) & UINT32_C(0xffff)))
#define AETHER_ABI_VERSION_GET_MAJOR(version) (((uint32_t)(version) >> 24) & UINT32_C(0xff))
#define AETHER_ABI_VERSION_GET_MINOR(version) (((uint32_t)(version) >> 16) & UINT32_C(0xff))
#define AETHER_ABI_VERSION_GET_PATCH(version) ((uint32_t)(version) & UINT32_C(0xffff))
#define AETHER_ABI_VERSION_CURRENT \
    AETHER_ABI_VERSION_MAKE(AETHER_ABI_VERSION_MAJOR, AETHER_ABI_VERSION_MINOR, AETHER_ABI_VERSION_PATCH)

typedef uint64_t aether_capabilities_t;

/* Each bit represents a tested public pipeline, not the mere presence of a
   symbol. In particular, STYLE_RUNTIME refers to the aether_style_runtime_*
   cascade API; it does not advertise legacy or experimental CSS entrypoints. */
#define AETHER_CAPABILITY_DOM           (UINT64_C(1) << 0)
#define AETHER_CAPABILITY_STYLE_RUNTIME (UINT64_C(1) << 1)
#define AETHER_CAPABILITY_LAYOUT        (UINT64_C(1) << 2)
#define AETHER_CAPABILITY_DISPLAY_LIST  (UINT64_C(1) << 3)

AETHER_API uint32_t              aether_abi_version(void);
AETHER_API aether_capabilities_t aether_abi_capabilities(void);

typedef int32_t aether_status_t;
#define AETHER_OK                   0
#define AETHER_ERROR_NULL_ARG      -1
#define AETHER_ERROR_INVALID_STATE -2
#define AETHER_ERROR_OUT_OF_MEMORY -3
#define AETHER_ERROR_DISPOSED      -4
#define AETHER_ERROR_PARSE_FAILED  -5

typedef int32_t aether_bool_t;
#define AETHER_TRUE  1
#define AETHER_FALSE 0

AETHER_API const char* aether_status_message(aether_status_t status);
AETHER_API void        aether_string_free(char* str);

/* ── Primitives ────────────────────────────────────────────────────── */

typedef struct { float x, y; }                  aether_pointf;
typedef struct { float width, height; }         aether_sizef;
typedef struct { float x, y, width, height; }   aether_rectf;
typedef struct { float left, top, right, bottom; } aether_thicknessf;

AETHER_API aether_bool_t aether_rectf_contains(aether_rectf r, aether_pointf p);
AETHER_API aether_rectf  aether_rectf_deflate(aether_rectf r, aether_thicknessf t);
AETHER_API aether_rectf  aether_rectf_inflate(aether_rectf r, float v);
AETHER_API aether_rectf  aether_rectf_offset(aether_rectf r, float dx, float dy);
AETHER_API aether_rectf  aether_rectf_from_edges(float left, float top, float right, float bottom);
AETHER_API float         aether_thicknessf_horizontal(aether_thicknessf t);
AETHER_API float         aether_thicknessf_vertical(aether_thicknessf t);

typedef struct { float r, g, b, a; }            aether_color_rgba;

typedef struct { aether_color_rgba color; float offset; } aether_gradient_stop;

typedef struct {
    float direction_x;
    float direction_y;
    aether_gradient_stop* stops;
    int32_t               stop_count;
} aether_linear_gradient;

AETHER_API void aether_linear_gradient_free(aether_linear_gradient* g);

typedef struct {
    float offset_x;
    float offset_y;
    float blur_radius;
    float spread_radius;
    aether_color_rgba color;
    aether_bool_t     inset;
} aether_box_shadow;

/* ── CSS Value Types ───────────────────────────────────────────────── */

typedef enum {
    AETHER_CSS_UNIT_AUTO = 0,
    AETHER_CSS_UNIT_PX,
    AETHER_CSS_UNIT_PERCENT,
    AETHER_CSS_UNIT_EM,
    AETHER_CSS_UNIT_REM,
    AETHER_CSS_UNIT_VW,
    AETHER_CSS_UNIT_VH,
    AETHER_CSS_UNIT_LINEAR
} aether_css_unit_t;

typedef struct {
    float value;
    aether_css_unit_t unit;
    float linear_percent;
    float linear_em;
    float linear_rem;
    float linear_vw;
    float linear_vh;
} aether_css_length;

AETHER_API aether_css_length aether_css_length_auto(void);
AETHER_API aether_css_length aether_css_length_zero(void);
AETHER_API aether_bool_t     aether_css_length_is_auto(aether_css_length l);
AETHER_API float             aether_css_length_resolve(aether_css_length l, float reference,
                                float font_size, float root_font_size,
                                float viewport_width, float viewport_height);
AETHER_API aether_bool_t     aether_css_length_try_parse(const char* input, aether_css_length* out);

AETHER_API aether_bool_t     aether_color_rgba_try_parse(const char* input, aether_color_rgba* out);

typedef struct {
    aether_css_length left, top, right, bottom;
} aether_css_edges;

AETHER_API aether_css_edges aether_css_edges_zero(void);

/* ── CSS Enums (ComputedStyle) ─────────────────────────────────────── */

typedef enum {
    AETHER_DISPLAY_NONE = 0,
    AETHER_DISPLAY_BLOCK,
    AETHER_DISPLAY_INLINE,
    AETHER_DISPLAY_INLINE_BLOCK,
    AETHER_DISPLAY_FLEX,
    AETHER_DISPLAY_LINE_BREAK
} aether_display_kind_t;

typedef enum {
    AETHER_FLEX_DIRECTION_ROW = 0,
    AETHER_FLEX_DIRECTION_COLUMN
} aether_flex_direction_t;

typedef enum {
    AETHER_JUSTIFY_START = 0,
    AETHER_JUSTIFY_CENTER,
    AETHER_JUSTIFY_END,
    AETHER_JUSTIFY_SPACE_BETWEEN
} aether_justify_content_t;

typedef enum {
    AETHER_ALIGN_STRETCH = 0,
    AETHER_ALIGN_START,
    AETHER_ALIGN_CENTER,
    AETHER_ALIGN_END
} aether_align_items_t;

typedef enum {
    AETHER_TEXT_ALIGN_START = 0,
    AETHER_TEXT_ALIGN_CENTER,
    AETHER_TEXT_ALIGN_END
} aether_text_align_t;

typedef enum {
    AETHER_FONT_WEIGHT_THIN       = 100,
    AETHER_FONT_WEIGHT_EXTRA_LIGHT = 200,
    AETHER_FONT_WEIGHT_LIGHT      = 300,
    AETHER_FONT_WEIGHT_NORMAL     = 400,
    AETHER_FONT_WEIGHT_MEDIUM     = 500,
    AETHER_FONT_WEIGHT_SEMI_BOLD  = 600,
    AETHER_FONT_WEIGHT_BOLD       = 700,
    AETHER_FONT_WEIGHT_EXTRA_BOLD = 800,
    AETHER_FONT_WEIGHT_BLACK      = 900
} aether_font_weight_t;

typedef enum {
    AETHER_BOX_SIZING_CONTENT_BOX = 0,
    AETHER_BOX_SIZING_BORDER_BOX
} aether_box_sizing_t;

typedef enum {
    AETHER_OVERFLOW_VISIBLE = 0,
    AETHER_OVERFLOW_HIDDEN,
    AETHER_OVERFLOW_AUTO,
    AETHER_OVERFLOW_SCROLL
} aether_overflow_kind_t;

typedef enum {
    AETHER_CURSOR_DEFAULT = 0,
    AETHER_CURSOR_POINTER,
    AETHER_CURSOR_TEXT
} aether_cursor_kind_t;

/* ── ComputedStyle (blittable flat struct) ─────────────────────────── */

typedef struct {
    /* Layout */
    int32_t           display;
    int32_t           flex_direction;
    int32_t           justify_content;
    int32_t           align_items;
    float             flex_grow;
    aether_bool_t     flex_wrap;
    float             gap;

    /* Sizing */
    aether_css_length width;
    aether_css_length height;
    aether_css_length min_width;
    aether_css_length min_height;
    aether_css_length max_width;
    aether_css_length max_height;
    aether_css_edges  margin;
    aether_css_edges  padding;
    aether_css_edges  border_width;
    int32_t           box_sizing;

    /* Paint */
    aether_color_rgba color;
    aether_color_rgba background_color;
    aether_bool_t     has_background_gradient;
    aether_linear_gradient background_gradient;
    aether_color_rgba border_color;
    aether_bool_t     has_text_gradient;
    aether_linear_gradient text_gradient;

    /* Box shadow (single for now; C# has a list) */
    aether_bool_t     has_box_shadow;
    aether_box_shadow box_shadow;

    /* Border radius + opacity */
    aether_css_length border_radius;
    float             opacity;

    /* Typography */
    char              font_family[64];
    float             font_size;
    int32_t           font_weight;
    float             line_height;
    int32_t           text_align;

    /* Misc */
    int32_t           overflow;
    int32_t           cursor;
    aether_bool_t     visible;
} aether_computed_style_t;

AETHER_API aether_computed_style_t aether_computed_style_default(void);
AETHER_API aether_computed_style_t aether_computed_style_inherit_from(const aether_computed_style_t* parent);
AETHER_API void aether_computed_style_free(aether_computed_style_t* s);

/* ── DOM ───────────────────────────────────────────────────────────── */

typedef enum {
    AETHER_NODE_DOCUMENT = 0,
    AETHER_NODE_ELEMENT,
    AETHER_NODE_TEXT,
    AETHER_NODE_COMMENT
} aether_node_type_t;

typedef enum {
    AETHER_STATE_NONE     = 0,
    AETHER_STATE_HOVER    = 1 << 0,
    AETHER_STATE_ACTIVE   = 1 << 1,
    AETHER_STATE_FOCUS    = 1 << 2,
    AETHER_STATE_INVALID  = 1 << 3,
} aether_element_state_t;

/* Opaque handles */
typedef struct aether_doc_s*  aether_doc_t;
typedef struct aether_node_s* aether_node_t;

/* Document lifecycle */
AETHER_API aether_status_t aether_doc_create(aether_doc_t* out_doc);
AETHER_API void            aether_doc_destroy(aether_doc_t doc);
AETHER_API aether_status_t aether_doc_parse_html(aether_doc_t doc, const char* html);
AETHER_API int64_t         aether_doc_get_version(aether_doc_t doc);
AETHER_API aether_node_t   aether_doc_get_body(aether_doc_t doc);
AETHER_API aether_node_t   aether_doc_get_document_element(aether_doc_t doc);
AETHER_API aether_node_t   aether_doc_get_element_by_id(aether_doc_t doc, const char* id);

/* Node queries */
AETHER_API aether_node_type_t aether_node_get_type(aether_node_t node);
AETHER_API aether_node_t      aether_node_get_parent(aether_node_t node);
AETHER_API int32_t            aether_node_get_child_count(aether_node_t node);
AETHER_API aether_node_t      aether_node_get_child(aether_node_t node, int32_t index);
AETHER_API char*              aether_node_get_text_content(aether_node_t node);

/* Element-specific */
AETHER_API char*           aether_element_get_tag_name(aether_node_t element);
AETHER_API char*           aether_element_get_attribute(aether_node_t element, const char* name);
AETHER_API int32_t         aether_element_get_attribute_count(aether_node_t element);
AETHER_API char*           aether_element_get_attribute_name(aether_node_t element, int32_t index);
AETHER_API char*           aether_element_get_attribute_value(aether_node_t element, int32_t index);
AETHER_API aether_status_t aether_element_set_attribute(aether_node_t element, const char* name, const char* value);
AETHER_API aether_bool_t   aether_element_has_attribute(aether_node_t element, const char* name);
AETHER_API aether_bool_t   aether_element_has_class(aether_node_t element, const char* class_name);
AETHER_API aether_bool_t   aether_element_is_required(aether_node_t element);
AETHER_API aether_bool_t   aether_element_is_disabled(aether_node_t element);
AETHER_API aether_bool_t   aether_element_is_focusable(aether_node_t element);
AETHER_API int32_t         aether_element_get_state(aether_node_t element);
AETHER_API void            aether_element_set_state(aether_node_t element, int32_t state);

/* Tree mutation */
AETHER_API aether_status_t aether_node_append_child(aether_node_t parent, aether_node_t child);
AETHER_API aether_status_t aether_node_remove_child(aether_node_t parent, aether_node_t child);

/* ── CSS ───────────────────────────────────────────────────────────── */

typedef struct aether_stylesheet_s*  aether_stylesheet_t;
typedef struct aether_style_map_s*   aether_style_map_t;
typedef struct aether_computed_style_handle_s* aether_computed_style_handle_t;

AETHER_API aether_status_t aether_css_parse_stylesheet(const char* source, aether_stylesheet_t* out_sheet);
AETHER_API void            aether_css_stylesheet_destroy(aether_stylesheet_t sheet);

AETHER_API aether_status_t aether_css_resolve(
    aether_doc_t doc,
    aether_stylesheet_t author_sheet,
    float viewport_width,
    float viewport_height,
    aether_style_map_t* out_map);

AETHER_API void aether_style_map_destroy(aether_style_map_t map);
AETHER_API aether_computed_style_handle_t aether_style_map_get(aether_style_map_t map, aether_node_t element);

AETHER_API aether_status_t aether_computed_style_copy(aether_computed_style_handle_t handle, aether_computed_style_t* out);
AETHER_API void            aether_computed_style_handle_destroy(aether_computed_style_handle_t handle);

/* ── Layout ────────────────────────────────────────────────────────── */

typedef struct {
    float width;
    float height;
    float baseline;
} aether_text_metrics_t;

typedef aether_text_metrics_t (*aether_text_measurer_fn)(
    const char* text,
    const aether_computed_style_t* style,
    float max_width,
    void* user_data);

typedef float (*aether_scroll_offset_fn)(
    aether_node_t element,
    void* user_data);

typedef struct aether_layout_engine_s*  aether_layout_engine_t;
typedef struct aether_layout_result_s*  aether_layout_result_t;
typedef struct aether_layout_box_s*     aether_layout_box_t;

AETHER_API aether_status_t aether_layout_create(
    aether_text_measurer_fn text_measurer,
    void* text_measurer_data,
    aether_layout_engine_t* out_engine);

AETHER_API void aether_layout_destroy(aether_layout_engine_t engine);

AETHER_API aether_status_t aether_layout_layout(
    aether_layout_engine_t engine,
    aether_doc_t doc,
    aether_style_map_t styles,
    float viewport_width,
    float viewport_height,
    aether_scroll_offset_fn scroll_fn,
    void* scroll_data,
    aether_layout_result_t* out_result);

AETHER_API void aether_layout_result_destroy(aether_layout_result_t result);

AETHER_API aether_layout_box_t aether_layout_result_get_root(aether_layout_result_t result);
AETHER_API aether_layout_box_t aether_layout_result_find_primary_box(aether_layout_result_t result, aether_node_t element);
AETHER_API aether_node_t       aether_layout_result_hit_test(aether_layout_result_t result, float x, float y);
AETHER_API int32_t             aether_layout_result_get_paint_order_count(aether_layout_result_t result);
AETHER_API aether_layout_box_t aether_layout_result_get_paint_order_box(aether_layout_result_t result, int32_t index);

/* LayoutBox queries */
AETHER_API int32_t         aether_layout_box_get_kind(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_x(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_y(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_width(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_height(aether_layout_box_t box);
AETHER_API aether_rectf    aether_layout_box_get_margin_box(aether_layout_box_t box);
AETHER_API aether_rectf    aether_layout_box_get_border_box(aether_layout_box_t box);
AETHER_API aether_rectf    aether_layout_box_get_padding_box(aether_layout_box_t box);
AETHER_API aether_rectf    aether_layout_box_get_content_box(aether_layout_box_t box);
AETHER_API aether_bool_t   aether_layout_box_is_atomic(aether_layout_box_t box);
AETHER_API char*           aether_layout_box_get_text(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_scrollable_overflow_y(aether_layout_box_t box);
AETHER_API float           aether_layout_box_get_scroll_offset_y(aether_layout_box_t box);
AETHER_API aether_node_t   aether_layout_box_get_element(aether_layout_box_t box);
AETHER_API int32_t         aether_layout_box_get_child_count(aether_layout_box_t box);
AETHER_API aether_layout_box_t aether_layout_box_get_child(aether_layout_box_t box, int32_t index);

/* ── Rendering (DisplayList) ───────────────────────────────────────── */

typedef enum {
    AETHER_PAINT_FILL_ROUNDED_RECT = 0,
    AETHER_PAINT_DRAW_BOX_SHADOW,
    AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT,
    AETHER_PAINT_STROKE_ROUNDED_RECT,
    AETHER_PAINT_DRAW_TEXT,
    AETHER_PAINT_DRAW_TEXT_GRADIENT,
    AETHER_PAINT_PUSH_CLIP,
    AETHER_PAINT_POP_CLIP,
} aether_paint_command_tag_t;

typedef struct {
    aether_paint_command_tag_t tag;
    union {
        struct { aether_rectf rect; float radius; aether_color_rgba color; } fill_rounded_rect;
        struct { aether_rectf rect; float radius; aether_box_shadow shadow; } draw_box_shadow;
        struct { aether_rectf rect; float radius; aether_linear_gradient gradient; float opacity; } fill_rounded_rect_gradient;
        struct { aether_rectf rect; float radius; float stroke_width; aether_color_rgba color; } stroke_rounded_rect;
        struct { char* text; aether_rectf rect; char font_family[64]; float font_size; int32_t font_weight; int32_t text_align; aether_color_rgba color; aether_bool_t wrap; } draw_text;
        struct { char* text; aether_rectf rect; char font_family[64]; float font_size; int32_t font_weight; int32_t text_align; aether_linear_gradient gradient; float opacity; } draw_text_gradient;
        struct { aether_rectf rect; float radius; } push_clip;
    } data;
} aether_paint_command_t;

/* Returns sizeof(aether_paint_command_t) for the loaded native library.
   FFI consumers must use this instead of hard-coding a command-buffer size. */
AETHER_API size_t aether_paint_command_size(void);

typedef struct aether_display_list_s*          aether_display_list_t;
typedef struct aether_display_list_builder_s*  aether_display_list_builder_t;

AETHER_API aether_status_t aether_display_list_builder_create(
    aether_text_measurer_fn text_measurer,
    void* text_measurer_data,
    aether_display_list_builder_t* out_builder);

AETHER_API void aether_display_list_builder_destroy(aether_display_list_builder_t builder);

AETHER_API aether_status_t aether_display_list_build(
    aether_display_list_builder_t builder,
    aether_layout_result_t layout,
    aether_node_t focused_element,
    aether_display_list_t* out_list);

AETHER_API void aether_display_list_destroy(aether_display_list_t list);
AETHER_API int32_t aether_display_list_get_count(aether_display_list_t list);
AETHER_API aether_status_t aether_display_list_get_command(aether_display_list_t list, int32_t index, aether_paint_command_t* out_cmd);

AETHER_API void aether_paint_command_free(aether_paint_command_t* cmd);

/* ── Full StyleRuntime (Phase 6) ──────────────────────────────────── */

typedef struct {
    aether_bool_t throw_on_invalid;
    aether_bool_t enforce_thread_affinity;
    int32_t       max_variable_depth;
    int32_t       max_variable_expansion_length;
} aether_style_runtime_options_t;

typedef struct {
    float viewport_width;
    float viewport_height;
    float dpi_scale;
    float root_font_size;
    int32_t color_scheme;
    int32_t pointer_accuracy;
    int32_t motion_preference;
} aether_style_environment_t;

/* Diagnostics callback: invoked by native StyleRuntimeBridge when a CSS property
   is parsed correctly but cannot be rendered by the current engine (e.g. inset
   box-shadow, position: absolute, transform). The callback fires on the native
   stack, so the managed side must marshal it to CssDiagnostics.ReportFromStyleRuntime. */
typedef void (*aether_style_diagnostic_fn)(
    const char* code,
    const char* property,
    const char* value,
    const char* message,
    void* user_data);

typedef struct aether_style_runtime_s* aether_style_runtime_t;
typedef struct aether_style_update_s*  aether_style_update_t;

AETHER_API aether_status_t aether_style_runtime_create(
    const aether_style_runtime_options_t* options,
    const aether_style_environment_t* environment,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data,
    aether_style_runtime_t* out_runtime);

AETHER_API void aether_style_runtime_set_diagnostics_callback(
    aether_style_runtime_t runtime,
    aether_style_diagnostic_fn callback,
    void* user_data);

AETHER_API void aether_style_runtime_destroy(aether_style_runtime_t runtime);

AETHER_API aether_status_t aether_style_runtime_add_stylesheet(
    aether_style_runtime_t runtime,
    const char* source,
    int32_t layer,
    const char* source_name,
    int64_t* out_handle,
    int32_t* out_rule_count);

AETHER_API aether_bool_t aether_style_runtime_remove_stylesheet(
    aether_style_runtime_t runtime,
    int64_t sheet_handle);

AETHER_API int32_t aether_style_runtime_resolve_subtree(
    aether_style_runtime_t runtime,
    aether_node_t root,
    aether_style_update_t** out_updates);

AETHER_API aether_status_t aether_style_runtime_get_update(
    aether_style_update_t update,
    aether_node_t* out_node,
    aether_computed_style_handle_t* out_current,
    uint32_t* out_impact);

AETHER_API void aether_style_updates_free(aether_style_update_t* updates, int32_t count);

AETHER_API int32_t aether_style_runtime_set_environment(
    aether_style_runtime_t runtime,
    const aether_style_environment_t* env,
    aether_node_t root,
    aether_style_update_t** out_updates);

/* Style values in the returned map borrow storage from the runtime. Dispose the map first. */
AETHER_API aether_status_t aether_style_runtime_create_resolved_style_map(
    aether_style_runtime_t runtime,
    aether_doc_t doc,
    aether_style_map_t* out_map);

AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime);

#ifdef __cplusplus
}
#endif

#endif /* AETHER_AETHER_H */
