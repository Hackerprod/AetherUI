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
/// Direct2D renderer region: Backdrop-filter scene targets, snapshots and composition.
/// </summary>
public sealed partial class Direct2DRenderBackend
{
    private readonly BackdropCompositionMetrics _backdropMetrics = new();

    // Allocated only for a frame that actually contains a backdrop operation. The scene bitmap
    // remains drawable and the snapshot remains sampleable; the DXGI back buffer cannot serve
    // both roles because it is intentionally created with CannotDraw.
    private ID2D1Bitmap1? _backdropSceneBitmap;
    private ID2D1Bitmap1? _backdropSnapshotBitmap;

    internal int LastBackdropAllocations => _backdropMetrics.Allocations;
    internal int LastBackdropCompositionPasses => _backdropMetrics.CompositionPasses;
    internal int LastBackdropSnapshotPasses => _backdropMetrics.SnapshotPasses;

    private void ConfigureSceneTarget(bool usesBackdropFilters)
    {
        var target = _renderTarget ?? throw new InvalidOperationException("The Direct2D render target is not initialized.");
        if (!usesBackdropFilters)
        {
            // A page can stop using backdrop-filter after a markup/state update. Release both
            // intermediate surfaces before the next direct frame so they never become a global
            // rendering tax for ordinary applications.
            ReleaseBackdropCompositionResources();
            target.Target = _targetBitmap;
            return;
        }

        EnsureBackdropCompositionResources();
        target.Target = _backdropSceneBitmap;
    }

    internal static bool RequiresBackdropComposition(DisplayList displayList) =>
        displayList.Commands.Any(static command => command is ApplyBackdropFilterCommand);

    private void EnsureBackdropCompositionResources()
    {
        if (_backdropSceneBitmap is not null && _backdropSnapshotBitmap is not null)
        {
            return;
        }

        ReleaseBackdropCompositionResources();
        var target = _renderTarget ?? throw new InvalidOperationException("The Direct2D render target is not initialized.");
        var dpi = 96f * _dpiScale;
        var properties = new BitmapProperties1(
            new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
            dpi,
            dpi,
            BitmapOptions.Target);
        var size = new SizeI(Math.Max(1, _pixelWidth), Math.Max(1, _pixelHeight));
        _backdropSceneBitmap = target.CreateBitmap(size, IntPtr.Zero, 0, properties);
        _backdropMetrics.RecordAllocation();
        try
        {
            // The snapshot never becomes a render target. It is a stable source for an effect
            // while the scene bitmap receives the filtered result.
            var snapshotProperties = new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.None);
            _backdropSnapshotBitmap = target.CreateBitmap(size, IntPtr.Zero, 0, snapshotProperties);
            _backdropMetrics.RecordAllocation();
        }
        catch
        {
            _backdropSceneBitmap.Dispose();
            _backdropSceneBitmap = null;
            throw;
        }
    }

    private void ReleaseBackdropCompositionResources()
    {
        // Detach before releasing: a device context retains its target independently from the
        // managed wrapper, and a stale target would make resize/device-loss recovery depend on
        // COM reference timing.
        if (_renderTarget is not null)
        {
            _renderTarget.Target = null;
        }
        _backdropSnapshotBitmap?.Dispose();
        _backdropSnapshotBitmap = null;
        _backdropSceneBitmap?.Dispose();
        _backdropSceneBitmap = null;
    }

    private void ApplyBackdropFilter(
        ID2D1DeviceContext target,
        ApplyBackdropFilterCommand command,
        Stack<ClipScope> clipStack)
    {
        if (_backdropSceneBitmap is null || _backdropSnapshotBitmap is null)
        {
            throw new InvalidOperationException("Backdrop composition surfaces were not initialized.");
        }

        var activeTransform = target.Transform;
        // The source must contain precisely the scene painted before this command. Detach the
        // target and close the segment before CopyFromBitmap to avoid a read/write feedback loop.
        SuspendClips(target, clipStack);
        var boundary = EndDrawSegment(target);
        boundary.CheckError();
        target.Target = null;
        _backdropSnapshotBitmap.CopyFromBitmap(_backdropSceneBitmap);
        _backdropMetrics.RecordSnapshotPass();
        target.Target = _backdropSceneBitmap;
        BeginDrawSegment(target);
        target.Transform = activeTransform;
        RestoreClips(target, clipStack);

        // The snapshot is an image in scene coordinates, while the filter rectangle is in the
        // element's local CSS coordinate system. Keep the transformed geometric clip active but
        // draw the snapshot with identity so its source pixels are neither translated nor rotated
        // a second time. This is also why transformed backdrop-filter needs more than simply
        // inheriting the ordinary target.Transform used by fills and text.
        var localClip = ApplyClip(target, new PushClipCommand(command.Rectangle, command.Radii), activeTransform, transformDepth: 0);
        try
        {
            target.Transform = Matrix3x2.Identity;
            Direct2DBackdropFilterPainter.Draw(
                target,
                _backdropSnapshotBitmap,
                command.Filters,
                TransformBounds(command.Rectangle, activeTransform));
        }
        finally
        {
            target.Transform = activeTransform;
            PopClip(target, localClip);
        }
    }

    private void CompositeBackdropScene(ID2D1DeviceContext target, ColorRgba clearColor)
    {
        if (_backdropSceneBitmap is null)
        {
            throw new InvalidOperationException("Backdrop scene bitmap was not initialized.");
        }

        target.Target = _targetBitmap;
        BeginDrawSegment(target);
        target.Transform = Matrix3x2.Identity;
        target.Clear(ToColor4(clearColor));
        target.DrawImage(_backdropSceneBitmap, Vector2.Zero, null, Vortice.Direct2D1.InterpolationMode.Linear, CompositeMode.SourceOver);
        var result = EndDrawSegment(target);
        result.CheckError();
        _backdropMetrics.RecordCompositionPass();
    }

}
