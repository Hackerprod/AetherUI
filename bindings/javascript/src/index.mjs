import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  CAPABILITIES,
  DEFAULT_CAPABILITIES,
  SUPPORTED_ABI,
  checkAbiCompatibility,
  createLoadedNativeApi,
  validateComputedStyleSize,
} from "./ffi.mjs";

const packageRoot = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const disposeSymbol = Symbol.dispose ?? Symbol.for("Symbol.dispose");
const finalizer = new FinalizationRegistry(({ handle, destroy, cleanup }) => {
  try {
    if (!isNullHandle(handle)) destroy(handle);
  } finally {
    cleanup?.();
  }
});

export { CAPABILITIES, DEFAULT_CAPABILITIES, SUPPORTED_ABI };

export const NodeType = Object.freeze({
  DOCUMENT: 0,
  ELEMENT: 1,
  TEXT: 2,
  COMMENT: 3,
});

export const ElementState = Object.freeze({
  NONE: 0,
  HOVER: 1 << 0,
  ACTIVE: 1 << 1,
  FOCUS: 1 << 2,
  INVALID: 1 << 3,
});

export const PaintCommandTag = Object.freeze({
  FILL_ROUNDED_RECT: 0,
  DRAW_BOX_SHADOW: 1,
  FILL_ROUNDED_RECT_GRADIENT: 2,
  STROKE_ROUNDED_RECT: 3,
  DRAW_TEXT: 4,
  DRAW_TEXT_GRADIENT: 5,
  PUSH_CLIP: 6,
  POP_CLIP: 7,
  FILL_BORDER_SIDE: 8,
  DRAW_TEXT_SHADOW: 9,
  DRAW_BACKGROUND_IMAGE: 10,
  APPLY_BACKDROP_FILTER: 11,
  PUSH_TRANSFORM: 12,
  POP_TRANSFORM: 13,
});

export class NativeStatusError extends Error {
  constructor(operation, status, message) {
    super(`${operation} failed with native status ${status}${message ? ` (${message})` : ""}.`);
    this.name = "NativeStatusError";
    this.operation = operation;
    this.status = status;
  }
}

function isNullHandle(value) {
  return value == null || value === 0 || value === 0n;
}

function assertStatus(api, status, operation) {
  if (Number(status) === 0) return;
  let message;
  try {
    message = api.functions.statusMessage(Number(status));
  } catch {
    message = undefined;
  }
  throw new NativeStatusError(operation, Number(status), message);
}

function assertInteger(value, name) {
  if (!Number.isInteger(value) || value < 0) throw new RangeError(`${name} must be a non-negative integer.`);
}

function readOwnedString(api, pointer) {
  if (isNullHandle(pointer)) return null;
  try {
    return api.ffi.decode(pointer, "const char *");
  } finally {
    api.functions.stringFree(pointer);
  }
}

function readBorrowedString(api, pointer) {
  if (isNullHandle(pointer)) return null;
  return api.ffi.decode(pointer, "const char *");
}

function decodeStruct(api, pointer, type) {
  if (pointer && typeof pointer === "object" && typeof pointer !== "bigint") return pointer;
  if (isNullHandle(pointer)) return null;
  return api.ffi.decode(pointer, type);
}

function cloneColor(color) {
  return color ? { r: Number(color.r), g: Number(color.g), b: Number(color.b), a: Number(color.a) } : null;
}

function cloneLength(length) {
  return length
    ? {
        value: Number(length.value),
        unit: Number(length.unit),
        linearPercent: Number(length.linear_percent),
        linearEm: Number(length.linear_em),
        linearRem: Number(length.linear_rem),
        linearVw: Number(length.linear_vw),
        linearVh: Number(length.linear_vh),
      }
    : null;
}

function cloneEdges(edges) {
  return edges
    ? {
        left: cloneLength(edges.left),
        top: cloneLength(edges.top),
        right: cloneLength(edges.right),
        bottom: cloneLength(edges.bottom),
      }
    : null;
}

function cloneColorEdges(edges) {
  return edges
    ? {
        left: cloneColor(edges.left),
        top: cloneColor(edges.top),
        right: cloneColor(edges.right),
        bottom: cloneColor(edges.bottom),
      }
    : null;
}

function cloneGradient(api, gradient) {
  if (!gradient) return null;
  const count = Math.max(0, Number(gradient.stop_count ?? 0));
  const stops = isNullHandle(gradient.stops)
    ? []
    : api.ffi.decode(gradient.stops, api.types.GradientStop, count).map((stop) => ({
        color: cloneColor(stop.color),
        offset: Number(stop.offset),
      }));
  return {
    directionX: Number(gradient.direction_x),
    directionY: Number(gradient.direction_y),
    stops,
  };
}

function cloneShadow(shadow) {
  return shadow
    ? {
        offsetX: Number(shadow.offset_x),
        offsetY: Number(shadow.offset_y),
        blurRadius: Number(shadow.blur_radius),
        spreadRadius: Number(shadow.spread_radius),
        color: cloneColor(shadow.color),
        inset: Boolean(shadow.inset),
      }
    : null;
}

function cloneShadowList(api, shadows) {
  const count = Math.max(0, Number(shadows?.count ?? 0));
  return count === 0 || isNullHandle(shadows.items)
    ? []
    : api.ffi.decode(shadows.items, api.types.Shadow, count).map(cloneShadow);
}

