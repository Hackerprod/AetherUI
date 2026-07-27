using System.Numerics;
using AetherUI.Primitives;
using AetherUI.Rendering;
using SharpGen.Runtime;
using Vortice;
using Vortice.DCommon;
using Vortice.Direct2D1;
using Vortice.DirectWrite;
using Vortice.DXGI;
using Vortice.Mathematics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Renders CSS text shadows from a glyph alpha mask. The Direct2D Shadow effect owns both the
/// Gaussian blur and tinting; the renderer never approximates a glyph with rectangles or repeats.
/// </summary>
internal static class Direct2DTextShadowPainter
{
    private const float MinimumSigma = .125f;

    internal static GlyphMask CreateMask(
        ID2D1DeviceContext target,
        DrawTextShadowCommand command,
        float dpiScale)
    {
        ArgumentNullException.ThrowIfNull(target);

        var rectangle = command.Rectangle;
        if (string.IsNullOrEmpty(command.Text) || rectangle.IsEmpty)
        {
            return GlyphMask.Empty;
        }

        var sigma = ToStandardDeviation(command.Shadow.BlurRadius);
        var padding = MathF.Ceiling(3f * sigma + 1f);
        var dpi = 96f * dpiScale;
        var width = Math.Max(1, (int)MathF.Ceiling((rectangle.Width + padding * 2f) * dpiScale));
        var height = Math.Max(1, (int)MathF.Ceiling((rectangle.Height + padding * 2f) * dpiScale));
        var bitmap = target.CreateBitmap(
            new SizeI(width, height),
            IntPtr.Zero,
            0,
            new BitmapProperties1(
                new PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.Target));

        return new GlyphMask(bitmap, padding, sigma, checked((long)width * height * 4));
    }

    /// <summary>
    /// Draws into a mask target during an already-active Direct2D draw segment. Segment lifetime
    /// deliberately belongs to <see cref="Direct2DRenderBackend"/>, which is the sole owner of
    /// BeginDraw/EndDraw state for both the scene and offscreen mask passes.
    /// </summary>
    internal static void RenderGlyphMask(
        ID2D1DeviceContext target,
        IDWriteFactory2 writeFactory,
        DrawTextShadowCommand command,
        GlyphMask mask,
        float dpiScale)
    {
        ArgumentNullException.ThrowIfNull(target);
        ArgumentNullException.ThrowIfNull(writeFactory);

        if (mask.IsEmpty)
        {
            return;
        }

        var rectangle = command.Rectangle;
        var padding = mask.Padding;
        target.Transform = Matrix3x2.Identity;
        target.Clear(new Color4(0, 0, 0, 0));

        var format = CreateFormat(writeFactory, command);
        using (format)
        using (var layout = writeFactory.CreateTextLayout(command.Text, format, rectangle.Width, rectangle.Height))
        using (var brush = target.CreateSolidColorBrush(new Color4(1, 1, 1, 1)))
        {
            var clip = new RawRectF(padding, padding, padding + rectangle.Width, padding + rectangle.Height);
            target.PushAxisAlignedClip(clip, AntialiasMode.PerPrimitive);
            try
            {
                layout.Draw(IntPtr.Zero, new AlphaGlyphTextRenderer(writeFactory, target, brush, dpiScale), padding, padding);
            }
            finally
            {
                target.PopAxisAlignedClip();
            }
        }
    }

    internal static void Draw(ID2D1DeviceContext target, GlyphMask mask, RectF rectangle, BoxShadow shadow)
    {
        if (mask.IsEmpty || shadow.Color.A <= 0)
        {
            return;
        }

        using var effect = new ID2D1Effect(target.CreateEffect(EffectGuids.Shadow));
        effect.SetInput(0, mask.Bitmap!, true);
        effect.SetValue((uint)ShadowProperties.BlurStandardDeviation, mask.StandardDeviation);
        effect.SetValue((uint)ShadowProperties.Color, new Color4(shadow.Color.R, shadow.Color.G, shadow.Color.B, shadow.Color.A));
        effect.SetValue((uint)ShadowProperties.Optimization, ShadowOptimization.Quality);
        using var effectImage = effect.QueryInterface<ID2D1Image>();
        target.DrawImage(
            effectImage,
            new Vector2(rectangle.X - mask.Padding + shadow.OffsetX, rectangle.Y - mask.Padding + shadow.OffsetY),
            null,
            InterpolationMode.Linear,
            CompositeMode.SourceOver);
    }

