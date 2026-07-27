import test from "node:test";
import assert from "node:assert/strict";
import {
  DesktopComponent,
  DesktopEvent,
  UiKey,
  isDesktopPaintCommand,
} from "../src/desktop/index.mjs";

test("desktop module is importable without creating a native window", () => {
  assert.equal(UiKey.TAB, "Tab");
  assert.equal(isDesktopPaintCommand({ tag: 4 }), true);
  assert.equal(isDesktopPaintCommand({ tag: 99 }), false);
});

test("DesktopComponent invalidation is deterministic", () => {
  let renders = 0;
  let invalidations = 0;
  const component = new DesktopComponent({ render: () => `frame-${++renders}` });
  const unsubscribe = component.onInvalidate(() => { invalidations += 1; });
  assert.equal(component.render({}), "frame-1");
  component.stateHasChanged();
  assert.equal(invalidations, 1);
  unsubscribe();
  component.stateHasChanged();
  assert.equal(invalidations, 1);
  component.dispose();
});

test("DesktopEvent exposes cancelation and propagation state", () => {
  const event = new DesktopEvent("click", null, {}, { key: UiKey.ENTER, character: "" });
  assert.equal(event.key, UiKey.ENTER);
  assert.equal(event.defaultPrevented, false);
  event.preventDefault();
  event.stopPropagation();
  assert.equal(event.isDefaultPrevented, true);
  assert.equal(event.isPropagationStopped, true);
});