function cloneBackgroundLayers(api, layers) {
  const count = Math.max(0, Number(layers?.count ?? 0));
  if (count === 0 || isNullHandle(layers.items)) return [];
  return api.ffi.decode(layers.items, api.types.BackgroundLayer, count).map((layer) => ({
    kind: Number(layer.kind),
    gradient: Number(layer.kind) === 0 ? cloneGradient(api, layer.gradient) : null,
    imageSource: layer.image_source ?? null,
    positionX: cloneLength(layer.position_x),
    positionY: cloneLength(layer.position_y),
    sizeKind: Number(layer.size_kind),
    sizeX: cloneLength(layer.size_x),
    sizeY: cloneLength(layer.size_y),
    repeatX: Number(layer.repeat_x),
    repeatY: Number(layer.repeat_y),
  }));
}

function cloneBackdropFilters(api, filters) {
  const count = Math.max(0, Number(filters?.count ?? 0));
  if (count === 0 || isNullHandle(filters.items)) return [];
  return api.ffi.decode(filters.items, api.types.BackdropFilter, count).map((filter) => ({
    kind: Number(filter.kind),
    lengthAmount: cloneLength(filter.length_amount),
    scalarAmount: Number(filter.scalar_amount),
  }));
}

function cloneTransforms(api, transforms) {
  const count = Math.max(0, Number(transforms?.count ?? 0));
  if (count === 0 || isNullHandle(transforms.items)) return [];
  return api.ffi.decode(transforms.items, api.types.TransformOperation, count).map((operation) => ({
    kind: Number(operation.kind),
    x: cloneLength(operation.x),
    y: cloneLength(operation.y),
    scalarX: Number(operation.scalar_x),
    scalarY: Number(operation.scalar_y),
    angleRadians: Number(operation.angle_radians),
    matrixA: Number(operation.matrix_a), matrixB: Number(operation.matrix_b),
    matrixC: Number(operation.matrix_c), matrixD: Number(operation.matrix_d),
    matrixE: Number(operation.matrix_e), matrixF: Number(operation.matrix_f),
  }));
}

function cloneComputedStyle(api, style) {
  if (!style) return null;
  const backgroundLayers = cloneBackgroundLayers(api, style.background_layers);
  const backgroundGradient = backgroundLayers.find((layer) => layer.kind === 0)?.gradient ?? null;
  const boxShadows = cloneShadowList(api, style.box_shadows);
  return {
    display: Number(style.display),
    flexDirection: Number(style.flex_direction),
    justifyContent: Number(style.justify_content),
    alignItems: Number(style.align_items),
    alignSelf: Number(style.align_self),
    flexGrow: Number(style.flex_grow),
    flexShrink: Number(style.flex_shrink),
    flexBasis: {
      kind: Number(style.flex_basis.kind),
      length: cloneLength(style.flex_basis.length),
    },
    flexWrap: Boolean(style.flex_wrap),
    gap: Number(style.gap),
    position: Number(style.position),
    inset: cloneEdges(style.inset),
    width: cloneLength(style.width),
    height: cloneLength(style.height),
    minWidth: cloneLength(style.min_width),
    minHeight: cloneLength(style.min_height),
    maxWidth: cloneLength(style.max_width),
    maxHeight: cloneLength(style.max_height),
    margin: cloneEdges(style.margin),
    padding: cloneEdges(style.padding),
    borderWidth: cloneEdges(style.border_width),
    boxSizing: Number(style.box_sizing),
    color: cloneColor(style.color),
    backgroundColor: cloneColor(style.background_color),
    backgroundLayers,
    backdropFilters: cloneBackdropFilters(api, style.backdrop_filters),
    // Retain the representative fields for existing JavaScript callers while
    // exposing the full declaration-order layer list above.
    hasBackgroundGradient: backgroundGradient !== null,
    backgroundGradient,
    borderColor: cloneColor(style.border_color),
    borderColors: cloneColorEdges(style.border_colors),
    borderStyles: {
      left: Number(style.border_styles.left), top: Number(style.border_styles.top),
      right: Number(style.border_styles.right), bottom: Number(style.border_styles.bottom),
    },
    hasTextGradient: Boolean(style.has_text_gradient),
    textGradient: cloneGradient(api, style.text_gradient),
    transforms: cloneTransforms(api, style.transforms),
    transformOrigin: { x: cloneLength(style.transform_origin.x), y: cloneLength(style.transform_origin.y) },
    textShadows: cloneShadowList(api, style.text_shadows),
    boxShadows,
    hasBoxShadow: boxShadows.length > 0,
    boxShadow: boxShadows[0] ?? null,
    borderRadii: {
      topLeft: { horizontal: cloneLength(style.border_radii.top_left.horizontal), vertical: cloneLength(style.border_radii.top_left.vertical) },
      topRight: { horizontal: cloneLength(style.border_radii.top_right.horizontal), vertical: cloneLength(style.border_radii.top_right.vertical) },
      bottomRight: { horizontal: cloneLength(style.border_radii.bottom_right.horizontal), vertical: cloneLength(style.border_radii.bottom_right.vertical) },
      bottomLeft: { horizontal: cloneLength(style.border_radii.bottom_left.horizontal), vertical: cloneLength(style.border_radii.bottom_left.vertical) },
    },
    opacity: Number(style.opacity),
    fontFamily: normalizeCharArray(style.font_family),
    fontSize: Number(style.font_size),
    fontWeight: Number(style.font_weight),
    lineHeight: Number(style.line_height),
    textAlign: Number(style.text_align),
    textTransform: Number(style.text_transform),
    overflow: Number(style.overflow),
    cursor: Number(style.cursor),
    visible: Boolean(style.visible),
  };
}

