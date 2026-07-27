import { createRequire } from "node:module";

const require = createRequire(import.meta.url);

export const Win32Message = Object.freeze({
  NCCREATE: 0x0081,
  NCDESTROY: 0x0082,
  DESTROY: 0x0002,
  CLOSE: 0x0010,
  PAINT: 0x000f,
  ERASE_BACKGROUND: 0x0014,
  SIZE: 0x0005,
  DPI_CHANGED: 0x02e0,
  MOUSE_MOVE: 0x0200,
  MOUSE_LEAVE: 0x02a3,
  MOUSE_WHEEL: 0x020a,
  LBUTTON_DOWN: 0x0201,
  LBUTTON_UP: 0x0202,
  KEY_DOWN: 0x0100,
  CHAR: 0x0102,
  SET_CURSOR: 0x0020,
  GET_MIN_MAX_INFO: 0x0024,
});

export const Win32Key = Object.freeze({
  BACKSPACE: 0x08,
  TAB: 0x09,
  ENTER: 0x0d,
  ESCAPE: 0x1b,
  SPACE: 0x20,
  END: 0x23,
  HOME: 0x24,
  LEFT: 0x25,
  UP: 0x26,
  RIGHT: 0x27,
  DOWN: 0x28,
  DELETE: 0x2e,
});

const WS_OVERLAPPEDWINDOW = 0x00cf0000;
const CW_USEDEFAULT = -2147483648;
const SW_SHOW = 5;
const GWLP_USERDATA = -21;
const CS_OWNDC = 0x0020;
const TME_LEAVE = 0x00000002;
const DPIX_CONTEXT_PER_MONITOR_V2 = -4;
const IDC_ARROW = 32512;
const IDC_IBEAM = 32513;
const IDC_HAND = 32649;
const HTCLIENT = 1;

const windowsStates = new WeakMap();
const windowsApis = new WeakMap();

function loadKoffi(ffi) {
  if (ffi) return ffi;
  try {
    return require("koffi");
  } catch (error) {
    throw new Error("The Windows desktop backend requires the 'koffi' package.", { cause: error });
  }
}

function pointerKey(pointer, ffi) {
  if (pointer == null) return "0";
  if (typeof pointer === "object" && typeof ffi?.address === "function") return String(ffi.address(pointer));
  return String(pointer);
}

function asBigInt(value, ffi) {
  if (typeof value === "object" && value && typeof ffi?.address === "function") return ffi.address(value);
  return typeof value === "bigint" ? value : BigInt(value ?? 0);
}

function signedWord(value) {
  const word = Number(asBigInt(value) & 0xffffn);
  return word > 0x7fff ? word - 0x10000 : word;
}

function packedPoint(lParam, ffi) {
  const packed = asBigInt(lParam, ffi);
  return { x: signedWord(packed), y: signedWord(packed >> 16n) };
}

function highWord(value) {
  return signedWord(asBigInt(value) >> 16n);
}

function intptrValue(value, ffi) {
  return asBigInt(value, ffi);
}

function f(library, name, result, args) {
  return library.func("__stdcall", name, result, args);
}

