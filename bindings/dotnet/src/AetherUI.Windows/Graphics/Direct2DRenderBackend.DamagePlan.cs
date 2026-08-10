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
/// Direct2D renderer region: Damage analysis and repaint-range planning for incremental rendering.
/// </summary>
public sealed partial class Direct2DRenderBackend
{
    private sealed record CommandRange(int Start, int End);

    private sealed record DamagePlan(
        int FirstChangedIndex,
        IReadOnlyList<RectF> DirtyRects,
        IReadOnlyList<CommandRange> ChangedRanges,
        IReadOnlyList<int> StateCommandIndices);

    private bool TryCreateDamagePlan(DisplayList previous, DisplayList current, out DamagePlan plan)
    {
        plan = new DamagePlan(0, Array.Empty<RectF>(), Array.Empty<CommandRange>(), Array.Empty<int>());

        static bool Reject(string reason, bool incrementalDebug, int index = -1)
        {
            if (incrementalDebug)
            {
                Console.WriteLine(reason);
            }

            return false;
        }

        var oldCommands = previous.Commands;
        var newCommands = current.Commands;
        if (oldCommands.Count != newCommands.Count || oldCommands.Count == 0)
        {
            return Reject(
                $"TryCreateDamagePlan reject: count mismatch old={oldCommands.Count} new={newCommands.Count}",
                _incrementalDebug);
        }

        var dirtyRects = new List<RectF>();
        var firstChanged = -1;
        var scrollOffsets = new Stack<ScrollVisualOffset>();
        var currentScrollOffset = new ScrollVisualOffset(0, 0);
        for (var i = 0; i < oldCommands.Count; i++)
        {
            var oldCommand = oldCommands[i];
            var newCommand = newCommands[i];
            if (oldCommand.GetType() != newCommand.GetType())
            {
                return Reject(
                    $"TryCreateDamagePlan reject: type mismatch index={i} old={oldCommand.GetType().Name} new={newCommand.GetType().Name}",
                    _incrementalDebug);
            }

            if (RequiresFullRenderForDamage(oldCommand))
            {
            return Reject(
                $"TryCreateDamagePlan reject: unsupported command for incremental index={i} type={oldCommand.GetType().Name}",
                _incrementalDebug);
        }

            if (oldCommand is PushScrollOffsetCommand oldScroll)
            {
                if (!PaintCommandsEqualForDamage(oldCommand, newCommand))
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: scroll container changed at index={i} type={oldCommand.GetType().Name}",
                        _incrementalDebug);
                }

                scrollOffsets.Push(currentScrollOffset);
                var offset = _scrollStateProvider?.Invoke(oldScroll.ScrollContainerId).Offset ?? default;
                currentScrollOffset = new ScrollVisualOffset(
                    currentScrollOffset.X + offset.X,
                    currentScrollOffset.Y + offset.Y);
                continue;
            }