function normalizeCharArray(value) {
  if (typeof value === "string") return value.replace(/\0.*$/s, "");
  if (!value) return "";
  return Array.from(value, (item) => (typeof item === "number" ? String.fromCharCode(item) : item))
    .join("")
    .replace(/\0.*$/s, "");
}

function readComputedStyle(api, handle) {
  if (isNullHandle(handle)) return null;
  const nativeStyle = {};
  try {
    assertStatus(api, api.functions.computedStyleCopy(handle, nativeStyle), "aether_computed_style_copy");
    return cloneComputedStyle(api, nativeStyle);
  } finally {
    api.functions.computedStyleFree(nativeStyle);
  }
}

class OwnedHandle {
  constructor(api, handle, label, destroy, cleanup) {
    if (isNullHandle(handle)) throw new Error(`${label} returned a null native handle.`);
    this._api = api;
    this._handle = handle;
    this._label = label;
    this._destroy = destroy;
    this._disposed = false;
    this._finalizerToken = {};
    this._cleanup = cleanup;
    finalizer.register(this, { handle, destroy, cleanup }, this._finalizerToken);
  }

  _assertAlive() {
    if (this._disposed || isNullHandle(this._handle)) throw new Error(`${this._label} has been disposed.`);
    return this._handle;
  }

  get disposed() {
    return this._disposed;
  }

  dispose() {
    if (this._disposed) return;
    const handle = this._handle;
    this._disposed = true;
    this._handle = null;
    finalizer.unregister(this._finalizerToken);
    try {
      if (!isNullHandle(handle)) this._destroy(handle);
    } finally {
      this._cleanup?.();
      this._cleanup = undefined;
    }
  }

  [disposeSymbol]() {
    this.dispose();
  }
}

export class NodeView {
  constructor(document, handle, generation, element = false) {
    this._document = document;
    this._handle = handle;
    this._generation = generation;
    this._element = element;
  }

  _assertAlive() {
    this._document._assertAlive();
    if (this._generation !== this._document._generation) {
      throw new Error("This borrowed node view is invalid because its document was reparsed.");
    }
    if (isNullHandle(this._handle)) throw new Error("This borrowed node view is null.");
    return this._handle;
  }

  get handle() {
    return this._assertAlive();
  }

  get document() {
    return this._document;
  }

  get type() {
    return Number(this._document._api.functions.nodeGetType(this._assertAlive()));
  }

  get parent() {
    const parent = this._document._api.functions.nodeGetParent(this._assertAlive());
    return this._document._wrapNode(parent);
  }

  get childCount() {
    return Number(this._document._api.functions.nodeGetChildCount(this._assertAlive()));
  }

  child(index) {
    assertInteger(index, "index");
    if (index >= this.childCount) return null;
    return this._document._wrapNode(this._document._api.functions.nodeGetChild(this._assertAlive(), index));
  }

  get textContent() {
    return readOwnedString(this._document._api, this._document._api.functions.nodeGetTextContent(this._assertAlive()));
  }

  get isElement() {
    return this.type === NodeType.ELEMENT;
  }

  _assertElement() {
    const handle = this._assertAlive();
    if (!this._element && this.type !== NodeType.ELEMENT) throw new TypeError("This node is not an element.");
    return handle;
  }

  get tagName() {
    return readOwnedString(this._document._api, this._document._api.functions.elementGetTagName(this._assertElement()));
  }

  getAttribute(name) {
    if (typeof name !== "string" || name.length === 0) throw new TypeError("name must be a non-empty string.");
    return readOwnedString(this._document._api, this._document._api.functions.elementGetAttribute(this._assertElement(), name));
  }

  get attributes() {
    const api = this._document._api;
    const handle = this._assertElement();
    const count = Number(api.functions.elementGetAttributeCount(handle));
    const attributes = [];
    for (let index = 0; index < count; index++) {
      const name = readOwnedString(api, api.functions.elementGetAttributeName(handle, index));
      const value = readOwnedString(api, api.functions.elementGetAttributeValue(handle, index));
      if (name != null) attributes.push({ name, value: value ?? "" });
    }
    return attributes;
  }

  setAttribute(name, value) {
    if (typeof name !== "string" || name.length === 0) throw new TypeError("name must be a non-empty string.");
    if (typeof value !== "string") throw new TypeError("value must be a string.");
    const api = this._document._api;
    assertStatus(api, api.functions.elementSetAttribute(this._assertElement(), name, value), "aether_element_set_attribute");
  }

  hasAttribute(name) {
    if (typeof name !== "string" || name.length === 0) throw new TypeError("name must be a non-empty string.");
    return Boolean(this._document._api.functions.elementHasAttribute(this._assertElement(), name));
  }

  hasClass(className) {
    if (typeof className !== "string" || className.length === 0) throw new TypeError("className must be a non-empty string.");
    return Boolean(this._document._api.functions.elementHasClass(this._assertElement(), className));
  }

  get required() {
    return Boolean(this._document._api.functions.elementIsRequired(this._assertElement()));
  }

  get disabled() {
    return Boolean(this._document._api.functions.elementIsDisabled(this._assertElement()));
  }

  get focusable() {
    return Boolean(this._document._api.functions.elementIsFocusable(this._assertElement()));
  }

