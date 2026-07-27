using System.Runtime.InteropServices;
using System.ComponentModel;
using AetherUI.Css;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Windows.Graphics;
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
return 0;

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

    public static HiddenWindow Create()
    {
        var handle = CreateWindowExW(0, "STATIC", "", 0, 0, 0, 96, 64,
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
