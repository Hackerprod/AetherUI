using System.Runtime.InteropServices;
using System.ComponentModel;
using System.Numerics;
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;
using AetherUI.Windows.Graphics;
using AetherUI.Windows.Hosting;
using Vortice;
using Vortice.DCommon;
using Vortice.Direct2D1;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DirectWrite;
using Vortice.DXGI;
using Vortice.Mathematics;
using UiFontWeight = AetherUI.Css.FontWeight;

const int width = 960;
const int height = 600;
var pixels = RenderWindowShadow();
var hash = Fnv1a64(pixels);
// aether_os.html .window: 600x400 at (300,100), 12px radii and
// box-shadow: 0 20px 40px rgba(0,0,0,0.3), rendered by D3D11 WARP at 96 DPI.
const ulong expectedHash = 0xCE3F539297F00F76UL;
if (hash != expectedHash)
{
    Console.Error.WriteLine($"FAIL: D3D11 WARP .window shadow baseline changed: 0x{hash:X16}");
    return 1;
}

Console.WriteLine("OK: D3D11 WARP/96-DPI box-shadow pixel baseline passed");
VerifyTextShadowPixels(RenderTextShadow());

VerifyTextShadowCacheSemantics();
Console.WriteLine("OK: D3D11 WARP text-shadow baseline and cache metrics passed");
VerifyBackgroundImagePainter();
Console.WriteLine("OK: D3D11 WARP background-image painter passed");
VerifyBackdropFilterPainter();
Console.WriteLine("OK: D3D11 WARP backdrop-filter composition passed");
VerifyBackendBackdropMetrics();
Console.WriteLine("OK: Direct2D backend backdrop metrics passed");
VerifyTransformPainter();
VerifyTransformedBorderSideLayerIsolation();
VerifyBackendTransformStack();
Console.WriteLine("OK: Direct2D transform stack and transformed clips passed");
VerifyVitalisNestedFlexReflowAtValidatorWidth();
Console.WriteLine("OK: DirectWrite Vitalis nested-flex reflow invariants passed");
VerifyViewportResizeCoalescing();
Console.WriteLine("OK: viewport resize coalescing passed");
VerifyScrollTileCacheMetrics();
Console.WriteLine("OK: Direct2D scroll tile cache metrics passed");
VerifyRoundedAvatarPixelsThroughSwapChain();
Console.WriteLine("OK: Direct2D WARP avatar-radius swap-chain probe passed");
VerifyFullKanbanAvatarComposition();
return 0;

static void VerifyRoundedAvatarPixelsThroughSwapChain()
{
    var blue = new ColorRgba(0x3b / 255f, 0x82 / 255f, 0xf6 / 255f, 1);
    var green = new ColorRgba(.1f, .8f, .3f, 1);
    var white = new ColorRgba(1, 1, 1, 1);
    foreach (var dpiScale in new[] { 1f, 1.25f, 1.5f })
    {
        var pixelWidth = (int)MathF.Round(160 * dpiScale);
        var pixelHeight = (int)MathF.Round(96 * dpiScale);
        using var window = HiddenWindow.Create(pixelWidth, pixelHeight);
        using var backend = new Direct2DRenderBackend(window.Handle, pixelWidth, pixelHeight, dpiScale, DriverType.Warp);
        var frame = new DisplayList([
            // The exact avatar fill/stroke pair emitted by the native diagnostic.
            new FillRoundedRectangleCommand(new RectF(20, 20, 28, 28), CornerRadii.Uniform(14), blue),
            new StrokeRoundedRectangleCommand(new RectF(21, 21, 26, 26), CornerRadii.Uniform(13), 2, white),
            // A valid 24x40 CSS box with 50% radii must remain an ellipse.
            new FillRoundedRectangleCommand(new RectF(80, 20, 24, 40),
                new CornerRadii(12, 20, 12, 20, 12, 20, 12, 20), green),
        ]);
        var pixels = backend.RenderAndCaptureBackBufferForDiagnostics(frame, new ColorRgba(0, 0, 0, 1));

        var blueBounds = FindSolidColorBounds(pixels, pixelWidth, pixelHeight, blue);
        var whiteBounds = FindSolidColorBounds(pixels, pixelWidth, pixelHeight, white);
        var ellipseBounds = FindSolidColorBounds(pixels, pixelWidth, pixelHeight, green);
        var circleExpected = (int)MathF.Round(28 * dpiScale);
        var ellipseWidthExpected = (int)MathF.Round(24 * dpiScale);
        var ellipseHeightExpected = (int)MathF.Round(40 * dpiScale);
        Console.WriteLine($"avatar WARP dpi={dpiScale:F2}: blue={blueBounds}, white={whiteBounds}, ellipse={ellipseBounds}");

        RequireNearSquare(blueBounds, (int)MathF.Round(24 * dpiScale), "avatar interior", dpiScale);
        RequireNearSquare(whiteBounds, circleExpected, "avatar outer border", dpiScale);
        if (Math.Abs(ellipseBounds.Width - ellipseWidthExpected) > 2 ||
            Math.Abs(ellipseBounds.Height - ellipseHeightExpected) > 2 ||
            ellipseBounds.Height <= ellipseBounds.Width + MathF.Max(3, dpiScale * 8))
        {
            throw new InvalidOperationException(
                $"A 24x40 box with 50% radii must remain an ellipse at dpi={dpiScale:F2}: {ellipseBounds}.");
        }
    }
}

static void RequireNearSquare(PixelBounds bounds, int expectedExtent, string label, float dpiScale)
{
    if (Math.Abs(bounds.Width - bounds.Height) > 1 ||
        Math.Abs(bounds.Width - expectedExtent) > 2 || Math.Abs(bounds.Height - expectedExtent) > 2)
    {
        throw new InvalidOperationException(
            $"Direct2D WARP distorted {label} at dpi={dpiScale:F2}: {bounds}, expected approximately {expectedExtent}px square.");
    }
}

static void VerifyScrollTileCacheMetrics()
{
    using var window = HiddenWindow.Create(256, 256);
    using var backend = new Direct2DRenderBackend(window.Handle, 256, 256, 1f, DriverType.Warp);
    backend.SetScrollVisualStateProvider(id => new ScrollVisualState(
        id == 42 ? new ScrollVisualOffset(16, 24) : default,
        default));

    var cacheable = new DisplayList([
        new PushClipCommand(new RectF(0, 0, 128, 128), CornerRadii.Zero),
        new PushScrollOffsetCommand(42),
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 96), CornerRadii.Zero, new ColorRgba(.9f, .1f, .1f, 1)),
        new FillRoundedRectangleCommand(new RectF(128, 0, 96, 96), CornerRadii.Zero, new ColorRgba(.1f, .9f, .1f, 1)),
        new FillRoundedRectangleCommand(new RectF(0, 128, 96, 96), CornerRadii.Zero, new ColorRgba(.1f, .1f, .9f, 1)),
        new DrawTextCommand("cached scroll content", new RectF(12, 180, 220, 32),
            "Segoe UI", 16, UiFontWeight.Normal, TextAlign.Start, new ColorRgba(1, 1, 1, 1), true),
        new PopScrollOffsetCommand(),
        new PopClipCommand(),
    ]);

    backend.SetScrollCompositionMode(true);
    _ = backend.RenderAndCaptureBackBufferForDiagnostics(cacheable, new ColorRgba(1, 1, 1, 1));
    if (backend.LastScrollTileCacheMisses <= 0 || backend.LastScrollTileRasterizedCommands <= 0 ||
        backend.LastScrollTileCacheHits != 0 || backend.LastScrollTileCacheBypasses != 0 ||
        backend.LastScrollTileBytes <= 0)
    {
        throw new InvalidOperationException(
            $"Expected first scroll tile frame to rasterize misses only: hits={backend.LastScrollTileCacheHits}, misses={backend.LastScrollTileCacheMisses}, rasterized={backend.LastScrollTileRasterizedCommands}, bypasses={backend.LastScrollTileCacheBypasses}, bytes={backend.LastScrollTileBytes}.");
    }

    backend.SetScrollCompositionMode(true);
    _ = backend.RenderAndCaptureBackBufferForDiagnostics(cacheable, new ColorRgba(1, 1, 1, 1));
    if (backend.LastScrollTileCacheHits <= 0 || backend.LastScrollTileCacheMisses != 0 ||
        backend.LastScrollTileRasterizedCommands != 0 || backend.LastScrollTileCacheBypasses != 0)
    {
        throw new InvalidOperationException(
            $"Expected repeat scroll tile frame to be cache-hit only: hits={backend.LastScrollTileCacheHits}, misses={backend.LastScrollTileCacheMisses}, rasterized={backend.LastScrollTileRasterizedCommands}, bypasses={backend.LastScrollTileCacheBypasses}.");
    }

    var bypassed = new DisplayList([
        new PushClipCommand(new RectF(0, 0, 128, 128), CornerRadii.Zero),
        new PushScrollOffsetCommand(42),
        new PushTransformCommand(new TransformMatrix(1, 0, 0, 1, 2, 0)),
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 96), CornerRadii.Zero, new ColorRgba(.2f, .2f, .2f, 1)),
        new PopTransformCommand(),
        new PopScrollOffsetCommand(),
        new PopClipCommand(),
    ]);
    backend.SetScrollCompositionMode(true);
    _ = backend.RenderAndCaptureBackBufferForDiagnostics(bypassed, new ColorRgba(1, 1, 1, 1));
    if (backend.LastScrollTileCacheBypasses != 1 || backend.LastScrollTileCacheMisses != 0 ||
        backend.LastScrollTileRasterizedCommands != 0)
    {
        throw new InvalidOperationException(
            $"Expected transformed scroll segment to bypass tile cache: bypasses={backend.LastScrollTileCacheBypasses}, misses={backend.LastScrollTileCacheMisses}, rasterized={backend.LastScrollTileRasterizedCommands}.");
    }
}