export function createWindowsApi(ffi) {
  const koffi = loadKoffi(ffi);
  if (process.platform !== "win32") throw new Error("The AetherUI Windows backend only runs on Windows.");
  const cached = windowsApis.get(koffi);
  if (cached) return cached;
  const user32 = koffi.load("user32.dll");
  const kernel32 = koffi.load("kernel32.dll");
  const gdi32 = koffi.load("gdi32.dll");
  const msimg32 = koffi.load("msimg32.dll");

  const opaque = () => koffi.opaque();
  const HWND = koffi.pointer("AetherHWND", opaque());
  const HINSTANCE = koffi.pointer("AetherHINSTANCE", opaque());
  const HICON = koffi.pointer("AetherHICON", opaque());
  const HCURSOR = koffi.pointer("AetherHCURSOR", opaque());
  const HDC = koffi.pointer("AetherHDC", opaque());
  const HGDIOBJ = koffi.pointer("AetherHGDIOBJ", opaque());
  const HBRUSH = HGDIOBJ;
  const HMENU = koffi.pointer("AetherHMENU", opaque());
  const VoidPointer = koffi.pointer("void");
  const WndProc = koffi.proto("__stdcall", "AetherWndProc", "intptr_t", [HWND, "uint32_t", "uintptr_t", VoidPointer]);
  const WndProcPointer = koffi.pointer(WndProc);

  const Point = koffi.struct("AetherWinPoint", { x: "int32_t", y: "int32_t" });
  const Rect = koffi.struct("AetherWinRect", {
    left: "int32_t",
    top: "int32_t",
    right: "int32_t",
    bottom: "int32_t",
  });
  const WindowClass = koffi.struct("AetherWndClassExW", {
    size: "uint32_t",
    style: "uint32_t",
    windowProc: WndProcPointer,
    classExtra: "int32_t",
    windowExtra: "int32_t",
    instance: HINSTANCE,
    icon: HICON,
    cursor: HCURSOR,
    background: HBRUSH,
    menuName: "str16",
    className: "str16",
    smallIcon: HICON,
  });
  const CreateStruct = koffi.struct("AetherCreateStructW", {
    createParams: VoidPointer,
    instance: HINSTANCE,
    menu: HMENU,
    parent: HWND,
    height: "int32_t",
    width: "int32_t",
    y: "int32_t",
    x: "int32_t",
    style: "int32_t",
    name: "str16",
    className: "str16",
    extendedStyle: "uint32_t",
  });
  const Message = koffi.struct("AetherMessage", {
    window: HWND,
    message: "uint32_t",
    wParam: "uintptr_t",
    lParam: "intptr_t",
    time: "uint32_t",
    point: Point,
    private: "uint32_t",
  });
  const PaintStruct = koffi.struct("AetherPaintStruct", {
    hdc: HDC,
    erase: "int32_t",
    paintRect: Rect,
    restore: "int32_t",
    incUpdate: "int32_t",
    reserved: koffi.array("uint8_t", 32),
  });
  const TrackMouse = koffi.struct("AetherTrackMouseEvent", {
    size: "uint32_t",
    flags: "uint32_t",
    window: HWND,
    hoverTime: "uint32_t",
  });
  const MinMaxInfo = koffi.struct("AetherMinMaxInfo", {
    reserved: Point,
    maxSize: Point,
    maxPosition: Point,
    minTrackSize: Point,
    maxTrackSize: Point,
  });
  const Size = koffi.struct("AetherGdiSize", { width: "int32_t", height: "int32_t" });
  const LogFont = koffi.struct("AetherLogFontW", {
    height: "int32_t",
    width: "int32_t",
    escapement: "int32_t",
    orientation: "int32_t",
    weight: "int32_t",
    italic: "uint8_t",
    underline: "uint8_t",
    strikeOut: "uint8_t",
    charSet: "uint8_t",
    outPrecision: "uint8_t",
    clipPrecision: "uint8_t",
    quality: "uint8_t",
    pitchAndFamily: "uint8_t",
    faceName: koffi.array("char16_t", 32, "String"),
  });
  const Vertex = koffi.struct("AetherTriVertex", {
    x: "int32_t",
    y: "int32_t",
    red: "uint16_t",
    green: "uint16_t",
    blue: "uint16_t",
    alpha: "uint16_t",
  });
  const GradientRect = koffi.struct("AetherGradientRect", { upperLeft: "uint32_t", lowerRight: "uint32_t" });

  const api = {
    ffi: koffi,
    types: { HWND, HINSTANCE, HDC, HGDIOBJ, VoidPointer, WndProc, WndProcPointer, Point, Rect, WindowClass, CreateStruct, Message, PaintStruct, TrackMouse, MinMaxInfo, Size, LogFont, Vertex, GradientRect },
    user32: {
      registerClass: f(user32, "RegisterClassExW", "uint16_t", [koffi.pointer(WindowClass)]),
      createWindow: f(user32, "CreateWindowExW", HWND, ["uint32_t", "str16", "str16", "uint32_t", "int32_t", "int32_t", "int32_t", "int32_t", HWND, HMENU, HINSTANCE, VoidPointer]),
      showWindow: f(user32, "ShowWindow", "int32_t", [HWND, "int32_t"]),
      updateWindow: f(user32, "UpdateWindow", "int32_t", [HWND]),
      getMessage: f(user32, "GetMessageW", "int32_t", [koffi.out(koffi.pointer(Message)), HWND, "uint32_t", "uint32_t"]),
      translateMessage: f(user32, "TranslateMessage", "int32_t", [koffi.pointer(Message)]),
      dispatchMessage: f(user32, "DispatchMessageW", "intptr_t", [koffi.pointer(Message)]),
      defWindowProc: f(user32, "DefWindowProcW", "intptr_t", [HWND, "uint32_t", "uintptr_t", "intptr_t"]),
      postQuit: f(user32, "PostQuitMessage", "void", ["int32_t"]),
      destroyWindow: f(user32, "DestroyWindow", "int32_t", [HWND]),
      getClientRect: f(user32, "GetClientRect", "int32_t", [HWND, koffi.out(koffi.pointer(Rect))]),
      invalidateRect: f(user32, "InvalidateRect", "int32_t", [HWND, koffi.pointer(Rect), "int32_t"]),
      beginPaint: f(user32, "BeginPaint", HDC, [HWND, koffi.out(koffi.pointer(PaintStruct))]),
      endPaint: f(user32, "EndPaint", "int32_t", [HWND, koffi.pointer(PaintStruct)]),
      fillRect: f(user32, "FillRect", "int32_t", [HDC, koffi.pointer(Rect), HBRUSH]),
      getDpiForWindow: f(user32, "GetDpiForWindow", "uint32_t", [HWND]),
      adjustWindowRect: f(user32, "AdjustWindowRectExForDpi", "int32_t", [koffi.inout(koffi.pointer(Rect)), "uint32_t", "int32_t", "uint32_t", "uint32_t"]),
      setWindowLongPtr: f(user32, "SetWindowLongPtrW", "intptr_t", [HWND, "int32_t", "intptr_t"]),
      getWindowLongPtr: f(user32, "GetWindowLongPtrW", "intptr_t", [HWND, "int32_t"]),
      setWindowPos: f(user32, "SetWindowPos", "int32_t", [HWND, HWND, "int32_t", "int32_t", "int32_t", "int32_t", "uint32_t"]),
      setCapture: f(user32, "SetCapture", HWND, [HWND]),
      releaseCapture: f(user32, "ReleaseCapture", "int32_t", []),
      setFocus: f(user32, "SetFocus", HWND, [HWND]),
      trackMouse: f(user32, "TrackMouseEvent", "int32_t", [koffi.inout(koffi.pointer(TrackMouse))]),
      screenToClient: f(user32, "ScreenToClient", "int32_t", [HWND, koffi.inout(koffi.pointer(Point))]),
      loadCursor: f(user32, "LoadCursorW", HCURSOR, [VoidPointer, "intptr_t"]),
      setCursor: f(user32, "SetCursor", HCURSOR, [HCURSOR]),
      setDpiAwareness: f(user32, "SetProcessDpiAwarenessContext", "int32_t", ["intptr_t"]),
      drawText: f(user32, "DrawTextW", "int32_t", [HDC, "str16", "int32_t", koffi.inout(koffi.pointer(Rect)), "uint32_t"]),
    },
    kernel32: {
      getModuleHandle: f(kernel32, "GetModuleHandleW", HINSTANCE, ["str16"]),
      getLastError: f(kernel32, "GetLastError", "uint32_t", []),
    },
    gdi32: {
      createCompatibleDC: f(gdi32, "CreateCompatibleDC", HDC, [HDC]),
      deleteDC: f(gdi32, "DeleteDC", "int32_t", [HDC]),
      createSolidBrush: f(gdi32, "CreateSolidBrush", HGDIOBJ, ["uint32_t"]),
      createPen: f(gdi32, "CreatePen", HGDIOBJ, ["int32_t", "int32_t", "uint32_t"]),
      createFontIndirect: f(gdi32, "CreateFontIndirectW", HGDIOBJ, [koffi.pointer(LogFont)]),
      getStockObject: f(gdi32, "GetStockObject", HGDIOBJ, ["int32_t"]),
      deleteObject: f(gdi32, "DeleteObject", "int32_t", [HGDIOBJ]),
      selectObject: f(gdi32, "SelectObject", HGDIOBJ, [HDC, HGDIOBJ]),
      roundRect: f(gdi32, "RoundRect", "int32_t", [HDC, "int32_t", "int32_t", "int32_t", "int32_t", "int32_t", "int32_t"]),
      rectangle: f(gdi32, "Rectangle", "int32_t", [HDC, "int32_t", "int32_t", "int32_t", "int32_t"]),
      saveDC: f(gdi32, "SaveDC", "int32_t", [HDC]),
      restoreDC: f(gdi32, "RestoreDC", "int32_t", [HDC, "int32_t"]),
      intersectClipRect: f(gdi32, "IntersectClipRect", "int32_t", [HDC, "int32_t", "int32_t", "int32_t", "int32_t"]),
      setBkMode: f(gdi32, "SetBkMode", "int32_t", [HDC, "int32_t"]),
      setTextColor: f(gdi32, "SetTextColor", "uint32_t", [HDC, "uint32_t"]),
      getTextExtent: f(gdi32, "GetTextExtentPoint32W", "int32_t", [HDC, "str16", "int32_t", koffi.out(koffi.pointer(Size))]),
      setTextAlign: f(gdi32, "SetTextAlign", "uint32_t", [HDC, "uint32_t"]),
      createRectRgn: f(gdi32, "CreateRectRgn", HGDIOBJ, ["int32_t", "int32_t", "int32_t", "int32_t"]),
      selectClipRgn: f(gdi32, "SelectClipRgn", "int32_t", [HDC, HGDIOBJ]),
    },
    msimg32: {
      gradientFill: f(msimg32, "GradientFill", "int32_t", [HDC, koffi.pointer(Vertex), "uint32_t", VoidPointer, "uint32_t", "uint32_t"]),
    },
  };
  windowsApis.set(koffi, api);
  return api;
}