            if (oldCommand is PopScrollOffsetCommand)
            {
                if (scrollOffsets.Count == 0)
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: unmatched scroll pop index={i}",
                        _incrementalDebug);
                }

                currentScrollOffset = scrollOffsets.Pop();
                continue;
            }

            if (oldCommand is PushClipCommand or PopClipCommand or DrawScrollbarChromeCommand)
            {
                if (!PaintCommandsEqualForDamage(oldCommand, newCommand))
                {
                    return Reject(
                        $"TryCreateDamagePlan reject: structural command changed index={i} type={oldCommand.GetType().Name}",
                        _incrementalDebug);
                }

                continue;
            }

            if (PaintCommandsEqualForDamage(oldCommand, newCommand))
            {
                continue;
            }

            if (!TryGetCommandBounds(oldCommand, out var oldBounds) ||
                !TryGetCommandBounds(newCommand, out var newBounds))
            {
                return Reject(
                    $"TryCreateDamagePlan reject: bounds unavailable index={i} type={oldCommand.GetType().Name}",
                    _incrementalDebug);
            }

            firstChanged = firstChanged < 0 ? i : firstChanged;
            oldBounds = TranslateForScroll(oldBounds, currentScrollOffset);
            newBounds = TranslateForScroll(newBounds, currentScrollOffset);
            AddDirtyRect(dirtyRects, oldBounds.Inflate(3));
            AddDirtyRect(dirtyRects, newBounds.Inflate(3));
        }

        if (firstChanged < 0)
        {
            plan = new DamagePlan(0, Array.Empty<RectF>(), Array.Empty<CommandRange>(), Array.Empty<int>());
            return true;
        }

        if (scrollOffsets.Count != 0)
        {
            return Reject("TryCreateDamagePlan reject: unmatched scroll push", _incrementalDebug);
        }

        var viewport = new RectF(0, 0, _targetPixelWidth / _targetDpiScale, _targetPixelHeight / _targetDpiScale);
        var clippedDirtyRects = new List<RectF>(dirtyRects.Count);
        var dirtyArea = 0f;
        foreach (var dirtyRect in dirtyRects)
        {
            var clipped = Intersect(dirtyRect, viewport);
            if (clipped.IsEmpty)
            {
                continue;
            }

            clippedDirtyRects.Add(clipped);
            dirtyArea += clipped.Width * clipped.Height;
        }

        if (_incrementalDebug && clippedDirtyRects.Count > 0)
        {
            Console.WriteLine($"TryCreateDamagePlan firstChanged={firstChanged} dirtyRects={dirtyRects.Count} clipped={clippedDirtyRects.Count}");
            for (var i = 0; i < clippedDirtyRects.Count; i++)
            {
                var rect = clippedDirtyRects[i];
                Console.WriteLine($"  dirty[{i}] x={rect.X:F2} y={rect.Y:F2} w={rect.Width:F2} h={rect.Height:F2}");
            }
        }

        if (clippedDirtyRects.Count == 0)
        {
            return Reject("TryCreateDamagePlan reject: no visible dirty area", _incrementalDebug);
        }

        if (dirtyArea > viewport.Width * viewport.Height * 0.45f)
        {
            if (_incrementalDebug)
            {
                Console.WriteLine(
                    $"TryCreateDamagePlan reject: dirty area too large area={dirtyArea} viewport={viewport.Width * viewport.Height}");
            }

            return false;
        }

        var changedRanges = CollectRepaintRanges(newCommands, clippedDirtyRects);
        if (changedRanges.Count == 0)
        {
            return Reject("TryCreateDamagePlan reject: dirty area has no repaintable commands", _incrementalDebug);
        }

        if (_incrementalDebug && changedRanges.Count > 0)
        {
            Console.WriteLine($"TryCreateDamagePlan changed ranges: {changedRanges.Count}.");
            for (var i = 0; i < changedRanges.Count; i++)
            {
                Console.WriteLine($"  range[{i}]: {changedRanges[i].Start}-{changedRanges[i].End}");
            }
        }

        if (!TryCollectStateCommandIndices(oldCommands, changedRanges, out var stateCommandIndices))
        {
            return Reject("TryCreateDamagePlan reject: unbalanced state stack.", _incrementalDebug);
        }

        plan = new DamagePlan(firstChanged, clippedDirtyRects, changedRanges, stateCommandIndices);
        return true;
    }

    private List<CommandRange> CollectRepaintRanges(
        IReadOnlyList<PaintCommand> commands,
        IReadOnlyList<RectF> dirtyRects)
    {
        var repaintIndices = new List<int>();
        var scrollOffsets = new Stack<ScrollVisualOffset>();
        var currentScrollOffset = new ScrollVisualOffset(0, 0);

        for (var i = 0; i < commands.Count; i++)
        {
            var command = commands[i];
            switch (command)
            {
                case PushScrollOffsetCommand scroll:
                {
                    scrollOffsets.Push(currentScrollOffset);
                    var offset = _scrollStateProvider?.Invoke(scroll.ScrollContainerId).Offset ?? default;
                    currentScrollOffset = new ScrollVisualOffset(
                        currentScrollOffset.X + offset.X,
                        currentScrollOffset.Y + offset.Y);
                    continue;
                }
                case PopScrollOffsetCommand:
                    if (scrollOffsets.Count == 0)
                    {
                        return [];
                    }

                    currentScrollOffset = scrollOffsets.Pop();
                    continue;
                case PushClipCommand or PopClipCommand or DrawScrollbarChromeCommand:
                    continue;
            }

            if (TryGetCommandBounds(command, out var bounds) &&
                CommandIntersectsDirtyRects(bounds, dirtyRects, currentScrollOffset))
            {
                repaintIndices.Add(i);
            }
        }

        if (scrollOffsets.Count != 0 || repaintIndices.Count == 0)
        {
            return [];
        }

        return ToCommandRanges(repaintIndices);
    }

    private static List<CommandRange> ToCommandRanges(List<int> indices)
    {
        indices.Sort();
        var ranges = new List<CommandRange>(indices.Count);
        var rangeStart = indices[0];
        var rangeEnd = rangeStart + 1;
        for (var i = 1; i < indices.Count; i++)
        {
            var index = indices[i];
            if (index == rangeEnd)
            {
                rangeEnd = index + 1;
            }
            else
            {
                ranges.Add(new CommandRange(rangeStart, rangeEnd));
                rangeStart = index;
                rangeEnd = index + 1;
            }
        }

        ranges.Add(new CommandRange(rangeStart, rangeEnd));
        return ranges;
    }

    private static bool TryCollectStateCommandIndices(
        IReadOnlyList<PaintCommand> commands,
        IReadOnlyList<CommandRange> changedRanges,
        out IReadOnlyList<int> stateCommandIndices)
    {
        stateCommandIndices = Array.Empty<int>();

        var stateStack = new Stack<(int index, bool isScroll)>();
        var statePairs = new List<(int pushIndex, int popIndex)>();

        for (var i = 0; i < commands.Count; i++)
        {
            switch (commands[i])
            {
                case PushClipCommand:
                    stateStack.Push((i, isScroll: false));
                    break;
                case PopClipCommand:
                    if (stateStack.Count == 0)
                    {
                        return false;
                    }

                    var statePop = stateStack.Pop();
                    if (statePop.isScroll)
                    {
                        return false;
                    }

                    statePairs.Add((statePop.index, i));
                    break;
                case PushScrollOffsetCommand:
                    stateStack.Push((i, isScroll: true));
                    break;
                case PopScrollOffsetCommand:
                    if (stateStack.Count == 0)
                    {
                        return false;
                    }

                    var statePush = stateStack.Pop();
                    if (!statePush.isScroll)
                    {
                        return false;
                    }

                    statePairs.Add((statePush.index, i));
                    break;
            }
        }

        if (stateStack.Count != 0)
        {
            return false;
        }

        static bool SpansChanged(int push, int pop, IReadOnlyList<CommandRange> ranges)
        {
            foreach (var range in ranges)
            {
                if (range.Start < pop && range.End > push)
                {
                    return true;
                }

                if (range.Start > pop)
                {
                    return false;
                }
            }

            return false;
        }

        var changedStateCommands = new HashSet<int>(Math.Min(commands.Count, 128));
        foreach (var (push, pop) in statePairs)
        {
            if (SpansChanged(push, pop, changedRanges))
            {
                changedStateCommands.Add(push);
                changedStateCommands.Add(pop);
            }
        }

        if (changedStateCommands.Count == 0)
        {
            return true;
        }

        var stateIndices = changedStateCommands.ToArray();
        Array.Sort(stateIndices);
        stateCommandIndices = stateIndices;
        return true;
    }

    private static bool RequiresFullRenderForDamage(PaintCommand command) =>
        command is PushTransformCommand or PopTransformCommand or
            ApplyBackdropFilterCommand or DrawTextShadowCommand;

    private static RectF TranslateForScroll(RectF rect, ScrollVisualOffset offset) =>
        new(rect.X - offset.X, rect.Y - offset.Y, rect.Width, rect.Height);

    private static void AddDirtyRect(List<RectF> dirtyRects, RectF rect)
    {
        if (rect.IsEmpty)
        {
            return;
        }

        for (var i = 0; i < dirtyRects.Count; i++)
        {
            if (!RectsIntersect(dirtyRects[i], rect))
            {
                continue;
            }

            dirtyRects[i] = Union(dirtyRects[i], rect);
            return;
        }

        dirtyRects.Add(rect);
    }

    private static bool PaintCommandsEqualForDamage(PaintCommand oldCommand, PaintCommand newCommand)
    {
        if (ReferenceEquals(oldCommand, newCommand))
        {
            return true;
        }

        if (oldCommand.GetType() != newCommand.GetType())
        {
            return false;
        }

        return oldCommand switch
        {
            FillRoundedRectangleGradientCommand oldGradient
                when newCommand is FillRoundedRectangleGradientCommand newGradient =>
                    oldGradient.Rectangle.Equals(newGradient.Rectangle) &&
                    oldGradient.Radii.Equals(newGradient.Radii) &&
                    oldGradient.GradientRectangle.Equals(newGradient.GradientRectangle) &&
                    oldGradient.Opacity.Equals(newGradient.Opacity) &&
                    LinearGradientsEqual(oldGradient.Gradient, newGradient.Gradient),

            DrawTextGradientCommand oldTextGradient
                when newCommand is DrawTextGradientCommand newTextGradient =>
                    string.Equals(oldTextGradient.Text, newTextGradient.Text, StringComparison.Ordinal) &&
                    oldTextGradient.Rectangle.Equals(newTextGradient.Rectangle) &&
                    string.Equals(oldTextGradient.FontFamily, newTextGradient.FontFamily, StringComparison.Ordinal) &&
                    oldTextGradient.FontSize.Equals(newTextGradient.FontSize) &&
                    oldTextGradient.FontWeight.Equals(newTextGradient.FontWeight) &&
                    oldTextGradient.Alignment.Equals(newTextGradient.Alignment) &&
                    oldTextGradient.Opacity.Equals(newTextGradient.Opacity) &&
                    LinearGradientsEqual(oldTextGradient.Gradient, newTextGradient.Gradient),

            _ => oldCommand.Equals(newCommand)
        };
    }

    private static bool LinearGradientsEqual(LinearGradient oldGradient, LinearGradient newGradient)
    {
        if (!oldGradient.DirectionX.Equals(newGradient.DirectionX) ||
            !oldGradient.DirectionY.Equals(newGradient.DirectionY) ||
            oldGradient.Stops.Count != newGradient.Stops.Count)
        {
            return false;
        }

        for (var i = 0; i < oldGradient.Stops.Count; i++)
        {
            if (!oldGradient.Stops[i].Equals(newGradient.Stops[i]))
            {
                return false;
            }
        }

        return true;
    }

    private static RectF? Union(RectF? current, RectF next) =>
        current is { } rect ? Union(rect, next) : next;

    private static RectF Union(RectF a, RectF b) =>
        RectF.FromEdges(MathF.Min(a.Left, b.Left), MathF.Min(a.Top, b.Top),
            MathF.Max(a.Right, b.Right), MathF.Max(a.Bottom, b.Bottom));

    private static bool RectsIntersect(RectF a, RectF b) =>
        a.Left < b.Right && a.Right > b.Left && a.Top < b.Bottom && a.Bottom > b.Top;

    private static bool TryGetCommandBounds(PaintCommand command, out RectF bounds)
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
                var inflated = shadow.BorderBoxRectangle.Inflate(spread + (blur * 2) + 4);
                bounds = inflated.Offset(shadow.Shadow.OffsetX, shadow.Shadow.OffsetY);
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
            case DrawBackgroundImageCommand image:
                bounds = image.HasResolvedGeometry ? image.DestinationRectangle : image.PaintRectangle;
                return true;
            case PushClipCommand or PopClipCommand:
                bounds = default;
                return false;
            default:
                bounds = default;
                return false;
        }
    }

    private static bool CommandIntersectsDirtyRects(RectF commandBounds, IReadOnlyList<RectF> dirtyRects, ScrollVisualOffset scrollOffset)
    {
        var translated = TranslateForScroll(commandBounds, scrollOffset);
        for (var i = 0; i < dirtyRects.Count; i++)
        {
            if (RectsIntersect(translated, dirtyRects[i]))
            {
                return true;
            }
        }

        return false;
    }

    private static RectF Intersect(RectF a, RectF b)
    {
        var left = MathF.Max(a.Left, b.Left);
        var top = MathF.Max(a.Top, b.Top);
        var right = MathF.Min(a.Right, b.Right);
        var bottom = MathF.Min(a.Bottom, b.Bottom);
        return RectF.FromEdges(left, top, right, bottom);
    }

}