static PixelBounds FindSolidColorBounds(byte[] pixels, int pixelWidth, int pixelHeight, ColorRgba color)
{
    var blue = (byte)MathF.Round(color.B * 255);
    var green = (byte)MathF.Round(color.G * 255);
    var red = (byte)MathF.Round(color.R * 255);
    var left = pixelWidth;
    var top = pixelHeight;
    var right = -1;
    var bottom = -1;
    for (var y = 0; y < pixelHeight; ++y)
    {
        for (var x = 0; x < pixelWidth; ++x)
        {
            var offset = ((y * pixelWidth) + x) * 4;
            if (Math.Abs(pixels[offset] - blue) > 4 || Math.Abs(pixels[offset + 1] - green) > 4 ||
                Math.Abs(pixels[offset + 2] - red) > 4 || pixels[offset + 3] < 250)
            {
                continue;
            }
            left = Math.Min(left, x);
            top = Math.Min(top, y);
            right = Math.Max(right, x);
            bottom = Math.Max(bottom, y);
        }
    }
    if (right < left || bottom < top)
    {
        throw new InvalidOperationException("The Direct2D WARP probe did not paint its expected opaque color.");
    }
    return new PixelBounds(left, top, right, bottom);
}

static void VerifyFullKanbanAvatarComposition()
{
    var path = FindKanbanFixture();
    if (path is null)
    {
        Console.WriteLine("SKIP: full Kanban avatar diagnostic fixture is unavailable outside the local Validator workspace.");
        return;
    }

    var markup = File.ReadAllText(path);
    var blue = new ColorRgba(0x3b / 255f, 0x82 / 255f, 0xf6 / 255f, 1);
    var pink = new ColorRgba(0xec / 255f, 0x48 / 255f, 0x99 / 255f, 1);
    var purple = new ColorRgba(0x8b / 255f, 0x5c / 255f, 0xf6 / 255f, 1);
    var green = new ColorRgba(0x10 / 255f, 0xb9 / 255f, 0x81 / 255f, 1);

    // This intentionally drives UiDocumentHost, not a hand-built DisplayList. It includes the
    // native DOM/CSS/layout pipeline, DirectWrite measurement, and the exact swap-chain renderer
    // used by Validator. The second case separates WARP from the actual GPU path.
    foreach (var probe in new[] { (Name: "WARP", Driver: DriverType.Warp), (Name: "hardware", Driver: DriverType.Hardware) })
    {
        using var window = HiddenWindow.Create(770, 870);
        using var renderer = new Direct2DRenderBackend(window.Handle, 770, 870, 1, probe.Driver);
        using var host = new UiDocumentHost(new RawMarkupComponent(markup), renderer, renderer, new UiHostOptions());
        host.SetViewport(770, 870, 1);
        renderer.RequestBackBufferCaptureForDiagnostics();
        host.Render();
        var pixels = renderer.TakeBackBufferCaptureForDiagnostics();
        var blueBounds = FindSolidColorBounds(pixels, 770, 870, blue);
        var pinkBounds = FindSolidColorBounds(pixels, 770, 870, pink);
        var purpleBounds = FindSolidColorBounds(pixels, 770, 870, purple);
        var greenBounds = FindSolidColorBounds(pixels, 770, 870, green);
        Console.WriteLine($"Kanban host {probe.Name}: blue={blueBounds}; pink={pinkBounds}; purple={purpleBounds}; green={greenBounds}");
    }
}

static string? FindKanbanFixture()
{
    for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
    {
        var candidate = Path.Combine(directory.FullName, ".tmp", "Pages", "kanban_board.html");
        if (File.Exists(candidate)) return candidate;
    }
    return null;
}

static void VerifyViewportResizeCoalescing()
{
    var coalescer = new ViewportUpdateCoalescer();
    coalescer.Queue(900, 600, 1);
    coalescer.Queue(901, 600, 1);
    coalescer.Queue(1172, 712, 1.25f);

    if (!coalescer.TryTake(out var final) || final != new ViewportUpdate(1172, 712, 1.25f) || coalescer.TryTake(out _))
    {
        throw new InvalidOperationException("Viewport resize coalescing did not retain exactly the final notification.");
    }

    coalescer.Queue(0, 712, 1);
    coalescer.Queue(1172, 0, 1);
    coalescer.Queue(1172, 712, 0);
    if (coalescer.TryTake(out _))
    {
        throw new InvalidOperationException("Viewport resize coalescing accepted an invalid client size or DPI.");
    }
}