function registerWindowClass(api, state) {
  if (state.classAtom) return;
  state.moduleHandle = api.kernel32.getModuleHandle(null);
  state.windowProc = (hwnd, message, wParam, lParam) => {
    try {
      const nativeLParam = intptrValue(lParam, api.ffi);
      let contextPointer = api.user32.getWindowLongPtr(hwnd, GWLP_USERDATA);
      if (message === Win32Message.NCCREATE) {
        const create = api.ffi.decode(lParam, api.types.CreateStruct);
        contextPointer = create.createParams;
        if (!contextPointer || contextPointer === 0n) return api.user32.defWindowProc(hwnd, message, wParam, nativeLParam);
        const pending = state.windows.get(pointerKey(contextPointer, api.ffi));
        if (pending) {
          pending._windowHandle = hwnd;
          api.user32.setWindowLongPtr(hwnd, GWLP_USERDATA, intptrValue(contextPointer, api.ffi));
        }
      }
      const window = state.windows.get(pointerKey(contextPointer, api.ffi));
      if (!window) return api.user32.defWindowProc(hwnd, message, wParam, nativeLParam);
      return window._handleMessage(message, wParam, lParam);
    } catch (error) {
      const pointer = api.user32.getWindowLongPtr(hwnd, GWLP_USERDATA);
      const window = state.windows.get(pointerKey(pointer, api.ffi));
      window?._fail(error);
      api.user32.postQuit(1);
      return 0;
    }
  };
  state.windowProcRegistration = api.ffi.register(state.windowProc, api.types.WndProcPointer);
  const className = state.className;
  const windowClass = {
    size: api.ffi.sizeof(api.types.WindowClass),
    style: CS_OWNDC,
    windowProc: state.windowProcRegistration,
    classExtra: 0,
    windowExtra: 0,
    instance: state.moduleHandle,
    icon: null,
    cursor: api.user32.loadCursor(null, IDC_ARROW),
    background: null,
    menuName: null,
    className,
    smallIcon: null,
  };
  state.classAtom = api.user32.registerClass(windowClass);
  if (!state.classAtom) throw new Error(`RegisterClassExW failed with Win32 error ${api.kernel32.getLastError()}.`);
}

