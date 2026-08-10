using System.Drawing;
using System.Numerics;
using System.Runtime.InteropServices;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using SharpGen.Runtime;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DirectWrite;
using Vortice.DXGI;
using Vortice.Mathematics;
using DWriteFontWeight = Vortice.DirectWrite.FontWeight;
using DWriteTextAlignment = Vortice.DirectWrite.TextAlignment;
using UiFontWeight = AetherUI.Css.FontWeight;
using UiTextAlign = AetherUI.Css.TextAlign;
using UiTextMetrics = AetherUI.Layout.TextMetrics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Direct2D renderer region: Text-shadow glyph masks and their render-side cache.
/// </summary>
public sealed partial class Direct2DRenderBackend
{
    private readonly TextShadowMaskCache _textShadowMasks = new();
    private readonly TextShadowRenderMetrics _textShadowMetrics = new();

    internal int LastTextShadowCacheHits => _textShadowMetrics.CacheHits;
    internal int LastTextShadowSceneSuspensions => _textShadowMetrics.SceneSuspensions;

    private Direct2DTextShadowPainter.GlyphMask CreateTextShadowMask(
        ID2D1DeviceContext target,
        DrawTextShadowCommand command)
    {
        var mask = Direct2DTextShadowPainter.CreateMask(target, command, _dpiScale);
        if (mask.IsEmpty)
        {
            return mask;
        }

        using var sceneTarget = target.Target ?? throw new InvalidOperationException("The Direct2D scene target is not initialized.");
        var sceneTransform = target.Transform;
        try
        {
            target.Target = mask.Bitmap;
            BeginDrawSegment(target);
            Direct2DTextShadowPainter.RenderGlyphMask(target, _writeFactory, command, mask, _dpiScale);
            var result = EndDrawSegment(target);
            if (result.Failure)
            {
                result.CheckError();
            }

            return mask;
        }
        catch
        {
            if (_isDrawSegmentActive)
            {
                _ = EndDrawSegment(target);
            }

            mask.Dispose();
            throw;
        }
        finally
        {
            target.Target = sceneTarget;
            target.Transform = sceneTransform;
        }
    }

}