static void VerifyVitalisNestedFlexReflowAtValidatorWidth()
{
    // This mirrors the production tree at the 770px client width captured by the Validator.
    // DirectWrite is deliberate: the native CTest measurer is synthetic and cannot exercise a
    // real line-break boundary produced by the host font stack.
    const string markup = """
        <style>
        * { box-sizing: border-box; }
        body, html { margin: 0; padding: 0; height: 100vh; font-family: 'Segoe UI', sans-serif; color: #1e293b; overflow: hidden; }
        .vitalis-container { display: flex; height: 100vh; }
        .sidebar { width: 100px; display: flex; flex-direction: column; align-items: center; padding: 30px 0; gap: 40px; }
        .patient-panel { width: 350px; display: flex; flex-direction: column; }
        .panel-header { padding: 30px; }
        .search-box { width: 100%; padding: 12px 15px; font-size: 14px; }
        .patient-list { flex: 1; overflow-y: auto; }
        .record-area { flex: 1; padding: 40px; overflow-y: auto; display: flex; flex-direction: column; gap: 30px; }
        .patient-header { display: flex; justify-content: space-between; align-items: flex-start; }
        .vital-grid { display: flex; gap: 20px; }
        .vital-card { flex: 1; background: white; border-radius: 24px; padding: 25px; border: 1px solid white; }
        .vital-label { font-size: 13px; color: #64748b; font-weight: 600; text-transform: uppercase; margin-bottom: 10px; display: block; }
        .vital-value { font-size: 28px; font-weight: 800; color: #0f172a; }
        .vital-unit { font-size: 14px; color: #94a3b8; font-weight: 400; }
        .history-card { background: white; border-radius: 24px; padding: 30px; flex: 1; }
        .timeline { margin-top: 20px; }
        .timeline-item { border-left: 2px solid #e0f2fe; padding-left: 30px; padding-bottom: 30px; }
        </style>
        <body><div class="vitalis-container">
          <div class="sidebar"><div>V</div></div>
          <div class="patient-panel"><div class="panel-header"><input class="search-box" placeholder="Search patients..."></div><div class="patient-list"></div></div>
          <div id="record" class="record-area">
            <div id="header" class="patient-header">
              <div id="copy"><h1 style="font-size: 32px; font-weight: 800; margin: 0;">James Wilson</h1><p id="note" style="color: #64748b; margin-top: 5px;">Last visit: July 20, 2026 &#8226; Dr. Aether</p></div>
              <div style="display: flex; gap: 10px;"><button style="padding: 12px 20px; background: white; border: 1px solid #e2e8f0; border-radius: 12px; font-weight: 600;">Edit Profile</button><button style="padding: 12px 20px; background: #0ea5e9; color: white; border: none; border-radius: 12px; font-weight: 600;">New Appointment</button></div>
            </div>
            <div id="grid" class="vital-grid">
              <div class="vital-card"><span class="vital-label">Heart Rate</span><div class="vital-value">72 <span class="vital-unit">bpm</span></div><div style="margin-top: 15px; color: #10b981; font-size: 12px; font-weight: 600;">Normal Range</div></div>
              <div class="vital-card"><span class="vital-label">Blood Pressure</span><div class="vital-value">120/80 <span class="vital-unit">mmHg</span></div><div style="margin-top: 15px; color: #10b981; font-size: 12px; font-weight: 600;">Optimal</div></div>
              <div class="vital-card"><span class="vital-label">Body Temp</span><div class="vital-value">36.6 <span class="vital-unit">&#176;C</span></div><div style="margin-top: 15px; color: #10b981; font-size: 12px; font-weight: 600;">Normal</div></div>
            </div>
            <div class="history-card"><h3 style="margin: 0 0 25px 0; font-size: 18px;">Medical History</h3><div class="timeline"><div class="timeline-item"><div style="font-weight: 700; font-size: 15px;">General Checkup</div><div style="font-size: 13px; color: #64748b; margin-top: 4px;">Patient reported mild fatigue. All vitals normal. Recommended Vitamin D.</div><div style="font-size: 11px; font-weight: 700; color: #0ea5e9; margin-top: 10px;">JUL 20, 2026</div></div><div class="timeline-item"><div style="font-weight: 700; font-size: 15px;">Cardiology Consultation</div><div style="font-size: 13px; color: #64748b; margin-top: 4px;">Routine stress test performed. Cardiac output within expected parameters.</div><div style="font-size: 11px; font-weight: 700; color: #0ea5e9; margin-top: 10px;">MAR 15, 2026</div></div></div></div>
          </div>
        </div></body>
        """;

    using var window = HiddenWindow.Create(770, 870);
    var renderer = new Direct2DRenderBackend(window.Handle, 770, 870, 1);
    using var host = new UiDocumentHost(new RawMarkupComponent(markup), renderer, renderer, new UiHostOptions());
    host.SetViewport(770, 870, 1);
    host.Render();

    var layout = host.Layout ?? throw new InvalidOperationException("Vitalis layout was not produced.");
    LayoutBox Box(string id) => layout.FindPrimaryBox(host.Document?.GetElementById(id)
        ?? throw new InvalidOperationException($"Vitalis fixture element '{id}' was not found."))
        ?? throw new InvalidOperationException($"Vitalis fixture box '{id}' was not found.");

    var header = Box("header");
    var copy = Box("copy");
    var note = Box("note");
    var grid = Box("grid");
    var headerBottom = header.BorderBox.Bottom;
    var noteMarginBottom = note.MarginBox.Bottom;
    if (headerBottom + .01f < noteMarginBottom || grid.MarginBox.Top + .01f < headerBottom + 30)
    {
        throw new InvalidOperationException(
            $"Vitalis nested flex reflow diverged at 770px: record={Box("record").ContentBox.Width:F2}, " +
            $"header=[{header.BorderBox.X:F2},{header.BorderBox.Y:F2},{header.BorderBox.Width:F2},{header.BorderBox.Height:F2}], " +
            $"copy=[{copy.BorderBox.X:F2},{copy.BorderBox.Y:F2},{copy.BorderBox.Width:F2},{copy.BorderBox.Height:F2}], " +
            $"noteMarginBottom={noteMarginBottom:F2}, gridTop={grid.MarginBox.Top:F2}.");
    }
}

static byte[] RenderWindowShadow()
{
    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm,
        width,
        height,
        1,
        1,
        BindFlags.RenderTarget,
        ResourceUsage.Default,
        CpuAccessFlags.None,
        1,
        0,
        ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var targetBitmap = target.CreateBitmapFromDxgiSurface(
        surface,
        new BitmapProperties1(
            new PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
            96,
            96,
            BitmapOptions.Target | BitmapOptions.CannotDraw));
    target.Target = targetBitmap;
    target.TextAntialiasMode = Vortice.Direct2D1.TextAntialiasMode.Grayscale;

    var brushes = new Dictionary<ColorRgba, ID2D1SolidColorBrush>();
    try
    {
        ID2D1SolidColorBrush Brush(ColorRgba color) => brushes.TryGetValue(color, out var brush)
            ? brush
            : brushes[color] = target.CreateSolidColorBrush(new Color4(color.R, color.G, color.B, color.A));

        var command = new DrawBoxShadowCommand(
            new RectF(300, 100, 600, 400), CornerRadii.Uniform(12),
            new BoxShadow(0, 20, 40, 0, new ColorRgba(0, 0, 0, .3f), false));
        target.BeginDraw();
        target.Clear(new Color4(1, 1, 1, 1));
        Direct2DBoxShadowPainter.Draw(factory, target, command, Brush);
        target.EndDraw().CheckError();

        using var staging = device.CreateTexture2D(new Texture2DDescription(
            Format.B8G8R8A8_UNorm,
            width,
            height,
            1,
            1,
            BindFlags.None,
            ResourceUsage.Staging,
            CpuAccessFlags.Read,
            1,
            0,
            ResourceOptionFlags.None));
        context.CopyResource(staging, texture);
        context.Flush();
        return ReadPixels(context, staging);
    }
    finally
    {
        foreach (var brush in brushes.Values)
        {
            brush.Dispose();
        }
    }
}

static byte[] RenderTextShadow()
{
    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var textFactory = DWrite.DWriteCreateFactory<IDWriteFactory2>();
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.RenderTarget,
        ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var targetBitmap = target.CreateBitmapFromDxgiSurface(
        surface,
        new BitmapProperties1(
            new PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
            96, 96, BitmapOptions.Target | BitmapOptions.CannotDraw));
    target.Target = targetBitmap;
    target.TextAntialiasMode = Vortice.Direct2D1.TextAntialiasMode.Grayscale;

    var command = new DrawTextShadowCommand(
        "Shadow\nwrap", new RectF(200, 120, 560, 160), "Segoe UI", 36,
        UiFontWeight.Normal, TextAlign.Center,
        new BoxShadow(4, 6, 12, 0, new ColorRgba(.1f, .2f, .9f, .8f), false), Wrap: true);
    using var mask = Direct2DTextShadowPainter.CreateMask(target, command, 1f);
    target.Target = mask.Bitmap;
    target.BeginDraw();
    Direct2DTextShadowPainter.RenderGlyphMask(target, textFactory, command, mask, 1f);
    target.EndDraw().CheckError();
    target.Target = targetBitmap;
    target.BeginDraw();
    target.Clear(new Color4(1, 1, 1, 1));
    Direct2DTextShadowPainter.Draw(target, mask, command.Rectangle, command.Shadow);
    Direct2DTextShadowPainter.Draw(target, mask, command.Rectangle,
        new BoxShadow(-3, -2, 4, 0, new ColorRgba(.9f, .1f, .1f, .5f), false));
    target.EndDraw().CheckError();

    using var staging = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.None,
        ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
    context.CopyResource(staging, texture);
    context.Flush();
    return ReadPixels(context, staging);
}

static void VerifyTextShadowPixels(byte[] pixels)
{
    // DirectWrite deliberately resolves the installed font collection. Its glyph antialiasing can
    // therefore differ across Windows images even while D3D11 WARP is deterministic. Validate the
    // rendering contract instead: both declared shadows must tint pixels around the wrapped text,
    // and the effect must not escape the conservative bounds implied by its offsets and blur radii.
    var tinted = 0;
    var blueDominant = 0;
    var redDominant = 0;
    var minimumX = width;
    var minimumY = height;
    var maximumX = -1;
    var maximumY = -1;
    for (var y = 0; y < height; y++)
    {
        for (var x = 0; x < width; x++)
        {
            var offset = ((y * width) + x) * 4;
            var blue = pixels[offset];
            var green = pixels[offset + 1];
            var red = pixels[offset + 2];
            if (blue == 255 && green == 255 && red == 255)
            {
                continue;
            }

            tinted++;
            minimumX = Math.Min(minimumX, x);
            minimumY = Math.Min(minimumY, y);
            maximumX = Math.Max(maximumX, x);
            maximumY = Math.Max(maximumY, y);
            if (blue > red + 8 && blue > green + 4) blueDominant++;
            if (red > blue + 8 && red > green + 4) redDominant++;
        }
    }

    if (tinted < 100 || blueDominant < 25 || redDominant < 25)
    {
        throw new InvalidOperationException(
            $"D3D11 WARP text-shadow did not render both declared colors: tinted={tinted}, blue={blueDominant}, red={redDominant}.");
    }

    if (minimumX < 120 || minimumY < 60 || maximumX > 840 || maximumY > 380)
    {
        throw new InvalidOperationException(
            $"D3D11 WARP text-shadow escaped its conservative text bounds: ({minimumX},{minimumY})-({maximumX},{maximumY}).");
    }
}