export class Win32Window {
  static create(options = {}, ffi) {
    const api = createWindowsApi(ffi);
    let state = windowsStates.get(api.ffi);
    if (!state) {
      state = { api, windows: new Map(), nextId: 1, className: `AetherUI.JavaScript.${process.pid}`, classAtom: 0 };
      windowsStates.set(api.ffi, state);
    }
    registerWindowClass(api, state);
    api.user32.setDpiAwareness(DPIX_CONTEXT_PER_MONITOR_V2);
    return new Win32Window(api, state, options);
  }

  constructor(api, state, options = {}) {
    this.api = api;
    this._state = state;
    this.title = options.title ?? "AetherUI";
    this.width = Math.max(1, options.width ?? 960);
    this.height = Math.max(1, options.height ?? 640);
    this.minimumWidth = Math.max(1, options.minimumWidth ?? 420);
    this.minimumHeight = Math.max(1, options.minimumHeight ?? 260);
    this._windowHandle = null;
    this._contextPointer = null;
    this._host = null;
    this._trackingMouse = false;
    this._disposed = false;
    this._fatalError = null;
    this._pendingHighSurrogate = "";
    this._id = state.nextId++;
  }

  attachHost(host) {
    this._host = host;
    if (this._windowHandle) this._updateViewport();
  }

