import { createWindowsApi } from "./windows.mjs";

const HOLLOW_BRUSH = 5;
const TRANSPARENT = 1;
const DT_CENTER = 0x00000001;
const DT_VCENTER = 0x00000004;
const DT_WORDBREAK = 0x00000010;
const DT_SINGLELINE = 0x00000020;
const DT_CALCRECT = 0x00000400;
const GRADIENT_FILL_RECT_H = 0;
const GRADIENT_FILL_RECT_V = 1;

function clamp(value, min = 0, max = 1) {
  return Math.min(max, Math.max(min, Number(value) || 0));
}

function byte(value) {
  return Math.round(clamp(value) * 255);
}

function toColorRef(color, background = { r: 255, g: 255, b: 255, a: 1 }) {
  const alpha = clamp(color?.a, 0, 1);
  const backgroundAlpha = clamp(background?.a, 0, 1);
  const effectiveBackground = {
    r: clamp(background?.r),
    g: clamp(background?.g),
    b: clamp(background?.b),
    a: backgroundAlpha,
  };
  const outputAlpha = alpha + effectiveBackground.a * (1 - alpha);
  const channel = (foreground, under) => {
    if (outputAlpha <= 0) return 0;
    return clamp((clamp(foreground) * alpha + under * effectiveBackground.a * (1 - alpha)) / outputAlpha);
  };
  const red = byte(channel(color?.r, effectiveBackground.r));
  const green = byte(channel(color?.g, effectiveBackground.g));
  const blue = byte(channel(color?.b, effectiveBackground.b));
  return (blue << 16) | (green << 8) | red;
}

function toPixelRect(rect, dpi) {
  const x = Math.round(Number(rect?.x) * dpi);
  const y = Math.round(Number(rect?.y) * dpi);
  const width = Math.max(0, Math.round(Number(rect?.width) * dpi));
  const height = Math.max(0, Math.round(Number(rect?.height) * dpi));
  return { left: x, top: y, right: x + width, bottom: y + height };
}

function radiusInPixels(radius, dpi) {
  const value = Math.max(0, Math.round(Number(radius) * dpi));
  return Math.max(1, value * 2);
}

function fontWeight(value) {
  const weight = Number(value) || 400;
  return Math.max(100, Math.min(900, Math.round(weight / 100) * 100));
}

function fontKey(style = {}) {
  return `${style.fontFamily ?? "Segoe UI"}\u0000${Number(style.fontSize ?? 16)}\u0000${fontWeight(style.fontWeight)}`;
}

function fontDefinition(api, style = {}, dpi = 1) {
  const size = Math.max(1, Number(style.fontSize ?? 16));
  return {
    height: -Math.max(1, Math.round(size * dpi)),
    width: 0,
    escapement: 0,
    orientation: 0,
    weight: fontWeight(style.fontWeight),
    italic: 0,
    underline: 0,
    strikeOut: 0,
    charSet: 1,
    outPrecision: 0,
    clipPrecision: 0,
    quality: 5,
    pitchAndFamily: 0,
    faceName: String(style.fontFamily || "Segoe UI").slice(0, 31),
  };
}

function selectObject(api, hdc, object, callback) {
  if (!object) return callback(null);
  const previous = api.gdi32.selectObject(hdc, object);
  try {
    return callback(previous);
  } finally {
    if (previous) api.gdi32.selectObject(hdc, previous);
  }
}

function createGradient(api, hdc, command, dpi, background) {
  const gradient = command.gradient;
  if (!gradient?.stops?.length) return false;
  const rect = toPixelRect(command.rect, dpi);
  if (rect.right <= rect.left || rect.bottom <= rect.top) return true;
  const first = gradient.stops[0];
  const last = gradient.stops[gradient.stops.length - 1];
  const opacity = clamp(command.opacity ?? 1);
  const firstColor = { ...first.color, a: clamp(first.color?.a) * opacity };
  const lastColor = { ...last.color, a: clamp(last.color?.a) * opacity };
  const vertices = [
    {
      x: rect.left,
      y: rect.top,
      red: byte(firstColor?.r) * 257,
      green: byte(firstColor?.g) * 257,
      blue: byte(firstColor?.b) * 257,
      alpha: byte(firstColor?.a) * 257,
    },
    {
      x: rect.right,
      y: rect.bottom,
      red: byte(lastColor?.r) * 257,
      green: byte(lastColor?.g) * 257,
      blue: byte(lastColor?.b) * 257,
      alpha: byte(lastColor?.a) * 257,
    },
  ];
  const vertexMemory = api.ffi.alloc(api.types.Vertex, vertices.length);
  const gradientRectMemory = api.ffi.alloc(api.types.GradientRect, 1);
  try {
    api.ffi.encode(vertexMemory, api.types.Vertex, vertices, vertices.length);
    api.ffi.encode(gradientRectMemory, api.types.GradientRect, [{ upperLeft: 0, lowerRight: 1 }], 1);
    const horizontal = Math.abs(Number(gradient.directionY ?? 0)) < Math.abs(Number(gradient.directionX ?? 1));
    const mode = horizontal ? GRADIENT_FILL_RECT_H : GRADIENT_FILL_RECT_V;
    if (!api.msimg32.gradientFill(hdc, vertexMemory, 2, gradientRectMemory, 1, mode)) {
        const brush = api.gdi32.createSolidBrush(toColorRef(firstColor, background));
      try {
        api.user32.fillRect(hdc, rect, brush);
      } finally {
        if (brush) api.gdi32.deleteObject(brush);
      }
    }
  } finally {
    api.ffi.free(vertexMemory);
    api.ffi.free(gradientRectMemory);
  }
  return true;
}