  get state() {
    return Number(this._document._api.functions.elementGetState(this._assertElement()));
  }

  set state(value) {
    if (!Number.isInteger(value)) throw new TypeError("state must be an integer bit mask.");
    this._document._api.functions.elementSetState(this._assertElement(), value);
  }

  appendChild(child) {
    if (!(child instanceof NodeView) || child.document !== this._document) throw new TypeError("child must belong to the same document.");
    assertStatus(this._document._api, this._document._api.functions.nodeAppendChild(this._assertAlive(), child._assertAlive()), "aether_node_append_child");
  }

  removeChild(child) {
    if (!(child instanceof NodeView) || child.document !== this._document) throw new TypeError("child must belong to the same document.");
    assertStatus(this._document._api, this._document._api.functions.nodeRemoveChild(this._assertAlive(), child._assertAlive()), "aether_node_remove_child");
  }
}

export class ElementView extends NodeView {}

export class Document extends OwnedHandle {
  static _parse(api, html) {
    if (typeof html !== "string") throw new TypeError("html must be a string.");
    const out = [null];
    assertStatus(api, api.functions.docCreate(out), "aether_doc_create");
    try {
      const document = new Document(api, out[0]);
      try {
        document._parse(html);
        return document;
      } catch (error) {
        document.dispose();
        throw error;
      }
    } catch (error) {
      if (!(error instanceof NativeStatusError) && !isNullHandle(out[0])) api.functions.docDestroy(out[0]);
      throw error;
    }
  }

  constructor(api, handle) {
    super(api, handle, "Document", api.functions.docDestroy);
    this._generation = 0;
  }

  _assertAlive() {
    return super._assertAlive();
  }

  _parse(html) {
    assertStatus(this._api, this._api.functions.docParseHtml(this._assertAlive(), html), "aether_doc_parse_html");
    this._generation++;
  }

  parse(html) {
    this._parse(html);
  }

  get version() {
    return BigInt(this._api.functions.docGetVersion(this._assertAlive()));
  }

  _wrapNode(handle) {
    if (isNullHandle(handle)) return null;
    const type = Number(this._api.functions.nodeGetType(handle));
    return type === NodeType.ELEMENT
      ? new ElementView(this, handle, this._generation, true)
      : new NodeView(this, handle, this._generation, false);
  }

  get body() {
    return this._wrapNode(this._api.functions.docGetBody(this._assertAlive()));
  }

  get documentElement() {
    return this._wrapNode(this._api.functions.docGetDocumentElement(this._assertAlive()));
  }

  getElementById(id) {
    if (typeof id !== "string" || id.length === 0) throw new TypeError("id must be a non-empty string.");
    return this._wrapNode(this._api.functions.docGetElementById(this._assertAlive(), id));
  }
}

export class StyleMap extends OwnedHandle {
  constructor(api, handle, runtime, document) {
    super(api, handle, "StyleMap", api.functions.styleMapDestroy);
    this._runtime = runtime;
    this._document = document;
  }

  dispose() {
    if (this.disposed) return;
    super.dispose();
    this._runtime?._maps.delete(this);
  }

  _assertAlive() {
    super._assertAlive();
    this._runtime?._assertAlive();
    this._document?._assertAlive();
    return this._handle;
  }

  get(element) {
    if (!(element instanceof NodeView) || element.document !== this._document) throw new TypeError("element must belong to the map's document.");
    const handle = this._api.functions.styleMapGet(this._assertAlive(), element.handle);
    if (isNullHandle(handle)) return null;
    try {
      return readComputedStyle(this._api, handle);
    } finally {
      this._api.functions.computedStyleHandleDestroy(handle);
    }
  }
}

function normalizeStyleOptions(options = {}) {
  return {
    throw_on_invalid: options.throwOnInvalid ? 1 : 0,
    enforce_thread_affinity: options.enforceThreadAffinity === false ? 0 : 1,
    max_variable_depth: options.maxVariableDepth ?? 64,
    max_variable_expansion_length: options.maxVariableExpansionLength ?? 65536,
  };
}

function normalizeEnvironment(environment = {}) {
  return {
    viewport_width: environment.viewportWidth ?? 0,
    viewport_height: environment.viewportHeight ?? 0,
    dpi_scale: environment.dpiScale ?? 1,
    root_font_size: environment.rootFontSize ?? 16,
    color_scheme: environment.colorScheme ?? 0,
    pointer_accuracy: environment.pointerAccuracy ?? 0,
    motion_preference: environment.motionPreference ?? 0,
  };
}

function copyUpdates(runtime, root, call) {
  const api = runtime._api;
  runtime._assertAlive();
  if (!(root instanceof NodeView)) throw new TypeError("root must be a NodeView.");
  const updatesOut = [null];
  const count = Number(call(updatesOut));
  if (count < 0) throw new NativeStatusError("style runtime update", count, api.functions.statusMessage(count));
  const updatesPointer = updatesOut[0];
  const updates = [];
  try {
    for (let index = 0; index < count; index++) {
      const updateHandle = api.ffi.decode(updatesPointer, api.types.StyleUpdate, count)[index];
      const nodeOut = [null];
      const currentOut = [null];
      const impactOut = [0];
      assertStatus(
        api,
        api.functions.styleRuntimeGetUpdate(updateHandle, nodeOut, currentOut, impactOut),
        "aether_style_runtime_get_update",
      );
      let style = null;
      try {
        style = readComputedStyle(api, currentOut[0]);
      } finally {
        if (!isNullHandle(currentOut[0])) api.functions.computedStyleHandleDestroy(currentOut[0]);
      }
      updates.push({
        node: root.document._wrapNode(nodeOut[0]),
        style,
        impact: Number(impactOut[0]),
      });
    }
    return updates;
  } finally {
    if (!isNullHandle(updatesPointer)) api.functions.styleUpdatesFree(updatesPointer, count);
  }
}

