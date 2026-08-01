using AetherUI.Css;
using AetherUI.Primitives;

namespace AetherUI.Rendering;

public abstract record PaintCommand;

public readonly record struct CornerRadii(
    float TopLeftX, float TopLeftY, float TopRightX, float TopRightY,
    float BottomRightX, float BottomRightY, float BottomLeftX, float BottomLeftY)
{
    public static readonly CornerRadii Zero = default;
    public static CornerRadii Uniform(float radius) => new(radius, radius, radius, radius, radius, radius, radius, radius);
    public bool IsZero => TopLeftX <= .5f && TopLeftY <= .5f && TopRightX <= .5f && TopRightY <= .5f &&
                          BottomRightX <= .5f && BottomRightY <= .5f && BottomLeftX <= .5f && BottomLeftY <= .5f;
}

public sealed record FillRoundedRectangleCommand(RectF Rectangle, CornerRadii Radii, ColorRgba Color) : PaintCommand;
public sealed record DrawBoxShadowCommand(RectF BorderBoxRectangle, CornerRadii Radii, BoxShadow Shadow) : PaintCommand;
/// <summary>
/// Fills <see cref="Rectangle"/> while evaluating the gradient in <see cref="GradientRectangle"/>
/// coordinates. The distinction preserves phase across clipped/repeated CSS background tiles.
/// </summary>
public sealed record FillRoundedRectangleGradientCommand(
    RectF Rectangle, CornerRadii Radii, RectF GradientRectangle, LinearGradient Gradient, float Opacity) : PaintCommand;
public sealed record StrokeRoundedRectangleCommand(RectF Rectangle, CornerRadii Radii, float StrokeWidth, ColorRgba Color) : PaintCommand;
public sealed record FillBorderSideCommand(
    RectF OuterRectangle, CornerRadii OuterRadii,
    RectF InnerRectangle, CornerRadii InnerRadii,
    int Side, int Style, ColorRgba Color) : PaintCommand;
public sealed record DrawTextCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    ColorRgba Color,
    bool Wrap = false) : PaintCommand;
public sealed record DrawTextGradientCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    LinearGradient Gradient,
    float Opacity) : PaintCommand;
public sealed record DrawTextShadowCommand(
    string Text,
    RectF Rectangle,
    string FontFamily,
    float FontSize,
    FontWeight FontWeight,
    TextAlign Alignment,
    BoxShadow Shadow,
    bool Wrap = false) : PaintCommand;
public enum BackgroundImageSizeKind { Auto, Explicit, Cover, Contain }
public enum BackgroundRepeat { Repeat, NoRepeat, Space, Round }
public enum BackdropFilterKind { Blur, Brightness, Contrast, Saturate }
public readonly record struct BackdropFilter(BackdropFilterKind Kind, CssLength LengthAmount, float ScalarAmount);
public sealed record ApplyBackdropFilterCommand(
    RectF Rectangle, CornerRadii Radii, IReadOnlyList<BackdropFilter> Filters) : PaintCommand;
public sealed record DrawBackgroundImageCommand(
    string Source,
    RectF PaintRectangle,
    CssLength PositionX,
    CssLength PositionY,
    BackgroundImageSizeKind SizeKind,
    CssLength SizeX,
    CssLength SizeY,
    BackgroundRepeat RepeatX,
    BackgroundRepeat RepeatY,
    float Opacity,
    RectF SourceRectangle,
    RectF DestinationRectangle,
    bool HasResolvedGeometry,
    AetherUI.Runtime.ImageResourceResult? Resource = null) : PaintCommand;
public sealed record PushClipCommand(RectF Rectangle, CornerRadii Radii) : PaintCommand;
public sealed record PopClipCommand : PaintCommand;
/// <summary>Post-layout affine transform bracket. Nested brackets preserve CSS ancestor scope.</summary>
public readonly record struct TransformMatrix(float M11, float M12, float M21, float M22, float Dx, float Dy);
public sealed record PushTransformCommand(TransformMatrix Matrix) : PaintCommand;
public sealed record PopTransformCommand : PaintCommand;
/// <summary>Applies a live two-axis offset for the retained scroll container.</summary>
public sealed record PushScrollOffsetCommand(long ScrollContainerId) : PaintCommand;
public sealed record PopScrollOffsetCommand : PaintCommand;
/// <summary>Delegates dynamic thumb geometry to the renderer's scroll-state provider.</summary>
public sealed record DrawScrollbarChromeCommand(long ScrollContainerId) : PaintCommand;

