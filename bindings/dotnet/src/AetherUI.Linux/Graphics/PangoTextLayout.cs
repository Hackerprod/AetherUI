using System.Runtime.InteropServices;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using Microsoft.Win32.SafeHandles;

namespace AetherUI.Linux.Graphics;

/// <summary>Owner-thread, bounded Pango shaping pipeline. All sizes are DIPs, not device pixels.</summary>
internal sealed class PangoTextLayout : IDisposable
{
    private const int Scale = 1024;
    private const int MaximumEntries = 128;
    private const int MaximumCachedCharacters = 262144;
    private readonly int _ownerThread = Environment.CurrentManagedThreadId;
    private readonly GObjectHandle _context;
    private readonly Dictionary<LayoutKey, GObjectHandle> _layouts = new();
    private int _cachedCharacters;
    private bool _disposed;

    public PangoTextLayout()
    {
        try
        {
            // Borrowed default font map; the returned context is owned by this instance.
            var fontMap = Native.pango_cairo_font_map_get_default();
            if (fontMap == IntPtr.Zero) throw new InvalidOperationException("PangoCairo could not create its default font map.");
            var context = new GObjectHandle(Native.pango_font_map_create_context(fontMap));
            try
            {
                Native.pango_cairo_context_set_resolution(context.DangerousGetHandle(), 96);
                _context = context;
            }
            catch { context.Dispose(); throw; }
        }
        catch (DllNotFoundException exception)
        {
            throw new InvalidOperationException("Linux text rendering requires libpangocairo-1.0.so.0, libpango-1.0.so.0 and libgobject-2.0.so.0. Install distribution Cairo/Pango runtime packages.", exception);
        }
        catch (EntryPointNotFoundException exception)
        {
            throw new InvalidOperationException("The installed Pango/PangoCairo runtime is missing a required layout symbol.", exception);
        }
    }

    public TextMetrics Measure(string text, ComputedStyle style, float maxWidth)
    {
        CheckAccess();
        ArgumentNullException.ThrowIfNull(text);
        ArgumentNullException.ThrowIfNull(style);
        if (text.Length == 0) return new TextMetrics(0, style.FontSize * style.LineHeight, style.FontSize * .8f);
        // As on DirectWrite, intrinsic measurement is unwrapped and includes trailing spaces.
        var layout = GetLayout(text, style.FontFamily, style.FontSize, style.FontWeight, false, 0);
        Native.pango_layout_get_size(layout.DangerousGetHandle(), out var width, out var height);
        return new TextMetrics(width / (float)Scale,
            MathF.Max(style.FontSize * style.LineHeight, height / (float)Scale),
            Native.pango_layout_get_baseline(layout.DangerousGetHandle()) / (float)Scale);
    }

    /// <summary>Draws with the current Cairo source. Path mode appends glyph outlines; caller owns clipping and filling.</summary>
    public void Draw(IntPtr cairo, string text, RectF rect, string family, float size,
        FontWeight weight, TextAlign align, bool wrap, bool path = false)
    {
        CheckAccess();
        ArgumentNullException.ThrowIfNull(text);
        if (cairo == IntPtr.Zero) throw new ArgumentException("A valid Cairo context is required.", nameof(cairo));
        if (text.Length == 0 || rect.IsEmpty) return;
        var layout = GetLayout(text, family, size, weight, wrap, rect.Width, align);
        Native.pango_layout_get_size(layout.DangerousGetHandle(), out var width, out _);
        var x = rect.X;
        // Unwrapped layouts have natural width. Alignment belongs to the target rectangle,
        // not to an artificial million-DIP layout width. Wrapped alignment is handled by Pango.
        if (!wrap)
        {
            if (align == TextAlign.Center) x += (rect.Width - width / (float)Scale) * .5f;
            else if (align is TextAlign.End or TextAlign.Right) x += rect.Width - width / (float)Scale;
        }
        if (path)
        {
            Native.cairo_move_to(cairo, x, rect.Y);
            Native.pango_cairo_layout_path(cairo, layout.DangerousGetHandle());
            return;
        }
        Native.cairo_save(cairo);
        try
        {
            Native.cairo_rectangle(cairo, rect.X, rect.Y, rect.Width, rect.Height);
            Native.cairo_clip(cairo);
            Native.cairo_move_to(cairo, x, rect.Y);
            // Do not update the layout from the drawing CTM: that would mutate cached metrics
            // according to device scale. Cairo transforms the shared DIP layout at playback.
            Native.pango_cairo_show_layout(cairo, layout.DangerousGetHandle());
        }
        finally { Native.cairo_restore(cairo); }
    }

    public void Clear()
    {
        CheckAccess();
        foreach (var layout in _layouts.Values) layout.Dispose();
        _layouts.Clear();
        _cachedCharacters = 0;
    }

