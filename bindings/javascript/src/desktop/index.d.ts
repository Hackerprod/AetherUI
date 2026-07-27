import type {
  AetherBinding,
  Color,
  DisplayList,
  LayoutResult,
  NodeView,
  TextMetrics,
  TextMeasurer,
} from "@aetherui/javascript";

export const UiKey: Readonly<{
  NONE: "None";
  BACKSPACE: "Backspace";
  TAB: "Tab";
  ENTER: "Enter";
  ESCAPE: "Escape";
  SPACE: "Space";
  LEFT: "Left";
  UP: "Up";
  RIGHT: "Right";
  DOWN: "Down";
  HOME: "Home";
  END: "End";
  DELETE: "Delete";
}>;

export const DEFAULT_DESKTOP_CSS: string;

export type DesktopRenderResult = string | { markup: string; handlers?: DesktopHandlers };
export type DesktopHandler = (event: DesktopEvent) => void;
export type DesktopHandlers = Record<string, DesktopHandler>;
export type DesktopRenderFunction = (host: DesktopHost) => DesktopRenderResult;
export type DesktopComponentLike = DesktopComponent | DesktopRenderFunction | { render: DesktopRenderFunction; handlers?: DesktopHandlers; dispose?: () => void };

export interface DesktopComponentOptions {
  render: DesktopRenderFunction;
  handlers?: DesktopHandlers;
  dispose?: () => void;
}

export class DesktopEvent {
  readonly type: string;
  readonly target: NodeView | null;
  currentTarget: NodeView | null;
  readonly host: DesktopHost;
  readonly pointerPosition: Readonly<{ x: number; y: number }>;
  readonly key: string;
  readonly character: string;
  readonly value: string | null;
  readonly defaultPrevented: boolean;
  readonly propagationStopped: boolean;
  constructor(type: string, target: NodeView | null, host: DesktopHost, options?: { point?: { x: number; y: number }; key?: string; character?: string });
  preventDefault(): void;
  stopPropagation(): void;
  readonly isDefaultPrevented: boolean;
  readonly isPropagationStopped: boolean;
}

export class DesktopComponent {
  constructor(options: DesktopComponentOptions);
  render(host: DesktopHost): DesktopRenderResult;
  readonly handlers: DesktopHandlers;
  onInvalidate(callback: () => void): () => void;
  stateHasChanged(): void;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface DesktopRenderer {
  measureText: TextMeasurer;
  render(displayList: DisplayList, clearColor: Color, paintContext: unknown): void;
  resize?(pixelWidth: number, pixelHeight: number, dpiScale: number): void;
  dispose?(): void;
}

export interface DesktopWindow {
  attachHost?(host: DesktopHost): void;
  create?(): void;
  show?(): void;
  run?(): number;
  close?(): void;
  requestRender?(): void;
  dispose?(): void;
}

export interface DesktopHostOptions {
  renderer: DesktopRenderer;
  window?: DesktopWindow | null;
  width?: number;
  height?: number;
  dpiScale?: number;
  clearColor?: Color;
  defaultCss?: string;
  onDiagnostic?: (diagnostic: unknown) => void;
}

export class DesktopHost {
  constructor(aether: AetherBinding, component: DesktopComponent | DesktopRenderFunction | { render: DesktopRenderFunction; handlers?: DesktopHandlers; dispose?: () => void }, options: DesktopHostOptions);
  readonly disposed: boolean;
  readonly document: import("@aetherui/javascript").Document | null;
  readonly layout: LayoutResult | null;
  readonly focusedElement: NodeView | null;
  readonly needsRender: boolean;
  readonly dpiScale: number;
  readonly viewport: Readonly<{ width: number; height: number }>;
  invalidate(): void;
  invalidateMarkup(): void;
  setViewport(pixelWidth: number, pixelHeight: number, dpiScale?: number): void;
  render(paintContext: unknown): void;
  pointerMoved(point: { x: number; y: number }): void;
  pointerLeft(): void;
  pointerPressed(point: { x: number; y: number }): void;
  pointerReleased(point: { x: number; y: number }): void;
  textInput(character: string): void;
  keyPressed(key: string): void;
  mouseWheelScrolled(point: { x: number; y: number }, notches: number): void;
  cursorAt(point: { x: number; y: number }): number;
  dispose(): void;
  [Symbol.dispose](): void;
}

export class Win32Window implements DesktopWindow {
  static create(options?: Win32WindowOptions, ffi?: unknown): Win32Window;
  readonly api: unknown;
  constructor(api: unknown, state: unknown, options?: Win32WindowOptions);
  readonly title: string;
  readonly width: number;
  readonly height: number;
  attachHost(host: DesktopHost): void;
  create(): void;
  show(): void;
  requestRender(): void;
  run(): number;
  close(): void;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface Win32WindowOptions {
  title?: string;
  width?: number;
  height?: number;
  minimumWidth?: number;
  minimumHeight?: number;
  ffi?: unknown;
}

export class GdiRenderBackend implements DesktopRenderer {
  constructor(options?: { ffi?: unknown; clearColor?: Color });
  readonly api: unknown;
  dpiScale: number;
  pixelWidth: number;
  pixelHeight: number;
  clearColor: Color;
  resize(pixelWidth: number, pixelHeight: number, dpiScale?: number): void;
  measureText(text: string, style: import("@aetherui/javascript").ComputedStyle | Record<string, unknown>, maxWidth: number): TextMetrics;
  render(displayList: DisplayList, clearColor: Color, paintContext: unknown): void;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface DesktopApplicationOptions {
  aether?: AetherBinding;
  aetherOptions?: import("@aetherui/javascript").CreateAetherOptions;
  window?: DesktopWindow | null;
  windowOptions?: Win32WindowOptions;
  renderer?: DesktopRenderer | null;
  rendererOptions?: { ffi?: unknown; clearColor?: Color };
  hostOptions?: Omit<DesktopHostOptions, "renderer" | "window">;
}

export class DesktopApplication {
  constructor(component: DesktopComponentLike, options?: DesktopApplicationOptions);
  readonly aether: AetherBinding;
  readonly window: DesktopWindow;
  readonly renderer: DesktopRenderer;
  readonly host: DesktopHost;
  create(): this;
  show(): this;
  run(): number;
  close(): void;
  dispose(): void;
  [Symbol.dispose](): void;
}

export function createDesktopComponent(options: DesktopComponentOptions): DesktopComponent;
export function createDesktopApplication(component: DesktopComponentLike, options?: DesktopApplicationOptions): DesktopApplication;
export function runDesktop(component: DesktopComponentLike, options?: DesktopApplicationOptions): number;
export function isDesktopPaintCommand(command: unknown): boolean;
