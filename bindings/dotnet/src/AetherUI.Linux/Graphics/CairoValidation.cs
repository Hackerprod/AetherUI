using AetherUI.Primitives;
using AetherUI.Rendering;
using static AetherUI.Linux.Graphics.CairoNative;
namespace AetherUI.Linux.Graphics;

public sealed partial class CairoRenderBackend
{
    private static void ValidateRect(RectF r) => Finite(r.X, r.Y, r.Width, r.Height, r.Right, r.Bottom);
    private static void ValidateRadii(CornerRadii r) => Finite(r.TopLeftX, r.TopLeftY, r.TopRightX, r.TopRightY, r.BottomLeftX, r.BottomLeftY, r.BottomRightX, r.BottomRightY);
    private static void ValidateColor(ColorRgba c) => Finite(c.R, c.G, c.B, c.A);
    private static void ValidateCommandShadow(BoxShadow s)
    {
        Finite(s.OffsetX, s.OffsetY, s.BlurRadius, s.SpreadRadius);
        ValidateColor(s.Color);
    }
    private static void ValidateGradient(LinearGradient g)
    {
        Finite(g.DirectionX, g.DirectionY);
        if (g.Stops is null || g.Stops.Count == 0 || g.Stops.Count > 4096)
            throw new ArgumentException("Invalid gradient stop count.");
        foreach (var s in g.Stops)
        {
            Finite(s.Offset);
            ValidateColor(s.Color);
        }
    }
    private static void ValidateVisualState(ScrollVisualState s)
    {
        Finite(s.Offset.X, s.Offset.Y);
        var c = s.Chrome;
        ValidateRect(c.HorizontalTrack);
        ValidateRect(c.HorizontalThumb);
        ValidateRect(c.VerticalTrack);
        ValidateRect(c.VerticalThumb);
        ValidateRect(c.Corner);
    }
    private static void ValidateGeometry(PaintCommand c)
    {
        switch (c)
        {
            case FillRoundedRectangleCommand x:
                ValidateRect(x.Rectangle);
                ValidateRadii(x.Radii);
                ValidateColor(x.Color);
                break;
            case StrokeRoundedRectangleCommand x:
                ValidateRect(x.Rectangle);
                ValidateRadii(x.Radii);
                ValidateColor(x.Color);
                Finite(x.StrokeWidth);
                break;
            case FillRoundedRectangleGradientCommand x:
                ValidateRect(x.Rectangle);
                ValidateRect(x.GradientRectangle);
                ValidateRadii(x.Radii);
                ValidateGradient(x.Gradient);
                Finite(x.Opacity);
                break;
            case FillBorderSideCommand x:
                ValidateRect(x.OuterRectangle);
                ValidateRect(x.InnerRectangle);
                ValidateRadii(x.OuterRadii);
                ValidateRadii(x.InnerRadii);
                ValidateColor(x.Color);
                break;
            case DrawBoxShadowCommand x:
                ValidateRect(x.BorderBoxRectangle);
                ValidateRadii(x.Radii);
                ValidateCommandShadow(x.Shadow);
                break;
            case DrawTextCommand x:
                ValidateRect(x.Rectangle);
                ValidateColor(x.Color);
                Finite(x.FontSize);
                ArgumentNullException.ThrowIfNull(x.Text);
                break;
            case DrawTextGradientCommand x:
                ValidateRect(x.Rectangle);
                ValidateGradient(x.Gradient);
                Finite(x.FontSize, x.Opacity);
                ArgumentNullException.ThrowIfNull(x.Text);
                break;
            case DrawTextShadowCommand x:
                ValidateRect(x.Rectangle);
                ValidateCommandShadow(x.Shadow);
                Finite(x.FontSize);
                ArgumentNullException.ThrowIfNull(x.Text);
                break;
            case DrawBackgroundImageCommand x:
                ValidateRect(x.PaintRectangle);
                ValidateRect(x.SourceRectangle);
                ValidateRect(x.DestinationRectangle);
                Finite(x.Opacity);
                break;
            case ApplyBackdropFilterCommand x:
                ValidateRect(x.Rectangle);
                ValidateRadii(x.Radii);
                if (x.Filters is null || x.Filters.Count > 256)
                    throw new ArgumentException("Invalid filter count.");
                foreach (var f in x.Filters)
                {
                    Finite(f.ScalarAmount, f.LengthAmount.Value);
                    if (!Enum.IsDefined(f.Kind))
                        throw new ArgumentException("Unknown backdrop filter.");
                }
                break;
            case PushClipCommand x:
                ValidateRect(x.Rectangle);
                ValidateRadii(x.Radii);
                break;
            case PushTransformCommand x:
                var m = x.Matrix;
                Finite(m.M11, m.M12, m.M21, m.M22, m.Dx, m.Dy);
                break;
        }
    }
}