  _assertAlive() {
    if (this._disposed) throw new Error("Win32Window has been disposed.");
  }

  create() {
    this._assertAlive();
    if (this._windowHandle) throw new Error("The window has already been created.");
    const rect = { left: 0, top: 0, right: this.width, bottom: this.height };
    this.api.user32.adjustWindowRect(rect, WS_OVERLAPPEDWINDOW, 0, 0, 96);
    this._contextPointer = this.api.ffi.alloc("uint64_t", 1);
    this.api.ffi.encode(this._contextPointer, "uint64_t", BigInt(this._id));
    this._state.windows.set(pointerKey(this._contextPointer, this.api.ffi), this);
    try {
      const hwnd = this.api.user32.createWindow(
        0,
        this._state.className,
        this.title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        Math.max(1, rect.right - rect.left),
        Math.max(1, rect.bottom - rect.top),
        null,
        null,
        this._state.moduleHandle,
        this._contextPointer,
      );
      if (!hwnd || hwnd === 0n) throw new Error(`CreateWindowExW failed with Win32 error ${this.api.kernel32.getLastError()}.`);
      this._windowHandle = hwnd;
      this._updateViewport();
    } catch (error) {
      this._state.windows.delete(pointerKey(this._contextPointer, this.api.ffi));
      this.api.ffi.free(this._contextPointer);
      this._contextPointer = null;
      throw error;
    }
  }

  show() {
    this._assertAlive();
    if (!this._windowHandle) throw new Error("create() must be called before show().");
    this.api.user32.showWindow(this._windowHandle, SW_SHOW);
    this.api.user32.updateWindow(this._windowHandle);
  }

  requestRender() {
    if (this._windowHandle && !this._disposed) this.api.user32.invalidateRect(this._windowHandle, null, 0);
  }

