using System.Diagnostics;
using AetherUI.Primitives;
using AetherUI.Rendering;

namespace AetherUI.Windows.Graphics;

internal sealed record CaptureRequest(long RequestId, int ExpectedPixelWidth, int ExpectedPixelHeight, float ExpectedDpiScale);

internal sealed record CapturedFrame(long RequestId, long BackendFrameSequence, long DeviceGeneration,
    int PixelWidth, int PixelHeight, float DpiScale, int RowStride, byte[] BgraPremultiplied,
    long ReadbackStartTicks, long ReadbackEndTicks, long CommittedTicks);

public sealed partial class Direct2DRenderBackend
{
    private readonly int _diagnosticOwnerThread = Environment.CurrentManagedThreadId;
    private CaptureRequest? _diagnosticCaptureRequest;
    private CapturedFrame? _diagnosticCandidate;
    private CapturedFrame? _diagnosticResult;
    private long _diagnosticLastRequestId;
    private long? _legacyCaptureRequestId;
    private long _diagnosticDeviceGeneration;
    private long? _failDiagnosticCommitRequestId;

    internal long DiagnosticCaptureRequestCount { get; private set; }
    internal long DiagnosticCaptureCopyCount { get; private set; }
    internal long DiagnosticCaptureCommitCount { get; private set; }
    internal long BackendFrameSequence { get; private set; }
    internal long DeviceGeneration => _diagnosticDeviceGeneration;
    internal int DiagnosticTargetPixelWidth => _targetPixelWidth;
    internal int DiagnosticTargetPixelHeight => _targetPixelHeight;
    internal float DiagnosticTargetDpiScale => _targetDpiScale;

