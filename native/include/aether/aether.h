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
#define AETHER_ABI_VERSION_MINOR 18u
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
AETHER_API size_t                aether_computed_style_size(void);

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
typedef struct { aether_color_rgba left, top, right, bottom; } aether_color_edges;

/* Border styles are kept per edge, just like widths and colors.  The values
   intentionally follow the CSS keyword family instead of exposing renderer
   implementation details to bindings. */
typedef enum {
    AETHER_BORDER_STYLE_NONE = 0,
    AETHER_BORDER_STYLE_HIDDEN,
    AETHER_BORDER_STYLE_DOTTED,
    AETHER_BORDER_STYLE_DASHED,
    AETHER_BORDER_STYLE_SOLID,
    AETHER_BORDER_STYLE_DOUBLE,
    AETHER_BORDER_STYLE_GROOVE,
    AETHER_BORDER_STYLE_RIDGE,
    AETHER_BORDER_STYLE_INSET,
    AETHER_BORDER_STYLE_OUTSET
} aether_border_style_t;

typedef struct { int32_t left, top, right, bottom; } aether_border_style_edges;

typedef struct {
    float top_left_x, top_left_y, top_right_x, top_right_y;
    float bottom_right_x, bottom_right_y, bottom_left_x, bottom_left_y;
} aether_corner_radii;

typedef struct { aether_color_rgba color; float offset; } aether_gradient_stop;

typedef struct {
    float direction_x;
    float direction_y;
    aether_gradient_stop* stops;
    int32_t               stop_count;
} aether_linear_gradient;

AETHER_API void aether_linear_gradient_free(aether_linear_gradient* g);

/* A single CSS shadow. A list owns its contiguous item storage; use the
   shadow-list helpers below to copy, compare, or release it. */
typedef struct {
    float offset_x;
    float offset_y;
    float blur_radius;
    float spread_radius;
    aether_color_rgba color;
    aether_bool_t     inset;
} aether_shadow;

typedef struct {
    aether_shadow* items;
    int32_t        count;
} aether_shadow_list;

/* A copy leaves its destination as a deep copy on success and as the canonical
   empty list on failure. A list with count zero must have a null items pointer. */
AETHER_API aether_bool_t aether_shadow_list_copy(const aether_shadow_list* source,
                                                  aether_shadow_list* destination);
AETHER_API aether_bool_t aether_shadow_list_equal(const aether_shadow_list* left,
                                                   const aether_shadow_list* right);
AETHER_API void          aether_shadow_list_free(aether_shadow_list* list);

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

/* flex-basis needs to distinguish auto (use the main-size property), content
   (intrinsic content size), and a concrete CSS length. A plain length cannot
   preserve that distinction because both auto and content defer resolution. */
typedef enum {
    AETHER_FLEX_BASIS_AUTO = 0,
    AETHER_FLEX_BASIS_CONTENT,
    AETHER_FLEX_BASIS_LENGTH
} aether_flex_basis_kind_t;

typedef struct {
    int32_t           kind;
    aether_css_length length;
} aether_flex_basis;

/* CSS Backgrounds & Borders §4.1: every corner has independent horizontal
   and vertical radii. Keep symbolic lengths until the border box is known. */
typedef struct { aether_css_length horizontal, vertical; } aether_corner_radius;
typedef struct {
    aether_corner_radius top_left, top_right, bottom_right, bottom_left;
} aether_border_radii;

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

/* CSS background layers are painted from the last declared layer to the first,
   above background-color. Resource loading remains outside the core: an image
   layer carries its normalized UTF-8 source for the binding to resolve. */
typedef enum {
    AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT = 0,
    AETHER_BACKGROUND_LAYER_IMAGE_URL
} aether_background_layer_kind_t;

typedef enum {
    AETHER_BACKGROUND_SIZE_AUTO = 0,
    AETHER_BACKGROUND_SIZE_EXPLICIT,
    AETHER_BACKGROUND_SIZE_COVER,
    AETHER_BACKGROUND_SIZE_CONTAIN
} aether_background_size_kind_t;

typedef enum {
    AETHER_BACKGROUND_REPEAT = 0,
    AETHER_BACKGROUND_NO_REPEAT,
    AETHER_BACKGROUND_SPACE,
    AETHER_BACKGROUND_ROUND
} aether_background_repeat_t;

typedef struct {
    int32_t                kind;
    aether_linear_gradient gradient;
    char*                  image_source;
    aether_css_length      position_x;
    aether_css_length      position_y;
    int32_t                size_kind;
    aether_css_length      size_x;
    aether_css_length      size_y;
    int32_t                repeat_x;
    int32_t                repeat_y;
} aether_background_layer;

typedef struct {
    aether_background_layer* items;
    int32_t                  count;
} aether_background_layer_list;

AETHER_API aether_bool_t aether_background_layer_list_copy(const aether_background_layer_list* source,
                                                             aether_background_layer_list* destination);
