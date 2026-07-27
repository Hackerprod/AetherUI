import {
  ElementState,
  NodeView,
  PaintCommandTag,
} from "../index.mjs";

export const UiKey = Object.freeze({
  NONE: "None",
  BACKSPACE: "Backspace",
  TAB: "Tab",
  ENTER: "Enter",
  ESCAPE: "Escape",
  SPACE: "Space",
  LEFT: "Left",
  UP: "Up",
  RIGHT: "Right",
  DOWN: "Down",
  HOME: "Home",
  END: "End",
  DELETE: "Delete",
});

/** CSS defaults used by the desktop host for semantic HTML controls. */
export const DEFAULT_DESKTOP_CSS = `
html, body { display: block; }
body { margin: 8px; color: #202124; font-family: "Segoe UI"; font-size: 16px; line-height: 1.25; }
head, title, meta, link, style, script { display: none; }
div, form, p, section, article, header, footer, main, nav, h1, h2, h3, h4, h5, h6 { display: block; }
span, label, strong, em, a { display: inline; }
h1 { font-size: 2em; font-weight: 700; margin: .67em 0; }
h2 { font-size: 1.5em; font-weight: 700; margin: .83em 0; }
h3 { font-size: 1.17em; font-weight: 700; margin: 1em 0; }
h4 { font-size: 1em; font-weight: 700; margin: 1.33em 0; }
p { margin: 1em 0; }
input, button { display: inline-block; font-family: "Segoe UI"; font-size: 14px; box-sizing: border-box; }
input { min-width: 180px; padding: 6px 9px; border: 1px solid #9aa0a6; border-radius: 4px; background-color: white; color: #202124; cursor: text; }
input:focus { border-color: #2563eb; }
input:invalid { border-color: #dc2626; }
button { min-width: 84px; padding: 6px 14px; border: 1px solid #9aa0a6; border-radius: 4px; background-color: white; cursor: pointer; text-align: center; }
button:hover { background-color: #f1f5f9; }
button:active { background-color: #e2e8f0; }
button:disabled { opacity: .55; cursor: default; }
`;

export class DesktopEvent {
  constructor(type, target, host, { point = { x: 0, y: 0 }, key = UiKey.NONE, character = "" } = {}) {
    this.type = type;
    this.target = target;
    this.currentTarget = target;
    this.host = host;
    this.pointerPosition = Object.freeze({ x: Number(point.x ?? 0), y: Number(point.y ?? 0) });
    this.key = key;
    this.character = character;
    this.value = target?.getAttribute?.("value") ?? null;
    this.defaultPrevented = false;
    this.propagationStopped = false;
  }

  preventDefault() {
    this.defaultPrevented = true;
  }

  stopPropagation() {
    this.propagationStopped = true;
  }

  get isDefaultPrevented() {
    return this.defaultPrevented;
  }

  get isPropagationStopped() {
    return this.propagationStopped;
  }
}

/**
 * Small component adapter for applications that want stateful render functions.
 * The render callback receives the host and returns an HTML string. Handlers are
 * referenced from markup as attributes such as onclick="save".
 */
export class DesktopComponent {
  constructor({ render, handlers = {}, dispose } = {}) {
    if (typeof render !== "function") throw new TypeError("DesktopComponent.render must be a function.");
    this._render = render;
    this._handlers = handlers;
    this._dispose = dispose;
    this._invalidated = new Set();
  }

  render(host) {
    return this._render(host);
  }

  get handlers() {
    return this._handlers;
  }

  onInvalidate(callback) {
    if (typeof callback !== "function") throw new TypeError("callback must be a function.");
    this._invalidated.add(callback);
    return () => this._invalidated.delete(callback);
  }

  stateHasChanged() {
    for (const callback of this._invalidated) callback();
  }

  dispose() {
    this._invalidated.clear();
    this._dispose?.();
  }
}

function asComponent(component) {
  if (typeof component === "function") return { render: component, handlers: {} };
  if (!component || typeof component.render !== "function") {
    throw new TypeError("component must be a render function or an object with render().");
  }
  return component;
}

function asHandlers(component) {
  const handlers = typeof component.handlers === "function" ? component.handlers() : component.handlers;
  return handlers ?? {};
}

function getHandler(handlers, token) {
  if (!token) return null;
  if (handlers instanceof Map) return handlers.get(token) ?? null;
  return typeof handlers[token] === "function" ? handlers[token] : null;
}

function isElement(node) {
  return node instanceof NodeView && node.isElement;
}

