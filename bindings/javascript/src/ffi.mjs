import { createRequire } from "node:module";
import path from "node:path";
import { existsSync } from "node:fs";

// The command union and computed-style layout include retained scroll commands
// typed two-axis scroll metrics and pointer-events through ABI 0.26; accepting an older minor
// would read invalid offsets or miss command tags.
export const SUPPORTED_ABI = Object.freeze({ major: 0, minor: 26, patch: 0 });

export const CAPABILITIES = Object.freeze({
  DOM: 1n << 0n,
  STYLE_RUNTIME: 1n << 1n,
  LAYOUT: 1n << 2n,
  DISPLAY_LIST: 1n << 3n,
});

export const DEFAULT_CAPABILITIES =
  CAPABILITIES.DOM |
  CAPABILITIES.STYLE_RUNTIME |
  CAPABILITIES.LAYOUT |
  CAPABILITIES.DISPLAY_LIST;

export function parseAbiVersion(version) {
  const packed = Number(version) >>> 0;
  return {
    version: packed,
    major: (packed >>> 24) & 0xff,
    minor: (packed >>> 16) & 0xff,
    patch: packed & 0xffff,
  };
}

export function checkAbiCompatibility(version, capabilities, paintCommandSize, options = {}) {
  const expected = {
    ...SUPPORTED_ABI,
    ...(options.expectedAbi ?? {}),
  };
  const parsed = parseAbiVersion(version);
  if (parsed.major !== expected.major) {
    throw new Error(
      `Incompatible AetherUI ABI major version ${parsed.major}; ` +
        `this binding requires ${expected.major}.${expected.minor}.x.`,
    );
  }
  if (parsed.minor < expected.minor) {
    throw new Error(
      `AetherUI ABI ${parsed.major}.${parsed.minor}.${parsed.patch} is older than ` +
        `the binding requirement ${expected.major}.${expected.minor}.0.`,
    );
  }

  const actualCapabilities = BigInt(capabilities ?? 0);
  const requiredCapabilities =
    options.requiredCapabilities == null
      ? DEFAULT_CAPABILITIES
      : BigInt(options.requiredCapabilities);
  const missing = requiredCapabilities & ~actualCapabilities;
  if (missing !== 0n) {
    throw new Error(
      `The loaded AetherUI library is missing required capability bits 0x${missing.toString(16)}.`,
    );
  }

  const size = Number(paintCommandSize);
  if (!Number.isSafeInteger(size) || size <= 0) {
    throw new Error(`The loaded AetherUI library returned an invalid paint command size: ${paintCommandSize}.`);
  }

  return Object.freeze({
    ...parsed,
    capabilities: actualCapabilities,
    paintCommandSize: size,
  });
}

export function validateComputedStyleSize(nativeSize, bindingSize) {
  const native = Number(nativeSize);
  const binding = Number(bindingSize);
  if (!Number.isSafeInteger(native) || native <= 0) {
    throw new Error(`The loaded AetherUI library returned an invalid computed style size: ${nativeSize}.`);
  }
  if (native !== binding) {
    throw new Error(
      `The loaded AetherUI library reports computed style size ${native}, ` +
      `but this binding requires exactly ${binding} bytes for its known ABI fields.`,
    );
  }
}

export function resolveNativeLibraryCandidates({
  platform = process.platform,
  arch = process.arch,
  packageRoot,
  libraryPath,
  installPrefix = process.env.AETHERUI_INSTALL_PREFIX,
} = {}) {
  if (libraryPath) return [path.resolve(libraryPath)];

  const filename =
    platform === "win32"
      ? "aether_core.dll"
      : platform === "darwin"
        ? "libaether_core.dylib"
        : "libaether_core.so";
  const candidates = [];
  if (installPrefix) {
    candidates.push(
      path.join(installPrefix, "bin", filename),
      path.join(installPrefix, "lib", filename),
    );
  }
  if (packageRoot) {
    candidates.push(
      path.join(packageRoot, "native", `${platform}-${arch}`, filename),
      path.join(packageRoot, "native", platform, arch, filename),
    );
  }
  candidates.push(filename, "aether_core");
  return [...new Set(candidates)];
}