static void VerifyTextShadowCacheSemantics()
{
    var first = new DrawTextShadowCommand("fixed", new RectF(10, 20, 120, 40), "Segoe UI", 16,
        UiFontWeight.Normal, TextAlign.Start, default, Wrap: false);
    var second = first with { Rectangle = new RectF(40, 60, 120, 40) };
    var resized = first with { Rectangle = new RectF(10, 20, 121, 40) };
    if (TextShadowMaskKey.From(first, 1f) != TextShadowMaskKey.From(second, 1f) ||
        TextShadowMaskKey.From(first, 1f) == TextShadowMaskKey.From(resized, 1f))
    {
        throw new InvalidOperationException("Text-shadow cache key must ignore position but include layout dimensions.");
    }

    // TODO(backfill): exercise these counters through a real Direct2DRenderBackend, as the
    // backdrop-filter integration test does below. This preserves the cache-key unit coverage
    // while making the pipeline's hit/miss instrumentation independently verifiable.
    var metrics = new TextShadowRenderMetrics();
    metrics.RecordCacheHit();
    if (metrics.CacheHits != 1 || metrics.SceneSuspensions != 0)
    {
        throw new InvalidOperationException("A text-shadow cache hit must not suspend the scene.");
    }

    metrics.Reset();
    metrics.RecordSceneSuspension();
    if (metrics.CacheHits != 0 || metrics.SceneSuspensions != 1)
    {
        throw new InvalidOperationException("A text-shadow cache miss must suspend the scene exactly once.");
    }
}

static void VerifyBackgroundImagePainter()
{
    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.RenderTarget,
        ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var targetBitmap = target.CreateBitmapFromDxgiSurface(
        surface,
        new BitmapProperties1(
            new PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
            96, 96, BitmapOptions.Target | BitmapOptions.CannotDraw));
    target.Target = targetBitmap;

    // Four opaque source corners make placement, crop and edge coverage observable through the
    // production painter. The native geometry module decides which tile commands to emit; this
    // test verifies that each resolved command reaches the exact destination pixels.
    var pixels = new byte[4 * 4 * 4];
    for (var y = 0; y < 4; y++)
    for (var x = 0; x < 4; x++)
    {
        var offset = ((y * 4) + x) * 4;
        // BGRA quadrants: red / green / blue / white. Each 2x2 block gives linear
        // interpolation a stable interior sample instead of testing an edge texel.
        var color = y < 2
            ? (x < 2 ? (B: (byte)0, G: (byte)0, R: (byte)255) : (B: (byte)0, G: (byte)255, R: (byte)0))
            : (x < 2 ? (B: (byte)255, G: (byte)0, R: (byte)0) : (B: (byte)255, G: (byte)255, R: (byte)255));
        pixels[offset] = color.B;
        pixels[offset + 1] = color.G;
        pixels[offset + 2] = color.R;
        pixels[offset + 3] = 0xFF;
    }
    var resource = new AetherUI.Runtime.ImageResourceResult(new(4, 4), pixels);
    unsafe
    {
        using var pin = resource.Pixels.Pin();
        using var bitmap = target.CreateBitmap(new SizeI(4, 4), (IntPtr)pin.Pointer, 16,
            new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
                Vortice.DCommon.AlphaMode.Premultiplied), 96, 96, BitmapOptions.None));
        var cover = new DrawBackgroundImageCommand(
            "memory://corners", new RectF(0, 0, width, height), CssLength.Zero, CssLength.Zero,
            BackgroundImageSizeKind.Explicit, CssLength.Zero, CssLength.Zero,
            BackgroundRepeat.NoRepeat, BackgroundRepeat.NoRepeat, 1,
            new RectF(0, 0, 4, 4), new RectF(0, 0, width, height), true, resource);
        var cropped = cover with
        {
            SourceRectangle = new RectF(2, 0, 2, 2),
            DestinationRectangle = new RectF(100, 120, 180, 120),
        };
        // A partial final repeat tile has a clipped source rectangle. Its right edge is the
        // regression point for an uncovered ClearColor strip.
        var partialTile = cover with
        {
            SourceRectangle = new RectF(0, 0, 2, 2),
            DestinationRectangle = new RectF(width - 75, height - 90, 75, 90),
        };
        target.BeginDraw();
        target.Clear(new Color4(0, 0, 0, 1));
        Direct2DBackgroundImagePainter.Draw(target, bitmap, cover);
        Direct2DBackgroundImagePainter.Draw(target, bitmap, cropped);
        Direct2DBackgroundImagePainter.Draw(target, bitmap, partialTile);
        target.EndDraw().CheckError();
    }

    using var staging = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.None,
        ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
    context.CopyResource(staging, texture);
    context.Flush();
    var rendered = ReadPixels(context, staging);
    AssertPixel(rendered, 2, 2, 0x00, 0x00, 0xFF, "cover top-left");
    AssertPixel(rendered, width - 3, 2, 0x00, 0xFF, 0x00, "cover right edge");
    AssertPixel(rendered, 2, height - 3, 0xFF, 0x00, 0x00, "cover bottom-left");
    AssertPixel(rendered, width - 3, height - 100, 0xFF, 0xFF, 0xFF, "cover bottom-right");
    AssertPixel(rendered, 190, 180, 0x00, 0xFF, 0x00, "non-zero source crop");
    AssertOpaque(rendered, width - 3, height - 85, "partial repeat right edge");
}

static void VerifyBackdropFilterPainter()
{
    var directFrame = new DisplayList([
        new FillRoundedRectangleCommand(new RectF(0, 0, 20, 20), CornerRadii.Zero, new ColorRgba(1, 1, 1, 1)),
    ]);
    if (Direct2DRenderBackend.RequiresBackdropComposition(directFrame))
    {
        throw new InvalidOperationException("A frame without ApplyBackdropFilter commands must remain on the direct swap-chain path.");
    }

    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.RenderTarget,
        ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var output = target.CreateBitmapFromDxgiSurface(surface,
        new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
            Vortice.DCommon.AlphaMode.Premultiplied), 96, 96,
            BitmapOptions.Target | BitmapOptions.CannotDraw));
    using var scene = target.CreateBitmap(new SizeI(width, height), IntPtr.Zero, 0,
        new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
            Vortice.DCommon.AlphaMode.Premultiplied), 96, 96, BitmapOptions.Target));
    using var snapshot = target.CreateBitmap(new SizeI(width, height), IntPtr.Zero, 0,
        new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
            Vortice.DCommon.AlphaMode.Premultiplied), 96, 96, BitmapOptions.None));

    target.Target = scene;
    using (var red = target.CreateSolidColorBrush(new Color4(1, 0, 0, 1)))
    using (var blue = target.CreateSolidColorBrush(new Color4(0, 0, 1, 1)))
    {
        target.BeginDraw();
        target.Clear(new Color4(0, 0, 0, 1));
        target.FillRectangle(new RawRectF(0, 0, width / 2f, height), red);
        target.FillRectangle(new RawRectF(width / 2f, 0, width, height), blue);
        target.EndDraw().CheckError();
    }

    snapshot.CopyFromBitmap(scene);
    var command = new ApplyBackdropFilterCommand(
        new RectF(360, 180, 240, 160), CornerRadii.Uniform(40),
        [new BackdropFilter(BackdropFilterKind.Blur, new CssLength(16, CssUnit.Pixels), 0)]);
    using (var geometry = factory.CreateRoundedRectangleGeometry(new RoundedRectangle(
        new System.Drawing.RectangleF(command.Rectangle.X, command.Rectangle.Y,
            command.Rectangle.Width, command.Rectangle.Height), 40, 40)))
    {
        target.BeginDraw();
        target.PushLayer(new LayerParameters
        {
            ContentBounds = new RawRectF(command.Rectangle.Left, command.Rectangle.Top,
                command.Rectangle.Right, command.Rectangle.Bottom),
            GeometricMask = geometry,
            MaskAntialiasMode = AntialiasMode.PerPrimitive,
            MaskTransform = System.Numerics.Matrix3x2.Identity,
            Opacity = 1,
            LayerOptions = LayerOptions.None,
        }, null);
        Direct2DBackdropFilterPainter.Draw(target, snapshot, command);
        target.PopLayer();
        target.EndDraw().CheckError();
    }

    target.Target = output;
    target.BeginDraw();
    target.Clear(new Color4(0, 0, 0, 1));
    target.DrawImage(scene, System.Numerics.Vector2.Zero, null, Vortice.Direct2D1.InterpolationMode.Linear, CompositeMode.SourceOver);
    target.EndDraw().CheckError();

    using var staging = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.None,
        ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
    context.CopyResource(staging, texture);
    context.Flush();
    var rendered = ReadPixels(context, staging);

    // The rounded clip must keep the unfiltered red scene outside its corner.
    AssertPixel(rendered, 365, 185, 0x00, 0x00, 0xFF, "backdrop rounded-corner exterior");
    // The red/blue boundary lies within the clipped backdrop. A non-zero contribution from both
    // colors proves that the snapshot was blurred, not merely copied back into the scene.
    var center = ((260 * width) + 478) * 4;
    if (rendered[center] == 0 || rendered[center + 2] == 0 || rendered[center + 3] != 0xFF)
    {
        throw new InvalidOperationException($"The WARP backdrop-filter painter did not blur the painted scene inside its clip: BGRA({rendered[center]},{rendered[center + 1]},{rendered[center + 2]},{rendered[center + 3]}).");
    }

    var metrics = new BackdropCompositionMetrics();
    if (metrics.Allocations != 0 || metrics.CompositionPasses != 0 || metrics.SnapshotPasses != 0)
    {
        throw new InvalidOperationException("A frame without ApplyBackdropFilter commands must not allocate or compose backdrop surfaces.");
    }
}