    private static IDWriteTextFormat CreateFormat(IDWriteFactory2 factory, DrawTextShadowCommand command)
    {
        var family = string.IsNullOrWhiteSpace(command.FontFamily) ? "Segoe UI" : command.FontFamily;
        var size = float.IsFinite(command.FontSize) && command.FontSize > 0 ? command.FontSize : 16f;
        var format = factory.CreateTextFormat(
            family,
            (FontWeight)(ushort)command.FontWeight,
            FontStyle.Normal,
            FontStretch.Normal,
            size);
        format.TextAlignment = command.Alignment switch
        {
            AetherUI.Css.TextAlign.Center => TextAlignment.Center,
            AetherUI.Css.TextAlign.End or AetherUI.Css.TextAlign.Right => TextAlignment.Trailing,
            _ => TextAlignment.Leading,
        };
        format.ParagraphAlignment = ParagraphAlignment.Near;
        format.WordWrapping = command.Wrap ? WordWrapping.Wrap : WordWrapping.NoWrap;
        return format;
    }

    private static float ToStandardDeviation(float cssBlurRadius)
    {
        // CSS does not expose a Gaussian sigma. Keep the established engine mapping used for
        // box-shadow, then pad the mask to three sigma so the effect kernel is not clipped.
        return MathF.Max(MinimumSigma, MathF.Max(0f, cssBlurRadius) * .5f);
    }

    internal sealed class GlyphMask : IDisposable
    {
        internal static GlyphMask Empty { get; } = new(null, 0, 0, 0);

        internal GlyphMask(ID2D1Bitmap1? bitmap, float padding, float standardDeviation, long byteSize)
        {
            Bitmap = bitmap;
            Padding = padding;
            StandardDeviation = standardDeviation;
            ByteSize = byteSize;
        }

        internal ID2D1Bitmap1? Bitmap { get; }
        internal float Padding { get; }
        internal float StandardDeviation { get; }
        internal long ByteSize { get; }
        internal bool IsEmpty => Bitmap is null;

        public void Dispose() => Bitmap?.Dispose();
    }

    private sealed class AlphaGlyphTextRenderer : TextRendererBase
    {
        private const uint DefaultColorPalette = 0;
        private readonly IDWriteFactory2 _writeFactory;
        private readonly ID2D1RenderTarget _target;
        private readonly ID2D1Brush _brush;
        private readonly float _pixelsPerDip;

        internal AlphaGlyphTextRenderer(IDWriteFactory2 writeFactory, ID2D1RenderTarget target, ID2D1Brush brush, float pixelsPerDip)
        {
            _writeFactory = writeFactory;
            _target = target;
            _brush = brush;
            _pixelsPerDip = pixelsPerDip;
        }

        public override RawBool IsPixelSnappingDisabled(IntPtr clientDrawingContext) => false;
        public override Matrix3x2 GetCurrentTransform(IntPtr clientDrawingContext) => _target.Transform;
        public override float GetPixelsPerDip(IntPtr clientDrawingContext) => _pixelsPerDip;

