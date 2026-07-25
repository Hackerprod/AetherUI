namespace AetherUI.StyleRuntime;

internal abstract class MediaCondition
{
    public static MediaCondition Always { get; } = new ConstantCondition(true);
    public static MediaCondition Never { get; } = new ConstantCondition(false);

    public abstract bool Matches(in StyleEnvironment environment);

    public static MediaCondition And(MediaCondition left, MediaCondition right)
    {
        if (ReferenceEquals(left, Always))
        {
            return right;
        }

        if (ReferenceEquals(right, Always))
        {
            return left;
        }

        if (ReferenceEquals(left, Never) || ReferenceEquals(right, Never))
        {
            return Never;
        }

        return new AndCondition(left, right);
    }

    public static MediaCondition Or(MediaCondition left, MediaCondition right)
    {
        if (ReferenceEquals(left, Never))
        {
            return right;
        }

        if (ReferenceEquals(right, Never))
        {
            return left;
        }

        if (ReferenceEquals(left, Always) || ReferenceEquals(right, Always))
        {
            return Always;
        }

        return new OrCondition(left, right);
    }

    public static MediaCondition Not(MediaCondition value) => new NotCondition(value);

    public static MediaCondition MinimumWidth(StyleLength value) => new LengthFeatureCondition(LengthFeature.MinimumWidth, value);
    public static MediaCondition MaximumWidth(StyleLength value) => new LengthFeatureCondition(LengthFeature.MaximumWidth, value);
    public static MediaCondition MinimumHeight(StyleLength value) => new LengthFeatureCondition(LengthFeature.MinimumHeight, value);
    public static MediaCondition MaximumHeight(StyleLength value) => new LengthFeatureCondition(LengthFeature.MaximumHeight, value);
    public static MediaCondition WidthEquals(StyleLength value) => new LengthFeatureCondition(LengthFeature.WidthEquals, value);
    public static MediaCondition HeightEquals(StyleLength value) => new LengthFeatureCondition(LengthFeature.HeightEquals, value);
    public static MediaCondition ColorScheme(StyleColorScheme value) => new ColorSchemeCondition(value);
    public static MediaCondition Pointer(StylePointerAccuracy value) => new PointerCondition(value);
    public static MediaCondition Motion(StyleMotionPreference value) => new MotionCondition(value);

    private sealed class ConstantCondition(bool value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) => value;
    }

    private sealed class AndCondition(MediaCondition left, MediaCondition right) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) =>
            left.Matches(environment) && right.Matches(environment);
    }

    private sealed class OrCondition(MediaCondition left, MediaCondition right) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) =>
            left.Matches(environment) || right.Matches(environment);
    }

    private sealed class NotCondition(MediaCondition value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) => !value.Matches(environment);
    }

    private enum LengthFeature : byte
    {
        MinimumWidth,
        MaximumWidth,
        MinimumHeight,
        MaximumHeight,
        WidthEquals,
        HeightEquals,
    }

    private sealed class LengthFeatureCondition(LengthFeature feature, StyleLength value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment)
        {
            var isWidth = feature is LengthFeature.MinimumWidth or LengthFeature.MaximumWidth or LengthFeature.WidthEquals;
            var actual = isWidth ? environment.ViewportWidth : environment.ViewportHeight;
            var expected = value.Resolve(new StyleLengthResolveContext(
                actual,
                environment.RootFontSize,
                environment.RootFontSize,
                environment.ViewportWidth,
                environment.ViewportHeight));
            if (!float.IsFinite(expected))
            {
                return false;
            }

            return feature switch
            {
                LengthFeature.MinimumWidth or LengthFeature.MinimumHeight => actual >= expected,
                LengthFeature.MaximumWidth or LengthFeature.MaximumHeight => actual <= expected,
                _ => Math.Abs(actual - expected) < 0.01f,
            };
        }
    }

    private sealed class ColorSchemeCondition(StyleColorScheme value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) => environment.ColorScheme == value;
    }

    private sealed class PointerCondition(StylePointerAccuracy value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) => environment.PointerAccuracy == value;
    }

    private sealed class MotionCondition(StyleMotionPreference value) : MediaCondition
    {
        public override bool Matches(in StyleEnvironment environment) => environment.MotionPreference == value;
    }
}