function nearestElement(node) {
  let current = node;
  while (current && !isElement(current)) current = current.parent;
  return current;
}

function childElements(node) {
  const result = [];
  for (let index = 0; index < node.childCount; index++) {
    const child = node.child(index);
    if (!child) continue;
    if (isElement(child)) result.push(child);
    result.push(...childElements(child));
  }
  return result;
}

function findElementsByTag(node, tagName, result = []) {
  if (!node) return result;
  if (isElement(node) && node.tagName?.toLowerCase() === tagName) result.push(node);
  for (let index = 0; index < node.childCount; index++) {
    const child = node.child(index);
    if (child) findElementsByTag(child, tagName, result);
  }
  return result;
}

function findAncestor(node, tagName) {
  for (let current = nearestElement(node); current; current = nearestElement(current.parent)) {
    if (current.tagName?.toLowerCase() === tagName) return current;
  }
  return null;
}

function parsePositiveInt(value) {
  const parsed = Number.parseInt(value ?? "", 10);
  return Number.isInteger(parsed) && parsed >= 0 ? parsed : null;
}

function elementId(node) {
  return node?.getAttribute?.("id") || null;
}

function handleKey(handle) {
  return handle == null ? "" : String(handle);
}

function disposeResources(resources) {
  let firstError = null;
  for (const resource of resources) {
    if (!resource) continue;
    try {
      resource.dispose?.();
    } catch (error) {
      firstError ??= error;
    }
  }
  if (firstError) throw firstError;
}

export class DesktopHost {
  constructor(aether, component, {
    renderer,
    window = null,
    width = 960,
    height = 640,
    dpiScale = 1,
    clearColor = { r: 1, g: 1, b: 1, a: 1 },
    defaultCss = DEFAULT_DESKTOP_CSS,
    onDiagnostic,
  } = {}) {
    if (!aether) throw new TypeError("aether is required.");
    if (!renderer || typeof renderer.measureText !== "function" || typeof renderer.render !== "function") {
      throw new TypeError("renderer must implement measureText() and render().");
    }
    this.aether = aether;
    this.component = asComponent(component);
    this.renderer = renderer;
    this.window = window;
    this.defaultCss = defaultCss;
    this.clearColor = { ...clearColor };
    this._onDiagnostic = onDiagnostic;
    this._viewport = { width: Math.max(1, Number(width)), height: Math.max(1, Number(height)) };
    this._dpiScale = Number.isFinite(dpiScale) && dpiScale > 0 ? dpiScale : 1;
    this._markupDirty = true;
    this._visualDirty = true;
    this._disposed = false;
    this._document = null;
    this._styleRuntime = null;
    this._styles = null;
    this._layoutEngine = null;
    this._layout = null;
    this._displayBuilder = null;
    this._displayList = null;
    this._focused = null;
    this._hovered = null;
    this._active = null;
    this._scrollOffsets = new Map();
    this._inputValues = new Map();
    this._unsubscribeInvalidated = null;
    this._renderedHandlers = null;

    try {
      this.renderer.resize?.(this._viewport.width * this._dpiScale, this._viewport.height * this._dpiScale, this._dpiScale);
      this._layoutEngine = this.aether.layout.create((text, style, maxWidth) => this.renderer.measureText(text, style, maxWidth));
      this._displayBuilder = this.aether.displayList.create((text, style, maxWidth) => this.renderer.measureText(text, style, maxWidth));
      if (typeof this.component.onInvalidate === "function") {
        this._unsubscribeInvalidated = this.component.onInvalidate(() => this.invalidate());
      }
      if (this.window?.attachHost) this.window.attachHost(this);
    } catch (error) {
      this._unsubscribeInvalidated?.();
      disposeResources([this._displayBuilder, this._layoutEngine, this.renderer]);
      throw error;
    }
  }

  get disposed() {
    return this._disposed;
  }

  get document() {
    return this._document;
  }

  get layout() {
    return this._layout;
  }

  get focusedElement() {
    return this._focused;
  }

  get needsRender() {
    return this._markupDirty || this._visualDirty;
  }

  get dpiScale() {
    return this._dpiScale;
  }

  get viewport() {
    return Object.freeze({ ...this._viewport });
  }

  _assertAlive() {
    if (this._disposed) throw new Error("DesktopHost has been disposed.");
  }

  invalidate() {
    this._assertAlive();
    this._visualDirty = true;
    this.window?.requestRender?.();
  }

  invalidateMarkup() {
    this._assertAlive();
    this._markupDirty = true;
    this._visualDirty = true;
    this.window?.requestRender?.();
  }

