using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using static AetherUI.Linux.Graphics.CairoNative;
namespace AetherUI.Linux.Graphics;

/// <summary>Owner-thread Linux software renderer. No window, GPU, event loop or incremental rendering.</summary>
public sealed partial class CairoRenderBackend : IRenderBackendWithText
{
    public const int MaximumDimension = 8192;
    public const int MaximumSurfaceBytes = 64 * 1024 * 1024;
    private readonly int _owner = Environment.CurrentManagedThreadId;
    private readonly ILinuxFramePresenter _presenter;
    private readonly PangoTextLayout _text;
    private readonly Action<int, int>? _beforeSurfaceAllocation;
    private int _width, _height;
    private float _dpi;
    private bool _disposed, _busy;
    private byte[]? _lastFrame;
    private Func<long, ScrollVisualOffset>? _offsetProvider;
    private Func<long, ScrollVisualState>? _stateProvider;
    public bool PresentationUncertain
    {
        get; private set;
    }
    public ReadOnlyMemory<byte> LastConfirmedFrame
    {
        get
        {
            Guard();
            return _lastFrame is null ? ReadOnlyMemory<byte>.Empty : new ReadOnlyMemory<byte>((byte[])_lastFrame.Clone());
        }
    }
    public CairoRenderBackend(LinuxSoftwareRenderTarget target, RenderViewport viewport) : this(target, viewport, null) { }
    internal CairoRenderBackend(LinuxSoftwareRenderTarget target, RenderViewport viewport, Action<int, int>? beforeSurfaceAllocation)
    {
        _beforeSurfaceAllocation = beforeSurfaceAllocation;
        ArgumentNullException.ThrowIfNull(target);
        _presenter = target.Presenter ?? throw new ArgumentException("A presenter is required.", nameof(target));
        if (!OperatingSystem.IsLinux() || !BitConverter.IsLittleEndian)
            throw new PlatformNotSupportedException("Cairo backend currently supports little-endian Linux.");
        ValidateViewport(viewport.PixelWidth, viewport.PixelHeight, viewport.DpiScale);
        try
        {
            if (viewport.PixelWidth > 0 && viewport.PixelHeight > 0)
            {
                using var probe = CreateSurface(viewport.PixelWidth, viewport.PixelHeight);
            }
            _text = new PangoTextLayout();
        }
        catch (DllNotFoundException ex) { throw new PlatformNotSupportedException("Install distribution Cairo, Pango, PangoCairo and GLib/GObject runtime libraries (Linux glibc x64).", ex); }
        catch (EntryPointNotFoundException ex) { throw new PlatformNotSupportedException("Cairo/Pango runtime is missing a required symbol; install compatible distribution libraries.", ex); }
        _width = viewport.PixelWidth;
        _height = viewport.PixelHeight;
        _dpi = viewport.DpiScale;
    }
    private CairoSurface CreateSurface(int width, int height)
    {
        _beforeSurfaceAllocation?.Invoke(width, height);
        return new CairoSurface(width, height);
    }
    private void Guard()
    {
        if (Environment.CurrentManagedThreadId != _owner)
            throw new InvalidOperationException("Use the renderer on its owner thread.");
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_busy)
            throw new InvalidOperationException("Renderer reentry is not permitted.");
    }
    private static void ValidateViewport(int width, int height, float dpi)
    {
        if (!float.IsFinite(dpi) || dpi <= 0)
            throw new ArgumentOutOfRangeException(nameof(dpi));
        if (width < 0 || height < 0 || width > MaximumDimension || height > MaximumDimension || (long)width * height * 4 > MaximumSurfaceBytes)
            throw new ArgumentOutOfRangeException(nameof(width), "Surface exceeds 8192 dimension or 64 MiB pixel budget.");
    }
    public void Resize(int pixelWidth, int pixelHeight, float dpiScale)
    {
        Guard();
        ValidateViewport(pixelWidth, pixelHeight, dpiScale);
        // Probe the replacement before changing state. A zero extent suspends without allocation.
        if (pixelWidth > 0 && pixelHeight > 0)
        {
            using var probe = CreateSurface(pixelWidth, pixelHeight);
        }
        _text.Clear();
        _width = pixelWidth;
        _height = pixelHeight;
        _dpi = dpiScale;
    }
    public TextMetrics MeasureText(string text, ComputedStyle style, float maxWidth = float.PositiveInfinity)
    {
        Guard();
        _busy = true;
        try
        {
            return _text.Measure(text, style, maxWidth);
        }
        finally { _busy = false; }
    }
    public void SetScrollCompositionMode(bool enabled)
    {
        Guard();
    }
    public void SetScrollOffsetProvider(Func<long, ScrollVisualOffset>? provider)
    {
        Guard();
        _offsetProvider = provider;
        _stateProvider = null;
    }
    public void SetScrollVisualStateProvider(Func<long, ScrollVisualState>? provider)
    {
        Guard();
        _stateProvider = provider;
        _offsetProvider = null;
    }
    public void Render(DisplayList displayList, ColorRgba clearColor)
    {
        Guard();
        ArgumentNullException.ThrowIfNull(displayList);
        _busy = true;
        try
        {
            var commands = displayList.Commands.ToArray();
            ValidateCommands(commands);
            ValidateColor(clearColor);
            if (_width == 0 || _height == 0)
                return;
            var ids = commands.Select(c => c switch { PushScrollOffsetCommand s => (long?)s.ScrollContainerId, DrawScrollbarChromeCommand s => (long?)s.ScrollContainerId, _ => null }).Where(x => x.HasValue).Select(x => x!.Value).Distinct();
            var states = new Dictionary<long, ScrollVisualState>();
            foreach (var id in ids)
            {
                var state = _stateProvider?.Invoke(id) ?? new ScrollVisualState(_offsetProvider?.Invoke(id) ?? default, default);
                ValidateVisualState(state);
                states[id] = state;
            }
            using var surface = CreateSurface(_width, _height);
            using var context = surface.CreateContext();
            var cr = context.Pointer;
            Color(cr, clearColor);
            cairo_set_operator(cr, 1);
            cairo_paint(cr);
            cairo_set_operator(cr, 2);
            cairo_scale(cr, _dpi, _dpi);
            foreach (var command in commands)
                Draw(cr, command, states);
            Check(cairo_status(cr));
            Check(cairo_surface_status(surface.Handle));
            var frame = surface.ReadPixels();
            try
            {
                _presenter.Present(_width, _height, checked(_width * 4), frame);
            }
            catch (Exception ex) { PresentationUncertain = true; throw new FramePresentationException(ex); }
            _lastFrame = frame;
            PresentationUncertain = false;
        }
        finally { _busy = false; }
    }
    private static void ValidateCommands(PaintCommand[] commands)
    {
        var stack = new Stack<Type>();
        foreach (var c in commands)
        {
            ValidateGeometry(c);
            switch (c)
            {
                case PushClipCommand:
                    stack.Push(typeof(PopClipCommand));
                    break;
                case PushTransformCommand:
                    stack.Push(typeof(PopTransformCommand));
                    break;
                case PushScrollOffsetCommand:
                    stack.Push(typeof(PopScrollOffsetCommand));
                    break;
                case PopClipCommand or PopTransformCommand or PopScrollOffsetCommand:
                    if (stack.Count == 0 || stack.Pop() != c.GetType())
                        throw new ArgumentException("Unbalanced or crossing drawing scopes.");
                    break;
                case FillRoundedRectangleCommand or DrawBoxShadowCommand or FillRoundedRectangleGradientCommand or StrokeRoundedRectangleCommand or FillBorderSideCommand or DrawTextCommand or DrawTextGradientCommand or DrawTextShadowCommand or ApplyBackdropFilterCommand or DrawBackgroundImageCommand or DrawScrollbarChromeCommand:
                    break;
                default:
                    throw new NotSupportedException($"Unsupported paint command: {c?.GetType().Name ?? "null"}.");
            }
        }
        if (stack.Count != 0)
            throw new ArgumentException("Unclosed drawing scopes.");
    }
    private void Draw(IntPtr cr, PaintCommand command, Dictionary<long, ScrollVisualState> states)
    {
        switch (command)
        {
            case FillRoundedRectangleCommand c:
                RoundPath(cr, c.Rectangle, c.Radii);
                Color(cr, c.Color);
                cairo_fill(cr);
                break;
            case StrokeRoundedRectangleCommand c:
                RoundPath(cr, c.Rectangle, c.Radii);
                Color(cr, c.Color);
                cairo_set_line_width(cr, Math.Max(.25, c.StrokeWidth));
                cairo_stroke(cr);
                break;
            case FillRoundedRectangleGradientCommand c:
                RoundPath(cr, c.Rectangle, c.Radii);
                Gradient(cr, c.GradientRectangle, c.Gradient, c.Opacity);
                cairo_fill(cr);
                break;
            case FillBorderSideCommand c:
                DrawBorder(cr, c);
                break;
            case DrawTextCommand c:
                DrawPlainText(cr, c);
                break;
            case DrawTextGradientCommand c:
                DrawGradientText(cr, c);
                break;
            case DrawTextShadowCommand c:
                DrawTextShadow(cr, c);
                break;
            case DrawBoxShadowCommand c:
                DrawBoxShadow(cr, c);
                break;
            case ApplyBackdropFilterCommand c:
                ApplyBackdrop(cr, c);
                break;
            case DrawBackgroundImageCommand c:
                DrawImage(cr, c);
                break;
            case PushClipCommand c:
                cairo_save(cr);
                RoundPath(cr, c.Rectangle, c.Radii);
                cairo_clip(cr);
                break;
            case PushTransformCommand c:
                cairo_save(cr);
                var m = new Matrix { Xx = c.Matrix.M11, Yx = c.Matrix.M12, Xy = c.Matrix.M21, Yy = c.Matrix.M22, X0 = c.Matrix.Dx, Y0 = c.Matrix.Dy };
                if (m.Xx * m.Yy - m.Yx * m.Xy == 0)
                {
                    cairo_new_path(cr);
                    cairo_clip(cr);
                }
                else
                {
                    cairo_transform(cr, ref m);
                }
                break;
            case PushScrollOffsetCommand c:
                cairo_save(cr);
                var o = states[c.ScrollContainerId].Offset;
                cairo_translate(cr, -o.X, -o.Y);
                break;
            case PopClipCommand or PopTransformCommand or PopScrollOffsetCommand:
                cairo_restore(cr);
                break;
            case DrawScrollbarChromeCommand c:
                DrawChrome(cr, states[c.ScrollContainerId].Chrome);
                break;
            default:
                throw new NotSupportedException();
        }
    }
    public void Dispose()
    {
        if (Environment.CurrentManagedThreadId != _owner)
            throw new InvalidOperationException("Use the renderer on its owner thread.");
        if (_disposed)
            return;
        Guard();
        _text.Dispose();
        _lastFrame = null;
        _disposed = true;
    }
}