static void VerifyBackendBackdropMetrics()
{
    using var window = HiddenWindow.Create();
    using var backend = new Direct2DRenderBackend(window.Handle, 96, 64, 1);
    var direct = new DisplayList([
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 64), CornerRadii.Zero, new ColorRgba(.1f, .2f, .3f, 1)),
    ]);
    backend.Render(direct, new ColorRgba(0, 0, 0, 1));
    if (backend.LastBackdropAllocations != 0 || backend.LastBackdropCompositionPasses != 0 || backend.LastBackdropSnapshotPasses != 0)
    {
        throw new InvalidOperationException("The real Direct2D backend allocated or composed backdrop resources for a frame without filters.");
    }

    var filtered = new DisplayList([
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 64), CornerRadii.Zero, new ColorRgba(.8f, .1f, .2f, 1)),
        new ApplyBackdropFilterCommand(new RectF(10, 8, 72, 48), CornerRadii.Uniform(8),
            [new BackdropFilter(BackdropFilterKind.Blur, new CssLength(6, CssUnit.Pixels), 0)]),
        new FillRoundedRectangleCommand(new RectF(10, 8, 72, 48), CornerRadii.Uniform(8), new ColorRgba(1, 1, 1, .2f)),
    ]);
    backend.Render(filtered, new ColorRgba(0, 0, 0, 1));
    if (backend.LastBackdropAllocations != 2 || backend.LastBackdropCompositionPasses != 1 || backend.LastBackdropSnapshotPasses != 1)
    {
        throw new InvalidOperationException($"The real Direct2D backdrop path reported unexpected first-frame metrics: allocations={backend.LastBackdropAllocations}, composition={backend.LastBackdropCompositionPasses}, snapshots={backend.LastBackdropSnapshotPasses}.");
    }

    backend.Render(filtered, new ColorRgba(0, 0, 0, 1));
    if (backend.LastBackdropAllocations != 0 || backend.LastBackdropCompositionPasses != 1 || backend.LastBackdropSnapshotPasses != 1)
    {
        throw new InvalidOperationException($"The real Direct2D backdrop cache reported unexpected repeat-frame metrics: allocations={backend.LastBackdropAllocations}, composition={backend.LastBackdropCompositionPasses}, snapshots={backend.LastBackdropSnapshotPasses}.");
    }

    backend.Render(direct, new ColorRgba(0, 0, 0, 1));
    if (backend.LastBackdropAllocations != 0 || backend.LastBackdropCompositionPasses != 0 || backend.LastBackdropSnapshotPasses != 0)
    {
        throw new InvalidOperationException("The real Direct2D backend did not return to the direct path after backdrop-filter was removed.");
    }
}