public sealed record PaintChunk(long StyleNodeId, int Start, int End, RectF Bounds, int Depth);

public readonly record struct ScrollVisualOffset(float X, float Y);
public readonly record struct ScrollbarChrome(
    bool HorizontalVisible,
    bool VerticalVisible,
    RectF HorizontalTrack,
    RectF HorizontalThumb,
    RectF VerticalTrack,
    RectF VerticalThumb,
    RectF Corner);
public readonly record struct ScrollVisualState(ScrollVisualOffset Offset, ScrollbarChrome Chrome);

public sealed class DisplayList
{
    private static long s_nextGeneration;

    public DisplayList(IReadOnlyList<PaintCommand> commands, IReadOnlyList<PaintChunk>? chunks = null)
    {
        Commands = commands ?? throw new ArgumentNullException(nameof(commands));
        PaintChunks = chunks ?? Array.Empty<PaintChunk>();
        Generation = System.Threading.Interlocked.Increment(ref s_nextGeneration);
    }

    public IReadOnlyList<PaintCommand> Commands { get; }
    public IReadOnlyList<PaintChunk> PaintChunks { get; }
    public long Generation { get; }
    public int Count => Commands.Count;

    public bool TryReplaceChunks(
        IReadOnlyCollection<long> dirtyStyleNodeIds,
        IReadOnlyDictionary<long, DisplayList> replacements,
        out DisplayList updated)
    {
        updated = this;
        if (dirtyStyleNodeIds.Count == 0 || PaintChunks.Count == 0)
        {
            return false;
        }

        var selected = PaintChunks
            .Where(chunk => dirtyStyleNodeIds.Contains(chunk.StyleNodeId) && replacements.ContainsKey(chunk.StyleNodeId))
            .OrderBy(chunk => chunk.Start)
            .ToList();
        if (selected.Count == 0)
        {
            return false;
        }

        // Replace only outermost dirty chunks. If both parent and descendant changed,
        // regenerating the parent subtree already contains the descendant's new paint.
        selected = selected
            .Where(chunk => !selected.Any(other =>
                !ReferenceEquals(chunk, other) &&
                other.Start <= chunk.Start && other.End >= chunk.End &&
                (other.Start != chunk.Start || other.End != chunk.End)))
            .ToList();

        var selectedRanges = selected.Select(chunk => (chunk.Start, chunk.End)).ToArray();
        var newCommands = new List<PaintCommand>(Commands.Count);
        var cursor = 0;
        foreach (var chunk in selected)
        {
            if (chunk.Start < cursor)
            {
                return false;
            }

            CopyCommands(Commands, cursor, chunk.Start, newCommands);
            CopyCommands(replacements[chunk.StyleNodeId].Commands, 0,
                replacements[chunk.StyleNodeId].Commands.Count, newCommands);
            cursor = chunk.End;
        }
        CopyCommands(Commands, cursor, Commands.Count, newCommands);

        var newChunks = new List<PaintChunk>(PaintChunks.Count);
        foreach (var oldChunk in PaintChunks)
        {
            if (selectedRanges.Any(range => oldChunk.Start >= range.Start && oldChunk.End <= range.End))
            {
                continue;
            }

            if (!TryMapPosition(oldChunk.Start, selected, replacements, out var mappedStart) ||
                !TryMapPosition(oldChunk.End, selected, replacements, out var mappedEnd))
            {
                continue;
            }

            newChunks.Add(oldChunk with { Start = mappedStart, End = mappedEnd });
        }

        var cumulativeDelta = 0;
        foreach (var chunk in selected)
        {
            var insertionStart = chunk.Start + cumulativeDelta;
            var replacement = replacements[chunk.StyleNodeId];
            foreach (var replacementChunk in replacement.PaintChunks)
            {
                newChunks.Add(replacementChunk with
                {
                    Start = insertionStart + replacementChunk.Start,
                    End = insertionStart + replacementChunk.End
                });
            }

            cumulativeDelta += replacement.Commands.Count - (chunk.End - chunk.Start);
        }

        updated = new DisplayList(newCommands, newChunks
            .OrderBy(chunk => chunk.Start)
            .ThenByDescending(chunk => chunk.End)
            .ThenBy(chunk => chunk.Depth)
            .ToArray());
        return true;
    }