/**
 * Small GDI renderer for the display-list ABI. It intentionally owns no window;
 * Win32Window supplies the paint HDC, which keeps the renderer replaceable for
 * Direct2D, Skia, or a test renderer.
 */
export class GdiRenderBackend {
  constructor({ ffi, clearColor = { r: 0.96, g: 0.97, b: 0.98, a: 1 } } = {}) {
    this.api = createWindowsApi(ffi);
    this.clearColor = clearColor;
    this.dpiScale = 1;
    this.pixelWidth = 1;
    this.pixelHeight = 1;
    this._measureDc = this.api.gdi32.createCompatibleDC(null);
    if (!this._measureDc) throw new Error("CreateCompatibleDC failed while initializing the GDI renderer.");
    this._fonts = new Map();
    this._disposed = false;
  }

  _assertAlive() {
    if (this._disposed) throw new Error("GdiRenderBackend has been disposed.");
  }

  resize(pixelWidth, pixelHeight, dpiScale = 1) {
    this._assertAlive();
    this.pixelWidth = Math.max(1, Math.round(Number(pixelWidth) || 1));
    this.pixelHeight = Math.max(1, Math.round(Number(pixelHeight) || 1));
    this.dpiScale = Number.isFinite(dpiScale) && dpiScale > 0 ? dpiScale : 1;
  }

  _font(style) {
    const key = `${fontKey(style)}\u0000${this.dpiScale}`;
    let font = this._fonts.get(key);
    if (!font) {
      font = this.api.gdi32.createFontIndirect(fontDefinition(this.api, style, this.dpiScale));
      if (!font) throw new Error(`CreateFontIndirectW failed with Win32 error ${this.api.kernel32.getLastError()}.`);
      this._fonts.set(key, font);
    }
    return font;
  }

  measureText(text, style = {}, maxWidth = Number.POSITIVE_INFINITY) {
    this._assertAlive();
    const value = String(text ?? "");
    const font = this._font(style);
    return selectObject(this.api, this._measureDc, font, () => {
      const widthLimit = Number.isFinite(maxWidth) ? Math.max(1, Math.round(maxWidth * this.dpiScale)) : Number.MAX_SAFE_INTEGER;
      const lineHeight = Math.max(1, Math.round(Number(style.lineHeight ?? style.fontSize ?? 16) * this.dpiScale));
      if (value.length === 0) return { width: 0, height: lineHeight / this.dpiScale, baseline: lineHeight * 0.8 / this.dpiScale };
      if (Number.isFinite(maxWidth)) {
        const rect = { left: 0, top: 0, right: widthLimit, bottom: 0 };
        this.api.user32.drawText(this._measureDc, value, -1, rect, DT_CALCRECT | DT_WORDBREAK);
        return { width: Math.min(widthLimit, Math.max(0, rect.right - rect.left)) / this.dpiScale, height: Math.max(lineHeight, rect.bottom - rect.top) / this.dpiScale, baseline: lineHeight * 0.8 / this.dpiScale };
      }
      const size = {};
      this.api.gdi32.getTextExtent(this._measureDc, value, value.length, size);
      const width = Math.min(widthLimit, Math.max(0, Number(size.width) || 0));
      const lines = value.split("\n").length;
      return { width: width / this.dpiScale, height: lineHeight * lines / this.dpiScale, baseline: lineHeight * 0.8 / this.dpiScale };
    });
  }

