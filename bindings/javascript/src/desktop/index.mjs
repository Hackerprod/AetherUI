import { createAether } from "../index.mjs";
import {
  DEFAULT_DESKTOP_CSS,
  DesktopComponent,
  DesktopEvent,
  DesktopHost,
  UiKey,
  createDesktopComponent,
  isDesktopPaintCommand,
} from "./host.mjs";
import { GdiRenderBackend } from "./renderer.mjs";
import { Win32Window } from "./windows.mjs";

/**
 * Owns the native binding, window, host, and renderer as one deterministic
 * desktop application. `runDesktop` is the convenient one-shot entrypoint.
 */
export class DesktopApplication {
  constructor(component, {
    aether = null,
    aetherOptions = {},
    window = null,
    windowOptions = {},
    renderer = null,
    rendererOptions = {},
    hostOptions = {},
  } = {}) {
    this.aether = aether ?? createAether(aetherOptions);
    this.window = window ?? Win32Window.create(windowOptions, rendererOptions.ffi ?? windowOptions.ffi);
    this.renderer = renderer ?? new GdiRenderBackend({
      ...rendererOptions,
      ffi: rendererOptions.ffi ?? this.window?.api?.ffi ?? windowOptions.ffi,
      clearColor: hostOptions.clearColor,
    });
    this.host = new DesktopHost(this.aether, component, {
      ...hostOptions,
      window: this.window,
      renderer: this.renderer,
    });
    this._created = false;
    this._disposed = false;
  }

  _assertAlive() {
    if (this._disposed) throw new Error("DesktopApplication has been disposed.");
  }

  create() {
    this._assertAlive();
    if (this._created) return this;
    this.window?.create?.();
    this._created = true;
    return this;
  }

  show() {
    this._assertAlive();
    this.create();
    this.window?.show?.();
    return this;
  }

  run() {
    this._assertAlive();
    this.show();
    return this.window?.run?.() ?? 0;
  }

  close() {
    if (!this._disposed) this.window?.close?.();
  }

  dispose() {
    if (this._disposed) return;
    this._disposed = true;
    try {
      this.host?.dispose?.();
    } finally {
      this.window?.dispose?.();
    }
  }

  [Symbol.dispose]() {
    this.dispose();
  }
}

export function createDesktopApplication(component, options) {
  return new DesktopApplication(component, options);
}

export function runDesktop(component, options) {
  const application = new DesktopApplication(component, options);
  try {
    return application.run();
  } finally {
    application.dispose();
  }
}

export {
  DEFAULT_DESKTOP_CSS,
  DesktopComponent,
  DesktopEvent,
  DesktopHost,
  GdiRenderBackend,
  UiKey,
  Win32Window,
  createDesktopComponent,
  isDesktopPaintCommand,
};