    private static void CopyCommands(IReadOnlyList<PaintCommand> source, int start, int end, List<PaintCommand> target)
    {
        for (var index = start; index < end; index++)
        {
            target.Add(source[index]);
        }
    }

    private static bool TryMapPosition(
        int position,
        IReadOnlyList<PaintChunk> selected,
        IReadOnlyDictionary<long, DisplayList> replacements,
        out int mapped)
    {
        var delta = 0;
        foreach (var chunk in selected)
        {
            if (position <= chunk.Start)
            {
                break;
            }

            if (position < chunk.End)
            {
                mapped = 0;
                return false;
            }

            delta += replacements[chunk.StyleNodeId].Commands.Count - (chunk.End - chunk.Start);
        }

        mapped = position + delta;
        return true;
    }
}

internal static class PaintCommandBounds
{
    internal static bool TryGet(PaintCommand command, out RectF bounds)
    {
        switch (command)
        {
            case FillRoundedRectangleCommand fill:
                bounds = fill.Rectangle;
                return true;
            case FillRoundedRectangleGradientCommand fill:
                bounds = fill.Rectangle;
                return true;
            case DrawBoxShadowCommand shadow:
            {
                var spread = MathF.Abs(shadow.Shadow.SpreadRadius);
                var blur = MathF.Max(0, shadow.Shadow.BlurRadius);
                bounds = shadow.BorderBoxRectangle.Inflate(spread + (blur * 2) + 4)
                    .Offset(shadow.Shadow.OffsetX, shadow.Shadow.OffsetY);
                return true;
            }
            case StrokeRoundedRectangleCommand stroke:
                bounds = stroke.Rectangle.Inflate(MathF.Max(.25f, stroke.StrokeWidth) * .5f + 2);
                return true;
            case FillBorderSideCommand border:
                bounds = border.OuterRectangle.Inflate(2);
                return true;
            case DrawTextCommand text:
                bounds = text.Rectangle.Inflate(2);
                return true;
            case DrawTextGradientCommand text:
                bounds = text.Rectangle.Inflate(2);
                return true;
            case DrawTextShadowCommand textShadow:
                bounds = textShadow.Rectangle
                    .Offset(textShadow.Shadow.OffsetX, textShadow.Shadow.OffsetY)
                    .Inflate(MathF.Max(0, textShadow.Shadow.BlurRadius) * 2 + 4);
                return true;
            case DrawBackgroundImageCommand image:
                bounds = image.HasResolvedGeometry ? image.DestinationRectangle : image.PaintRectangle;
                return true;
            case ApplyBackdropFilterCommand backdrop:
                bounds = backdrop.Rectangle;
                return true;
            default:
                bounds = default;
                return false;
        }
    }

    internal static RectF BoundsForRange(IReadOnlyList<PaintCommand> commands, int start, int end)
    {
        RectF? union = null;
        for (var index = start; index < end; index++)
        {
            if (!TryGet(commands[index], out var bounds) || bounds.IsEmpty)
            {
                continue;
            }

            union = union is { } current
                ? RectF.FromEdges(MathF.Min(current.Left, bounds.Left), MathF.Min(current.Top, bounds.Top),
                    MathF.Max(current.Right, bounds.Right), MathF.Max(current.Bottom, bounds.Bottom))
                : bounds;
        }

        return union ?? default;
    }
}

public interface IRenderBackend : IDisposable
{
    void Resize(int pixelWidth, int pixelHeight, float dpiScale);
    void Render(DisplayList displayList, ColorRgba clearColor);
    void SetScrollCompositionMode(bool enabled) { }
    void SetScrollOffsetProvider(Func<long, ScrollVisualOffset>? provider) { }
    void SetScrollVisualStateProvider(Func<long, ScrollVisualState>? provider)
    {
        SetScrollOffsetProvider(provider is null ? null : id => provider(id).Offset);
    }
}

public interface IIncrementalRenderBackend
{
    bool TryRenderIncremental(DisplayList displayList, ColorRgba clearColor);
}