  run() {
    this._assertAlive();
    const message = {};
    while (true) {
      const result = Number(this.api.user32.getMessage(message, null, 0, 0));
      if (result === -1) throw new Error(`GetMessageW failed with Win32 error ${this.api.kernel32.getLastError()}.`);
      if (result === 0) {
        if (this._fatalError) throw new Error("AetherUI window terminated because of an unhandled error.", { cause: this._fatalError });
        return Number(message.wParam ?? 0);
      }
      this.api.user32.translateMessage(message);
      this.api.user32.dispatchMessage(message);
    }
  }

  close() {
    if (this._windowHandle) this.api.user32.destroyWindow(this._windowHandle);
  }

  _updateViewport() {
    if (!this._windowHandle || !this._host) return;
    const dpiValue = Number(this.api.user32.getDpiForWindow(this._windowHandle)) || 96;
    const dpi = dpiValue / 96;
    const client = {};
    if (this.api.user32.getClientRect(this._windowHandle, client) && client.right > 0 && client.bottom > 0) {
      this._host.setViewport(client.right, client.bottom, dpi);
    }
  }

  _fail(error) {
    this._fatalError ??= error;
  }

  _hostPoint(lParam) {
    const raw = packedPoint(lParam, this.api.ffi);
    const dpiValue = this._windowHandle ? Number(this.api.user32.getDpiForWindow(this._windowHandle)) || 96 : 96;
    const dpi = dpiValue / 96;
    return { x: raw.x / dpi, y: raw.y / dpi };
  }

  _cursorFromHost(point) {
    const cursor = Number(this._host?.cursorAt?.(point) ?? 0);
    const id = cursor === 2 ? IDC_IBEAM : cursor === 1 ? IDC_HAND : IDC_ARROW;
    this.api.user32.setCursor(this.api.user32.loadCursor(null, id));
  }

