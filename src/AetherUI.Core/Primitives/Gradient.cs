namespace AetherUI.Primitives;

public readonly record struct GradientStop(ColorRgba Color, float Offset);

/// <summary>A linear gradient fill. Direction is a unit vector rather than an angle, since that's
/// what a renderer needs to map onto a fill rectangle's start/end points directly.</summary>
public readonly record struct LinearGradient(float DirectionX, float DirectionY, IReadOnlyList<GradientStop> Stops);