export class StyleRuntime extends OwnedHandle {
  static _create(api, options = {}) {
    const nativeOptions = normalizeStyleOptions(options);
    const nativeEnvironment = normalizeEnvironment(options.environment);
    let diagnosticRegistration;
    const diagnostic = typeof options.onDiagnostic === "function"
      ? (code, property, value, message) => {
          try {
            options.onDiagnostic({ code, property, value, message });
          } catch {
            // Never let a JS diagnostic exception cross the C callback boundary.
          }
        }
      : null;
    if (diagnostic) diagnosticRegistration = api.ffi.register(diagnostic, api.types.StyleDiagnosticPointer);
    const out = [null];
    try {
      assertStatus(
        api,
        api.functions.styleRuntimeCreate(nativeOptions, nativeEnvironment, diagnosticRegistration ?? null, null, out),
        "aether_style_runtime_create",
      );
      const runtime = new StyleRuntime(api, out[0], nativeEnvironment, diagnosticRegistration);
      return runtime;
    } catch (error) {
      if (diagnosticRegistration) api.ffi.unregister(diagnosticRegistration);
      if (!isNullHandle(out[0])) api.functions.styleRuntimeDestroy(out[0]);
      throw error;
    }
  }

  constructor(api, handle, environment, diagnosticRegistration) {
    super(api, handle, "StyleRuntime", api.functions.styleRuntimeDestroy, () => {
      if (diagnosticRegistration) api.ffi.unregister(diagnosticRegistration);
    });
    this.environment = Object.freeze({ ...environment });
    this._maps = new Set();
  }

  dispose() {
    for (const map of [...this._maps]) map.dispose();
    super.dispose();
  }

  addStylesheet(source, { layer = 0, sourceName = "inline.css" } = {}) {
    if (typeof source !== "string") throw new TypeError("source must be a string.");
    const sheetOut = [0n];
    const ruleCountOut = [0];
    assertStatus(
      this._api,
      this._api.functions.styleRuntimeAddStylesheet(this._assertAlive(), source, layer, sourceName, sheetOut, ruleCountOut),
      "aether_style_runtime_add_stylesheet",
    );
    return Object.freeze({ handle: BigInt(sheetOut[0]), ruleCount: Number(ruleCountOut[0]) });
  }

  removeStylesheet(handle) {
    const removed = this._api.functions.styleRuntimeRemoveStylesheet(this._assertAlive(), handle);
    return Boolean(removed);
  }

  resolveSubtree(root) {
    if (!(root instanceof NodeView)) throw new TypeError("root must be a NodeView.");
    return copyUpdates(this, root, (out) => this._api.functions.styleRuntimeResolveSubtree(this._assertAlive(), root.handle, out));
  }

  setEnvironment(environment, root) {
    if (!(root instanceof NodeView)) throw new TypeError("root must be a NodeView.");
    const nativeEnvironment = normalizeEnvironment(environment);
    this.environment = Object.freeze({ ...nativeEnvironment });
    return copyUpdates(this, root, (out) => this._api.functions.styleRuntimeSetEnvironment(this._assertAlive(), nativeEnvironment, root.handle, out));
  }

  createResolvedStyleMap(document) {
    if (!(document instanceof Document)) throw new TypeError("document must be a Document.");
    const out = [null];
    assertStatus(
      this._api,
      this._api.functions.styleRuntimeCreateResolvedStyleMap(this._assertAlive(), document._assertAlive(), out),
      "aether_style_runtime_create_resolved_style_map",
    );
    const map = new StyleMap(this._api, out[0], this, document);
    this._maps.add(map);
    return map;
  }

  clearCache() {
    this._api.functions.styleRuntimeClearCache(this._assertAlive());
  }
}

export class LayoutBox {
  constructor(result, handle) {
    this._result = result;
    this._handle = handle;
  }

  _assertAlive() {
    this._result._assertAlive();
    if (isNullHandle(this._handle)) throw new Error("This borrowed layout box is null.");
    return this._handle;
  }