static void VerifyTransformPainter()
{
    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.RenderTarget,
        ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var output = target.CreateBitmapFromDxgiSurface(surface,
        new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
            Vortice.DCommon.AlphaMode.Premultiplied), 96, 96,
            BitmapOptions.Target | BitmapOptions.CannotDraw));
    using var green = target.CreateSolidColorBrush(new Color4(0, 1, 0, 1));
    using var red = target.CreateSolidColorBrush(new Color4(1, 0, 0, 1));
    using var blue = target.CreateSolidColorBrush(new Color4(0, 0, 1, 1));
    using var yellow = target.CreateSolidColorBrush(new Color4(1, 1, 0, 1));
    using var white = target.CreateSolidColorBrush(new Color4(1, 1, 1, 1));
    var borderBrushes = new Dictionary<ColorRgba, ID2D1SolidColorBrush>();
    ID2D1SolidColorBrush BorderBrush(ColorRgba color) => borderBrushes.TryGetValue(color, out var brush)
        ? brush
        : borderBrushes[color] = target.CreateSolidColorBrush(new Color4(color.R, color.G, color.B, color.A));
    target.Target = output;

    // This uses the same row-vector composition as Direct2DRenderBackend. Translation establishes
    // the outer CSS transform; the nested scale must operate in its local coordinates before that
    // translation, which is the discriminating nested-transform case.
    var outer = new System.Numerics.Matrix3x2(1, 0, 0, 1, 20, 10);
    var nested = new System.Numerics.Matrix3x2(2, 0, 0, 2, 0, 0) * outer;
    target.BeginDraw();
    target.Clear(new Color4(0, 0, 0, 1));
    target.Transform = outer;
    target.FillRectangle(new RawRectF(0, 0, 20, 20), green);
    target.Transform = nested;
    target.FillRectangle(new RawRectF(5, 5, 15, 15), red);
    // A transformed overflow clip must become a geometric layer. This mirrors the production
    // backend's identity MaskTransform under the saved world transform; using the matrix in both
    // places would apply it twice and move the clip away from its content.
    target.Transform = outer;
    using (var clip = factory.CreateRoundedRectangleGeometry(new RoundedRectangle(
        new System.Drawing.RectangleF(0, 30, 12, 12), 0, 0)))
    {
        target.PushLayer(new LayerParameters
        {
            ContentBounds = new RawRectF(0, 30, 12, 42),
            GeometricMask = clip,
            MaskAntialiasMode = AntialiasMode.PerPrimitive,
            MaskTransform = System.Numerics.Matrix3x2.Identity,
            Opacity = 1,
            LayerOptions = LayerOptions.None,
        }, null);
        target.FillRectangle(new RawRectF(0, 30, 40, 40), blue);
        target.PopLayer();
    }
    // Translation alone cannot reveal whether ContentBounds and GeometricMask are interpreted in
    // the same transformed coordinate space. Rotate the layer around a non-zero pivot: this is
    // the exact condition used by CSS borders in Fitness Tracker and distinguishes a local bound
    // from an incorrectly treated device-space one.
    var rotated = new System.Numerics.Matrix3x2(0, 1, -1, 0, 60, 10);
    target.Transform = rotated;
    using (var clip = factory.CreateRoundedRectangleGeometry(new RoundedRectangle(
        new System.Drawing.RectangleF(10, 10, 20, 12), 0, 0)))
    {
        target.PushLayer(new LayerParameters
        {
            ContentBounds = new RawRectF(10, 10, 30, 22),
            GeometricMask = clip,
            MaskAntialiasMode = AntialiasMode.PerPrimitive,
            MaskTransform = System.Numerics.Matrix3x2.Identity,
            Opacity = 1,
            LayerOptions = LayerOptions.None,
        }, null);
        target.FillRectangle(new RawRectF(0, 0, 40, 40), yellow);
        target.PopLayer();
    }
    // Exercise the production non-uniform border painter, not a synthetic mask. Each coloured
    // side is an intersection of the same annular geometry and diagonal side partition that CSS
    // uses for borders with per-side colours. A 90° rotation makes every expected destination
    // point unambiguous and catches a split nested-mask implementation immediately.
    var borderOuter = new RectF(120, 80, 100, 100);
    var borderInner = new RectF(135, 95, 70, 70);
    // Deliberately asymmetric elliptical corners exercise the complete CSS radii model. The
    // inner radii are independently inset from their matching outer corner axes.
    var borderOuterRadii = new CornerRadii(24, 10, 12, 20, 18, 8, 30, 16);
    var borderInnerRadii = new CornerRadii(9, 0, 0, 5, 3, 0, 15, 1);
    var identityBorderOuter = new RectF(300, 80, 100, 100);
    var identityBorderInner = new RectF(315, 95, 70, 70);
    target.Transform = System.Numerics.Matrix3x2.Identity;
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(identityBorderOuter, CornerRadii.Uniform(50), identityBorderInner, CornerRadii.Uniform(35), 0, 4, new ColorRgba(1, 0, 0, 1)),
        BorderBrush);
    var borderRotation = new System.Numerics.Matrix3x2(0, 1, -1, 0, 300, -40);
    target.Transform = borderRotation;
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(borderOuter, borderOuterRadii, borderInner, borderInnerRadii, 0, 4, new ColorRgba(1, 0, 0, 1)),
        BorderBrush);
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(borderOuter, borderOuterRadii, borderInner, borderInnerRadii, 1, 4, new ColorRgba(0, 1, 0, 1)),
        BorderBrush);
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(borderOuter, borderOuterRadii, borderInner, borderInnerRadii, 2, 4, new ColorRgba(0, 0, 1, 1)),
        BorderBrush);
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(borderOuter, borderOuterRadii, borderInner, borderInnerRadii, 3, 4, new ColorRgba(1, 1, 0, 1)),
        BorderBrush);
    var scaledBorderOuter = new RectF(40, 260, 100, 100);
    var scaledBorderInner = new RectF(55, 275, 70, 70);
    target.Transform = new System.Numerics.Matrix3x2(1.2f, 0, 0, .8f, 500, 0);
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(scaledBorderOuter, CornerRadii.Uniform(30), scaledBorderInner, CornerRadii.Uniform(15), 0, 4, new ColorRgba(1, 0, 1, 1)),
        BorderBrush);
    var skewedBorderOuter = new RectF(400, 260, 100, 100);
    var skewedBorderInner = new RectF(415, 275, 70, 70);
    target.Transform = new System.Numerics.Matrix3x2(1, 0, .4f, 1, 0, 0);
    Direct2DBorderSidePainter.Draw(factory, target,
        new FillBorderSideCommand(skewedBorderOuter, CornerRadii.Uniform(30), skewedBorderInner, CornerRadii.Uniform(15), 0, 4, new ColorRgba(0, 1, 1, 1)),
        BorderBrush);
    // Control: a single annular stroke under the exact same arbitrary rotation remains a
    // continuous ring. This eliminates the target transform and the ring itself as sources of
    // the Fitness Tracker fragments; only the four side partitions remain variable.
    target.Transform = System.Numerics.Matrix3x2.CreateRotation(MathF.PI / 4f, new System.Numerics.Vector2(800, 420));
    target.DrawEllipse(new Ellipse(new System.Numerics.Vector2(800, 420), 42.5f, 42.5f), white, 15);
    target.Transform = System.Numerics.Matrix3x2.Identity;
    target.EndDraw().CheckError();
    foreach (var brush in borderBrushes.Values) brush.Dispose();

    using var staging = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.None,
        ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
    context.CopyResource(staging, texture);
    context.Flush();
    var pixels = ReadPixels(context, staging);
    AssertPixel(pixels, 22, 12, 0x00, 0xFF, 0x00, "outer transformed fill");
    AssertPixel(pixels, 35, 25, 0x00, 0x00, 0xFF, "nested transformed fill");
    AssertPixel(pixels, 5, 5, 0x00, 0x00, 0x00, "untransformed exterior");
    AssertPixel(pixels, 22, 42, 0xFF, 0x00, 0x00, "transformed clip interior");
    AssertPixel(pixels, 35, 42, 0x00, 0x00, 0x00, "transformed clip exterior");
    AssertPixel(pixels, 45, 25, 0x00, 0xFF, 0xFF, "rotated transformed clip interior");
    AssertPixel(pixels, 45, 45, 0x00, 0x00, 0x00, "rotated transformed clip exterior");
    AssertPixel(pixels, 350, 87, 0x00, 0x00, 0xFF, "untransformed top border side");
    AssertPixel(pixels, 213, 130, 0x00, 0x00, 0xFF, "rotated top border side");
    AssertPixel(pixels, 170, 173, 0x00, 0xFF, 0x00, "rotated right border side");
    AssertPixel(pixels, 127, 130, 0xFF, 0x00, 0x00, "rotated bottom border side");
    AssertPixel(pixels, 170, 87, 0x00, 0xFF, 0xFF, "rotated left border side");
    AssertPixel(pixels, 214, 96, 0x00, 0x00, 0xFF, "rotated asymmetric elliptical corner");
    AssertPixel(pixels, 608, 214, 0xFF, 0x00, 0xFF, "scaled top border side");
    AssertPixel(pixels, 557, 267, 0xFF, 0xFF, 0x00, "skewed top border side");
    AssertPixel(pixels, 800, 380, 0xFF, 0xFF, 0xFF, "rotated uniform annular ring top");
    AssertPixel(pixels, 840, 420, 0xFF, 0xFF, 0xFF, "rotated uniform annular ring right");
    AssertPixel(pixels, 800, 460, 0xFF, 0xFF, 0xFF, "rotated uniform annular ring bottom");
    AssertPixel(pixels, 760, 420, 0xFF, 0xFF, 0xFF, "rotated uniform annular ring left");
}

