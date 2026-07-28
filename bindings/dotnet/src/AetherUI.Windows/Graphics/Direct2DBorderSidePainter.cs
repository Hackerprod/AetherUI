using System.Numerics;
using System.Collections.Generic;
using AetherUI.Css;
using AetherUI.Primitives;
using AetherUI.Rendering;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Mathematics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Paints one CSS border side from the intersection of its annular border geometry and
/// its diagonal side partition. The intersection is materialized before PushLayer so one
/// transformed Direct2D layer owns the entire operation. Nested mask layers are not stable
/// under all affine world transforms and split non-uniform rounded borders into fragments.
/// </summary>
internal static class Direct2DBorderSidePainter
{
    internal static void Draw(
        ID2D1Factory factory,
        ID2D1RenderTarget target,
        FillBorderSideCommand command,
        Func<ColorRgba, ID2D1SolidColorBrush> getBrush)
    {
        using var geometry = CreateSideGeometry(
            factory,
            command.OuterRectangle,
            command.OuterRadii,
            command.InnerRectangle,
            command.InnerRadii,
            command.Side);
        var layer = new LayerParameters
        {
            // Layer bounds are local to the active world transform, like ordinary drawing
            // operations. The mask itself is consequently transformed exactly once.
            ContentBounds = ToRawRect(command.OuterRectangle),
            GeometricMask = geometry,
            MaskAntialiasMode = AntialiasMode.PerPrimitive,
            MaskTransform = Matrix3x2.Identity,
            Opacity = 1f,
            LayerOptions = LayerOptions.None,
        };
        target.PushLayer(layer, null);
        try
        {
            FillStyle(target, command, getBrush);
        }
        finally
        {
            target.PopLayer();
        }
    }

    // Internal so the WARP conformance suite can compare the exact CSS geometry drawn directly
    // with the production PushLayer path. It is not part of the renderer's public API.
    internal static ID2D1PathGeometry CreateSideGeometry(
        ID2D1Factory factory,
        RectF outer,
        CornerRadii outerRadii,
        RectF inner,
        CornerRadii innerRadii,
        int side)
    {
        using var outerGeometry = CreateRoundedGeometry(factory, outer, outerRadii);
        using var innerGeometry = CreateRoundedGeometry(factory, inner, innerRadii);
        using var partition = CreateSidePartition(factory, outer, inner, side);
        using var ring = Combine(factory, outerGeometry, innerGeometry, CombineMode.Exclude);
        return Combine(factory, ring, partition, CombineMode.Intersect);
    }