  _handleMessage(message, wParam, lParam) {
    switch (Number(message)) {
      case Win32Message.ERASE_BACKGROUND:
        return 1;
      case Win32Message.PAINT: {
        const paint = {};
        const hdc = this.api.user32.beginPaint(this._windowHandle, paint);
        try {
          this._host?.render(hdc);
        } finally {
          this.api.user32.endPaint(this._windowHandle, paint);
        }
        return 0;
      }
      case Win32Message.SIZE:
        this._updateViewport();
        return 0;
      case Win32Message.DPI_CHANGED: {
        const suggested = this.api.ffi.decode(lParam, this.api.types.Rect);
        this.api.user32.setWindowPos(this._windowHandle, null, suggested.left, suggested.top, suggested.right - suggested.left, suggested.bottom - suggested.top, 0x0010 | 0x0004);
        this._updateViewport();
        return 0;
      }
      case Win32Message.MOUSE_MOVE: {
        if (!this._trackingMouse) {
          const tracking = { size: this.api.ffi.sizeof(this.api.types.TrackMouse), flags: TME_LEAVE, window: this._windowHandle, hoverTime: 0 };
          this._trackingMouse = Boolean(this.api.user32.trackMouse(tracking));
        }
        const point = this._hostPoint(lParam);
        this._host?.pointerMoved(point);
        this._cursorFromHost(point);
        return 0;
      }
      case Win32Message.MOUSE_LEAVE:
        this._trackingMouse = false;
        this._host?.pointerLeft();
        this._cursorFromHost({ x: 0, y: 0 });
        return 0;
      case Win32Message.MOUSE_WHEEL: {
        const screenPoint = packedPoint(lParam, this.api.ffi);
        const point = { x: screenPoint.x, y: screenPoint.y };
        this.api.user32.screenToClient(this._windowHandle, point);
        const dpiValue = Number(this.api.user32.getDpiForWindow(this._windowHandle)) || 96;
        const dpi = dpiValue / 96;
        this._host?.mouseWheelScrolled({ x: point.x / dpi, y: point.y / dpi }, highWord(wParam) / 120);
        return 0;
      }
      case Win32Message.LBUTTON_DOWN:
        this.api.user32.setCapture(this._windowHandle);
        this.api.user32.setFocus(this._windowHandle);
        this._host?.pointerPressed(this._hostPoint(lParam));
        return 0;
      case Win32Message.LBUTTON_UP:
        this.api.user32.releaseCapture();
        this._host?.pointerReleased(this._hostPoint(lParam));
        return 0;
      case Win32Message.KEY_DOWN: {
        const key = mapVirtualKey(Number(wParam));
        if (key) this._host?.keyPressed(key);
        return 0;
      }
      case Win32Message.CHAR: {
        const code = Number(wParam);
        if (code >= 0xd800 && code <= 0xdbff) {
          this._pendingHighSurrogate = String.fromCharCode(code);
        } else if (code >= 0xdc00 && code <= 0xdfff && this._pendingHighSurrogate) {
          this._host?.textInput(this._pendingHighSurrogate + String.fromCharCode(code));
          this._pendingHighSurrogate = "";
        } else {
          this._host?.textInput(String.fromCodePoint(code));
          this._pendingHighSurrogate = "";
        }
        return 0;
      }
      case Win32Message.SET_CURSOR:
        if (Number(asBigInt(lParam, this.api.ffi) & 0xffffn) === HTCLIENT) {
          this._cursorFromHost({ x: 0, y: 0 });
          return 1;
        }
        break;
      case Win32Message.GET_MIN_MAX_INFO: {
        const info = this.api.ffi.decode(lParam, this.api.types.MinMaxInfo);
        const dpiValue = Number(this.api.user32.getDpiForWindow(this._windowHandle)) || 96;
        const dpi = dpiValue / 96;
        info.minTrackSize.x = Math.ceil(this.minimumWidth * dpi);
        info.minTrackSize.y = Math.ceil(this.minimumHeight * dpi);
        this.api.ffi.encode(lParam, this.api.types.MinMaxInfo, info);
        return 0;
      }
      case Win32Message.CLOSE:
        this.close();
        return 0;
      case Win32Message.DESTROY:
        this.api.user32.postQuit(0);
        return 0;
      case Win32Message.NCDESTROY:
        {
          const destroyedWindow = this._windowHandle;
          this.api.user32.setWindowLongPtr(destroyedWindow, GWLP_USERDATA, 0);
          this._state.windows.delete(pointerKey(this._contextPointer, this.api.ffi));
          if (this._contextPointer) {
            this.api.ffi.free(this._contextPointer);
            this._contextPointer = null;
          }
          this._windowHandle = null;
          return this.api.user32.defWindowProc(destroyedWindow, message, wParam, intptrValue(lParam, this.api.ffi));
        }
      default:
        break;
    }
    return this.api.user32.defWindowProc(this._windowHandle, message, wParam, intptrValue(lParam, this.api.ffi));
  }

  dispose() {
    if (this._disposed) return;
    this._disposed = true;
    if (this._windowHandle) this.api.user32.destroyWindow(this._windowHandle);
    if (this._contextPointer) {
      this._state.windows.delete(pointerKey(this._contextPointer, this.api.ffi));
      this.api.ffi.free(this._contextPointer);
      this._contextPointer = null;
    }
    this._windowHandle = null;
  }

  [Symbol.dispose]() {
    this.dispose();
  }
}

function mapVirtualKey(value) {
  switch (value) {
    case Win32Key.BACKSPACE: return "Backspace";
    case Win32Key.TAB: return "Tab";
    case Win32Key.ENTER: return "Enter";
    case Win32Key.ESCAPE: return "Escape";
    case Win32Key.SPACE: return "Space";
    case Win32Key.END: return "End";
    case Win32Key.HOME: return "Home";
    case Win32Key.LEFT: return "Left";
    case Win32Key.UP: return "Up";
    case Win32Key.RIGHT: return "Right";
    case Win32Key.DOWN: return "Down";
    case Win32Key.DELETE: return "Delete";
    default: return null;
  }
}

export function loadKoffiForWindows(ffi) {
  return loadKoffi(ffi);
}
