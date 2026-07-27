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

internal static unsafe partial class NativeMethods
{
    private const string Lib = "aether_core";

    [LibraryImport(Lib, EntryPoint = "aether_abi_version")]
    internal static partial uint AbiVersion();

    [LibraryImport(Lib, EntryPoint = "aether_abi_capabilities")]
    internal static partial ulong AbiCapabilities();

    [LibraryImport(Lib, EntryPoint = "aether_paint_command_size")]
    internal static partial nuint PaintCommandSize();

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

    [LibraryImport(Lib, EntryPoint = "aether_layout_box_get_scrollable_overflow_y")]
    internal static partial float LayoutBoxGetScrollableOverflowY(IntPtr box);

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
}
