namespace AetherUI.StyleRuntime;

public enum StyleColorScheme : byte
{
    Light,
    Dark,
}

public enum StylePointerAccuracy : byte
{
    None,
    Coarse,
    Fine,
}

public enum StyleMotionPreference : byte
{
    NoPreference,
    Reduce,
}

/// <summary>Live values owned by the Aether host and consumed by media conditions and relative units.</summary>
public readonly record struct StyleEnvironment(
    float ViewportWidth,
    float ViewportHeight,
    float DpiScale = 1f,
    float RootFontSize = 16f,
    StyleColorScheme ColorScheme = StyleColorScheme.Light,
    StylePointerAccuracy PointerAccuracy = StylePointerAccuracy.Fine,
    StyleMotionPreference MotionPreference = StyleMotionPreference.NoPreference)
{
    public static StyleEnvironment Default => new(1280f, 720f);

    public StyleEnvironment Validate()
    {
        if (!float.IsFinite(ViewportWidth) || ViewportWidth < 0f)
        {
            throw new ArgumentOutOfRangeException(nameof(ViewportWidth));
        }

        if (!float.IsFinite(ViewportHeight) || ViewportHeight < 0f)
        {
            throw new ArgumentOutOfRangeException(nameof(ViewportHeight));
        }

        if (!float.IsFinite(DpiScale) || DpiScale <= 0f)
        {
            throw new ArgumentOutOfRangeException(nameof(DpiScale));
        }

        if (!float.IsFinite(RootFontSize) || RootFontSize <= 0f)
        {
            throw new ArgumentOutOfRangeException(nameof(RootFontSize));
        }

        return this;
    }
}