export function loadKoffi(ffi) {
  if (ffi) return ffi;
  try {
    return createRequire(import.meta.url)("koffi");
  } catch (error) {
    throw new Error(
      "The AetherUI JavaScript binding requires the 'koffi' package. " +
        "Run npm install in bindings/javascript before loading the binding.",
      { cause: error },
    );
  }
}

export function loadNativeLibrary({ ffi, library, ...options }) {
  if (library) return library;
  const koffi = loadKoffi(ffi);
  const candidates = resolveNativeLibraryCandidates(options);
  const attempts = [];
  for (const candidate of candidates) {
    try {
      return { ffi: koffi, library: koffi.load(candidate), path: candidate };
    } catch (error) {
      attempts.push(`${candidate}: ${error?.message ?? error}`);
    }
  }
  throw new Error(
    "Unable to load the AetherUI native library. Set AETHERUI_INSTALL_PREFIX or " +
      "pass libraryPath to createAether().\n" +
      attempts.join("\n"),
  );
}

function createTypes(ffi) {
  const pointer = (name, target) => ffi.pointer(name, target ?? ffi.opaque());
  const voidPointer = ffi.pointer("void");

  const Point = ffi.struct("aether_pointf", { x: "float", y: "float" });
  const Size = ffi.struct("aether_sizef", { width: "float", height: "float" });
  const Rect = ffi.struct("aether_rectf", {
    x: "float",
    y: "float",
    width: "float",
    height: "float",
  });
  const Thickness = ffi.struct("aether_thicknessf", {
    left: "float",
    top: "float",
    right: "float",
    bottom: "float",
  });
  const Color = ffi.struct("aether_color_rgba", {
    r: "float",
    g: "float",
    b: "float",
    a: "float",
  });
  const ColorEdges = ffi.struct("aether_color_edges", {
    left: Color,
    top: Color,
    right: Color,
    bottom: Color,
  });
  const BorderStyleEdges = ffi.struct("aether_border_style_edges", {
    left: "int32_t",
    top: "int32_t",
    right: "int32_t",
    bottom: "int32_t",
  });
  const CornerRadii = ffi.struct("aether_corner_radii", {
    top_left_x: "float", top_left_y: "float",
    top_right_x: "float", top_right_y: "float",
    bottom_right_x: "float", bottom_right_y: "float",
    bottom_left_x: "float", bottom_left_y: "float",
  });
  const CssLength = ffi.struct("aether_css_length", {
    value: "float",
    unit: "int32_t",
    linear_percent: "float",
    linear_em: "float",
    linear_rem: "float",
    linear_vw: "float",
    linear_vh: "float",
  });
  const GradientStop = ffi.struct("aether_gradient_stop", {
    color: Color,
    position: CssLength,
    offset: "float",
    has_position: "int32_t",
  });
  const GradientStopPointer = ffi.pointer(GradientStop);
  const LinearGradient = ffi.struct("aether_linear_gradient", {
    direction_x: "float",
    direction_y: "float",
    stops: GradientStopPointer,
    stop_count: "int32_t",
  });
  const Shadow = ffi.struct("aether_shadow", {
    offset_x: "float",
    offset_y: "float",
    blur_radius: "float",
    spread_radius: "float",
    color: Color,
    inset: "int32_t",
  });
  const ShadowPointer = ffi.pointer(Shadow);
  const ShadowList = ffi.struct("aether_shadow_list", {
    items: ShadowPointer,
    count: "int32_t",
  });
  const FlexBasis = ffi.struct("aether_flex_basis", {
    kind: "int32_t",
    length: CssLength,
  });
  const CssEdges = ffi.struct("aether_css_edges", {
    left: CssLength,
    top: CssLength,
    right: CssLength,
    bottom: CssLength,
  });
  const CornerRadius = ffi.struct("aether_corner_radius", {
    horizontal: CssLength,
    vertical: CssLength,
  });
  const BorderRadii = ffi.struct("aether_border_radii", {
    top_left: CornerRadius,
    top_right: CornerRadius,
    bottom_right: CornerRadius,
    bottom_left: CornerRadius,
  });
  const BackgroundLayer = ffi.struct("aether_background_layer", {
    kind: "int32_t",
    gradient: LinearGradient,
    image_source: "str",
    position_x: CssLength,
    position_y: CssLength,
    size_kind: "int32_t",
    size_x: CssLength,
    size_y: CssLength,
    repeat_x: "int32_t",
    repeat_y: "int32_t",
  });
  const BackgroundLayerPointer = ffi.pointer(BackgroundLayer);
  const BackgroundLayerList = ffi.struct("aether_background_layer_list", {
    items: BackgroundLayerPointer,
    count: "int32_t",
  });
  const BackdropFilter = ffi.struct("aether_backdrop_filter", {
    kind: "int32_t",
    length_amount: CssLength,
    scalar_amount: "float",
  });
  const BackdropFilterPointer = ffi.pointer(BackdropFilter);
  const BackdropFilterList = ffi.struct("aether_backdrop_filter_list", {
    items: BackdropFilterPointer,
    count: "int32_t",
  });
  const TransformOperation = ffi.struct("aether_transform_operation", {
    kind: "int32_t",
    x: CssLength,
    y: CssLength,
    scalar_x: "float",
    scalar_y: "float",
    angle_radians: "float",
    matrix_a: "float",
    matrix_b: "float",
    matrix_c: "float",
    matrix_d: "float",
    matrix_e: "float",
    matrix_f: "float",
  });
  const TransformOperationPointer = ffi.pointer(TransformOperation);
  const TransformList = ffi.struct("aether_transform_list", {
    items: TransformOperationPointer,
    count: "int32_t",
  });
  const TransformOrigin = ffi.struct("aether_transform_origin", { x: CssLength, y: CssLength });
  const StyleOptions = ffi.struct("aether_style_runtime_options", {
    throw_on_invalid: "int32_t",
    enforce_thread_affinity: "int32_t",
    max_variable_depth: "int32_t",
    max_variable_expansion_length: "int32_t",
  });
  const StyleEnvironment = ffi.struct("aether_style_environment", {
    viewport_width: "float",
    viewport_height: "float",
    dpi_scale: "float",
    root_font_size: "float",
    color_scheme: "int32_t",
    pointer_accuracy: "int32_t",
    motion_preference: "int32_t",
  });
  const ComputedStyle = ffi.struct("aether_computed_style", {
    display: "int32_t",
    flex_direction: "int32_t",
    justify_content: "int32_t",
    align_items: "int32_t",
    align_self: "int32_t",
    flex_grow: "float",
    flex_shrink: "float",
    flex_basis: FlexBasis,
    flex_wrap: "int32_t",
    gap: "float",
    position: "int32_t",
    inset: CssEdges,
    width: CssLength,
    height: CssLength,
    min_width: CssLength,
    min_height: CssLength,
    max_width: CssLength,
    max_height: CssLength,
    margin: CssEdges,
    padding: CssEdges,
    border_width: CssEdges,
    box_sizing: "int32_t",
    color: Color,
    background_color: Color,
    background_layers: BackgroundLayerList,
    backdrop_filters: BackdropFilterList,
    border_color: Color,
    border_colors: ColorEdges,
    border_styles: BorderStyleEdges,
    has_text_gradient: "int32_t",
    text_gradient: LinearGradient,
    transforms: TransformList,
    transform_origin: TransformOrigin,
    text_shadows: ShadowList,
    box_shadows: ShadowList,
    border_radii: BorderRadii,
    opacity: "float",
    font_family: ffi.array("char", 64, "String"),
    font_size: "float",
    font_weight: "int32_t",
    line_height: "float",
    text_align: "int32_t",
    text_transform: "int32_t",
    overflow_x: "int32_t",
    overflow_y: "int32_t",
    cursor: "int32_t",
    pointer_events: "int32_t",
    visible: "int32_t",
  });
  const TextMetrics = ffi.struct("aether_text_metrics", {
    width: "float",
    height: "float",
    baseline: "float",
  });
  const ScrollOffsetValue = ffi.struct("aether_scroll_offset", {
    x: "float",
    y: "float",
  });
  const ScrollMetrics = ffi.struct("aether_scroll_metrics", {
    scrollable_overflow_x: "float",
    scrollable_overflow_y: "float",
    scroll_offset_x: "float",
    scroll_offset_y: "float",
  });
  const ScrollbarGeometry = ffi.struct("aether_scrollbar_geometry", {
    horizontal_visible: "int32_t",
    vertical_visible: "int32_t",
    horizontal_track: Rect,
    horizontal_thumb: Rect,
    vertical_track: Rect,
    vertical_thumb: Rect,
    corner: Rect,
  });

  const Doc = pointer("aether_doc_t");
  const Node = pointer("aether_node_t");
  const Stylesheet = pointer("aether_stylesheet_t");
  const StyleMap = pointer("aether_style_map_t");
  const ComputedStyleHandle = pointer("aether_computed_style_handle_t");
  const LayoutEngine = pointer("aether_layout_engine_t");
  const LayoutResult = pointer("aether_layout_result_t");
  const LayoutBox = pointer("aether_layout_box_t");
  const DisplayList = pointer("aether_display_list_t");
  const DisplayListBuilder = pointer("aether_display_list_builder_t");
  const StyleRuntime = pointer("aether_style_runtime_t");
  const StyleUpdate = pointer("aether_style_update_t");

  const TextMeasurer = ffi.proto(TextMetrics, ["str", ffi.pointer(ComputedStyle), "float", voidPointer]);
  const TextMeasurerPointer = ffi.pointer(TextMeasurer);
  const ScrollOffset = ffi.proto(ScrollOffsetValue, [Node, voidPointer]);
  const ScrollOffsetPointer = ffi.pointer(ScrollOffset);
  const StyleDiagnostic = ffi.proto("void", ["str", "str", "str", "str", voidPointer]);
  const StyleDiagnosticPointer = ffi.pointer(StyleDiagnostic);

  const FillRoundedRect = ffi.struct("aether_paint_fill_rounded_rect", {
    rect: Rect,
    radii: CornerRadii,
    color: Color,
  });
  const DrawBoxShadow = ffi.struct("aether_paint_draw_box_shadow", {
    rect: Rect,
    radii: CornerRadii,
    shadow: Shadow,
  });
  const FillRoundedRectGradient = ffi.struct("aether_paint_fill_rounded_rect_gradient", {
    rect: Rect,
    radii: CornerRadii,
    gradient_rect: Rect,
    gradient: LinearGradient,
    opacity: "float",
  });
  const StrokeRoundedRect = ffi.struct("aether_paint_stroke_rounded_rect", {
    rect: Rect,
    radii: CornerRadii,
    stroke_width: "float",
    color: Color,
  });
  const DrawText = ffi.struct("aether_paint_draw_text", {
    text: ffi.pointer("char"),
    rect: Rect,
    font_family: ffi.array("char", 64, "String"),
    font_size: "float",
    font_weight: "int32_t",
    text_align: "int32_t",
    color: Color,
    wrap: "int32_t",
  });
  const DrawTextGradient = ffi.struct("aether_paint_draw_text_gradient", {
    text: ffi.pointer("char"),
    rect: Rect,
    font_family: ffi.array("char", 64, "String"),
    font_size: "float",
    font_weight: "int32_t",
    text_align: "int32_t",
    gradient: LinearGradient,
    opacity: "float",
  });
  const DrawTextShadow = ffi.struct("aether_paint_draw_text_shadow", {
    text: ffi.pointer("char"),
    rect: Rect,
    font_family: ffi.array("char", 64, "String"),
    font_size: "float",
    font_weight: "int32_t",
    text_align: "int32_t",
    shadow: Shadow,
    wrap: "int32_t",
  });
  const PushClip = ffi.struct("aether_paint_push_clip", { rect: Rect, radii: CornerRadii });
  const TransformMatrix = ffi.struct("aether_transform_matrix", {
    m11: "float", m12: "float", m21: "float", m22: "float", dx: "float", dy: "float",
  });
  const PushTransform = ffi.struct("aether_paint_push_transform", { matrix: TransformMatrix });
  const FillBorderSide = ffi.struct("aether_paint_fill_border_side", {
    outer_rect: Rect,
    outer_radii: CornerRadii,
    inner_rect: Rect,
    inner_radii: CornerRadii,
    side: "int32_t",
    style: "int32_t",
    color: Color,
  });
  const DrawBackgroundImage = ffi.struct("aether_paint_draw_background_image", {
    source: ffi.pointer("char"),
    paint_rect: Rect,
    position_x: CssLength,
    position_y: CssLength,
    size_kind: "int32_t",
    size_x: CssLength,
    size_y: CssLength,
    repeat_x: "int32_t",
    repeat_y: "int32_t",
    opacity: "float",
    source_rect: Rect,
    destination_rect: Rect,
    has_resolved_geometry: "int32_t",
  });
  const ApplyBackdropFilter = ffi.struct("aether_paint_apply_backdrop_filter", {
    rect: Rect,
    radii: CornerRadii,
    filters: BackdropFilterList,
  });
  const PaintData = ffi.union("aether_paint_data", {
    fill_rounded_rect: FillRoundedRect,
    draw_box_shadow: DrawBoxShadow,
    fill_rounded_rect_gradient: FillRoundedRectGradient,
    stroke_rounded_rect: StrokeRoundedRect,
    draw_text: DrawText,
    draw_text_gradient: DrawTextGradient,
    draw_text_shadow: DrawTextShadow,
    push_clip: PushClip,
    fill_border_side: FillBorderSide,
    draw_background_image: DrawBackgroundImage,
    apply_backdrop_filter: ApplyBackdropFilter,
    push_transform: PushTransform,
  });
  const PaintCommand = ffi.struct("aether_paint_command", {
    tag: "int32_t",
    data: PaintData,
  });

  return {
    Point,
    Size,
    Rect,
    Thickness,
    Color,
    GradientStop,
    LinearGradient,
    Shadow,
    ShadowList,
    CssLength,
    CssEdges,
    ColorEdges,
    BorderStyleEdges,
    CornerRadii,
    CornerRadius,
    BorderRadii,
    BackgroundLayer,
    BackgroundLayerList,
    BackdropFilter,
    TransformOperation,
    BackdropFilterList,
    StyleOptions,
    StyleEnvironment,
    ComputedStyle,
    TextMetrics,
    ScrollOffsetValue,
    ScrollMetrics,
    ScrollbarGeometry,
    Doc,
    Node,
    Stylesheet,
    StyleMap,
    ComputedStyleHandle,
    LayoutEngine,
    LayoutResult,
    LayoutBox,
    DisplayList,
    DisplayListBuilder,
    StyleRuntime,
    StyleUpdate,
    TextMeasurer,
    TextMeasurerPointer,
    ScrollOffset,
    ScrollOffsetPointer,
    StyleDiagnostic,
    StyleDiagnosticPointer,
    PaintCommand,
    PaintData,
    FillRoundedRect,
    DrawBoxShadow,
    FillRoundedRectGradient,
    StrokeRoundedRect,
    DrawText,
    DrawTextGradient,
    DrawTextShadow,
    PushClip,
    PushTransform,
    FillBorderSide,
    DrawBackgroundImage,
    ApplyBackdropFilter,
    PaintDataOffset: ffi.offsetof(PaintCommand, "data"),
    VoidPointer: voidPointer,
    CharPointer: ffi.pointer("char"),
    DocOut: ffi.out(ffi.pointer(Doc)),
    StylesheetOut: ffi.out(ffi.pointer(Stylesheet)),
    StyleMapOut: ffi.out(ffi.pointer(StyleMap)),
    ComputedStyleHandleOut: ffi.out(ffi.pointer(ComputedStyleHandle)),
    LayoutEngineOut: ffi.out(ffi.pointer(LayoutEngine)),
    LayoutResultOut: ffi.out(ffi.pointer(LayoutResult)),
    DisplayListOut: ffi.out(ffi.pointer(DisplayList)),
    DisplayListBuilderOut: ffi.out(ffi.pointer(DisplayListBuilder)),
    StyleRuntimeOut: ffi.out(ffi.pointer(StyleRuntime)),
    StyleUpdateArrayPointer: ffi.pointer(StyleUpdate),
    StyleUpdateArrayOut: ffi.out(ffi.pointer(ffi.pointer(StyleUpdate))),
  };
}