  get handle() { return this._assertAlive(); }
  get kind() { return Number(this._result._api.functions.layoutBoxGetKind(this._assertAlive())); }
  get x() { return Number(this._result._api.functions.layoutBoxGetX(this._assertAlive())); }
  get y() { return Number(this._result._api.functions.layoutBoxGetY(this._assertAlive())); }
  get width() { return Number(this._result._api.functions.layoutBoxGetWidth(this._assertAlive())); }
  get height() { return Number(this._result._api.functions.layoutBoxGetHeight(this._assertAlive())); }
  get marginBox() { return { ...this._result._api.functions.layoutBoxGetMarginBox(this._assertAlive()) }; }
  get borderBox() { return { ...this._result._api.functions.layoutBoxGetBorderBox(this._assertAlive()) }; }
  get paddingBox() { return { ...this._result._api.functions.layoutBoxGetPaddingBox(this._assertAlive()) }; }
  get contentBox() { return { ...this._result._api.functions.layoutBoxGetContentBox(this._assertAlive()) }; }
  get isAtomic() { return Boolean(this._result._api.functions.layoutBoxIsAtomic(this._assertAlive())); }
  get text() { return readOwnedString(this._result._api, this._result._api.functions.layoutBoxGetText(this._assertAlive())); }
  get scrollableOverflowY() { return Number(this._result._api.functions.layoutBoxGetScrollableOverflowY(this._assertAlive())); }
  get scrollOffsetY() { return Number(this._result._api.functions.layoutBoxGetScrollOffsetY(this._assertAlive())); }
  get element() { return this._result.document._wrapNode(this._result._api.functions.layoutBoxGetElement(this._assertAlive())); }
  get childCount() { return Number(this._result._api.functions.layoutBoxGetChildCount(this._assertAlive())); }
  child(index) {
    assertInteger(index, "index");
    if (index >= this.childCount) return null;
    return new LayoutBox(this._result, this._result._api.functions.layoutBoxGetChild(this._assertAlive(), index));
  }
}

export class LayoutResult extends OwnedHandle {
  constructor(api, handle, document, engine) {
    super(api, handle, "LayoutResult", api.functions.layoutResultDestroy);
    this.document = document;
    this._engine = engine;
  }

  dispose() {
    if (this.disposed) return;
    super.dispose();
    this._engine?._results.delete(this);
  }

  get root() {
    const box = this._api.functions.layoutResultGetRoot(this._assertAlive());
    return isNullHandle(box) ? null : new LayoutBox(this, box);
  }

  findPrimaryBox(element) {
    if (!(element instanceof NodeView) || element.document !== this.document) throw new TypeError("element must belong to the layout document.");
    const box = this._api.functions.layoutResultFindPrimaryBox(this._assertAlive(), element.handle);
    return isNullHandle(box) ? null : new LayoutBox(this, box);
  }

  hitTest(x, y) {
    const node = this._api.functions.layoutResultHitTest(this._assertAlive(), x, y);
    return this.document._wrapNode(node);
  }

  get paintOrderCount() {
    return Number(this._api.functions.layoutResultGetPaintOrderCount(this._assertAlive()));
  }

  paintOrderBox(index) {
    assertInteger(index, "index");
    if (index >= this.paintOrderCount) return null;
    const box = this._api.functions.layoutResultGetPaintOrderBox(this._assertAlive(), index);
    return isNullHandle(box) ? null : new LayoutBox(this, box);
  }
}

function normalizeMetrics(value) {
  return {
    width: Number(value?.width ?? 0),
    height: Number(value?.height ?? 0),
    baseline: Number(value?.baseline ?? 0),
  };
}

function makeMeasureCallback(api, callback) {
  return (text, style, maxWidth) => {
    try {
      const decodedStyle = decodeStruct(api, style, api.types.ComputedStyle);
      return normalizeMetrics(callback(String(text ?? ""), cloneComputedStyle(api, decodedStyle), Number(maxWidth)));
    } catch {
      return { width: 0, height: 0, baseline: 0 };
    }
  };
}

export class LayoutEngine extends OwnedHandle {
  static _create(api, measure) {
    if (typeof measure !== "function") throw new TypeError("measure must be a function.");
    const callback = makeMeasureCallback(api, measure);
    const registration = api.ffi.register(callback, api.types.TextMeasurerPointer);
    const out = [null];
    try {
      assertStatus(api, api.functions.layoutCreate(registration, null, out), "aether_layout_create");
      return new LayoutEngine(api, out[0], registration);
    } catch (error) {
      api.ffi.unregister(registration);
      if (!isNullHandle(out[0])) api.functions.layoutDestroy(out[0]);
      throw error;
    }
  }

  constructor(api, handle, measureRegistration) {
    super(api, handle, "LayoutEngine", api.functions.layoutDestroy, () => api.ffi.unregister(measureRegistration));
    this._results = new Set();
  }

  dispose() {
    for (const result of [...this._results]) result.dispose();
    super.dispose();
  }

  layout(document, styles, width, height, scrollOffset = () => 0) {
    if (!(document instanceof Document)) throw new TypeError("document must be a Document.");
    if (!(styles instanceof StyleMap)) throw new TypeError("styles must be a StyleMap.");
    if (styles._document !== document) throw new TypeError("styles must belong to document.");
    if (typeof scrollOffset !== "function") throw new TypeError("scrollOffset must be a function.");
    const scrollCallback = (element) => {
      try {
        return Number(scrollOffset(document._wrapNode(element)) ?? 0);
      } catch {
        return 0;
      }
    };
    const out = [null];
    assertStatus(
      this._api,
      this._api.functions.layoutLayout(this._assertAlive(), document._assertAlive(), styles._assertAlive(), width, height, scrollCallback, null, out),
      "aether_layout_layout",
    );
    const result = new LayoutResult(this._api, out[0], document, this);
    this._results.add(result);
    return result;
  }
}

function copyPaintGradient(api, gradient) {
  if (!gradient) return null;
  const count = Math.max(0, Number(gradient.stop_count ?? 0));
  return {
    directionX: Number(gradient.direction_x),
    directionY: Number(gradient.direction_y),
    stops: isNullHandle(gradient.stops)
      ? []
      : api.ffi.decode(gradient.stops, api.types.GradientStop, count).map((stop) => ({
          color: cloneColor(stop.color),
          offset: Number(stop.offset),
        })),
  };
}

