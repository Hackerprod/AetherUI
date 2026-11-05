using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void StyleDiagnosticCallback(
    IntPtr code,
    IntPtr property,
    IntPtr value,
    IntPtr message,
    IntPtr userData);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeTextMetrics(float Width, float Height, float Baseline);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeRect(float X, float Y, float Width, float Height);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeScrollOffset(float X, float Y);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeScrollMetrics(
    float ScrollableOverflowX,
    float ScrollableOverflowY,
    float ScrollOffsetX,
    float ScrollOffsetY);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeScrollbarGeometry(
    int HorizontalVisible,
    int VerticalVisible,
    NativeRect HorizontalTrack,
    NativeRect HorizontalThumb,
    NativeRect VerticalTrack,
    NativeRect VerticalThumb,
    NativeRect Corner);

/// <summary>Blittable aether_transform_matrix (row-vector affine: x' = x*m11 + y*m21 + dx).</summary>
[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeTransformMatrix(
    float M11, float M12,
    float M21, float M22,
    float Dx, float Dy);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeRenderPlanInput(
    uint Invalidation,
    int MarkupDirty,
    int HasPendingPointerMove,
    int DisplayListAvailable,
    int DisplayListContainsPatchedChunks,
    int RendererSupportsIncremental);

[StructLayout(LayoutKind.Sequential)]
public readonly record struct NativeRenderPlan(
    uint NormalizedInvalidation,
    int RequiresStyle,
    int RequiresLayout,
    int RequiresDisplayList,
    int RequiresPaintChunks,
    int PointerOnlyRender,
    int ScrollOnlyRender,
    int AllowIncrementalRender,
    int UseScrollComposition);

internal static unsafe partial class NativeMethods
{
    private const string Lib = "aether_core";

    [LibraryImport(Lib, EntryPoint = "aether_abi_version")]
    internal static partial uint AbiVersion();

    [LibraryImport(Lib, EntryPoint = "aether_abi_capabilities")]
    internal static partial ulong AbiCapabilities();

    [LibraryImport(Lib, EntryPoint = "aether_computed_style_size")]
    internal static partial nuint ComputedStyleSize();

    [LibraryImport(Lib, EntryPoint = "aether_paint_command_size")]
    internal static partial nuint PaintCommandSize();

    [LibraryImport(Lib, EntryPoint = "aether_visual_invalidation_from_style_impact")]
    internal static partial uint VisualInvalidationFromStyleImpact(uint impact);

    [LibraryImport(Lib, EntryPoint = "aether_visual_invalidation_for_scroll")]
    internal static partial uint VisualInvalidationForScroll(int displayListContainsPatchedChunks);

    [LibraryImport(Lib, EntryPoint = "aether_render_plan_evaluate")]
    internal static partial int RenderPlanEvaluate(in NativeRenderPlanInput input, out NativeRenderPlan plan);

    // ── Status & Lifecycle ──────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_status_message")]
    internal static partial IntPtr GetStatusMessage(int status);

    [LibraryImport(Lib, EntryPoint = "aether_string_free")]
    internal static partial void StringFree(IntPtr str);

    // ── Document ────────────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_doc_create")]
    internal static partial int DocCreate(out IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_doc_destroy")]
    internal static partial void DocDestroy(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_doc_parse_html")]
    internal static partial int DocParseHtml(IntPtr handle, byte* html);

    [LibraryImport(Lib, EntryPoint = "aether_doc_get_version")]
    internal static partial long DocGetVersion(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_doc_get_body")]
    internal static partial IntPtr DocGetBody(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_doc_get_document_element")]
    internal static partial IntPtr DocGetDocumentElement(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_doc_get_element_by_id")]
    internal static partial IntPtr DocGetElementById(IntPtr handle, byte* id);

    // ── Node ────────────────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_node_get_type")]
    internal static partial int NodeTypeGet(IntPtr node);

    [LibraryImport(Lib, EntryPoint = "aether_node_get_parent")]
    internal static partial IntPtr NodeGetParent(IntPtr node);

    [LibraryImport(Lib, EntryPoint = "aether_node_get_child_count")]
    internal static partial int NodeGetChildCount(IntPtr node);

    [LibraryImport(Lib, EntryPoint = "aether_node_get_child")]
    internal static partial IntPtr NodeGetChild(IntPtr node, int index);

    [LibraryImport(Lib, EntryPoint = "aether_node_get_text_content")]
    internal static partial IntPtr NodeGetTextContent(IntPtr node);
    [LibraryImport(Lib, EntryPoint = "aether_node_get_style_node_id")]
    internal static partial long NodeGetStyleNodeId(IntPtr node);

    // ── Element ─────────────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_element_get_tag_name")]
    internal static partial IntPtr ElementGetTagName(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_get_attribute")]
    internal static partial IntPtr ElementGetAttribute(IntPtr element, byte* name);

    [LibraryImport(Lib, EntryPoint = "aether_element_get_attribute_count")]
    internal static partial int ElementGetAttributeCount(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_get_attribute_name")]
    internal static partial IntPtr ElementGetAttributeName(IntPtr element, int index);

    [LibraryImport(Lib, EntryPoint = "aether_element_get_attribute_value")]
    internal static partial IntPtr ElementGetAttributeValue(IntPtr element, int index);

    [LibraryImport(Lib, EntryPoint = "aether_element_set_attribute")]
    internal static partial int ElementSetAttribute(IntPtr element, byte* name, byte* value);

    [LibraryImport(Lib, EntryPoint = "aether_element_has_attribute")]
    internal static partial int ElementHasAttribute(IntPtr element, byte* name);

    [LibraryImport(Lib, EntryPoint = "aether_element_has_class")]
    internal static partial int ElementHasClass(IntPtr element, byte* className);

    [LibraryImport(Lib, EntryPoint = "aether_element_is_required")]
    internal static partial int ElementIsRequired(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_is_disabled")]
    internal static partial int ElementIsDisabled(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_is_focusable")]
    internal static partial int ElementIsFocusable(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_get_state")]
    internal static partial int ElementGetState(IntPtr element);

    [LibraryImport(Lib, EntryPoint = "aether_element_set_state")]
    internal static partial void ElementSetState(IntPtr element, int state);

    // ── Tree Mutation ───────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_node_append_child")]
    internal static partial int NodeAppendChild(IntPtr parent, IntPtr child);

    [LibraryImport(Lib, EntryPoint = "aether_node_remove_child")]
    internal static partial int NodeRemoveChild(IntPtr parent, IntPtr child);

    [LibraryImport(Lib, EntryPoint = "aether_layout_create")]
    internal static partial int LayoutCreate(IntPtr measurer, IntPtr data, out IntPtr engine);

    [LibraryImport(Lib, EntryPoint = "aether_layout_destroy")]
    internal static partial void LayoutDestroy(IntPtr engine);

    [LibraryImport(Lib, EntryPoint = "aether_layout_prepare_transition_context")]
    internal static partial int LayoutPrepareTransitionContext(IntPtr engine, IntPtr runtime,
        IntPtr styles, IntPtr previousLayout, out int changed);

    [LibraryImport(Lib, EntryPoint = "aether_layout_layout")]
    internal static partial int LayoutLayout(
        IntPtr engine, IntPtr document, IntPtr styles, float width, float height,
        IntPtr scrollCallback, IntPtr scrollData, out IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_destroy")]
    internal static partial void LayoutResultDestroy(IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_get_root")]
    internal static partial IntPtr LayoutResultGetRoot(IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_get_paint_order_count")]
    internal static partial int LayoutResultGetPaintOrderCount(IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_get_paint_order_box")]
    internal static partial IntPtr LayoutResultGetPaintOrderBox(IntPtr result, int index);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_get_viewport_overflow_x")]
    internal static partial int LayoutResultGetViewportOverflowX(IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_get_viewport_overflow_y")]
    internal static partial int LayoutResultGetViewportOverflowY(IntPtr result);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_hit_test_with_scroll_offsets")]
    internal static partial IntPtr LayoutResultHitTestWithScrollOffsets(
        IntPtr result, float x, float y, IntPtr scrollCallback, IntPtr scrollData);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_hit_test_with_styles_and_scroll_offsets")]
    internal static partial IntPtr LayoutResultHitTestWithStylesAndScrollOffsets(
        IntPtr result, IntPtr styles, float x, float y, IntPtr scrollCallback, IntPtr scrollData);

    [LibraryImport(Lib, EntryPoint = "aether_layout_result_hit_test_input")]
    [UnmanagedCallConv(CallConvs = [typeof(System.Runtime.CompilerServices.CallConvCdecl)])]
    internal static partial int LayoutResultHitTestInput(IntPtr result, IntPtr currentStyles,
        float x, float y, IntPtr scrollCallback, IntPtr scrollData, out NativeInputHitPart part, out IntPtr owner);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_kind")]
    internal static partial int LayoutBoxGetKind(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_margin_box")]
    internal static partial NativeRect LayoutBoxGetMarginBox(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_border_box")]
    internal static partial NativeRect LayoutBoxGetBorderBox(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_padding_box")]
    internal static partial NativeRect LayoutBoxGetPaddingBox(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_content_box")]
    internal static partial NativeRect LayoutBoxGetContentBox(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_is_atomic")]
    internal static partial int LayoutBoxIsAtomic(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_text")]
    internal static partial IntPtr LayoutBoxGetText(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scroll_metrics")]
    internal static partial NativeScrollMetrics LayoutBoxGetScrollMetrics(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scrollbar_geometry")]
    internal static partial NativeScrollbarGeometry LayoutBoxGetScrollbarGeometry(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scrollbar_geometry_at_offset")]
    internal static partial NativeScrollbarGeometry LayoutBoxGetScrollbarGeometryAtOffset(
        IntPtr box, NativeScrollOffset offset);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scrollable_overflow_x")]
    internal static partial float LayoutBoxGetScrollableOverflowX(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scrollable_overflow_y")]
    internal static partial float LayoutBoxGetScrollableOverflowY(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scroll_offset_x")]
    internal static partial float LayoutBoxGetScrollOffsetX(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scroll_offset_y")]
    internal static partial float LayoutBoxGetScrollOffsetY(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_element")]
    internal static partial IntPtr LayoutBoxGetElement(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_child_count")]
    internal static partial int LayoutBoxGetChildCount(IntPtr box);

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_child")]
    internal static partial IntPtr LayoutBoxGetChild(IntPtr box, int index);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_builder_create")]
    internal static partial int DisplayListBuilderCreate(IntPtr measurer, IntPtr data, out IntPtr builder);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_builder_destroy")]
    internal static partial void DisplayListBuilderDestroy(IntPtr builder);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_builder_set_image_dimensions_callback")]
    internal static partial void DisplayListBuilderSetImageDimensionsCallback(IntPtr builder, IntPtr callback, IntPtr data);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_build")]
    internal static partial int DisplayListBuild(IntPtr builder, IntPtr layout, IntPtr focusedElement, out IntPtr displayList);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_build_with_styles")]
    internal static partial int DisplayListBuildWithStyles(
        IntPtr builder, IntPtr layout, IntPtr currentStyles, IntPtr focusedElement, out IntPtr displayList);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_build_node_with_styles")]
    internal static partial int DisplayListBuildNodeWithStyles(
        IntPtr builder, IntPtr layout, IntPtr currentStyles, IntPtr element, IntPtr focusedElement, out IntPtr displayList);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_destroy")]
    internal static partial void DisplayListDestroy(IntPtr displayList);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_get_count")]
    internal static partial int DisplayListGetCount(IntPtr displayList);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_get_command")]
    internal static partial int DisplayListGetCommand(IntPtr displayList, int index, IntPtr command);

    [LibraryImport(Lib, EntryPoint = "aether_paint_command_free")]
    internal static partial void PaintCommandFree(IntPtr command);

    // ── StyleRuntime ───────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_create")]
    internal static partial int StyleRuntimeCreate(
        IntPtr options,
        IntPtr environment,
        StyleDiagnosticCallback? diagnosticsCallback,
        IntPtr diagnosticsUserData,
        out IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_destroy")]
    internal static partial void StyleRuntimeDestroy(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_defaults_css")]
    internal static partial IntPtr StyleRuntimeDefaultsCss();

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_set_diagnostics_callback")]
    internal static partial void StyleRuntimeSetDiagnosticsCallback(
        IntPtr handle,
        StyleDiagnosticCallback? callback,
        IntPtr userData);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_add_stylesheet")]
    internal static partial int StyleRuntimeAddStyleSheet(
        IntPtr handle, byte* source, int layer, byte* sourceName,
        out long sheetHandle, out int ruleCount);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_remove_stylesheet")]
    internal static partial int StyleRuntimeRemoveStyleSheet(IntPtr handle, long sheetHandle);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_resolve_subtree")]
    internal static partial int StyleRuntimeResolveSubtree(
        IntPtr handle, IntPtr root, out IntPtr updates);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_resolve_state_changes")]
    internal static partial int StyleRuntimeResolveStateChanges(
        IntPtr handle, IntPtr root, IntPtr changedNodes, int changedCount, out IntPtr updates);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_get_update")]
    internal static partial int StyleRuntimeGetUpdate(
        IntPtr update, out IntPtr node, out IntPtr current, out uint impact);

    [LibraryImport(Lib, EntryPoint = "aether_style_updates_free")]
    internal static partial void StyleUpdatesFree(IntPtr updates, int count);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_set_environment")]
    internal static partial int StyleRuntimeSetEnvironment(
        IntPtr handle, IntPtr environment, IntPtr root, out IntPtr updates);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_create_resolved_style_map")]
    internal static partial int StyleRuntimeCreateResolvedStyleMap(IntPtr runtime, IntPtr document, out IntPtr styleMap);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_update_resolved_style_map")]
    internal static partial int StyleRuntimeUpdateResolvedStyleMap(
        IntPtr runtime, IntPtr styleMap, IntPtr nodes, int nodeCount);

    [LibraryImport(Lib, EntryPoint = "aether_style_map_destroy")]
    internal static partial void StyleMapDestroy(IntPtr styleMap);

    [LibraryImport(Lib, EntryPoint = "aether_style_runtime_clear_cache")]
    internal static partial void StyleRuntimeClearCache(IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_computed_style_copy")]
    internal static partial int ComputedStyleCopy(IntPtr handle, IntPtr destination);

    [LibraryImport(Lib, EntryPoint = "aether_computed_style_free")]
    internal static partial void ComputedStyleFree(IntPtr style);

    [LibraryImport(Lib, EntryPoint = "aether_computed_style_handle_destroy")]
    internal static partial void ComputedStyleHandleDestroy(IntPtr handle);

    // ── Transitions ─────────────────────────────────────────────────
    // Entry points for the native CSS transition runtime. The host clock and
    // ticker wiring arrive in the next slice; these signatures are kept exact
    // against transition_runtime.h.

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_create")]
    internal static partial int TransitionRuntimeCreate(out IntPtr handle);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_destroy")]
    internal static partial void TransitionRuntimeDestroy(IntPtr runtime);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_observe")]
    internal static partial int TransitionRuntimeObserve(
        IntPtr runtime, IntPtr node, IntPtr before, IntPtr after, long nowMs);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_tick")]
    internal static partial int TransitionRuntimeTick(IntPtr runtime, long nowMs);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_has_active")]
    internal static partial int TransitionRuntimeHasActive(IntPtr runtime);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_opacity_override")]
    internal static partial int TransitionRuntimeOpacityOverride(
        IntPtr runtime, IntPtr node, out float opacity);

    [LibraryImport(Lib, EntryPoint = "aether_transition_runtime_transform_override")]
    internal static partial int TransitionRuntimeTransformOverride(
        IntPtr runtime, IntPtr node, out NativeTransformMatrix matrix);

    [LibraryImport(Lib, EntryPoint = "aether_display_list_builder_set_transition_runtime")]
    internal static partial void DisplayListBuilderSetTransitionRuntime(IntPtr builder, IntPtr runtime);

    // ── Images ──────────────────────────────────────────────────────

    [LibraryImport(Lib, EntryPoint = "aether_image_decode")]
    internal static partial int ImageDecode(byte* bytes, nuint byteCount, out int width, out int height, out byte* pixels);

    [LibraryImport(Lib, EntryPoint = "aether_image_pixels_free")]
    internal static partial void ImagePixelsFree(byte* pixels);
}