  _fillRoundedRect(hdc, rect, radius, color) {
    const brush = this.api.gdi32.createSolidBrush(toColorRef(color, this.clearColor));
    if (!brush) return;
    try {
      selectObject(this.api, hdc, brush, () => this.api.gdi32.roundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius));
    } finally {
      this.api.gdi32.deleteObject(brush);
    }
  }

  _strokeRoundedRect(hdc, rect, radius, strokeWidth, color) {
    const pen = this.api.gdi32.createPen(0, Math.max(1, Math.round(strokeWidth * this.dpiScale)), toColorRef(color, this.clearColor));
    if (!pen) return;
    try {
      const hollow = this.api.gdi32.getStockObject(HOLLOW_BRUSH);
      const previousPen = this.api.gdi32.selectObject(hdc, pen);
      const previousBrush = this.api.gdi32.selectObject(hdc, hollow);
      try {
        this.api.gdi32.roundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
      } finally {
        if (previousBrush) this.api.gdi32.selectObject(hdc, previousBrush);
        if (previousPen) this.api.gdi32.selectObject(hdc, previousPen);
      }
    } finally {
      this.api.gdi32.deleteObject(pen);
    }
  }

  _drawText(hdc, command, color) {
    if (command.text == null || command.text.length === 0) return;
    const rect = toPixelRect(command.rect, this.dpiScale);
    const flags = (command.wrap ? DT_WORDBREAK : DT_SINGLELINE) | (command.textAlign === 1 ? DT_CENTER : command.textAlign === 2 ? 0x00000002 : 0) | (command.wrap ? 0 : DT_VCENTER);
    const font = this._font({ fontFamily: command.fontFamily, fontSize: command.fontSize, fontWeight: command.fontWeight, lineHeight: command.fontSize });
    const previousMode = this.api.gdi32.setBkMode(hdc, TRANSPARENT);
    const previousColor = this.api.gdi32.setTextColor(hdc, toColorRef(color, this.clearColor));
    const effectiveColor = command.opacity == null ? color : { ...color, a: clamp(color?.a) * clamp(command.opacity) };
    this.api.gdi32.setTextColor(hdc, toColorRef(effectiveColor, this.clearColor));
    selectObject(this.api, hdc, font, () => this.api.user32.drawText(hdc, command.text, -1, rect, flags));
    this.api.gdi32.setBkMode(hdc, previousMode);
    this.api.gdi32.setTextColor(hdc, previousColor);
  }

  render(displayList, clearColor = this.clearColor, hdc) {
    this._assertAlive();
    if (!hdc) throw new TypeError("GdiRenderBackend.render requires the HDC supplied by WM_PAINT.");
    const api = this.api;
    const saved = api.gdi32.saveDC(hdc);
    if (!saved) throw new Error("SaveDC failed while rendering AetherUI content.");
    try {
      const background = clearColor ?? this.clearColor;
      const canvas = { left: 0, top: 0, right: this.pixelWidth, bottom: this.pixelHeight };
      const brush = api.gdi32.createSolidBrush(toColorRef(background, background));
      if (brush) {
        try { api.user32.fillRect(hdc, canvas, brush); } finally { api.gdi32.deleteObject(brush); }
      }
      let clipDepth = 0;
      for (const command of displayList?.toArray?.() ?? []) {
        const rect = toPixelRect(command.rect, this.dpiScale);
        switch (command.kind) {
          case "fillRoundedRect":
            this._fillRoundedRect(hdc, rect, radiusInPixels(command.radius, this.dpiScale), command.color);
            break;
          case "drawBoxShadow": {
            const shadowRect = { ...rect, left: rect.left + Math.round(command.shadow.offsetX * this.dpiScale), right: rect.right + Math.round(command.shadow.offsetX * this.dpiScale), top: rect.top + Math.round(command.shadow.offsetY * this.dpiScale), bottom: rect.bottom + Math.round(command.shadow.offsetY * this.dpiScale) };
            const color = { ...command.shadow.color, a: clamp(command.shadow.color?.a) * 0.28 };
            this._fillRoundedRect(hdc, shadowRect, radiusInPixels(command.radius + command.shadow.blurRadius, this.dpiScale), color);
            break;
          }
          case "fillRoundedRectGradient":
            createGradient(api, hdc, command, this.dpiScale, background);
            break;
          case "strokeRoundedRect":
            this._strokeRoundedRect(hdc, rect, radiusInPixels(command.radius, this.dpiScale), command.strokeWidth, command.color);
            break;
          case "drawText":
            this._drawText(hdc, command, command.color);
            break;
          case "drawTextGradient":
            this._drawText(hdc, command, command.gradient?.stops?.[0]?.color ?? { r: 0, g: 0, b: 0, a: 1 });
            break;
          case "pushClip":
            if (api.gdi32.saveDC(hdc)) {
              clipDepth += 1;
              api.gdi32.intersectClipRect(hdc, rect.left, rect.top, rect.right, rect.bottom);
            }
            break;
          case "popClip":
            if (clipDepth > 0) {
              api.gdi32.restoreDC(hdc, -1);
              clipDepth -= 1;
            }
            break;
          default:
            break;
        }
      }
    } finally {
      api.gdi32.restoreDC(hdc, saved);
    }
  }

  dispose() {
    if (this._disposed) return;
    this._disposed = true;
    for (const font of this._fonts.values()) this.api.gdi32.deleteObject(font);
    this._fonts.clear();
    if (this._measureDc) this.api.gdi32.deleteDC(this._measureDc);
    this._measureDc = null;
  }

  [Symbol.dispose]() {
    this.dispose();
  }
}

export function createGdiRenderer(options) {
  return new GdiRenderBackend(options);
}
