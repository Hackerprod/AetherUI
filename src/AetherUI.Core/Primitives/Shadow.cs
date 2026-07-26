namespace AetherUI.Primitives;

/// <summary>A resolved (pixel-space) CSS box-shadow. Inset shadows are parsed but not yet painted.</summary>
public readonly record struct BoxShadow(float OffsetX, float OffsetY, float BlurRadius, float SpreadRadius, ColorRgba Color, bool Inset);
