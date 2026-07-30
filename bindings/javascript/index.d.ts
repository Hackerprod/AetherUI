export const SUPPORTED_ABI: Readonly<{ major: number; minor: number; patch: number }>;
export const CAPABILITIES: Readonly<{
  DOM: bigint;
  STYLE_RUNTIME: bigint;
  LAYOUT: bigint;
  DISPLAY_LIST: bigint;
}>;
export const DEFAULT_CAPABILITIES: bigint;

export const NodeType: Readonly<{ DOCUMENT: 0; ELEMENT: 1; TEXT: 2; COMMENT: 3 }>;
export const ElementState: Readonly<{ NONE: 0; HOVER: 1; ACTIVE: 2; FOCUS: 4; INVALID: 8 }>;
export const PaintCommandTag: Readonly<{
  FILL_ROUNDED_RECT: 0;
  DRAW_BOX_SHADOW: 1;
  FILL_ROUNDED_RECT_GRADIENT: 2;
  STROKE_ROUNDED_RECT: 3;
  DRAW_TEXT: 4;
  DRAW_TEXT_GRADIENT: 5;
  PUSH_CLIP: 6;
  POP_CLIP: 7;
  FILL_BORDER_SIDE: 8;
  DRAW_TEXT_SHADOW: 9;
  DRAW_BACKGROUND_IMAGE: 10;
  APPLY_BACKDROP_FILTER: 11;
  PUSH_TRANSFORM: 12;
  POP_TRANSFORM: 13;
}>;

export interface AbiInfo {
  readonly version: number;
  readonly major: number;
  readonly minor: number;
  readonly patch: number;
  readonly capabilities: bigint;
  readonly paintCommandSize: number;
}

export interface CreateAetherOptions {
  libraryPath?: string;
  installPrefix?: string;
  expectedAbi?: { major?: number; minor?: number; patch?: number };
  requiredCapabilities?: bigint | number;
  /** Advanced testing/embedding hook. The object must implement the Koffi API. */
  ffi?: unknown;
  /** Advanced embedding hook: an already loaded Koffi library object. */
  library?: unknown;
}

export class NativeStatusError extends Error {
  readonly operation: string;
  readonly status: number;
  constructor(operation: string, status: number, message?: string);
}

export interface Color { r: number; g: number; b: number; a: number }
export interface CssLength {
  value: number;
  unit: number;
  linearPercent: number;
  linearEm: number;
  linearRem: number;
  linearVw: number;
  linearVh: number;
}
export interface CssEdges { left: CssLength; top: CssLength; right: CssLength; bottom: CssLength }
export interface GradientStop {
  color: Color;
  /** Authored symbolic stop position; offsets are resolved per final paint box. */
  position: CssLength;
  hasPosition: boolean;
  offset: number;
}
export interface LinearGradient { directionX: number; directionY: number; stops: GradientStop[] }
export interface BoxShadow {
  offsetX: number;
  offsetY: number;
  blurRadius: number;
  spreadRadius: number;
  color: Color;
  inset: boolean;
}
export interface BackgroundLayer {
  kind: number;
  gradient: LinearGradient | null;
  imageSource: string | null;
  positionX: CssLength;
  positionY: CssLength;
  sizeKind: number;
  sizeX: CssLength;
  sizeY: CssLength;
  repeatX: number;
  repeatY: number;
}
export interface BackdropFilter {
  kind: number;
  lengthAmount: CssLength;
  scalarAmount: number;
}
export interface FlexBasis {
  kind: number;
  length: CssLength;
}
export interface TransformOperation {
  kind: number;
  x: CssLength;
  y: CssLength;
  scalarX: number;
  scalarY: number;
  angleRadians: number;
  matrixA: number; matrixB: number; matrixC: number;
  matrixD: number; matrixE: number; matrixF: number;
}
export interface TransformOrigin { x: CssLength; y: CssLength }
export interface ColorEdges { left: Color; top: Color; right: Color; bottom: Color }
export interface BorderStyleEdges { left: number; top: number; right: number; bottom: number }
export interface CornerRadius { horizontal: CssLength; vertical: CssLength }
export interface BorderRadii {
  topLeft: CornerRadius;
  topRight: CornerRadius;
  bottomRight: CornerRadius;
  bottomLeft: CornerRadius;
}
export interface ComputedStyle {
  display: number;
  flexDirection: number;
  justifyContent: number;
  alignItems: number;
  alignSelf: number;
  flexGrow: number;
  flexShrink: number;
  flexBasis: FlexBasis;
  flexWrap: boolean;
  gap: number;
  position: number;
  inset: CssEdges;
  width: CssLength;
  height: CssLength;
  minWidth: CssLength;
  minHeight: CssLength;
  maxWidth: CssLength;
  maxHeight: CssLength;
  margin: CssEdges;
  padding: CssEdges;
  borderWidth: CssEdges;
  boxSizing: number;
  color: Color;
  backgroundColor: Color;
  backgroundLayers: BackgroundLayer[];
  backdropFilters: BackdropFilter[];
  hasBackgroundGradient: boolean;
  backgroundGradient: LinearGradient | null;
  borderColor: Color;
  borderColors: ColorEdges;
  borderStyles: BorderStyleEdges;
  hasTextGradient: boolean;
  textGradient: LinearGradient | null;
  transforms: TransformOperation[];
  transformOrigin: TransformOrigin;
  textShadows: BoxShadow[];
  boxShadows: BoxShadow[];
  hasBoxShadow: boolean;
  boxShadow: BoxShadow | null;
  borderRadii: BorderRadii;
  opacity: number;
  fontFamily: string;
  fontSize: number;
  fontWeight: number;
  lineHeight: number;
  textAlign: number;
  textTransform: number;
  /** Computed horizontal overflow behavior. */
  overflowX: number;
  /** Computed vertical overflow behavior. */
  overflowY: number;
  cursor: number;
  pointerEvents: number;
  visible: boolean;
}

