using System.ComponentModel;
using System.Runtime.InteropServices;
using AetherUI.Css;
using AetherUI.Events;
using AetherUI.Primitives;
using AetherUI.Runtime;
using AetherUI.Windows.Graphics;
using AetherUI.Windows.Interop;

namespace AetherUI.Windows.Hosting;

internal sealed class NativeWindow : IDisposable
{
    private const string WindowClassName = "AetherUI.NativeWindow.1";
    private const uint ClientHitTest = 1;
    private static readonly object ClassGate = new();
    private static readonly NativeMethods.WindowProcedure WindowProcedure = StaticWindowProcedure;
    private static ushort _windowClassAtom;
    private static nint _moduleHandle;

    private readonly UiHostOptions _options;
    private readonly ViewportUpdateCoalescer _viewportUpdates = new();
    private GCHandle _selfHandle;
    private nint _windowHandle;
    private UiDocumentHost? _host;
    private Exception? _fatalException;
    private float _dpiScale = 1f;
    private ViewportUpdate? _appliedViewport;
    private bool _trackingMouse;
    private bool _disposed;
    private CursorKind _cursor = CursorKind.Default;

    public NativeWindow(UiHostOptions options)
    {
        _options = options ?? throw new ArgumentNullException(nameof(options));
    }

    public void Create()
    {
        ThrowIfDisposed();
        if (_windowHandle != nint.Zero)
        {
            throw new InvalidOperationException("La ventana ya fue creada.");
        }

        _ = NativeMethods.SetProcessDpiAwarenessContext(NativeMethods.DpiAwarenessContextPerMonitorAwareV2);
        EnsureWindowClass();
        const uint initialDpi = 96;
        var initialBounds = _options.InitialWindowBounds;
        var outer = new NativeMethods.Rect();
        var x = unchecked((int)NativeMethods.CwUseDefault);
        var y = unchecked((int)NativeMethods.CwUseDefault);
        if (initialBounds is { IsUsable: true } placement)
        {
            outer.Left = placement.X;
            outer.Top = placement.Y;
            outer.Right = placement.X + placement.Width;
            outer.Bottom = placement.Y + placement.Height;
            x = placement.X;
            y = placement.Y;
        }
        else
        {
            outer.Left = 0;
            outer.Top = 0;
            outer.Right = Math.Max(1, _options.Width);
            outer.Bottom = Math.Max(1, _options.Height);
            _ = NativeMethods.AdjustWindowRectExForDpi(
                ref outer,
                NativeMethods.WsOverlappedWindow,
                false,
                0,
                initialDpi);
        }

        _selfHandle = GCHandle.Alloc(this, GCHandleType.Normal);
        _windowHandle = NativeMethods.CreateWindowExW(
            0,
            WindowClassName,
            _options.Title,
            NativeMethods.WsOverlappedWindow,
            x,
            y,
            Math.Max(1, outer.Width),
            Math.Max(1, outer.Height),
            nint.Zero,
            nint.Zero,
            _moduleHandle,
            GCHandle.ToIntPtr(_selfHandle));
        if (_windowHandle == nint.Zero)
        {
            var error = Marshal.GetLastWin32Error();
            _selfHandle.Free();
            throw new Win32Exception(error, "No se pudo crear la ventana nativa de AetherUI.");
        }

        QueueDpiAndViewport();
    }

    public void Attach(UiComponent component)
    {
        ThrowIfDisposed();
        ArgumentNullException.ThrowIfNull(component);
        if (_windowHandle == nint.Zero)
        {
            throw new InvalidOperationException("Create debe ejecutarse antes de Attach.");
        }

        if (_host is not null)
        {
            throw new InvalidOperationException("La ventana ya tiene un componente asociado.");
        }

        NativeMethods.GetClientRect(_windowHandle, out var client);
        Direct2DRenderBackend? renderer = null;
        UiDocumentHost? host = null;
        try
        {
            renderer = new Direct2DRenderBackend(
                _windowHandle,
                Math.Max(1, client.Width),
                Math.Max(1, client.Height),
                _dpiScale);
            host = new UiDocumentHost(component, renderer, renderer, _options);
            host.RenderRequested += OnRenderRequested;
            _host = host;
            host = null;
            renderer = null; // UiDocumentHost owns the backend after a successful construction.
            QueueDpiAndViewport();
            ApplyPendingViewport();
            RequestRender();
        }
        catch
        {
            if (host is not null)
            {
                host.RenderRequested -= OnRenderRequested;
                host.Dispose();
            }
            else
            {
                renderer?.Dispose();
                component.Dispose();
            }

            throw;
        }
    }