  setViewport(pixelWidth, pixelHeight, dpiScale = this._dpiScale) {
    this._assertAlive();
    if (!Number.isFinite(pixelWidth) || !Number.isFinite(pixelHeight) || pixelWidth <= 0 || pixelHeight <= 0) return;
    if (!Number.isFinite(dpiScale) || dpiScale <= 0) return;
    this._dpiScale = dpiScale;
    this._viewport = { width: pixelWidth / dpiScale, height: pixelHeight / dpiScale };
    this.renderer.resize?.(pixelWidth, pixelHeight, dpiScale);
    const root = this._document?.documentElement ?? this._document?.body;
    if (this._styleRuntime && root) {
      this._styleRuntime.setEnvironment({
        viewportWidth: this._viewport.width,
        viewportHeight: this._viewport.height,
        dpiScale: this._dpiScale,
      }, root);
    }
    this.invalidate();
  }

  render(paintContext) {
    this._assertAlive();
    this._ensureVisualTree();
    this.renderer.render(this._displayList, this.clearColor, paintContext);
    this._visualDirty = false;
  }

  _renderMarkup() {
    const rendered = this.component.render(this);
    if (typeof rendered === "string") {
      this._renderedHandlers = null;
      return rendered;
    }
    if (rendered && typeof rendered.markup === "string") {
      this._renderedHandlers = rendered.handlers ?? null;
      return rendered.markup;
    }
    throw new TypeError("component.render() must return an HTML string or { markup, handlers }.");
  }

  _captureState() {
    const ids = {
      focused: elementId(this._focused),
      hovered: elementId(this._hovered),
      active: elementId(this._active),
    };
    if (this._document) {
      for (const input of findElementsByTag(this._document.documentElement, "input")) {
        const id = elementId(input);
        const value = input.getAttribute("value");
        if (id && value != null) this._inputValues.set(id, value);
      }
    }
    return ids;
  }

  _restoreState(ids) {
    const restore = (id, state) => {
      if (!id || !this._document) return null;
      const element = this._document.getElementById(id);
      if (!element) return null;
      element.state = element.state | state;
      return element;
    };
    this._focused = restore(ids.focused, ElementState.FOCUS);
    this._hovered = restore(ids.hovered, ElementState.HOVER);
    this._active = restore(ids.active, ElementState.ACTIVE);
    for (const input of findElementsByTag(this._document?.documentElement, "input")) {
      const id = elementId(input);
      if (!id || input.getAttribute("value") != null) continue;
      const value = this._inputValues.get(id);
      if (value != null) input.setAttribute("value", value);
    }
  }

  _extractAuthorCss(document) {
    return findElementsByTag(document.documentElement, "style")
      .map((style) => style.textContent ?? "")
      .filter(Boolean)
      .join("\n");
  }

  _disposeVisualResources() {
    const resources = [this._displayList, this._layout, this._styles];
    this._displayList = null;
    this._layout = null;
    this._styles = null;
    disposeResources(resources);
  }

  _disposeDocumentResources() {
    const resources = [this._displayList, this._layout, this._styles, this._styleRuntime, this._document];
    this._displayList = null;
    this._layout = null;
    this._styles = null;
    this._styleRuntime = null;
    this._document = null;
    disposeResources(resources);
  }

  _rebuildDocument() {
    const state = this._captureState();
    const markup = this._renderMarkup();
    const nextDocument = this.aether.document.parse(markup);
    const previousDocument = this._document;
    const previousRuntime = this._styleRuntime;
    let nextRuntime;
    try {
      nextRuntime = this.aether.styleRuntime.create({
        environment: {
          viewportWidth: this._viewport.width,
          viewportHeight: this._viewport.height,
          dpiScale: this._dpiScale,
        },
        onDiagnostic: this._onDiagnostic,
      });
      if (this.defaultCss) nextRuntime.addStylesheet(this.defaultCss, { layer: 0, sourceName: "aetherui-defaults.css" });
      const authorCss = this._extractAuthorCss(nextDocument);
      if (authorCss) nextRuntime.addStylesheet(authorCss, { layer: 1, sourceName: "component.css" });

      this._document = nextDocument;
      this._styleRuntime = nextRuntime;
      this._restoreState(state);
      this._markupDirty = false;
      this._visualDirty = true;
      previousRuntime?.dispose();
      previousDocument?.dispose();
    } catch (error) {
      if (this._document === nextDocument) this._document = previousDocument;
      if (this._styleRuntime === nextRuntime) this._styleRuntime = previousRuntime;
      nextRuntime?.dispose();
      nextDocument.dispose();
      throw error;
    }
  }

