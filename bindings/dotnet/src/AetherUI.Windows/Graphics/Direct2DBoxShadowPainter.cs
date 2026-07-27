using System.Numerics;
using AetherUI.Primitives;
using AetherUI.Rendering;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Mathematics;

namespace AetherUI.Windows.Graphics;

// Shared by the HWND backend and software regression target. Keeping the
// raster algorithm here makes a pixel baseline exercise production code.
internal static class Direct2DBoxShadowPainter
{
    internal static void Draw(ID2D1Factory factory, ID2D1RenderTarget target,
                              DrawBoxShadowCommand command,
                              Func<ColorRgba, ID2D1SolidColorBrush> getBrush)
    {
        var shadow = command.Shadow;
        if (shadow.Color.A <= 0) return;

        var baseRect = command.BorderBoxRectangle
            .Inflate(shadow.SpreadRadius)
            .Offset(shadow.OffsetX, shadow.OffsetY);
        var baseRadii = Inflate(command.Radii, shadow.SpreadRadius);

        if (shadow.BlurRadius <= 0.5f)
        {
            FillRoundedGeometry(factory, target, baseRect, baseRadii, getBrush(shadow.Color));
            return;
        }

        const int steps = 16;
        var extent = MathF.Max(1f, shadow.BlurRadius * 1.5f);
        var sigma = MathF.Max(.25f, shadow.BlurRadius * .5f);
        var accumulatedAlpha = 0f;
        for (var index = steps; index >= 0; index--)
        {
            var expand = extent * index / steps;
            var targetAlpha = shadow.Color.A * .5f *
                MathF.Exp(-(expand * expand) / (2f * sigma * sigma));
            var layerAlpha = (targetAlpha - accumulatedAlpha) / MathF.Max(.0001f, 1f - accumulatedAlpha);
            if (layerAlpha <= .001f) continue;

            var rect = baseRect.Inflate(expand);
            FillRoundedGeometry(factory, target, rect, Inflate(baseRadii, expand),
                getBrush(shadow.Color.WithAlpha(MathF.Min(1f, layerAlpha))));
            accumulatedAlpha = targetAlpha;
        }
    }

    private static void FillRoundedGeometry(ID2D1Factory factory, ID2D1RenderTarget target,
                                            RectF rectangle, CornerRadii radii, ID2D1Brush brush)
    {
        if (radii.IsZero) { target.FillRectangle(ToRawRect(rectangle), brush); return; }
        using var geometry = CreateRoundedGeometry(factory, rectangle, radii);
        target.FillGeometry(geometry, brush);
    }

    private static ID2D1PathGeometry CreateRoundedGeometry(ID2D1Factory factory, RectF rectangle, CornerRadii input)
    {
        var radii = NormalizeRadii(rectangle, input);
        var geometry = factory.CreatePathGeometry();
        using var sink = geometry.Open();
        sink.BeginFigure(new Vector2(rectangle.Left + radii.TopLeftX, rectangle.Top), FigureBegin.Filled);
        sink.AddLine(new Vector2(rectangle.Right - radii.TopRightX, rectangle.Top));
        sink.AddArc(Arc(rectangle.Right, rectangle.Top + radii.TopRightY, radii.TopRightX, radii.TopRightY));
        sink.AddLine(new Vector2(rectangle.Right, rectangle.Bottom - radii.BottomRightY));
        sink.AddArc(Arc(rectangle.Right - radii.BottomRightX, rectangle.Bottom, radii.BottomRightX, radii.BottomRightY));
        sink.AddLine(new Vector2(rectangle.Left + radii.BottomLeftX, rectangle.Bottom));
        sink.AddArc(Arc(rectangle.Left, rectangle.Bottom - radii.BottomLeftY, radii.BottomLeftX, radii.BottomLeftY));
        sink.AddLine(new Vector2(rectangle.Left, rectangle.Top + radii.TopLeftY));
        sink.AddArc(Arc(rectangle.Left + radii.TopLeftX, rectangle.Top, radii.TopLeftX, radii.TopLeftY));
        sink.EndFigure(FigureEnd.Closed);
        sink.Close();
        return geometry;
    }

    private static ArcSegment Arc(float x, float y, float radiusX, float radiusY) => new()
    {
        Point = new Vector2(x, y), Size = new Size(radiusX, radiusY), RotationAngle = 0,
        SweepDirection = SweepDirection.Clockwise, ArcSize = ArcSize.Small,
    };

    private static CornerRadii Inflate(CornerRadii radii, float amount) => new(
        MathF.Max(0, radii.TopLeftX + amount), MathF.Max(0, radii.TopLeftY + amount), MathF.Max(0, radii.TopRightX + amount), MathF.Max(0, radii.TopRightY + amount),
        MathF.Max(0, radii.BottomRightX + amount), MathF.Max(0, radii.BottomRightY + amount), MathF.Max(0, radii.BottomLeftX + amount), MathF.Max(0, radii.BottomLeftY + amount));

    private static CornerRadii NormalizeRadii(RectF rectangle, CornerRadii radii)
    {
        var scale = MathF.Min(1f, MathF.Min(
            rectangle.Width / MathF.Max(.0001f, radii.TopLeftX + radii.TopRightX),
            MathF.Min(rectangle.Width / MathF.Max(.0001f, radii.BottomLeftX + radii.BottomRightX),
            MathF.Min(rectangle.Height / MathF.Max(.0001f, radii.TopLeftY + radii.BottomLeftY), rectangle.Height / MathF.Max(.0001f, radii.TopRightY + radii.BottomRightY)))));
        return scale < 1 ? new CornerRadii(radii.TopLeftX * scale, radii.TopLeftY * scale, radii.TopRightX * scale, radii.TopRightY * scale, radii.BottomRightX * scale, radii.BottomRightY * scale, radii.BottomLeftX * scale, radii.BottomLeftY * scale) : radii;
    }

    private static RawRectF ToRawRect(RectF rectangle) => new(rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom);
}