export function createNativeApi({ ffi, library }) {
  const types = createTypes(ffi);
  const f = (name, result, args) => library.func(name, result, args);
  const functions = {
    abiVersion: f("aether_abi_version", "uint32_t", []),
    abiCapabilities: f("aether_abi_capabilities", "uint64_t", []),
    paintCommandSize: f("aether_paint_command_size", "size_t", []),
    computedStyleSize: f("aether_computed_style_size", "size_t", []),
    statusMessage: f("aether_status_message", "str", ["int32_t"]),
    stringFree: f("aether_string_free", "void", [types.CharPointer]),

    docCreate: f("aether_doc_create", "int32_t", [types.DocOut]),
    docDestroy: f("aether_doc_destroy", "void", [types.Doc]),
    docParseHtml: f("aether_doc_parse_html", "int32_t", [types.Doc, "str"]),
    docGetVersion: f("aether_doc_get_version", "int64_t", [types.Doc]),
    docGetBody: f("aether_doc_get_body", types.Node, [types.Doc]),
    docGetDocumentElement: f("aether_doc_get_document_element", types.Node, [types.Doc]),
    docGetElementById: f("aether_doc_get_element_by_id", types.Node, [types.Doc, "str"]),
    nodeGetType: f("aether_node_get_type", "int32_t", [types.Node]),
    nodeGetParent: f("aether_node_get_parent", types.Node, [types.Node]),
    nodeGetChildCount: f("aether_node_get_child_count", "int32_t", [types.Node]),
    nodeGetChild: f("aether_node_get_child", types.Node, [types.Node, "int32_t"]),
    nodeGetTextContent: f("aether_node_get_text_content", types.CharPointer, [types.Node]),
    elementGetTagName: f("aether_element_get_tag_name", types.CharPointer, [types.Node]),
    elementGetAttribute: f("aether_element_get_attribute", types.CharPointer, [types.Node, "str"]),
    elementGetAttributeCount: f("aether_element_get_attribute_count", "int32_t", [types.Node]),
    elementGetAttributeName: f("aether_element_get_attribute_name", types.CharPointer, [types.Node, "int32_t"]),
    elementGetAttributeValue: f("aether_element_get_attribute_value", types.CharPointer, [types.Node, "int32_t"]),
    elementSetAttribute: f("aether_element_set_attribute", "int32_t", [types.Node, "str", "str"]),
    elementHasAttribute: f("aether_element_has_attribute", "int32_t", [types.Node, "str"]),
    elementHasClass: f("aether_element_has_class", "int32_t", [types.Node, "str"]),
    elementIsRequired: f("aether_element_is_required", "int32_t", [types.Node]),
    elementIsDisabled: f("aether_element_is_disabled", "int32_t", [types.Node]),
    elementIsFocusable: f("aether_element_is_focusable", "int32_t", [types.Node]),
    elementGetState: f("aether_element_get_state", "int32_t", [types.Node]),
    elementSetState: f("aether_element_set_state", "void", [types.Node, "int32_t"]),
    nodeAppendChild: f("aether_node_append_child", "int32_t", [types.Node, types.Node]),
    nodeRemoveChild: f("aether_node_remove_child", "int32_t", [types.Node, types.Node]),

    cssParseStylesheet: f("aether_css_parse_stylesheet", "int32_t", ["str", types.StylesheetOut]),
    cssStylesheetDestroy: f("aether_css_stylesheet_destroy", "void", [types.Stylesheet]),
    cssResolve: f("aether_css_resolve", "int32_t", [types.Doc, types.Stylesheet, "float", "float", types.StyleMapOut]),
    styleMapDestroy: f("aether_style_map_destroy", "void", [types.StyleMap]),
    styleMapGet: f("aether_style_map_get", types.ComputedStyleHandle, [types.StyleMap, types.Node]),
    computedStyleCopy: f("aether_computed_style_copy", "int32_t", [types.ComputedStyleHandle, ffi.out(ffi.pointer(types.ComputedStyle))]),
    computedStyleHandleDestroy: f("aether_computed_style_handle_destroy", "void", [types.ComputedStyleHandle]),
    computedStyleFree: f("aether_computed_style_free", "void", [ffi.inout(ffi.pointer(types.ComputedStyle))]),

    layoutCreate: f("aether_layout_create", "int32_t", [types.TextMeasurerPointer, types.VoidPointer, types.LayoutEngineOut]),
    layoutDestroy: f("aether_layout_destroy", "void", [types.LayoutEngine]),
    layoutLayout: f("aether_layout_layout", "int32_t", [types.LayoutEngine, types.Doc, types.StyleMap, "float", "float", types.ScrollOffsetPointer, types.VoidPointer, types.LayoutResultOut]),
    layoutResultDestroy: f("aether_layout_result_destroy", "void", [types.LayoutResult]),
    layoutResultGetRoot: f("aether_layout_result_get_root", types.LayoutBox, [types.LayoutResult]),
    layoutResultFindPrimaryBox: f("aether_layout_result_find_primary_box", types.LayoutBox, [types.LayoutResult, types.Node]),
    layoutResultHitTest: f("aether_layout_result_hit_test", types.Node, [types.LayoutResult, "float", "float"]),
    layoutResultGetPaintOrderCount: f("aether_layout_result_get_paint_order_count", "int32_t", [types.LayoutResult]),
    layoutResultGetPaintOrderBox: f("aether_layout_result_get_paint_order_box", types.LayoutBox, [types.LayoutResult, "int32_t"]),
    layoutBoxGetKind: f("aether_layout_box_get_kind", "int32_t", [types.LayoutBox]),
    layoutBoxGetX: f("aether_layout_box_get_x", "float", [types.LayoutBox]),
    layoutBoxGetY: f("aether_layout_box_get_y", "float", [types.LayoutBox]),
    layoutBoxGetWidth: f("aether_layout_box_get_width", "float", [types.LayoutBox]),
    layoutBoxGetHeight: f("aether_layout_box_get_height", "float", [types.LayoutBox]),
    layoutBoxGetMarginBox: f("aether_layout_box_get_margin_box", types.Rect, [types.LayoutBox]),
    layoutBoxGetBorderBox: f("aether_layout_box_get_border_box", types.Rect, [types.LayoutBox]),
    layoutBoxGetPaddingBox: f("aether_layout_box_get_padding_box", types.Rect, [types.LayoutBox]),
    layoutBoxGetContentBox: f("aether_layout_box_get_content_box", types.Rect, [types.LayoutBox]),
    layoutBoxIsAtomic: f("aether_layout_box_is_atomic", "int32_t", [types.LayoutBox]),
    layoutBoxGetText: f("aether_layout_box_get_text", types.CharPointer, [types.LayoutBox]),
    layoutBoxGetScrollMetrics: f("aether_layout_box_get_scroll_metrics", types.ScrollMetrics, [types.LayoutBox]),
    layoutBoxGetScrollbarGeometry: f("aether_layout_box_get_scrollbar_geometry", types.ScrollbarGeometry, [types.LayoutBox]),
    layoutBoxGetScrollableOverflowY: f("aether_layout_box_get_scrollable_overflow_y", "float", [types.LayoutBox]),
    layoutBoxGetScrollOffsetY: f("aether_layout_box_get_scroll_offset_y", "float", [types.LayoutBox]),
    layoutBoxGetElement: f("aether_layout_box_get_element", types.Node, [types.LayoutBox]),
    layoutBoxGetChildCount: f("aether_layout_box_get_child_count", "int32_t", [types.LayoutBox]),
    layoutBoxGetChild: f("aether_layout_box_get_child", types.LayoutBox, [types.LayoutBox, "int32_t"]),

    displayListBuilderCreate: f("aether_display_list_builder_create", "int32_t", [types.TextMeasurerPointer, types.VoidPointer, types.DisplayListBuilderOut]),
    displayListBuilderDestroy: f("aether_display_list_builder_destroy", "void", [types.DisplayListBuilder]),
    displayListBuild: f("aether_display_list_build", "int32_t", [types.DisplayListBuilder, types.LayoutResult, types.Node, types.DisplayListOut]),
    displayListDestroy: f("aether_display_list_destroy", "void", [types.DisplayList]),
    displayListGetCount: f("aether_display_list_get_count", "int32_t", [types.DisplayList]),
    displayListGetCommand: f("aether_display_list_get_command", "int32_t", [types.DisplayList, "int32_t", types.VoidPointer]),
    paintCommandFree: f("aether_paint_command_free", "void", [types.VoidPointer]),

    styleRuntimeCreate: f("aether_style_runtime_create", "int32_t", [ffi.pointer(types.StyleOptions), ffi.pointer(types.StyleEnvironment), types.StyleDiagnosticPointer, types.VoidPointer, types.StyleRuntimeOut]),
    styleRuntimeSetDiagnosticsCallback: f("aether_style_runtime_set_diagnostics_callback", "void", [types.StyleRuntime, types.StyleDiagnosticPointer, types.VoidPointer]),
    styleRuntimeDestroy: f("aether_style_runtime_destroy", "void", [types.StyleRuntime]),
    styleRuntimeAddStylesheet: f("aether_style_runtime_add_stylesheet", "int32_t", [types.StyleRuntime, "str", "int32_t", "str", ffi.out(ffi.pointer("int64_t")), ffi.out(ffi.pointer("int32_t"))]),
    styleRuntimeRemoveStylesheet: f("aether_style_runtime_remove_stylesheet", "int32_t", [types.StyleRuntime, "int64_t"]),
    styleRuntimeResolveSubtree: f("aether_style_runtime_resolve_subtree", "int32_t", [types.StyleRuntime, types.Node, types.StyleUpdateArrayOut]),
    styleRuntimeGetUpdate: f("aether_style_runtime_get_update", "int32_t", [types.StyleUpdate, ffi.out(ffi.pointer(types.Node)), types.ComputedStyleHandleOut, ffi.out(ffi.pointer("uint32_t"))]),
    styleUpdatesFree: f("aether_style_updates_free", "void", [types.StyleUpdateArrayPointer, "int32_t"]),
    styleRuntimeSetEnvironment: f("aether_style_runtime_set_environment", "int32_t", [types.StyleRuntime, ffi.pointer(types.StyleEnvironment), types.Node, types.StyleUpdateArrayOut]),
    styleRuntimeCreateResolvedStyleMap: f("aether_style_runtime_create_resolved_style_map", "int32_t", [types.StyleRuntime, types.Doc, types.StyleMapOut]),
    styleRuntimeClearCache: f("aether_style_runtime_clear_cache", "void", [types.StyleRuntime]),
  };

  return { ffi, library, types, functions };
}

export function createLoadedNativeApi(options = {}) {
  const loaded = loadNativeLibrary(options);
  const ffi = loaded.ffi ?? options.ffi ?? loadKoffi(options.ffi);
  const library = loaded.library ?? options.library ?? loaded;
  return { ...createNativeApi({ ffi, library }), libraryPath: loaded.path };
}

export function nativeLibraryExists(candidate) {
  return existsSync(candidate);
}