function decodePaintCommand(api, buffer) {
  const tag = Number(api.ffi.decode(buffer, "int32_t"));
  const command = { tag, raw: new Uint8Array(buffer) };
  const readData = (type) => api.ffi.decode(buffer, api.types.PaintDataOffset, type);
  switch (tag) {
    case PaintCommandTag.FILL_ROUNDED_RECT:
      { const data = readData(api.types.FillRoundedRect); return { ...command, kind: "fillRoundedRect", rect: { ...data.rect }, radii: { ...data.radii }, color: cloneColor(data.color) }; }
    case PaintCommandTag.DRAW_BOX_SHADOW:
      { const data = readData(api.types.DrawBoxShadow); return { ...command, kind: "drawBoxShadow", rect: { ...data.rect }, radii: { ...data.radii }, shadow: { ...data.shadow, color: cloneColor(data.shadow.color), inset: Boolean(data.shadow.inset) } }; }
    case PaintCommandTag.FILL_ROUNDED_RECT_GRADIENT:
      { const data = readData(api.types.FillRoundedRectGradient); return { ...command, kind: "fillRoundedRectGradient", rect: { ...data.rect }, radii: { ...data.radii }, gradient: copyPaintGradient(api, data.gradient), opacity: Number(data.opacity) }; }
    case PaintCommandTag.STROKE_ROUNDED_RECT:
      { const data = readData(api.types.StrokeRoundedRect); return { ...command, kind: "strokeRoundedRect", rect: { ...data.rect }, radii: { ...data.radii }, strokeWidth: Number(data.stroke_width), color: cloneColor(data.color) }; }
    case PaintCommandTag.DRAW_TEXT:
      { const data = readData(api.types.DrawText); return { ...command, kind: "drawText", text: readBorrowedString(api, data.text), rect: { ...data.rect }, fontFamily: normalizeCharArray(data.font_family), fontSize: Number(data.font_size), fontWeight: Number(data.font_weight), textAlign: Number(data.text_align), color: cloneColor(data.color), wrap: Boolean(data.wrap) }; }
    case PaintCommandTag.DRAW_TEXT_GRADIENT:
      { const data = readData(api.types.DrawTextGradient); return { ...command, kind: "drawTextGradient", text: readBorrowedString(api, data.text), rect: { ...data.rect }, fontFamily: normalizeCharArray(data.font_family), fontSize: Number(data.font_size), fontWeight: Number(data.font_weight), textAlign: Number(data.text_align), gradient: copyPaintGradient(api, data.gradient), opacity: Number(data.opacity) }; }
    case PaintCommandTag.DRAW_TEXT_SHADOW:
      { const data = readData(api.types.DrawTextShadow); return { ...command, kind: "drawTextShadow", text: readBorrowedString(api, data.text), rect: { ...data.rect }, fontFamily: normalizeCharArray(data.font_family), fontSize: Number(data.font_size), fontWeight: Number(data.font_weight), textAlign: Number(data.text_align), shadow: { ...data.shadow, color: cloneColor(data.shadow.color), inset: Boolean(data.shadow.inset) }, wrap: Boolean(data.wrap) }; }
    case PaintCommandTag.PUSH_CLIP:
      { const data = readData(api.types.PushClip); return { ...command, kind: "pushClip", rect: { ...data.rect }, radii: { ...data.radii } }; }
    case PaintCommandTag.POP_CLIP:
      return { ...command, kind: "popClip" };
    case PaintCommandTag.FILL_BORDER_SIDE:
      { const data = readData(api.types.FillBorderSide); return { ...command, kind: "fillBorderSide", outerRect: { ...data.outer_rect }, outerRadii: { ...data.outer_radii }, innerRect: { ...data.inner_rect }, innerRadii: { ...data.inner_radii }, side: Number(data.side), style: Number(data.style), color: cloneColor(data.color) }; }
    case PaintCommandTag.DRAW_BACKGROUND_IMAGE:
      { const data = readData(api.types.DrawBackgroundImage); return { ...command, kind: "drawBackgroundImage", source: readBorrowedString(api, data.source), paintRect: { ...data.paint_rect }, positionX: { ...data.position_x }, positionY: { ...data.position_y }, sizeKind: Number(data.size_kind), sizeX: { ...data.size_x }, sizeY: { ...data.size_y }, repeatX: Number(data.repeat_x), repeatY: Number(data.repeat_y), opacity: Number(data.opacity), sourceRect: { ...data.source_rect }, destinationRect: { ...data.destination_rect }, hasResolvedGeometry: Boolean(data.has_resolved_geometry) }; }
    case PaintCommandTag.APPLY_BACKDROP_FILTER:
      { const data = readData(api.types.ApplyBackdropFilter); return { ...command, kind: "applyBackdropFilter", rect: { ...data.rect }, radii: { ...data.radii }, filters: cloneBackdropFilters(api, data.filters) }; }
    case PaintCommandTag.PUSH_TRANSFORM:
      { const data = readData(api.types.PushTransform); return { ...command, kind: "pushTransform", matrix: { ...data.matrix } }; }
    case PaintCommandTag.POP_TRANSFORM:
      return { ...command, kind: "popTransform" };
    default:
      return { ...command, kind: "unknown" };
  }
}

export class DisplayList extends OwnedHandle {
  constructor(api, handle, builder) {
    super(api, handle, "DisplayList", api.functions.displayListDestroy);
    this._builder = builder;
  }