        public override void DrawGlyphRun(
            IntPtr clientDrawingContext,
            float baselineOriginX,
            float baselineOriginY,
            MeasuringMode measuringMode,
            GlyphRun glyphRun,
            GlyphRunDescription glyphRunDescription,
            IUnknown clientDrawingEffect)
        {
            var result = _writeFactory.TranslateColorGlyphRun(
                baselineOriginX,
                baselineOriginY,
                glyphRun,
                glyphRunDescription,
                measuringMode,
                _target.Transform,
                DefaultColorPalette,
                out IDWriteColorGlyphRunEnumerator? colorLayers);
            if (result.Failure || colorLayers is null)
            {
                _target.DrawGlyphRun(new Vector2(baselineOriginX, baselineOriginY), glyphRun, _brush, measuringMode);
                return;
            }

            var drewLayer = false;
            using (colorLayers)
            {
                while (colorLayers.MoveNext())
                {
                    drewLayer = true;
                    var layer = colorLayers.CurrentRun;
                    _target.DrawGlyphRun(new Vector2(layer.BaselineOriginX, layer.BaselineOriginY), layer.GlyphRun, _brush, measuringMode);
                }
            }

            if (!drewLayer)
            {
                _target.DrawGlyphRun(new Vector2(baselineOriginX, baselineOriginY), glyphRun, _brush, measuringMode);
            }
        }
    }
}

internal readonly record struct TextShadowMaskKey(
    string Text,
    string FontFamily,
    int FontSizeBits,
    int FontWeight,
    int Alignment,
    bool Wrap,
    int WidthBits,
    int HeightBits,
    int DpiScaleBits)
{
    internal static TextShadowMaskKey From(DrawTextShadowCommand command, float dpiScale) => new(
        command.Text,
        command.FontFamily,
        BitConverter.SingleToInt32Bits(command.FontSize),
        (int)command.FontWeight,
        (int)command.Alignment,
        command.Wrap,
        BitConverter.SingleToInt32Bits(command.Rectangle.Width),
        BitConverter.SingleToInt32Bits(command.Rectangle.Height),
        BitConverter.SingleToInt32Bits(dpiScale));
}

/// <summary>
/// Device-local LRU cache for alpha masks. A mask owns an ID2D1Bitmap1 and must never outlive
/// the D2D device context that created it.
/// </summary>
internal sealed class TextShadowMaskCache : IDisposable
{
    internal const long ByteBudget = 32L * 1024 * 1024;

    private readonly Dictionary<TextShadowMaskKey, LinkedListNode<Entry>> _entries = new();
    private readonly LinkedList<Entry> _lru = new();
    private long _bytes;

    internal bool TryGet(TextShadowMaskKey key, out Direct2DTextShadowPainter.GlyphMask mask)
    {
        if (!_entries.TryGetValue(key, out var node))
        {
            mask = Direct2DTextShadowPainter.GlyphMask.Empty;
            return false;
        }

        _lru.Remove(node);
        _lru.AddFirst(node);
        mask = node.Value.Mask;
        return true;
    }

    /// <summary>Transfers ownership on success; the caller disposes an oversized uncached mask.</summary>
    internal bool TryAdd(TextShadowMaskKey key, Direct2DTextShadowPainter.GlyphMask mask)
    {
        if (mask.IsEmpty || mask.ByteSize > ByteBudget)
        {
            return false;
        }

        while (_bytes + mask.ByteSize > ByteBudget && _lru.Last is { } oldest)
        {
            Remove(oldest);
        }

        var node = new LinkedListNode<Entry>(new Entry(key, mask));
        _lru.AddFirst(node);
        _entries.Add(key, node);
        _bytes += mask.ByteSize;
        return true;
    }

    internal void Clear()
    {
        while (_lru.Last is { } oldest)
        {
            Remove(oldest);
        }
    }

    public void Dispose() => Clear();

    private void Remove(LinkedListNode<Entry> node)
    {
        _entries.Remove(node.Value.Key);
        _lru.Remove(node);
        _bytes -= node.Value.Mask.ByteSize;
        node.Value.Mask.Dispose();
    }

    private readonly record struct Entry(TextShadowMaskKey Key, Direct2DTextShadowPainter.GlyphMask Mask);
}

internal sealed class TextShadowRenderMetrics
{
    internal int CacheHits { get; private set; }
    internal int SceneSuspensions { get; private set; }

    internal void Reset()
    {
        CacheHits = 0;
        SceneSuspensions = 0;
    }

    internal void RecordCacheHit() => CacheHits++;
    internal void RecordSceneSuspension() => SceneSuspensions++;
}
