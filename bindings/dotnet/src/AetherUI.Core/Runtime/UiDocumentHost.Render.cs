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
        if (_styleOperationDepth != 0 || _renderDepth != 0) { _renderDeferred = true; return; }
        _styleRetryBlocked = false; // An explicit call permits one new bounded attempt.
        ++_renderDepth;
        try { RenderCore(); }
        finally { --_renderDepth; DrainDomNotification(); }
    }

    private void RenderCore()
    {
        _renderDeferred = false;
        var initialPlan = EvaluateRenderPlan();
        var scrollInvalidated = (initialPlan.NormalizedInvalidation & (uint)VisualInvalidation.Scroll) != 0;
        var scrollOnly = initialPlan.UseScrollComposition != 0;
        var pointerRenderRequested = _pointerRenderPending;
        var pointerOnlyRender = pointerRenderRequested && initialPlan.PointerOnlyRender != 0;
        var displayListBuildsBefore = Interlocked.Read(ref _displayListBuildCount);
        var paintChunkBuildsBefore = Interlocked.Read(ref _paintChunkBuildCount);
        EnsureVisualTree();
        DrainDeferredInput();
        if (_disposed) return;
        EnsureVisualTree();
        // Visibility can change again when hover selectors resolve. Settle those
        // queued targets before publishing the frame, not on a hypothetical move.
        const int maxHoverPasses = 8;
        var hoverPasses = 0;
        while (_hasPendingPointerMove)
        {
            if (hoverPasses++ >= maxHoverPasses)
            {
                // CSS such as :hover { visibility:hidden } has no fixed point.
                // Clear hover until new input or a visibility invalidation.
                // The cursor still reflects the final resolved hit target.
                ResetUnstableHover();
                scrollOnly = false;
                break;
            }
            if (ApplyPendingPointerMove()) scrollOnly = false;
            EnsureVisualTree();
        }
        if (_disposed) return;
        EnsureVisualTree();
        if (!HasCertifiedStyleSnapshot()) throw new InvalidOperationException("The style snapshot is not current; no frame was presented.");
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

        var frameInputSerial = _hostInputSerial;
        var framePublicationSerial = _hostPublicationSerial;
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
        if (_disposed || _hostInputSerial != frameInputSerial || _hostPublicationSerial != framePublicationSerial || !HasCertifiedStyleSnapshot())
        {
            RejectStyleProjection();
            throw new InvalidOperationException("Inputs changed during frame notification; the candidate frame was not presented.");
        }
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
        _renderDeferred = false;
        Interlocked.Increment(ref _renderCallCount);
        if (scrollOnly) Interlocked.Increment(ref _scrollCompositionCount);
        _options.MetricsSink?.Invoke(Metrics);
    }

    private ScrollVisualState ScrollVisualStateFor(long styleNodeId)
    {
        var offset = _scrollOffsetsByStyleNode.GetValueOrDefault(styleNodeId);
        var chrome = _scrollBoxesByStyleNode.TryGetValue(styleNodeId, out var box) && IsCurrentlyVisible(box.Element)
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
        ProduceInvalidation(VisualInvalidation.DisplayList);
        RequestRender();
    }

    private void RequestRender()
    {
        if (_styleOperationDepth != 0) { _renderDeferred = true; return; }
        if (!_disposed && !_styleRetryBlocked) RenderRequested?.Invoke(this, EventArgs.Empty);
    }

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
            ProduceInvalidation(VisualInvalidation.Layout);
            return true;
        }

        if (_nativeVisualStyleMap is null)
            _nativeVisualStyleMap = _nativeStyleRuntime.CreateResolvedStyleMap(_nativeDom!.Native);
        var runtime = _transitionRuntime!;
        var transitionToken = _publishedTransitionToken;
        var displayListStart = Stopwatch.GetTimestamp();
        ++_styleOperationDepth;
        try
        {
            var candidate = AttachResolvedImages(_renderingInterop.Build(_nativeLayout, _nativeVisualStyleMap,
                _focused is null ? IntPtr.Zero : _nativeDom!.GetNativeHandle(_focused)));
            ValidateDerivedTransition(runtime, transitionToken);
            _displayList = candidate;
            _displayListContainsPatchedChunks = false;
            Interlocked.Add(ref _displayListBuildTimestampTicks, Stopwatch.GetTimestamp() - displayListStart);
            Interlocked.Increment(ref _displayListBuildCount);
            AcknowledgeInvalidation(VisualInvalidation.DisplayList);
        }
        catch
        {
            RejectStyleProjection();
            _styleRetryBlocked = true;
            throw;
        }
        finally { LeaveStyleOperation(); }
        return false;
    }

}