AETHER_API aether_bool_t aether_background_layer_list_equal(const aether_background_layer_list* left,
                                                              const aether_background_layer_list* right);
AETHER_API void          aether_background_layer_list_free(aether_background_layer_list* list);

/* Backdrop filters preserve the complete ordered operation model even though v1
   currently parses blur(). Future compositor operations use scalar_amount while
   blur uses length_amount. */
typedef enum {
    AETHER_BACKDROP_FILTER_BLUR = 0,
    AETHER_BACKDROP_FILTER_BRIGHTNESS,
    AETHER_BACKDROP_FILTER_CONTRAST,
    AETHER_BACKDROP_FILTER_SATURATE
} aether_backdrop_filter_kind_t;

typedef struct {
    int32_t           kind;
    aether_css_length length_amount;
    float             scalar_amount;
} aether_backdrop_filter;

typedef struct {
    aether_backdrop_filter* items;
    int32_t                 count;
} aether_backdrop_filter_list;

AETHER_API aether_bool_t aether_backdrop_filter_list_copy(const aether_backdrop_filter_list* source,
                                                            aether_backdrop_filter_list* destination);
AETHER_API aether_bool_t aether_backdrop_filter_list_equal(const aether_backdrop_filter_list* left,
                                                             const aether_backdrop_filter_list* right);
AETHER_API void          aether_backdrop_filter_list_free(aether_backdrop_filter_list* list);

/* CSS transforms retain their authored operations until paint time: percentage
   translations and transform-origin both need the element border-box, which is
   deliberately unavailable during cascade.  The values are normalized into a
   small 2D operation set rather than preserving parser strings. */
typedef enum {
    AETHER_TRANSFORM_TRANSLATE = 0,
    AETHER_TRANSFORM_SCALE,
    AETHER_TRANSFORM_ROTATE,
    AETHER_TRANSFORM_SKEW_X,
    AETHER_TRANSFORM_SKEW_Y,
    AETHER_TRANSFORM_MATRIX
} aether_transform_operation_kind_t;

typedef struct {
    int32_t           kind;
    aether_css_length x;
    aether_css_length y;
    float             scalar_x;
    float             scalar_y;
    float             angle_radians;
    float             matrix_a;
    float             matrix_b;
    float             matrix_c;
    float             matrix_d;
    float             matrix_e;
    float             matrix_f;
} aether_transform_operation;

typedef struct {
    aether_transform_operation* items;
    int32_t                     count;
} aether_transform_list;

typedef struct {
    aether_css_length x;
    aether_css_length y;
} aether_transform_origin;

/* Row-vector affine matrix matching the native transform primitives used by
   Direct2D, Skia and CoreGraphics adapters: x' = x*m11 + y*m21 + dx and
   y' = x*m12 + y*m22 + dy. */
typedef struct {
    float m11, m12;
    float m21, m22;
    float dx, dy;
} aether_transform_matrix;

AETHER_API aether_bool_t aether_transform_list_copy(const aether_transform_list* source,
                                                      aether_transform_list* destination);
AETHER_API aether_bool_t aether_transform_list_equal(const aether_transform_list* left,
                                                       const aether_transform_list* right);
AETHER_API void          aether_transform_list_free(aether_transform_list* list);

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
    AETHER_POSITION_STATIC = 0,
    AETHER_POSITION_RELATIVE,
    AETHER_POSITION_ABSOLUTE,
    AETHER_POSITION_FIXED
} aether_position_kind_t;

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

/* align-self has the same resolved values as align-items, plus auto to inherit the
   flex container's cross-axis alignment. Keeping it distinct prevents auto from
   being conflated with stretch in the computed-style ABI. */
typedef enum {
    AETHER_ALIGN_SELF_AUTO = -1,
    AETHER_ALIGN_SELF_STRETCH = AETHER_ALIGN_STRETCH,
    AETHER_ALIGN_SELF_START = AETHER_ALIGN_START,
    AETHER_ALIGN_SELF_CENTER = AETHER_ALIGN_CENTER,
    AETHER_ALIGN_SELF_END = AETHER_ALIGN_END
} aether_align_self_t;

typedef enum {
    AETHER_TEXT_ALIGN_START = 0,
    AETHER_TEXT_ALIGN_CENTER,
    AETHER_TEXT_ALIGN_END,
    /* Physical values remain distinct from logical start/end so direction and
       writing-mode can resolve them later without losing cascade semantics. */
    AETHER_TEXT_ALIGN_LEFT,
    AETHER_TEXT_ALIGN_RIGHT,
    AETHER_TEXT_ALIGN_JUSTIFY
} aether_text_align_t;

