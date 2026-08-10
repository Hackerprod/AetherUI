using AetherUI.Css;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using System.Diagnostics;

namespace AetherUI.Runtime;

/// <summary>
/// Document host region: Display-list build, attach and render scheduling.
/// </summary>
public sealed partial class UiDocumentHost
{
    public event EventHandler? RenderRequested;

    public void Render()
    {
        ThrowIfDisposed();
        var initialPlan = EvaluateRenderPlan();
        var scrollInvalidated = (initialPlan.NormalizedInvalidation & (uint)VisualInvalidation.Scroll) != 0;
        var scrollOnly = initialPlan.UseScrollComposition != 0;
        var pointerRenderRequested = _pointerRenderPending;
        var pointerOnlyRender = pointerRenderRequested && initialPlan.PointerOnlyRender != 0;
        var displayListBuildsBefore = Interlocked.Read(ref _displayListBuildCount);
        var paintChunkBuildsBefore = Interlocked.Read(ref _paintChunkBuildCount);
        EnsureVisualTree();
        if (ApplyPendingPointerMove())
        {
            scrollOnly = false;
            EnsureVisualTree();
        }
        var renderPlan = EvaluateRenderPlan();

        _pointerRenderPending = false;
        var displayListChanged =
            Interlocked.Read(ref _displayListBuildCount) != displayListBuildsBefore ||
            Interlocked.Read(ref _paintChunkBuildCount) != paintChunkBuildsBefore;
        if ((pointerOnlyRender || pointerRenderRequested) && !displayListChanged && !scrollOnly)
        {
            _options.MetricsSink?.Invoke(Metrics);
            return;
        }

        if (_imageResolver is not null)
            foreach (var image in _displayList!.Commands.OfType<DrawBackgroundImageCommand>())
                _imageResolver.Request(image.Source);
        _renderer.SetScrollVisualStateProvider(ScrollVisualStateFor);
        // Only a pure scroll frame may use retained scroll composition. A frame that also
        // rebuilt or patched paint must first publish a canonical full frame, otherwise the
        // renderer would composite new scroll offsets over a retained bitmap from a different
        // property state. This mirrors Chromium's split between active compositor property
        // updates and main-thread commits without pretending that a flat command list is a
        // complete property tree.
        _renderer.SetScrollCompositionMode(scrollOnly);
        var renderStart = Stopwatch.GetTimestamp();
        if (!scrollInvalidated &&
            !scrollOnly &&
            renderPlan.AllowIncrementalRender != 0 &&
            _renderer is IIncrementalRenderBackend incrementalRenderer &&
            incrementalRenderer.TryRenderIncremental(_displayList!, _options.ClearColor))
        {
            Interlocked.Increment(ref _incrementalRenderCallCount);
        }
        else
        {
            _renderer.Render(_displayList!, _options.ClearColor);
        }
        Interlocked.Add(ref _renderTimestampTicks, Stopwatch.GetTimestamp() - renderStart);
        Interlocked.Increment(ref _renderCallCount);
        if (scrollOnly) Interlocked.Increment(ref _scrollCompositionCount);
        _options.MetricsSink?.Invoke(Metrics);
    }

    private ScrollVisualState ScrollVisualStateFor(long styleNodeId)
    {
        var offset = _scrollOffsetsByStyleNode.GetValueOrDefault(styleNodeId);
        var chrome = _scrollBoxesByStyleNode.TryGetValue(styleNodeId, out var box)
            ? ToScrollbarChrome(_layoutInterop.GetScrollbarGeometryAtOffset(
                box,
                new ScrollOffset(offset.X, offset.Y)))
            : default;
        return new ScrollVisualState(offset, chrome);
    }

    private static ScrollbarChrome ToScrollbarChrome(ScrollbarGeometry geometry) => new(
        geometry.HorizontalVisible,
        geometry.VerticalVisible,
        geometry.HorizontalTrack,
        geometry.HorizontalThumb,
        geometry.VerticalTrack,
        geometry.VerticalThumb,
        geometry.Corner);

    private DisplayList AttachResolvedImages(DisplayList displayList)
    {
        if (_imageResolver is null) return displayList;
        return new DisplayList(displayList.Commands.Select(command => command is DrawBackgroundImageCommand image &&
            _imageResolver.TryGetResource(image.Source, out var resource)
                ? image with { Resource = resource }
                : command).ToArray(), displayList.PaintChunks);
    }

    private NativeRenderPlan EvaluateRenderPlan()
    {
        var input = new NativeRenderPlanInput(
            (uint)_invalidation,
            _markupDirty ? 1 : 0,
            _hasPendingPointerMove ? 1 : 0,
            _displayList is not null ? 1 : 0,
            _displayListContainsPatchedChunks ? 1 : 0,
            _renderer is IIncrementalRenderBackend ? 1 : 0);
        return NativeRenderPlanInterop.Evaluate(input);
    }

    private void InvalidateDisplayList()
    {
        _invalidation |= VisualInvalidation.DisplayList;
        RequestRender();
    }

    private void RequestRender() => RenderRequested?.Invoke(this, EventArgs.Empty);

    private void OnImageResourceChanged(object? sender, ImageResourceChangedEventArgs e)
    {
        if (_hostSynchronizationContext is not null && SynchronizationContext.Current != _hostSynchronizationContext)
        {
            _hostSynchronizationContext.Post(static state => ((UiDocumentHost)state!).InvalidateDisplayList(), this);
            return;
        }
        InvalidateDisplayList();
    }

    private bool BuildDisplayList()
    {
        if ((_invalidation & VisualInvalidation.DisplayList) == 0)
        {
            return false;
        }

        if (_nativeLayout is null)
        {
            _invalidation |= VisualInvalidation.Layout;
            return true;
        }

        if (_nativeVisualStyleMap is null)
            _nativeVisualStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
        var displayListStart = Stopwatch.GetTimestamp();
        _displayList = AttachResolvedImages(_renderingInterop.Build(_nativeLayout, _nativeVisualStyleMap,
            _focused is null ? IntPtr.Zero : _nativeDom!.GetNativeHandle(_focused)));
        _displayListContainsPatchedChunks = false;
        Interlocked.Add(ref _displayListBuildTimestampTicks, Stopwatch.GetTimestamp() - displayListStart);
        Interlocked.Increment(ref _displayListBuildCount);
        _invalidation &= ~VisualInvalidation.DisplayList;
        return false;
    }

}
