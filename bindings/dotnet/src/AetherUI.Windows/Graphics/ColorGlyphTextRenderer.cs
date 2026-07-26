using System.Numerics;
using SharpGen.Runtime;
using Vortice;
using Vortice.DCommon;
using Vortice.Direct2D1;
using Vortice.DirectWrite;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Draws DirectWrite glyph runs while preserving a color font's palette layers. The normal
/// <c>ID2D1RenderTarget.DrawText</c> path flattens every run through one brush, which turns emoji
/// and other COLR fonts into monochrome outlines. Runs that do not expose color layers keep the
/// exact existing Direct2D glyph path.
/// </summary>
internal sealed class ColorGlyphTextRenderer : TextRendererBase
{
    private const uint DefaultColorPalette = 0;

    private readonly IDWriteFactory2 _writeFactory;
    private readonly ID2D1RenderTarget _target;
    private readonly ID2D1Brush _foregroundBrush;
    private readonly float _pixelsPerDip;

    public ColorGlyphTextRenderer(
        IDWriteFactory2 writeFactory,
        ID2D1RenderTarget target,
        ID2D1Brush foregroundBrush,
        float pixelsPerDip)
    {
        _writeFactory = writeFactory;
        _target = target;
        _foregroundBrush = foregroundBrush;
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
            _target.DrawGlyphRun(new Vector2(baselineOriginX, baselineOriginY), glyphRun, _foregroundBrush, measuringMode);
            return;
        }

        var drewColorLayer = false;
        using (colorLayers)
        {
            while (colorLayers.MoveNext())
            {
                drewColorLayer = true;
                var layer = colorLayers.CurrentRun;
                var brush = layer.PaletteIndex == ushort.MaxValue
                    ? _foregroundBrush
                    : _target.CreateSolidColorBrush(layer.RunColor);
                try
                {
                    _target.DrawGlyphRun(
                        new Vector2(layer.BaselineOriginX, layer.BaselineOriginY),
                        layer.GlyphRun,
                        brush,
                        measuringMode);
                }
                finally
                {
                    if (!ReferenceEquals(brush, _foregroundBrush)) brush.Dispose();
                }
            }
        }

        // An enumerator with no layers is not a drawable color run. Keep ordinary text visible
        // rather than turning an unexpected DirectWrite response into missing content.
        if (!drewColorLayer)
        {
            _target.DrawGlyphRun(new Vector2(baselineOriginX, baselineOriginY), glyphRun, _foregroundBrush, measuringMode);
        }
    }
}
