namespace AetherUI.Primitives;

public readonly record struct PointF(float X, float Y);

public readonly record struct SizeF(float Width, float Height)
{
    public static readonly SizeF Empty = new(0, 0);
}

public readonly record struct RectF(float X, float Y, float Width, float Height)
{
    public float Left => X;
    public float Top => Y;
    public float Right => X + Width;
    public float Bottom => Y + Height;
    public bool IsEmpty => Width <= 0 || Height <= 0;

    public bool Contains(PointF point) =>
        point.X >= Left && point.X <= Right && point.Y >= Top && point.Y <= Bottom;

    public RectF Deflate(ThicknessF value) => new(
        X + value.Left,
        Y + value.Top,
        MathF.Max(0, Width - value.Horizontal),
        MathF.Max(0, Height - value.Vertical));

    public RectF Inflate(float value) => new(
        X - value,
        Y - value,
        Width + (value * 2),
        Height + (value * 2));

    public RectF Offset(float dx, float dy) => new(X + dx, Y + dy, Width, Height);

    public static RectF FromEdges(float left, float top, float right, float bottom) =>
        new(left, top, MathF.Max(0, right - left), MathF.Max(0, bottom - top));
}

public readonly record struct ThicknessF(float Left, float Top, float Right, float Bottom)
{
    public ThicknessF(float uniform) : this(uniform, uniform, uniform, uniform)
    {
    }

    public float Horizontal => Left + Right;
    public float Vertical => Top + Bottom;
    public static readonly ThicknessF Zero = new(0);
}