// Diagnostic, intentionally not yet a pass/fail regression: it renders the exact same four CSS
// side geometries two ways under an arbitrary transform. The result tells us whether a visual
// discontinuity belongs to the geometry/partition itself or to the production PushLayer path.
static void VerifyTransformedBorderSideLayerIsolation()
{
    using var device = D3D11.D3D11CreateDevice(DriverType.Warp, DeviceCreationFlags.BgraSupport);
    using var context = device.ImmediateContext;
    using var dxgiDevice = device.QueryInterface<IDXGIDevice>();
    using var factory = D2D1.D2D1CreateFactory<ID2D1Factory1>();
    using var d2dDevice = factory.CreateDevice(dxgiDevice);
    using var target = d2dDevice.CreateDeviceContext(DeviceContextOptions.None);
    using var texture = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.RenderTarget,
        ResourceUsage.Default, CpuAccessFlags.None, 1, 0, ResourceOptionFlags.None));
    using var surface = texture.QueryInterface<IDXGISurface>();
    using var output = target.CreateBitmapFromDxgiSurface(surface,
        new BitmapProperties1(new PixelFormat(Format.B8G8R8A8_UNorm,
            Vortice.DCommon.AlphaMode.Premultiplied), 96, 96,
            BitmapOptions.Target | BitmapOptions.CannotDraw));
    target.Target = output;

    var colours = new[]
    {
        new ColorRgba(1, 0, 0, 1), new ColorRgba(0, 1, 0, 1),
        new ColorRgba(0, 0, 1, 1), new ColorRgba(1, 1, 0, 1),
    };
    var brushes = new Dictionary<ColorRgba, ID2D1SolidColorBrush>();
    ID2D1SolidColorBrush GetBrush(ColorRgba colour) => brushes.TryGetValue(colour, out var brush)
        ? brush
        : brushes[colour] = target.CreateSolidColorBrush(new Color4(colour.R, colour.G, colour.B, colour.A));

    const float directCenterX = 260;
    const float layeredCenterX = 700;
    const float centerY = 350;
    var directOuter = new RectF(directCenterX - 100, centerY - 100, 200, 200);
    var directInner = new RectF(directCenterX - 85, centerY - 85, 170, 170);
    var layeredOuter = new RectF(layeredCenterX - 100, centerY - 100, 200, 200);
    var layeredInner = new RectF(layeredCenterX - 85, centerY - 85, 170, 170);
    var outerRadii = CornerRadii.Uniform(100);
    var innerRadii = CornerRadii.Uniform(85);

    target.BeginDraw();
    target.Clear(new Color4(0, 0, 0, 1));
    target.Transform = Matrix3x2.CreateRotation(MathF.PI / 4f, new Vector2(directCenterX, centerY));
    for (var side = 0; side < 4; side++)
    {
        using var geometry = Direct2DBorderSidePainter.CreateSideGeometry(
            factory, directOuter, outerRadii, directInner, innerRadii, side);
        target.FillGeometry(geometry, GetBrush(colours[side]));
    }

    target.Transform = Matrix3x2.CreateRotation(MathF.PI / 4f, new Vector2(layeredCenterX, centerY));
    for (var side = 0; side < 4; side++)
    {
        Direct2DBorderSidePainter.Draw(factory, target,
            new FillBorderSideCommand(layeredOuter, outerRadii, layeredInner, innerRadii, side, 4, colours[side]),
            GetBrush);
    }

    // A second pair exercises the same complete four-side construction with elliptical radii
    // under a composed rotate/scale/skew transform. The original Fitness Tracker rings are
    // circular, but CSS corner ownership must remain continuous for every affine transform.
    const float ellipticalDirectCenterX = 220;
    const float ellipticalLayeredCenterX = 700;
    const float ellipticalCenterY = 120;
    var ellipticalDirectOuter = new RectF(ellipticalDirectCenterX - 100, ellipticalCenterY - 70, 200, 140);
    var ellipticalDirectInner = new RectF(ellipticalDirectCenterX - 85, ellipticalCenterY - 55, 170, 110);
    var ellipticalLayeredOuter = new RectF(ellipticalLayeredCenterX - 100, ellipticalCenterY - 70, 200, 140);
    var ellipticalLayeredInner = new RectF(ellipticalLayeredCenterX - 85, ellipticalCenterY - 55, 170, 110);
    var ellipticalOuterRadii = new CornerRadii(100, 70, 100, 70, 100, 70, 100, 70);
    var ellipticalInnerRadii = new CornerRadii(85, 55, 85, 55, 85, 55, 85, 55);
    Matrix3x2 EllipticalTransform(float x)
    {
        // Compose local scale, skew, and rotation, then derive the translation that keeps the
        // element's border-box centre invariant. This mirrors a CSS transform-origin rather
        // than assuming a particular helper-composition order in System.Numerics.
        var transform = Matrix3x2.CreateScale(1.1f, .85f) *
                        Matrix3x2.CreateSkew(.2f, .1f) *
                        Matrix3x2.CreateRotation(.4f);
        var center = new Vector2(x, ellipticalCenterY);
        var transformedCenter = Vector2.Transform(center, transform);
        transform.M31 += center.X - transformedCenter.X;
        transform.M32 += center.Y - transformedCenter.Y;
        return transform;
    }
    target.Transform = EllipticalTransform(ellipticalDirectCenterX);
    for (var side = 0; side < 4; side++)
    {
        using var geometry = Direct2DBorderSidePainter.CreateSideGeometry(
            factory, ellipticalDirectOuter, ellipticalOuterRadii, ellipticalDirectInner, ellipticalInnerRadii, side);
        target.FillGeometry(geometry, GetBrush(colours[side]));
    }
    target.Transform = EllipticalTransform(ellipticalLayeredCenterX);
    for (var side = 0; side < 4; side++)
    {
        Direct2DBorderSidePainter.Draw(factory, target,
            new FillBorderSideCommand(ellipticalLayeredOuter, ellipticalOuterRadii, ellipticalLayeredInner, ellipticalInnerRadii, side, 4, colours[side]),
            GetBrush);
    }
    target.Transform = Matrix3x2.Identity;
    target.EndDraw().CheckError();
    foreach (var brush in brushes.Values) brush.Dispose();

    using var staging = device.CreateTexture2D(new Texture2DDescription(
        Format.B8G8R8A8_UNorm, width, height, 1, 1, BindFlags.None,
        ResourceUsage.Staging, CpuAccessFlags.Read, 1, 0, ResourceOptionFlags.None));
    context.CopyResource(staging, texture);
    context.Flush();
    var rendered = ReadPixels(context, staging);
    var directCovered = 0;
    var layeredCovered = 0;
    var mismatches = 0;
    var seamFailures = 0;
    const float sampleRadius = 92.5f;
    for (var sample = 0; sample < 1440; sample++)
    {
        var angle = MathF.Tau * sample / 1440f;
        var dx = (int)MathF.Round(MathF.Cos(angle) * sampleRadius);
        var dy = (int)MathF.Round(MathF.Sin(angle) * sampleRadius);
        var directOffset = (((int)centerY + dy) * width + ((int)directCenterX + dx)) * 4;
        var layeredOffset = (((int)centerY + dy) * width + ((int)layeredCenterX + dx)) * 4;
        var directHasColour = rendered[directOffset] != 0 || rendered[directOffset + 1] != 0 || rendered[directOffset + 2] != 0;
        var layeredHasColour = rendered[layeredOffset] != 0 || rendered[layeredOffset + 1] != 0 || rendered[layeredOffset + 2] != 0;
        directCovered += directHasColour ? 1 : 0;
        layeredCovered += layeredHasColour ? 1 : 0;
        mismatches += rendered[directOffset] != rendered[layeredOffset] ||
                      rendered[directOffset + 1] != rendered[layeredOffset + 1] ||
                      rendered[directOffset + 2] != rendered[layeredOffset + 2] ? 1 : 0;
    }

    // The CSS side partitions meet at the four diagonal corner rays. Probe the exact seams and
    // their immediate neighbours so a narrow gap cannot hide between uniform samples.
    foreach (var seamDegrees in new[] { 0f, 90f, 180f, 270f })
    {
        foreach (var offsetDegrees in new[] { -.75f, 0f, .75f })
        {
            var angle = (seamDegrees + offsetDegrees) * MathF.PI / 180f;
            var x = (int)MathF.Round(layeredCenterX + MathF.Cos(angle) * sampleRadius);
            var y = (int)MathF.Round(centerY + MathF.Sin(angle) * sampleRadius);
            var offset = (y * width + x) * 4;
            if (rendered[offset] == 0 && rendered[offset + 1] == 0 && rendered[offset + 2] == 0) seamFailures++;
        }
    }

    if (directCovered != 1440 || layeredCovered != 1440 || mismatches != 0 || seamFailures != 0)
    {
        throw new InvalidOperationException(
            $"Transformed CSS border-side partitions must form one continuous, equivalent ring: " +
            $"direct={directCovered}/1440, layer={layeredCovered}/1440, mismatches={mismatches}, seams={seamFailures}/12.");
    }

    // 45° clockwise in Direct2D moves the original top/right/bottom/left sectors to
    // NE/SE/SW/NW. Check ownership as well as coverage, so a side cannot silently invade
    // its neighbour while still producing a continuous ring.
    AssertRingColour(rendered, directCenterX, centerY, sampleRadius, -45, 0x00, 0x00, 0xFF, "direct transformed top side");
    AssertRingColour(rendered, directCenterX, centerY, sampleRadius, 45, 0x00, 0xFF, 0x00, "direct transformed right side");
    AssertRingColour(rendered, directCenterX, centerY, sampleRadius, 135, 0xFF, 0x00, 0x00, "direct transformed bottom side");
    AssertRingColour(rendered, directCenterX, centerY, sampleRadius, 225, 0x00, 0xFF, 0xFF, "direct transformed left side");
    AssertRingColour(rendered, layeredCenterX, centerY, sampleRadius, -45, 0x00, 0x00, 0xFF, "layered transformed top side");
    AssertRingColour(rendered, layeredCenterX, centerY, sampleRadius, 45, 0x00, 0xFF, 0x00, "layered transformed right side");
    AssertRingColour(rendered, layeredCenterX, centerY, sampleRadius, 135, 0xFF, 0x00, 0x00, "layered transformed bottom side");
    AssertRingColour(rendered, layeredCenterX, centerY, sampleRadius, 225, 0x00, 0xFF, 0xFF, "layered transformed left side");

    var ellipticalDirectCovered = 0;
    var ellipticalLayeredCovered = 0;
    var ellipticalMismatches = 0;
    for (var sample = 0; sample < 1440; sample++)
    {
        var angle = MathF.Tau * sample / 1440f;
        var directPoint = Vector2.Transform(new Vector2(
            ellipticalDirectCenterX + MathF.Cos(angle) * 92.5f,
            ellipticalCenterY + MathF.Sin(angle) * 62.5f), EllipticalTransform(ellipticalDirectCenterX));
        var layeredPoint = Vector2.Transform(new Vector2(
            ellipticalLayeredCenterX + MathF.Cos(angle) * 92.5f,
            ellipticalCenterY + MathF.Sin(angle) * 62.5f), EllipticalTransform(ellipticalLayeredCenterX));
        var directOffset = ((int)MathF.Round(directPoint.Y) * width + (int)MathF.Round(directPoint.X)) * 4;
        var layeredOffset = ((int)MathF.Round(layeredPoint.Y) * width + (int)MathF.Round(layeredPoint.X)) * 4;
        var directHasColour = rendered[directOffset] != 0 || rendered[directOffset + 1] != 0 || rendered[directOffset + 2] != 0;
        var layeredHasColour = rendered[layeredOffset] != 0 || rendered[layeredOffset + 1] != 0 || rendered[layeredOffset + 2] != 0;
        ellipticalDirectCovered += directHasColour ? 1 : 0;
        ellipticalLayeredCovered += layeredHasColour ? 1 : 0;
        ellipticalMismatches += rendered[directOffset] != rendered[layeredOffset] ||
                                rendered[directOffset + 1] != rendered[layeredOffset + 1] ||
                                rendered[directOffset + 2] != rendered[layeredOffset + 2] ? 1 : 0;
    }
    if (ellipticalDirectCovered != 1440 || ellipticalLayeredCovered != 1440 || ellipticalMismatches != 0)
    {
        throw new InvalidOperationException(
            $"Elliptical transformed border-side partitions must remain continuous and equivalent: " +
            $"direct={ellipticalDirectCovered}/1440, layer={ellipticalLayeredCovered}/1440, mismatches={ellipticalMismatches}.");
    }
}