  _rebuildVisual() {
    const root = this._document?.documentElement ?? this._document?.body;
    if (!root || !this._styleRuntime) {
      this._disposeVisualResources();
      return;
    }
    let nextStyles;
    let nextLayout;
    let nextDisplayList;
    try {
      this._styleRuntime.resolveSubtree(root);
      nextStyles = this._styleRuntime.createResolvedStyleMap(this._document);
      nextLayout = this._layoutEngine.layout(
        this._document,
        nextStyles,
        this._viewport.width,
        this._viewport.height,
        (element) => this._scrollOffsets.get(handleKey(element.handle)) ?? 0,
      );
      nextDisplayList = this._displayBuilder.build(nextLayout, this._focused);
    } catch (error) {
      nextDisplayList?.dispose();
      nextLayout?.dispose();
      nextStyles?.dispose();
      throw error;
    }
    const previousStyles = this._styles;
    const previousLayout = this._layout;
    const previousDisplayList = this._displayList;
    this._styles = nextStyles;
    this._layout = nextLayout;
    this._displayList = nextDisplayList;
    previousDisplayList?.dispose();
    previousLayout?.dispose();
    previousStyles?.dispose();
  }

  _ensureVisualTree() {
    this._assertAlive();
    if (this._markupDirty) this._rebuildDocument();
    if (!this._visualDirty && this._displayList) return;
    this._rebuildVisual();
  }

  _dispatch(type, target, point, key = UiKey.NONE, character = "") {
    const element = nearestElement(target);
    if (!element) return new DesktopEvent(type, null, this, { point, key, character });
    const event = new DesktopEvent(type, element, this, { point, key, character });
    const handlers = this._renderedHandlers ?? asHandlers(this.component);
    for (let current = element; current; current = nearestElement(current.parent)) {
      event.currentTarget = current;
      const token = current.getAttribute(`on${type}`);
      const handler = getHandler(handlers, token);
      if (handler) handler(event);
      if (event.propagationStopped) break;
    }
    return event;
  }

  _focus(target) {
    if (target === this._focused) return;
    const previous = this._focused;
    if (previous) {
      previous.state = previous.state & ~ElementState.FOCUS;
      this._dispatch("blur", previous);
    }
    this._focused = target;
    if (target) {
      target.state = target.state | ElementState.FOCUS;
      this._dispatch("focus", target);
    }
    this.invalidate();
  }

  pointerMoved(point) {
    this._assertAlive();
    this._ensureVisualTree();
    const target = nearestElement(this._layout.hitTest(point.x, point.y));
    if (target === this._hovered) return;
    if (this._hovered) {
      this._hovered.state = this._hovered.state & ~ElementState.HOVER;
      this._dispatch("mouseout", this._hovered, point);
    }
    this._hovered = target;
    if (target) {
      target.state = target.state | ElementState.HOVER;
      this._dispatch("mouseover", target, point);
    }
    this.invalidate();
  }

  pointerLeft() {
    this._assertAlive();
    if (!this._hovered) return;
    const previous = this._hovered;
    this._hovered = null;
    previous.state = previous.state & ~ElementState.HOVER;
    this._dispatch("mouseout", previous);
    this.invalidate();
  }

  pointerPressed(point) {
    this._assertAlive();
    this._ensureVisualTree();
    const target = nearestElement(this._layout.hitTest(point.x, point.y));
    if (!target || target.disabled) {
      this._focus(null);
      return;
    }
    if (this._active) this._active.state = this._active.state & ~ElementState.ACTIVE;
    this._active = target;
    target.state = target.state | ElementState.ACTIVE;
    this._focus(target.focusable ? target : null);
    this._dispatch("mousedown", target, point);
    this.invalidate();
  }

  pointerReleased(point) {
    this._assertAlive();
    this._ensureVisualTree();
    const target = nearestElement(this._layout.hitTest(point.x, point.y));
    const pressed = this._active;
    if (pressed) {
      pressed.state = pressed.state & ~ElementState.ACTIVE;
      this._dispatch("mouseup", pressed, point);
    }
    this._active = null;
    if (pressed && pressed === target && !pressed.disabled) {
      const click = this._dispatch("click", pressed, point);
      if (!click.defaultPrevented && pressed.tagName?.toLowerCase() === "button") this._submitClosestForm(pressed);
    }
    this.invalidate();
  }