export interface Attribute { name: string; value: string }
export interface StyleDiagnostic { code: string; property: string; value: string; message: string }
export interface StyleRuntimeOptions {
  throwOnInvalid?: boolean;
  enforceThreadAffinity?: boolean;
  maxVariableDepth?: number;
  maxVariableExpansionLength?: number;
  environment?: StyleEnvironmentOptions;
  onDiagnostic?: (diagnostic: StyleDiagnostic) => void;
}
export interface StyleEnvironmentOptions {
  viewportWidth?: number;
  viewportHeight?: number;
  dpiScale?: number;
  rootFontSize?: number;
  colorScheme?: number;
  pointerAccuracy?: number;
  motionPreference?: number;
}
export interface StylesheetOptions { layer?: number; sourceName?: string }
export interface StylesheetInfo { readonly handle: bigint; readonly ruleCount: number }
export interface StyleUpdate { node: NodeView | null; style: ComputedStyle | null; impact: number }

export class NodeView {
  readonly handle: bigint;
  readonly document: Document;
  readonly type: number;
  readonly parent: NodeView | null;
  readonly childCount: number;
  readonly textContent: string | null;
  readonly isElement: boolean;
  child(index: number): NodeView | null;
  readonly tagName: string | null;
  getAttribute(name: string): string | null;
  readonly attributes: Attribute[];
  setAttribute(name: string, value: string): void;
  hasAttribute(name: string): boolean;
  hasClass(className: string): boolean;
  readonly required: boolean;
  readonly disabled: boolean;
  readonly focusable: boolean;
  state: number;
  appendChild(child: NodeView): void;
  removeChild(child: NodeView): void;
}
export class ElementView extends NodeView {}

export class Document {
  readonly disposed: boolean;
  readonly version: bigint;
  readonly body: NodeView | null;
  readonly documentElement: ElementView | NodeView | null;
  parse(html: string): void;
  getElementById(id: string): ElementView | NodeView | null;
  dispose(): void;
  [Symbol.dispose](): void;
}