    internal void BeginDiagnosticCapture(CaptureRequest request)
    {
        CheckDiagnosticOwner();
        ArgumentNullException.ThrowIfNull(request);
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_diagnosticCaptureRequest is not null || _diagnosticResult is not null || _legacyCaptureRequestId.HasValue)
            throw new InvalidOperationException("A diagnostic capture is already pending or unconsumed.");
        if (request.RequestId <= _diagnosticLastRequestId || request.RequestId <= 0)
            throw new ArgumentOutOfRangeException(nameof(request), "Capture IDs must increase monotonically.");
        ValidateDiagnosticTarget(request.ExpectedPixelWidth, request.ExpectedPixelHeight, request.ExpectedDpiScale);
        _diagnosticCandidate = null;
        _diagnosticResult = null;
        _diagnosticCaptureRequest = request;
        _diagnosticLastRequestId = request.RequestId;
        DiagnosticCaptureRequestCount++;
    }

    internal CapturedFrame TakeDiagnosticCapture(long requestId)
    {
        CheckDiagnosticOwner();
        if (_legacyCaptureRequestId.HasValue)
            throw new InvalidOperationException("Use the legacy Take wrapper for a legacy capture request.");
        return TakeDiagnosticCaptureCore(requestId);
    }

    private CapturedFrame TakeDiagnosticCaptureCore(long requestId)
    {
        CheckDiagnosticOwner();
        ObjectDisposedException.ThrowIf(_disposed, this);
        var frame = _diagnosticResult;
        if (frame is null || frame.RequestId != requestId || _diagnosticCaptureRequest is not null)
            throw new InvalidOperationException("No committed frame exists for this capture request.");
        ValidateDiagnosticTarget(frame.PixelWidth, frame.PixelHeight, frame.DpiScale);
        if (frame.DeviceGeneration != _diagnosticDeviceGeneration)
            throw new InvalidOperationException("The capture belongs to a previous device generation.");
        _diagnosticResult = null;
        return frame;
    }

    internal void CancelDiagnosticCapture(long requestId)
    {
        CheckDiagnosticOwner();
        if (_diagnosticCaptureRequest?.RequestId == requestId) _diagnosticCaptureRequest = null;
        if (_diagnosticCandidate?.RequestId == requestId) _diagnosticCandidate = null;
        if (_diagnosticResult?.RequestId == requestId) _diagnosticResult = null;
        if (_legacyCaptureRequestId == requestId) _legacyCaptureRequestId = null;
        if (_failDiagnosticCommitRequestId == requestId) _failDiagnosticCommitRequestId = null;
    }

    internal byte[] RenderAndCaptureBackBufferForDiagnostics(DisplayList displayList, ColorRgba clearColor)
    {
        RequestBackBufferCaptureForDiagnostics();
        var requestId = _legacyCaptureRequestId!.Value;
        try
        {
            Render(displayList, clearColor);
            return TakeBackBufferCaptureForDiagnostics();
        }
        finally { CancelDiagnosticCapture(requestId); }
    }

    internal void RequestBackBufferCaptureForDiagnostics()
    {
        CheckDiagnosticOwner();
        if (_legacyCaptureRequestId is { } previous) CancelDiagnosticCapture(previous);
        // Begin rejects an explicit pending/unconsumed operation. Never cancel another caller's request.
        var request = new CaptureRequest(checked(_diagnosticLastRequestId + 1),
            _targetPixelWidth, _targetPixelHeight, _targetDpiScale);
        BeginDiagnosticCapture(request);
        _legacyCaptureRequestId = request.RequestId;
    }

    internal byte[] TakeBackBufferCaptureForDiagnostics()
    {
        CheckDiagnosticOwner();
        var requestId = _legacyCaptureRequestId
            ?? throw new InvalidOperationException("No legacy capture request exists.");
        var frame = TakeDiagnosticCaptureCore(requestId);
        _legacyCaptureRequestId = null;
        return frame.BgraPremultiplied;
    }

    private void ValidateDiagnosticTarget(int width, int height, float dpi)
    {
        if (_swapChain is null || _targetBitmap is null || _renderTarget is null ||
            width <= 0 || height <= 0 || !float.IsFinite(dpi) || dpi <= 0 ||
            width != _targetPixelWidth || height != _targetPixelHeight || dpi != _targetDpiScale)
            throw new InvalidOperationException("The effective render target does not match the capture request.");
    }

    private void CheckDiagnosticOwner()
    {
        if (Environment.CurrentManagedThreadId != _diagnosticOwnerThread)
            throw new InvalidOperationException("Diagnostic capture must run on the backend owner thread.");
    }

    private void CheckDiagnosticRenderThread()
    {
        if (_diagnosticCaptureRequest is not null) CheckDiagnosticOwner();
    }

    private void CopyDiagnosticCandidate()
    {
        if (_diagnosticCaptureRequest is not { } request) return;
        CheckDiagnosticOwner();
        _diagnosticCandidate = null;
        ValidateDiagnosticTarget(request.ExpectedPixelWidth, request.ExpectedPixelHeight, request.ExpectedDpiScale);
        long generation = _diagnosticDeviceGeneration;
        long start = Stopwatch.GetTimestamp();
        byte[] bytes = CopyBackBufferForDiagnostics();
        long end = Stopwatch.GetTimestamp();
        DiagnosticCaptureCopyCount++;
        _diagnosticCandidate = new CapturedFrame(request.RequestId, 0, generation,
            _targetPixelWidth, _targetPixelHeight, _targetDpiScale, checked(_targetPixelWidth * 4),
            bytes, start, end, 0);
    }

    // Called only after EndDraw, retained cache work and Present all succeeded for this attempt.
    private void CompleteDiagnosticRender()
    {
        if (_diagnosticCaptureRequest is not { } request)
        {
            BackendFrameSequence++;
            return;
        }
        var candidate = _diagnosticCandidate
            ?? throw new InvalidOperationException("A rendered capture has no candidate pixels.");
        ValidateDiagnosticTarget(candidate.PixelWidth, candidate.PixelHeight, candidate.DpiScale);
        if (candidate.RequestId != request.RequestId || candidate.DeviceGeneration != _diagnosticDeviceGeneration)
            throw new InvalidOperationException("The capture candidate became stale before commit.");
        if (_failDiagnosticCommitRequestId == request.RequestId)
        {
            // One-shot failure after genuine readback and Present, before publication. The
            // public render catch must discard these candidate bytes; this is not GPU loss.
            _failDiagnosticCommitRequestId = null;
            throw new InvalidOperationException("Injected diagnostic capture commit failure.");
        }
        BackendFrameSequence++;
        _diagnosticResult = candidate with { BackendFrameSequence = BackendFrameSequence,
            CommittedTicks = Stopwatch.GetTimestamp() };
        _diagnosticCandidate = null;
        _diagnosticCaptureRequest = null;
        DiagnosticCaptureCommitCount++;
    }

    private void InvalidateDiagnosticCaptureResult()
    {
        _diagnosticCandidate = null;
        _diagnosticResult = null;
        // The armed intention survives the renderer's existing device-loss retry only.
    }

    // Arm only the matching live request. Unarmed rendering never consults this seam.
    internal void FailNextDiagnosticCommitForTesting(long requestId)
    {
        CheckDiagnosticOwner();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_diagnosticCaptureRequest?.RequestId != requestId || _failDiagnosticCommitRequestId.HasValue)
            throw new InvalidOperationException("A single matching pending capture request is required.");
        _failDiagnosticCommitRequestId = requestId;
    }

    // Diagnostic state-machine seam. This does not trigger or claim a GPU/device-loss event.
    internal void AbortDiagnosticCaptureForTesting()
    {
        CheckDiagnosticOwner();
        InvalidateDiagnosticCaptureResult();
        _diagnosticCaptureRequest = null;
        _legacyCaptureRequestId = null;
        _failDiagnosticCommitRequestId = null;
    }
}