    public void Show()
    {
        ThrowIfDisposed();
        if (_windowHandle == nint.Zero)
        {
            throw new InvalidOperationException("La ventana todavía no fue creada.");
        }

        _ = NativeMethods.ShowWindow(_windowHandle, NativeMethods.SwShow);
        _ = NativeMethods.UpdateWindow(_windowHandle);
    }

    public int RunMessageLoop()
    {
        ThrowIfDisposed();
        while (true)
        {
            var result = NativeMethods.GetMessageW(out var message, nint.Zero, 0, 0);
            if (result == -1)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "GetMessageW falló.");
            }

            if (result == 0)
            {
                if (_fatalException is not null)
                {
                    throw new InvalidOperationException("La ventana de AetherUI terminó por un error no controlado.", _fatalException);
                }

                return unchecked((int)message.WParam);
            }

            _ = NativeMethods.TranslateMessage(ref message);
            _ = NativeMethods.DispatchMessageW(ref message);
        }
    }

    private static void EnsureWindowClass()
    {
        if (_windowClassAtom != 0)
        {
            return;
        }

        lock (ClassGate)
        {
            if (_windowClassAtom != 0)
            {
                return;
            }

            _moduleHandle = NativeMethods.GetModuleHandleW(null);
            var windowClass = new NativeMethods.WindowClassEx
            {
                Size = (uint)Marshal.SizeOf<NativeMethods.WindowClassEx>(),
                Style = NativeMethods.CsOwnDc,
                WindowProcedure = Marshal.GetFunctionPointerForDelegate(WindowProcedure),
                Instance = _moduleHandle,
                Cursor = NativeMethods.LoadCursorW(nint.Zero, new nint(NativeMethods.IdcArrow)),
                ClassName = WindowClassName,
            };
            _windowClassAtom = NativeMethods.RegisterClassExW(ref windowClass);
            if (_windowClassAtom == 0)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "No se pudo registrar la clase Win32 de AetherUI.");
            }
        }
    }

    private static nint StaticWindowProcedure(nint window, uint message, nuint wParam, nint lParam)
    {
        NativeWindow? instance = null;
        if (message == NativeMethods.WmNcCreate)
        {
            var creation = Marshal.PtrToStructure<NativeMethods.CreateStruct>(lParam);
            if (creation.CreateParameters != nint.Zero)
            {
                var handle = GCHandle.FromIntPtr(creation.CreateParameters);
                instance = handle.Target as NativeWindow;
                if (instance is not null)
                {
                    instance._windowHandle = window;
                    _ = NativeMethods.SetWindowLongPtr(window, NativeMethods.GwlpUserData, creation.CreateParameters);
                }
            }
        }
        else
        {
            var pointer = NativeMethods.GetWindowLongPtr(window, NativeMethods.GwlpUserData);
            if (pointer != nint.Zero)
            {
                instance = GCHandle.FromIntPtr(pointer).Target as NativeWindow;
            }
        }

        if (instance is null)
        {
            return NativeMethods.DefWindowProcW(window, message, wParam, lParam);
        }

        try
        {
            return instance.HandleMessage(message, wParam, lParam);
        }
        catch (Exception exception)
        {
            instance._fatalException ??= exception;
            NativeMethods.PostQuitMessage(1);
            return nint.Zero;
        }
    }

    private nint HandleMessage(uint message, nuint wParam, nint lParam)
    {
        switch (message)
        {
            case NativeMethods.WmEraseBackground:
                return new nint(1);
            case NativeMethods.WmPaint:
                Paint();
                return nint.Zero;
            case NativeMethods.WmSize:
                QueueDpiAndViewport();
                RequestRender();
                return nint.Zero;
            case NativeMethods.WmDpiChanged:
                ApplyDpiChange(wParam, lParam);
                return nint.Zero;
            case NativeMethods.WmMouseMove:
                MouseMoved(lParam);
                return nint.Zero;
            case NativeMethods.WmMouseWheel:
                MouseWheel(wParam, lParam, horizontal: (wParam & 0xffffu & NativeMethods.MkShift) != 0);
                return nint.Zero;
            case NativeMethods.WmMouseHWheel:
                MouseWheel(wParam, lParam, horizontal: true);
                return nint.Zero;
            case NativeMethods.WmMouseLeave:
                _trackingMouse = false;
                _host?.PointerLeft();
                _cursor = CursorKind.Default;
                return nint.Zero;
            case NativeMethods.WmLButtonDown:
                _ = NativeMethods.SetCapture(_windowHandle);
                _ = NativeMethods.SetFocus(_windowHandle);
                _host?.PointerPressed(ToLogicalPoint(lParam));
                return nint.Zero;
            case NativeMethods.WmLButtonUp:
                _ = NativeMethods.ReleaseCapture();
                _host?.PointerReleased(ToLogicalPoint(lParam));
                return nint.Zero;
            case NativeMethods.WmKeyDown:
                if (MapKey((uint)wParam) is { } key)
                {
                    _host?.KeyPressed(key);
                }
                return nint.Zero;
            case NativeMethods.WmChar:
                var character = unchecked((char)(uint)wParam);
                if (!char.IsControl(character))
                {
                    _host?.TextInput(character);
                }
                return nint.Zero;
            case NativeMethods.WmSetCursor:
                if ((uint)((long)lParam & 0xffff) == ClientHitTest)
                {
                    SetCursor(_cursor);
                    return new nint(1);
                }
                break;
            case NativeMethods.WmGetMinMaxInfo:
                ApplyMinimumSize(lParam);
                return nint.Zero;
            case NativeMethods.WmGetDlgCode:
                return new nint(NativeMethods.DlGcWantAllKeys | NativeMethods.DlGcWantArrows | NativeMethods.DlGcWantTab);
            case NativeMethods.WmClose:
                _ = NativeMethods.DestroyWindow(_windowHandle);
                return nint.Zero;
            case NativeMethods.WmDestroy:
                NativeMethods.PostQuitMessage(0);
                return nint.Zero;
            case NativeMethods.WmNcDestroy:
            {
                var destroyedWindow = _windowHandle;
                _ = NativeMethods.SetWindowLongPtr(destroyedWindow, NativeMethods.GwlpUserData, nint.Zero);
                _windowHandle = nint.Zero;
                return NativeMethods.DefWindowProcW(destroyedWindow, message, wParam, lParam);
            }
        }

        return NativeMethods.DefWindowProcW(_windowHandle, message, wParam, lParam);
    }

    private void Paint()
    {
        _ = NativeMethods.BeginPaint(_windowHandle, out var paint);
        try
        {
            ApplyPendingViewport();
            _host?.Render();
        }
        finally
        {
            _ = NativeMethods.EndPaint(_windowHandle, ref paint);
        }
    }

    private void MouseMoved(nint lParam)
    {
        ApplyPendingViewport();
        if (!_trackingMouse)
        {
            var tracking = new NativeMethods.TrackMouseEventData
            {
                Size = (uint)Marshal.SizeOf<NativeMethods.TrackMouseEventData>(),
                Flags = NativeMethods.TmeLeave,
                TrackWindow = _windowHandle,
            };
            _trackingMouse = NativeMethods.TrackMouseEvent(ref tracking);
        }

        var point = ToLogicalPoint(lParam);
        _host?.PointerMoved(point);
        _cursor = _host?.CursorAt(point) ?? CursorKind.Default;
        SetCursor(_cursor);
    }

    private void MouseWheel(nuint wParam, nint lParam, bool horizontal)
    {
        ApplyPendingViewport();
        // High word of wParam is the signed wheel delta in multiples of WHEEL_DELTA (120).
        var rawDelta = unchecked((short)((wParam >> 16) & 0xffff));
        var notches = rawDelta / 120f;

        // lParam here is screen coordinates, unlike every other mouse message's client-relative
        // lParam (see ToLogicalPoint) — must convert before it means anything to the host/layout.
        var screenPoint = new NativeMethods.Point
        {
            X = unchecked((short)((long)lParam & 0xffff)),
            Y = unchecked((short)(((long)lParam >> 16) & 0xffff)),
        };
        _ = NativeMethods.ScreenToClient(_windowHandle, ref screenPoint);
        var point = new PointF(screenPoint.X / _dpiScale, screenPoint.Y / _dpiScale);
        if (horizontal)
        {
            _host?.MouseHorizontalWheelScrolled(point, notches);
        }
        else
        {
            _host?.MouseWheelScrolled(point, notches);
        }
    }

    private void QueueDpiAndViewport()
    {
        if (_windowHandle == nint.Zero)
        {
            return;
        }

        var dpi = NativeMethods.GetDpiForWindow(_windowHandle);
        _dpiScale = dpi == 0 ? 1f : dpi / 96f;
        if (NativeMethods.GetClientRect(_windowHandle, out var client) && client.Width > 0 && client.Height > 0)
        {
            _viewportUpdates.Queue(client.Width, client.Height, _dpiScale);
        }
    }

    private void ApplyPendingViewport()
    {
        if (_host is null || !_viewportUpdates.TryTake(out var viewport))
        {
            return;
        }

        if (_appliedViewport == viewport)
        {
            return;
        }

        _host.SetViewport(viewport.PixelWidth, viewport.PixelHeight, viewport.DpiScale);
        _appliedViewport = viewport;
    }

    private void ApplyDpiChange(nuint wParam, nint lParam)
    {
        var dpi = (uint)(wParam & 0xffff);
        _dpiScale = dpi == 0 ? 1f : dpi / 96f;
        var suggested = Marshal.PtrToStructure<NativeMethods.Rect>(lParam);
        _ = NativeMethods.SetWindowPos(
            _windowHandle,
            nint.Zero,
            suggested.Left,
            suggested.Top,
            Math.Max(1, suggested.Width),
            Math.Max(1, suggested.Height),
            NativeMethods.SwpNoActivate | NativeMethods.SwpNoZOrder);
        QueueDpiAndViewport();
    }

    private void ApplyMinimumSize(nint lParam)
    {
        var minimum = Marshal.PtrToStructure<NativeMethods.MinMaxInfo>(lParam);
        minimum.MinTrackSize.X = Math.Max(1, (int)MathF.Ceiling(_options.MinimumWidth * _dpiScale));
        minimum.MinTrackSize.Y = Math.Max(1, (int)MathF.Ceiling(_options.MinimumHeight * _dpiScale));
        Marshal.StructureToPtr(minimum, lParam, false);
    }

    private PointF ToLogicalPoint(nint lParam)
    {
        var packed = unchecked((long)lParam);
        var x = unchecked((short)(packed & 0xffff));
        var y = unchecked((short)((packed >> 16) & 0xffff));
        return new PointF(x / _dpiScale, y / _dpiScale);
    }

    private static UiKey? MapKey(uint key) => key switch
    {
        0x08 => UiKey.Backspace,
        0x09 => UiKey.Tab,
        0x0D => UiKey.Enter,
        0x1B => UiKey.Escape,
        0x20 => UiKey.Space,
        0x23 => UiKey.End,
        0x24 => UiKey.Home,
        0x25 => UiKey.Left,
        0x26 => UiKey.Up,
        0x27 => UiKey.Right,
        0x28 => UiKey.Down,
        0x2E => UiKey.Delete,
        _ => null,
    };

    private static void SetCursor(CursorKind cursor)
    {
        var cursorId = cursor switch
        {
            CursorKind.Text => NativeMethods.IdcIBeam,
            CursorKind.Pointer => NativeMethods.IdcHand,
            _ => NativeMethods.IdcArrow,
        };
        _ = NativeMethods.SetCursor(NativeMethods.LoadCursorW(nint.Zero, new nint(cursorId)));
    }

    private void OnRenderRequested(object? sender, EventArgs e) => RequestRender();

    private void RequestRender()
    {
        if (_windowHandle != nint.Zero)
        {
            _ = NativeMethods.InvalidateRect(_windowHandle, nint.Zero, false);
        }
    }

    private void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, this);

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        if (_host is not null)
        {
            _host.RenderRequested -= OnRenderRequested;
            _host.Dispose();
            _host = null;
        }

        if (_windowHandle != nint.Zero)
        {
            _ = NativeMethods.DestroyWindow(_windowHandle);
            _windowHandle = nint.Zero;
        }

        if (_selfHandle.IsAllocated)
        {
            _selfHandle.Free();
        }

        _disposed = true;
        GC.SuppressFinalize(this);
    }
}