    public void Dispose()
    {
        if (_ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Pango layout must be used on its owner thread.");
        if (_disposed) return;
        Clear();
        _context.Dispose();
        _disposed = true;
    }

    private GObjectHandle GetLayout(string text, string family, float size, FontWeight weight,
        bool wrap, float width, TextAlign align = TextAlign.Start)
    {
        if (text.Length > MaximumCachedCharacters) throw new ArgumentOutOfRangeException(nameof(text), "A single text run exceeds the 262144 UTF-16 character budget.");
        family = string.IsNullOrWhiteSpace(family) ? "sans-serif" : family;
        size = float.IsFinite(size) && size > 0 ? size : 16;
        if (size > 16384) throw new ArgumentOutOfRangeException(nameof(size));
        if (family.Length > 4096) throw new ArgumentOutOfRangeException(nameof(family), "Font family lists are limited to 4096 characters.");
        if (text.Length * (double)size > 1_000_000)
            throw new ArgumentOutOfRangeException(nameof(text), "The text run exceeds the Pango coordinate budget; split it into smaller runs.");
        var pangoWidth = wrap && float.IsFinite(width) && width > 0
            ? checked((int)Math.Min(Math.Round(width * (double)Scale), int.MaxValue)) : -1;
        var key = new LayoutKey(text, family, size, weight, pangoWidth, align);
        if (_layouts.TryGetValue(key, out var cached)) return cached;
        if (_layouts.Count >= MaximumEntries || _cachedCharacters + text.Length > MaximumCachedCharacters) Clear();
        var result = new GObjectHandle(Native.pango_layout_new(_context.DangerousGetHandle()));
        try
        {
            using var description = new FontDescriptionHandle(Native.pango_font_description_new());
            Native.pango_font_description_set_family(description.DangerousGetHandle(), family);
            Native.pango_font_description_set_absolute_size(description.DangerousGetHandle(), size * Scale);
            Native.pango_font_description_set_weight(description.DangerousGetHandle(), (int)weight);
            Native.pango_layout_set_font_description(result.DangerousGetHandle(), description.DangerousGetHandle());
            Native.pango_layout_set_text(result.DangerousGetHandle(), text, System.Text.Encoding.UTF8.GetByteCount(text));
            Native.pango_layout_set_width(result.DangerousGetHandle(), pangoWidth);
            Native.pango_layout_set_wrap(result.DangerousGetHandle(), 2); // PANGO_WRAP_WORD_CHAR
            Native.pango_layout_set_alignment(result.DangerousGetHandle(), align switch
            {
                TextAlign.Center => 1,
                TextAlign.End or TextAlign.Right => 2,
                _ => 0,
            });
            _layouts.Add(key, result);
            _cachedCharacters += text.Length;
            return result;
        }
        catch { result.Dispose(); throw; }
    }

    private void CheckAccess()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_ownerThread != Environment.CurrentManagedThreadId) throw new InvalidOperationException("Pango layout must be used on its owner thread.");
    }

    private readonly record struct LayoutKey(string Text, string Family, float Size, FontWeight Weight, int Width, TextAlign Align);

    private sealed class GObjectHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal GObjectHandle(IntPtr value) : base(true)
        {
            SetHandle(value);
            if (IsInvalid) throw new OutOfMemoryException("Pango object allocation failed.");
        }
        protected override bool ReleaseHandle() { Native.g_object_unref(handle); return true; }
    }

    private sealed class FontDescriptionHandle : SafeHandleZeroOrMinusOneIsInvalid
    {
        internal FontDescriptionHandle(IntPtr value) : base(true)
        {
            SetHandle(value);
            if (IsInvalid) throw new OutOfMemoryException("Pango font description allocation failed.");
        }
        protected override bool ReleaseHandle() { Native.pango_font_description_free(handle); return true; }
    }

    private static class Native
    {
        private const string Pango = "libpango-1.0.so.0";
        private const string PangoCairo = "libpangocairo-1.0.so.0";
        private const string Cairo = "libcairo.so.2";
        [DllImport(PangoCairo)] internal static extern IntPtr pango_cairo_font_map_get_default();
        [DllImport(PangoCairo)] internal static extern void pango_cairo_context_set_resolution(IntPtr context, double dpi);
        [DllImport(PangoCairo)] internal static extern void pango_cairo_show_layout(IntPtr cairo, IntPtr layout);
        [DllImport(PangoCairo)] internal static extern void pango_cairo_layout_path(IntPtr cairo, IntPtr layout);
        [DllImport(Pango)] internal static extern IntPtr pango_font_map_create_context(IntPtr fontMap);
        [DllImport(Pango)] internal static extern IntPtr pango_layout_new(IntPtr context);
        [DllImport(Pango)] internal static extern IntPtr pango_font_description_new();
        [DllImport(Pango)] internal static extern void pango_font_description_free(IntPtr description);
        [DllImport(Pango)] internal static extern void pango_font_description_set_family(IntPtr description, [MarshalAs(UnmanagedType.LPUTF8Str)] string family);
        [DllImport(Pango)] internal static extern void pango_font_description_set_absolute_size(IntPtr description, double size);
        [DllImport(Pango)] internal static extern void pango_font_description_set_weight(IntPtr description, int weight);
        [DllImport(Pango)] internal static extern void pango_layout_set_font_description(IntPtr layout, IntPtr description);
        [DllImport(Pango)] internal static extern void pango_layout_set_text(IntPtr layout, [MarshalAs(UnmanagedType.LPUTF8Str)] string text, int length);
        [DllImport(Pango)] internal static extern void pango_layout_set_width(IntPtr layout, int width);
        [DllImport(Pango)] internal static extern void pango_layout_set_wrap(IntPtr layout, int wrap);
        [DllImport(Pango)] internal static extern void pango_layout_set_alignment(IntPtr layout, int alignment);
        [DllImport(Pango)] internal static extern void pango_layout_get_size(IntPtr layout, out int width, out int height);
        [DllImport(Pango)] internal static extern int pango_layout_get_baseline(IntPtr layout);
        [DllImport("libgobject-2.0.so.0")] internal static extern void g_object_unref(IntPtr value);
        [DllImport(Cairo)] internal static extern void cairo_move_to(IntPtr cairo, double x, double y);
        [DllImport(Cairo)] internal static extern void cairo_rectangle(IntPtr cairo, double x, double y, double width, double height);
        [DllImport(Cairo)] internal static extern void cairo_clip(IntPtr cairo);
        [DllImport(Cairo)] internal static extern void cairo_save(IntPtr cairo);
        [DllImport(Cairo)] internal static extern void cairo_restore(IntPtr cairo);
    }
}