static void VerifyBackendTransformStack()
{
    using var window = HiddenWindow.Create();
    using var backend = new Direct2DRenderBackend(window.Handle, 96, 64, 1);
    var direct = new DisplayList([
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 64), CornerRadii.Zero, new ColorRgba(.1f, .2f, .3f, 1)),
    ]);
    backend.Render(direct, new ColorRgba(0, 0, 0, 1));
    if (backend.LastTransformPushes != 0 || backend.LastTransformPops != 0)
    {
        throw new InvalidOperationException("A display list without transforms must not alter the Direct2D transform stack.");
    }

    var transformed = new DisplayList([
        new PushTransformCommand(new TransformMatrix(1, 0, 0, 1, 12, 6)),
        new PushClipCommand(new RectF(0, 0, 48, 40), CornerRadii.Uniform(6)),
        new FillRoundedRectangleCommand(new RectF(0, 0, 64, 48), CornerRadii.Uniform(4), new ColorRgba(.9f, .2f, .1f, 1)),
        // A cache miss closes the scene to build the glyph mask and then restores all active
        // clips. Keeping this command inside the transformed rounded clip verifies that the
        // restored mask transform is the original one, not the transform active at some later
        // point in the display list.
        new DrawTextShadowCommand("S", new RectF(4, 6, 24, 20), "Segoe UI", 14,
            UiFontWeight.Normal, TextAlign.Start,
            new BoxShadow(1, 2, 3, 0, new ColorRgba(0, 0, 0, .75f), false), Wrap: false),
        new PopClipCommand(),
        new PushTransformCommand(new TransformMatrix(2, 0, 0, 2, 0, 0)),
        new FillRoundedRectangleCommand(new RectF(2, 2, 10, 10), CornerRadii.Zero, new ColorRgba(.2f, .8f, .3f, 1)),
        new PopTransformCommand(),
        new PopTransformCommand(),
    ]);
    backend.Render(transformed, new ColorRgba(0, 0, 0, 1));
    if (backend.LastTransformPushes != 2 || backend.LastTransformPops != 2)
    {
        throw new InvalidOperationException(
            $"The real Direct2D backend did not consume the complete transform bracket stack: pushes={backend.LastTransformPushes}, pops={backend.LastTransformPops}.");
    }

    // The backdrop snapshot is in scene coordinates, unlike ordinary local draw commands. This
    // exercises the production path that temporarily switches to identity to sample that image,
    // while retaining the translated geometric clip for the filtered element.
    var transformedBackdrop = new DisplayList([
        new FillRoundedRectangleCommand(new RectF(0, 0, 96, 64), CornerRadii.Zero, new ColorRgba(.8f, .1f, .2f, 1)),
        new PushTransformCommand(new TransformMatrix(1, 0, 0, 1, 12, 8)),
        new ApplyBackdropFilterCommand(new RectF(4, 4, 52, 36), CornerRadii.Uniform(6),
            [new BackdropFilter(BackdropFilterKind.Blur, new CssLength(4, CssUnit.Pixels), 0)]),
        new FillRoundedRectangleCommand(new RectF(4, 4, 52, 36), CornerRadii.Uniform(6), new ColorRgba(1, 1, 1, .2f)),
        new PopTransformCommand(),
    ]);
    backend.Render(transformedBackdrop, new ColorRgba(0, 0, 0, 1));
    if (backend.LastTransformPushes != 1 || backend.LastTransformPops != 1 ||
        backend.LastBackdropSnapshotPasses != 1 || backend.LastBackdropCompositionPasses != 1)
    {
        throw new InvalidOperationException("A transformed backdrop-filter did not preserve the Direct2D transform/scene boundaries.");
    }
}

static void AssertOpaque(byte[] pixels, int x, int y, string label)
{
    var offset = ((y * width) + x) * 4;
    if (pixels[offset + 3] != 0xFF || (pixels[offset] == 0 && pixels[offset + 1] == 0 && pixels[offset + 2] == 0))
    {
        throw new InvalidOperationException($"The WARP background-image painter left {label} uncovered.");
    }
}

static void AssertPixel(byte[] pixels, int x, int y, byte blue, byte green, byte red, string label)
{
    var offset = ((y * width) + x) * 4;
    if (pixels[offset] != blue || pixels[offset + 1] != green || pixels[offset + 2] != red || pixels[offset + 3] != 0xFF)
    {
        throw new InvalidOperationException($"The WARP background-image painter failed {label}: " +
            $"expected BGRA({blue},{green},{red},255), got BGRA({pixels[offset]},{pixels[offset + 1]},{pixels[offset + 2]},{pixels[offset + 3]}).");
    }
}

static void AssertRingColour(byte[] pixels, float centerX, float centerY, float radius, float degrees,
    byte blue, byte green, byte red, string label)
{
    var radians = degrees * MathF.PI / 180f;
    var x = (int)MathF.Round(centerX + MathF.Cos(radians) * radius);
    var y = (int)MathF.Round(centerY + MathF.Sin(radians) * radius);
    AssertPixel(pixels, x, y, blue, green, red, label);
}

static unsafe byte[] ReadPixels(ID3D11DeviceContext context, ID3D11Texture2D texture)
{
    var mapped = context.Map(texture, 0, MapMode.Read, Vortice.Direct3D11.MapFlags.None);
    try
    {
        var pixels = new byte[width * height * 4];
        var source = (byte*)mapped.DataPointer;
        for (var row = 0; row < height; row++)
        {
            Marshal.Copy((nint)(source + row * mapped.RowPitch), pixels, row * width * 4, width * 4);
        }

        return pixels;
    }
    finally
    {
        context.Unmap(texture, 0);
    }
}

static ulong Fnv1a64(IEnumerable<byte> bytes)
{
    const ulong offset = 14695981039346656037UL;
    const ulong prime = 1099511628211UL;
    var hash = offset;
    foreach (var value in bytes)
    {
        hash ^= value;
        hash *= prime;
    }

    return hash;
}

sealed class HiddenWindow : IDisposable
{
    private HiddenWindow(nint handle) => Handle = handle;
    public nint Handle { get; }

    public static HiddenWindow Create(int width = 96, int height = 64)
    {
        var handle = CreateWindowExW(0, "STATIC", "", 0, 0, 0, width, height,
            nint.Zero, nint.Zero, nint.Zero, nint.Zero);
        if (handle == nint.Zero)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not create the hidden Direct2D test window.");
        }
        return new HiddenWindow(handle);
    }

    public void Dispose()
    {
        if (Handle != nint.Zero)
        {
            _ = DestroyWindow(Handle);
        }
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowExW(uint extendedStyle, string className, string windowName,
        uint style, int x, int y, int width, int height, nint parent, nint menu, nint instance, nint parameter);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(nint window);
}

sealed class RawMarkupComponent(string markup) : UiComponent
{
    protected override void BuildRenderTree(MarkupWriter writer) => writer.WriteLiteral(markup);
}

readonly record struct PixelBounds(int Left, int Top, int Right, int Bottom)
{
    public int Width => Right - Left + 1;
    public int Height => Bottom - Top + 1;
}