export class StyleMap {
  readonly disposed: boolean;
  get(element: NodeView): ComputedStyle | null;
  dispose(): void;
  [Symbol.dispose](): void;
}
export class StyleRuntime {
  readonly disposed: boolean;
  readonly environment: Readonly<StyleEnvironmentOptions>;
  addStylesheet(source: string, options?: StylesheetOptions): StylesheetInfo;
  removeStylesheet(handle: bigint | number): boolean;
  resolveSubtree(root: NodeView): StyleUpdate[];
  setEnvironment(environment: StyleEnvironmentOptions, root: NodeView): StyleUpdate[];
  createResolvedStyleMap(document: Document): StyleMap;
  clearCache(): void;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface TextMetrics { width: number; height: number; baseline: number }
export type TextMeasurer = (text: string, style: ComputedStyle, maxWidth: number) => TextMetrics;
export interface ScrollOffsetValue { x: number; y: number }
export type ScrollOffset = (element: NodeView | null) => ScrollOffsetValue;

export class LayoutBox {
  readonly handle: bigint;
  readonly kind: number;
  readonly x: number;
  readonly y: number;
  readonly width: number;
  readonly height: number;
  readonly marginBox: { x: number; y: number; width: number; height: number };
  readonly borderBox: { x: number; y: number; width: number; height: number };
  readonly paddingBox: { x: number; y: number; width: number; height: number };
  readonly contentBox: { x: number; y: number; width: number; height: number };
  readonly isAtomic: boolean;
  readonly text: string | null;
  readonly scrollMetrics: { scrollableOverflowX: number; scrollableOverflowY: number; scrollOffsetX: number; scrollOffsetY: number };
  readonly scrollbarGeometry: {
    horizontalVisible: boolean; verticalVisible: boolean;
    horizontalTrack: { x: number; y: number; width: number; height: number };
    horizontalThumb: { x: number; y: number; width: number; height: number };
    verticalTrack: { x: number; y: number; width: number; height: number };
    verticalThumb: { x: number; y: number; width: number; height: number };
    corner: { x: number; y: number; width: number; height: number };
  };
  readonly scrollableOverflowX: number;
  readonly scrollableOverflowY: number;
  readonly scrollOffsetX: number;
  readonly scrollOffsetY: number;
  readonly element: NodeView | null;
  readonly childCount: number;
  child(index: number): LayoutBox | null;
}
export class LayoutResult {
  readonly disposed: boolean;
  readonly document: Document;
  readonly root: LayoutBox | null;
  readonly paintOrderCount: number;
  findPrimaryBox(element: NodeView): LayoutBox | null;
  hitTest(x: number, y: number): NodeView | null;
  paintOrderBox(index: number): LayoutBox | null;
  dispose(): void;
  [Symbol.dispose](): void;
}
export class LayoutEngine {
  readonly disposed: boolean;
  layout(document: Document, styles: StyleMap, width: number, height: number, scrollOffset?: ScrollOffset): LayoutResult;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface PaintCommandBase { tag: number; raw: Uint8Array; kind: string }
export interface CornerRadii {
  top_left_x: number; top_left_y: number; top_right_x: number; top_right_y: number;
  bottom_right_x: number; bottom_right_y: number; bottom_left_x: number; bottom_left_y: number;
}
export type PaintCommand =
  | (PaintCommandBase & { kind: "fillRoundedRect"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii; color: Color })
  | (PaintCommandBase & { kind: "drawBoxShadow"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii; shadow: BoxShadow })
  | (PaintCommandBase & { kind: "fillRoundedRectGradient"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii; gradient: LinearGradient; opacity: number })
  | (PaintCommandBase & { kind: "strokeRoundedRect"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii; strokeWidth: number; color: Color })
  | (PaintCommandBase & { kind: "drawText"; text: string | null; rect: { x: number; y: number; width: number; height: number }; fontFamily: string; fontSize: number; fontWeight: number; textAlign: number; color: Color; wrap: boolean })
  | (PaintCommandBase & { kind: "drawTextGradient"; text: string | null; rect: { x: number; y: number; width: number; height: number }; fontFamily: string; fontSize: number; fontWeight: number; textAlign: number; gradient: LinearGradient; opacity: number })
  | (PaintCommandBase & { kind: "drawTextShadow"; text: string | null; rect: { x: number; y: number; width: number; height: number }; fontFamily: string; fontSize: number; fontWeight: number; textAlign: number; shadow: BoxShadow; wrap: boolean })
  | (PaintCommandBase & { kind: "fillBorderSide"; outerRect: { x: number; y: number; width: number; height: number }; outerRadii: CornerRadii; innerRect: { x: number; y: number; width: number; height: number }; innerRadii: CornerRadii; side: number; style: number; color: Color })
  | (PaintCommandBase & { kind: "drawBackgroundImage"; source: string | null; paintRect: { x: number; y: number; width: number; height: number }; positionX: CssLength; positionY: CssLength; sizeKind: number; sizeX: CssLength; sizeY: CssLength; repeatX: number; repeatY: number; opacity: number; sourceRect: { x: number; y: number; width: number; height: number }; destinationRect: { x: number; y: number; width: number; height: number }; hasResolvedGeometry: boolean })
  | (PaintCommandBase & { kind: "applyBackdropFilter"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii; filters: BackdropFilter[] })
  | (PaintCommandBase & { kind: "pushClip"; rect: { x: number; y: number; width: number; height: number }; radii: CornerRadii })
  | (PaintCommandBase & { kind: "popClip" })
  | (PaintCommandBase & { kind: "unknown" });

export class DisplayList {
  readonly disposed: boolean;
  readonly count: number;
  command(index: number): PaintCommand | null;
  toArray(): PaintCommand[];
  dispose(): void;
  [Symbol.dispose](): void;
}
export class DisplayListBuilder {
  readonly disposed: boolean;
  build(layout: LayoutResult, focusedElement?: NodeView | null): DisplayList;
  dispose(): void;
  [Symbol.dispose](): void;
}

export class StylesheetHandle {
  readonly disposed: boolean;
  dispose(): void;
  [Symbol.dispose](): void;
}
export class LegacyStyleMap {
  readonly disposed: boolean;
  readonly document: Document;
  get(element: NodeView): ComputedStyle | null;
  dispose(): void;
  [Symbol.dispose](): void;
}

export interface AetherBinding {
  readonly abi: AbiInfo;
  readonly document: { parse(html: string): Document };
  readonly styleRuntime: { create(options?: StyleRuntimeOptions): StyleRuntime };
  readonly layout: { create(measure: TextMeasurer): LayoutEngine };
  readonly displayList: { create(measure: TextMeasurer): DisplayListBuilder };
  readonly css: {
    parseStylesheet(source: string): StylesheetHandle;
    resolve(document: Document, stylesheet: StylesheetHandle, viewportWidth: number, viewportHeight: number): LegacyStyleMap;
  };
}

export function createAether(options?: CreateAetherOptions): AetherBinding;