    private static void FillStyle(
        ID2D1RenderTarget target,
        FillBorderSideCommand command,
        Func<ColorRgba, ID2D1SolidColorBrush> getBrush)
    {
        var outer = command.OuterRectangle;
        var inner = command.InnerRectangle;
        var horizontal = command.Side is 0 or 2;
        var thickness = horizontal ? MathF.Abs(inner.Top - outer.Top) : MathF.Abs(inner.Left - outer.Left);
        if (command.Side == 2) thickness = MathF.Abs(outer.Bottom - inner.Bottom);
        if (command.Side == 1) thickness = MathF.Abs(outer.Right - inner.Right);
        var color = command.Color;
        ColorRgba Shade(float factor) => new(
            Math.Clamp(color.R * factor, 0, 1),
            Math.Clamp(color.G * factor, 0, 1),
            Math.Clamp(color.B * factor, 0, 1),
            color.A);

        // The managed enum mirrors aether_border_style_t. Keeping the semantic values here
        // prevents style-specific painting from drifting when the ABI evolves.
        var style = (BorderStyle)command.Style;
        if (style is BorderStyle.Inset or BorderStyle.Outset)
        {
            target.FillRectangle(ToRawRect(outer), getBrush(Shade(style == BorderStyle.Inset
                ? (command.Side is 0 or 3 ? .65f : 1.35f)
                : (command.Side is 0 or 3 ? 1.35f : .65f))));
            return;
        }

        if (style is BorderStyle.Groove or BorderStyle.Ridge && thickness >= 2)
        {
            var first = thickness / 2;
            var dark = style == BorderStyle.Groove ? command.Side is 0 or 3 : command.Side is 1 or 2;
            if (horizontal)
            {
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, outer.Width, first)), getBrush(Shade(dark ? .65f : 1.35f)));
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y + first, outer.Width, outer.Height - first)), getBrush(Shade(dark ? 1.35f : .65f)));
            }
            else
            {
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, first, outer.Height)), getBrush(Shade(dark ? .65f : 1.35f)));
                target.FillRectangle(ToRawRect(new RectF(outer.X + first, outer.Y, outer.Width - first, outer.Height)), getBrush(Shade(dark ? 1.35f : .65f)));
            }
            return;
        }

        if (style == BorderStyle.Double && thickness >= 3)
        {
            var band = thickness / 3;
            if (horizontal)
            {
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, outer.Width, band)), getBrush(color));
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Bottom - band, outer.Width, band)), getBrush(color));
            }
            else
            {
                target.FillRectangle(ToRawRect(new RectF(outer.X, outer.Y, band, outer.Height)), getBrush(color));
                target.FillRectangle(ToRawRect(new RectF(outer.Right - band, outer.Y, band, outer.Height)), getBrush(color));
            }
            return;
        }

        if (style is BorderStyle.Dotted or BorderStyle.Dashed)
        {
            var length = horizontal ? outer.Width : outer.Height;
            var segment = style == BorderStyle.Dotted ? thickness : MathF.Max(3 * thickness, 3);
            var step = segment + MathF.Max(thickness, 1);
            for (var position = 0f; position < length; position += step)
            {
                var part = horizontal
                    ? new RectF(outer.X + position, outer.Y, MathF.Min(segment, length - position), outer.Height)
                    : new RectF(outer.X, outer.Y + position, outer.Width, MathF.Min(segment, length - position));
                target.FillRectangle(ToRawRect(part), getBrush(color));
            }
            return;
        }

        target.FillRectangle(ToRawRect(outer), getBrush(color));
    }

    private static ID2D1PathGeometry Combine(
        ID2D1Factory factory,
        ID2D1Geometry first,
        ID2D1Geometry second,
        CombineMode mode)
    {
        var geometry = factory.CreatePathGeometry();
        using var sink = geometry.Open();
        sink.SetFillMode(FillMode.Winding);
        // Both operands already use the same CSS coordinate space. Explicit identity avoids
        // generated binding overloads that otherwise select only a flattening tolerance.
        first.CombineWithGeometry(second, mode, Matrix3x2.Identity, 0.25f, sink);
        sink.Close();
        return geometry;
    }

    private static ID2D1PathGeometry CreateRoundedGeometry(ID2D1Factory factory, RectF rectangle, CornerRadii radii)
    {
        var geometry = factory.CreatePathGeometry();
        using var sink = geometry.Open();
        sink.SetFillMode(FillMode.Winding);
        AddRoundedFigure(sink, rectangle, NormalizeRadii(rectangle, radii));
        sink.Close();
        return geometry;
    }

    private static ID2D1PathGeometry CreateSidePartition(ID2D1Factory factory, RectF outer, RectF inner, int side)
    {
        var outerTopLeft = new Vector2(outer.Left, outer.Top);
        var outerTopRight = new Vector2(outer.Right, outer.Top);
        var outerBottomRight = new Vector2(outer.Right, outer.Bottom);
        var outerBottomLeft = new Vector2(outer.Left, outer.Bottom);
        var innerTopLeft = new Vector2(inner.Left, inner.Top);
        var innerTopRight = new Vector2(inner.Right, inner.Top);
        var innerBottomRight = new Vector2(inner.Right, inner.Bottom);
        var innerBottomLeft = new Vector2(inner.Left, inner.Bottom);

        // CSS divides a corner between adjacent sides along the straight line from the
        // outer corner to the matching inner corner. The line is an infinite half-plane
        // boundary, not the finite outer/inner trapezoid: a finite strip leaves holes as
        // soon as a rounded corner extends below (or beside) the inner rectangle.
        // This conventional diagonal also applies unchanged to elliptical radii.
        var points = new List<Vector2>
        {
            outerTopLeft, outerTopRight, outerBottomRight, outerBottomLeft,
        };
        points = side switch
        {
            // top: above the TL and TR corner diagonals
            0 => ClipToHalfPlane(ClipToHalfPlane(points, outerTopLeft, innerTopLeft, keepNonPositive: true),
                                 outerTopRight, innerTopRight, keepNonPositive: false),
            // right: right of the TR and BR corner diagonals
            1 => ClipToHalfPlane(ClipToHalfPlane(points, outerTopRight, innerTopRight, keepNonPositive: true),
                                 outerBottomRight, innerBottomRight, keepNonPositive: false),
            // bottom: below the BR and BL corner diagonals
            2 => ClipToHalfPlane(ClipToHalfPlane(points, outerBottomRight, innerBottomRight, keepNonPositive: true),
                                 outerBottomLeft, innerBottomLeft, keepNonPositive: false),
            // left: left of the BL and TL corner diagonals
            _ => ClipToHalfPlane(ClipToHalfPlane(points, outerBottomLeft, innerBottomLeft, keepNonPositive: true),
                                 outerTopLeft, innerTopLeft, keepNonPositive: false),
        };
        var geometry = factory.CreatePathGeometry();
        if (points.Count < 3) return geometry;
        using var sink = geometry.Open();
        sink.BeginFigure(points[0], FigureBegin.Filled);
        sink.AddLines(points.GetRange(1, points.Count - 1).ToArray());
        sink.EndFigure(FigureEnd.Closed);
        sink.Close();
        return geometry;
    }

    private static List<Vector2> ClipToHalfPlane(
        List<Vector2> polygon, Vector2 lineStart, Vector2 lineEnd, bool keepNonPositive)
    {
        const float epsilon = .0001f;
        var line = lineEnd - lineStart;
        if (line.LengthSquared() <= epsilon * epsilon) return new List<Vector2>(polygon);

        float SignedSide(Vector2 point) => line.X * (point.Y - lineStart.Y) - line.Y * (point.X - lineStart.X);
        bool Keep(float signedSide) => keepNonPositive ? signedSide <= epsilon : signedSide >= -epsilon;

        var result = new List<Vector2>(polygon.Count + 2);
        for (var index = 0; index < polygon.Count; index++)
        {
            var from = polygon[index];
            var to = polygon[(index + 1) % polygon.Count];
            var fromSide = SignedSide(from);
            var toSide = SignedSide(to);
            var fromInside = Keep(fromSide);
            var toInside = Keep(toSide);
            if (fromInside && toInside)
            {
                result.Add(to);
                continue;
            }

            if (fromInside != toInside)
            {
                var fraction = fromSide / (fromSide - toSide);
                result.Add(from + (to - from) * fraction);
            }
            if (toInside) result.Add(to);
        }
        return result;
    }

    private static void AddRoundedFigure(ID2D1GeometrySink sink, RectF rectangle, CornerRadii radii)
    {
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
    }

    private static ArcSegment Arc(float x, float y, float radiusX, float radiusY) => new()
    {
        Point = new Vector2(x, y),
        Size = new Size(radiusX, radiusY),
        RotationAngle = 0,
        SweepDirection = SweepDirection.Clockwise,
        ArcSize = ArcSize.Small,
    };

    private static CornerRadii NormalizeRadii(RectF rectangle, CornerRadii radii)
    {
        var scale = MathF.Min(1f, MathF.Min(
            rectangle.Width / MathF.Max(.0001f, radii.TopLeftX + radii.TopRightX),
            MathF.Min(rectangle.Width / MathF.Max(.0001f, radii.BottomLeftX + radii.BottomRightX),
            MathF.Min(rectangle.Height / MathF.Max(.0001f, radii.TopLeftY + radii.BottomLeftY), rectangle.Height / MathF.Max(.0001f, radii.TopRightY + radii.BottomRightY)))));
        return scale < 1 ? new CornerRadii(
            radii.TopLeftX * scale, radii.TopLeftY * scale,
            radii.TopRightX * scale, radii.TopRightY * scale,
            radii.BottomRightX * scale, radii.BottomRightY * scale,
            radii.BottomLeftX * scale, radii.BottomLeftY * scale) : radii;
    }

    private static RawRectF ToRawRect(RectF rectangle) =>
        new(rectangle.Left, rectangle.Top, rectangle.Right, rectangle.Bottom);
}