typedef enum {
    AETHER_TEXT_TRANSFORM_NONE = 0,
    AETHER_TEXT_TRANSFORM_CAPITALIZE,
    AETHER_TEXT_TRANSFORM_UPPERCASE,
    AETHER_TEXT_TRANSFORM_LOWERCASE
} aether_text_transform_t;

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
    int32_t           align_self;
    float             flex_grow;
    float             flex_shrink;
    aether_flex_basis flex_basis;
    aether_bool_t     flex_wrap;
    float             gap;
    int32_t           position;
    aether_css_edges  inset;

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
    aether_background_layer_list background_layers;
    aether_backdrop_filter_list backdrop_filters;
    aether_color_rgba border_color;
    aether_color_edges border_colors;
    aether_border_style_edges border_styles;
    aether_bool_t     has_text_gradient;
    aether_linear_gradient text_gradient;

    /* Post-layout paint/hit-test transforms. They are not inherited, but their
       Display List brackets scope over the element's entire descendant subtree. */
    aether_transform_list   transforms;
    aether_transform_origin transform_origin;

    /* CSS shadow lists, each in declaration order. text-shadow inherits;
       box-shadow does not. */
    aether_shadow_list text_shadows;
    aether_shadow_list box_shadows;

    /* Border radius + opacity */
    aether_border_radii border_radii;
    float             opacity;

    /* Typography */
    char              font_family[64];
    float             font_size;
    int32_t           font_weight;
    float             line_height;
    int32_t           text_align;
    int32_t           text_transform;

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
    AETHER_PAINT_FILL_BORDER_SIDE,
    AETHER_PAINT_DRAW_TEXT_SHADOW,
    AETHER_PAINT_DRAW_BACKGROUND_IMAGE,
    AETHER_PAINT_APPLY_BACKDROP_FILTER,
    AETHER_PAINT_PUSH_TRANSFORM,
    AETHER_PAINT_POP_TRANSFORM,
} aether_paint_command_tag_t;

typedef struct {
    aether_paint_command_tag_t tag;
    union {
        struct { aether_rectf rect; aether_corner_radii radii; aether_color_rgba color; } fill_rounded_rect;
        struct { aether_rectf rect; aether_corner_radii radii; aether_shadow shadow; } draw_box_shadow;
        struct { aether_rectf rect; aether_corner_radii radii; aether_linear_gradient gradient; float opacity; } fill_rounded_rect_gradient;
        struct { aether_rectf rect; aether_corner_radii radii; float stroke_width; aether_color_rgba color; } stroke_rounded_rect;
        struct { char* text; aether_rectf rect; char font_family[64]; float font_size; int32_t font_weight; int32_t text_align; aether_color_rgba color; aether_bool_t wrap; } draw_text;
        struct { char* text; aether_rectf rect; char font_family[64]; float font_size; int32_t font_weight; int32_t text_align; aether_linear_gradient gradient; float opacity; } draw_text_gradient;
        struct { char* text; aether_rectf rect; char font_family[64]; float font_size; int32_t font_weight; int32_t text_align; aether_shadow shadow; aether_bool_t wrap; } draw_text_shadow;
        struct { aether_rectf rect; aether_corner_radii radii; } push_clip;
        struct { aether_rectf outer_rect; aether_corner_radii outer_radii;
                 aether_rectf inner_rect; aether_corner_radii inner_radii;
                 int32_t side; int32_t style; aether_color_rgba color; } fill_border_side;
        struct { char* source; aether_rectf paint_rect; aether_css_length position_x; aether_css_length position_y;
                 int32_t size_kind; aether_css_length size_x; aether_css_length size_y;
                 int32_t repeat_x; int32_t repeat_y; float opacity;
                 aether_rectf source_rect; aether_rectf destination_rect;
                 aether_bool_t has_resolved_geometry; } draw_background_image;
        struct { aether_rectf rect; aether_corner_radii radii;
                 aether_backdrop_filter_list filters; } apply_backdrop_filter;
        struct { aether_transform_matrix matrix; } push_transform;
    } data;
} aether_paint_command_t;

/* Returns sizeof(aether_paint_command_t) for the loaded native library.
   FFI consumers must use this instead of hard-coding a command-buffer size. */
AETHER_API size_t aether_paint_command_size(void);

typedef struct aether_display_list_s*          aether_display_list_t;
typedef struct aether_display_list_builder_s*  aether_display_list_builder_t;

typedef aether_bool_t (*aether_image_dimensions_fn)(const char* source, aether_sizef* out_size, void* user_data);

AETHER_API aether_status_t aether_display_list_builder_create(
    aether_text_measurer_fn text_measurer,
    void* text_measurer_data,
    aether_display_list_builder_t* out_builder);

AETHER_API void aether_display_list_builder_destroy(aether_display_list_builder_t builder);
AETHER_API void aether_display_list_builder_set_image_dimensions_callback(
    aether_display_list_builder_t builder, aether_image_dimensions_fn callback, void* user_data);

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
   stack; all string pointers are borrowed and valid only for the callback's
   duration, so consumers that retain them must copy their contents. */
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