  dispose() {
    if (this.disposed) return;
    super.dispose();
    this._builder?._lists.delete(this);
  }

  _assertAlive() {
    super._assertAlive();
    this._builder?._assertAlive();
    return this._handle;
  }

  get count() {
    return Number(this._api.functions.displayListGetCount(this._assertAlive()));
  }

  command(index) {
    assertInteger(index, "index");
    if (index >= this.count) return null;
    const size = this._api.abi.paintCommandSize;
    const buffer = Buffer.alloc(size);
    try {
      assertStatus(this._api, this._api.functions.displayListGetCommand(this._assertAlive(), index, buffer), "aether_display_list_get_command");
      return decodePaintCommand(this._api, buffer);
    } finally {
      this._api.functions.paintCommandFree(buffer);
    }
  }

  toArray() {
    return Array.from({ length: this.count }, (_, index) => this.command(index));
  }
}

export class DisplayListBuilder extends OwnedHandle {
  static _create(api, measure) {
    if (typeof measure !== "function") throw new TypeError("measure must be a function.");
    const callback = makeMeasureCallback(api, measure);
    const registration = api.ffi.register(callback, api.types.TextMeasurerPointer);
    const out = [null];
    try {
      assertStatus(api, api.functions.displayListBuilderCreate(registration, null, out), "aether_display_list_builder_create");
      return new DisplayListBuilder(api, out[0], registration);
    } catch (error) {
      api.ffi.unregister(registration);
      if (!isNullHandle(out[0])) api.functions.displayListBuilderDestroy(out[0]);
      throw error;
    }
  }

  constructor(api, handle, measureRegistration) {
    super(api, handle, "DisplayListBuilder", api.functions.displayListBuilderDestroy, () => api.ffi.unregister(measureRegistration));
    this._lists = new Set();
  }

  dispose() {
    for (const list of [...this._lists]) list.dispose();
    super.dispose();
  }

  build(layout, focusedElement = null) {
    if (!(layout instanceof LayoutResult)) throw new TypeError("layout must be a LayoutResult.");
    if (focusedElement != null && (!(focusedElement instanceof NodeView) || focusedElement.document !== layout.document)) {
      throw new TypeError("focusedElement must belong to the layout document.");
    }
    const out = [null];
    assertStatus(
      this._api,
      this._api.functions.displayListBuild(this._assertAlive(), layout._assertAlive(), focusedElement?.handle ?? null, out),
      "aether_display_list_build",
    );
    const list = new DisplayList(this._api, out[0], this);
    this._lists.add(list);
    return list;
  }
}

export class AetherBinding {
  constructor(api) {
    this._api = api;
    this.abi = api.abi;
    this.document = Object.freeze({ parse: (html) => Document._parse(api, html) });
    this.styleRuntime = Object.freeze({ create: (options) => StyleRuntime._create(api, options) });
    this.layout = Object.freeze({ create: (measure) => LayoutEngine._create(api, measure) });
    this.displayList = Object.freeze({ create: (measure) => DisplayListBuilder._create(api, measure) });
    this.css = Object.freeze({
      parseStylesheet: (source) => {
        const out = [null];
        assertStatus(api, api.functions.cssParseStylesheet(source, out), "aether_css_parse_stylesheet");
        return new StylesheetHandle(api, out[0]);
      },
      resolve: (document, stylesheet, viewportWidth, viewportHeight) => {
        if (!(document instanceof Document)) throw new TypeError("document must be a Document.");
        if (!(stylesheet instanceof StylesheetHandle)) throw new TypeError("stylesheet must be a StylesheetHandle.");
        const out = [null];
        assertStatus(api, api.functions.cssResolve(document._assertAlive(), stylesheet._assertAlive(), viewportWidth, viewportHeight, out), "aether_css_resolve");
        return new LegacyStyleMap(api, out[0], document);
      },
    });
  }
}

export class StylesheetHandle extends OwnedHandle {
  constructor(api, handle) {
    super(api, handle, "Stylesheet", api.functions.cssStylesheetDestroy);
  }
}

export class LegacyStyleMap extends OwnedHandle {
  constructor(api, handle, document) {
    super(api, handle, "LegacyStyleMap", api.functions.styleMapDestroy);
    this.document = document;
  }

  get(element) {
    if (!(element instanceof NodeView) || element.document !== this.document) throw new TypeError("element must belong to the style map's document.");
    const handle = this._api.functions.styleMapGet(this._assertAlive(), element.handle);
    if (isNullHandle(handle)) return null;
    try {
      return readComputedStyle(this._api, handle);
    } finally {
      this._api.functions.computedStyleHandleDestroy(handle);
    }
  }
}

export function createAether(options = {}) {
  const api = createLoadedNativeApi({ packageRoot, ...options });
  const abi = checkAbiCompatibility(
    api.functions.abiVersion(),
    api.functions.abiCapabilities(),
    api.functions.paintCommandSize(),
    options,
  );
  const minimumCommandSize = api.ffi.sizeof(api.types.PaintCommand);
  if (abi.paintCommandSize < minimumCommandSize) {
    throw new Error(
      `The loaded AetherUI library reports paint command size ${abi.paintCommandSize}, ` +
        `but this binding requires at least ${minimumCommandSize} bytes for the known ABI fields.`,
    );
  }
  validateComputedStyleSize(api.functions.computedStyleSize(), api.ffi.sizeof(api.types.ComputedStyle));
  api.abi = abi;
  return new AetherBinding(api);
}