  textInput(character) {
    this._assertAlive();
    if (!this._focused || this._focused.tagName?.toLowerCase() !== "input" || this._focused.disabled || !character || /[\u0000-\u001f]/u.test(character)) return;
    const input = this._focused;
    const current = input.getAttribute("value") ?? "";
    const maxLength = parsePositiveInt(input.getAttribute("maxlength"));
    if (maxLength != null && current.length >= maxLength) return;
    input.setAttribute("value", current + character);
    const event = this._dispatch("input", input, undefined, UiKey.NONE, character);
    if (event.defaultPrevented) input.setAttribute("value", current);
    this.invalidate();
  }

  keyPressed(key) {
    this._assertAlive();
    if (!this._focused) return;
    const normalized = typeof key === "string" ? key : UiKey.NONE;
    const event = this._dispatch("keydown", this._focused, undefined, normalized);
    if (event.defaultPrevented) return;
    if (normalized === UiKey.TAB) {
      this._focusNext();
      return;
    }
    if (normalized === UiKey.ENTER || normalized === UiKey.SPACE) {
      if (this._focused.tagName?.toLowerCase() === "button") {
        const click = this._dispatch("click", this._focused, undefined, normalized);
        if (!click.defaultPrevented) this._submitClosestForm(this._focused);
      } else if (normalized === UiKey.ENTER) {
        this._submitClosestForm(this._focused);
      }
      this.invalidate();
      return;
    }
    if (this._focused.tagName?.toLowerCase() !== "input" || this._focused.disabled) return;
    const input = this._focused;
    const current = input.getAttribute("value") ?? "";
    if ((normalized === UiKey.BACKSPACE || normalized === UiKey.DELETE) && current.length > 0) {
      input.setAttribute("value", current.slice(0, -1));
      this._dispatch("input", input, undefined, normalized);
      this.invalidate();
    }
  }

  _focusNext() {
    this._ensureVisualTree();
    const focusable = childElements(this._document.documentElement).filter((element) => element.focusable && !element.disabled);
    if (focusable.length === 0) return this._focus(null);
    const index = focusable.indexOf(this._focused);
    this._focus(focusable[(index + 1 + focusable.length) % focusable.length]);
  }

  _submitClosestForm(source) {
    const form = findAncestor(source, "form");
    if (!form || !this._validateForm(form)) return;
    this._dispatch("submit", form);
  }

  _validateForm(form) {
    let valid = true;
    const inputs = findElementsByTag(form, "input");
    for (const input of inputs) {
      if (!input.required) continue;
      const empty = !(input.getAttribute("value") ?? "").trim();
      input.state = empty ? input.state | ElementState.INVALID : input.state & ~ElementState.INVALID;
      if (empty) {
        valid = false;
        this._dispatch("invalid", input);
      }
    }
    if (!valid) this.invalidate();
    return valid;
  }

  mouseWheelScrolled(point, notches) {
    this._assertAlive();
    this._ensureVisualTree();
    const pixelsPerNotch = 48;
    for (let current = nearestElement(this._layout.hitTest(point.x, point.y)); current; current = nearestElement(current.parent)) {
      const box = this._layout.findPrimaryBox(current);
      if (!box || box.scrollableOverflowY <= 0) continue;
      const next = Math.max(0, Math.min(box.scrollableOverflowY, box.scrollOffsetY - notches * pixelsPerNotch));
      if (next !== box.scrollOffsetY) {
        this._scrollOffsets.set(handleKey(current.handle), next);
        this.invalidate();
      }
      return;
    }
  }

  cursorAt(point) {
    this._assertAlive();
    this._ensureVisualTree();
    const target = nearestElement(this._layout.hitTest(point.x, point.y));
    if (!target || !this._styles) return 0;
    return Number(this._styles.get(target)?.cursor ?? 0);
  }

  dispose() {
    if (this._disposed) return;
    this._disposed = true;
    this._unsubscribeInvalidated?.();
    this._unsubscribeInvalidated = null;
    const resources = [this._displayBuilder, this._layoutEngine, this.component, this.renderer];
    this._displayBuilder = null;
    this._layoutEngine = null;
    let firstError = null;
    try {
      this._disposeDocumentResources();
    } catch (error) {
      firstError = error;
    }
    try {
      disposeResources(resources);
    } catch (error) {
      firstError ??= error;
    }
    if (firstError) throw firstError;
  }

  [Symbol.dispose]() {
    this.dispose();
  }
}

export function createDesktopComponent(options) {
  return new DesktopComponent(options);
}

export function isDesktopPaintCommand(command) {
  return command && Object.values(PaintCommandTag).includes(command.tag);
}
