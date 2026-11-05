import assert from "node:assert/strict";
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import { createAether } from "../src/index.mjs";
import { loadKoffi, loadNativeLibrary } from "../src/ffi.mjs";

// Native tests deliberately have no mock/missing-dependency fallback.
const registrations = [];
const koffi = loadKoffi();
// Observe the actual registration, forwarding every call to real Koffi. The
// shim invokes the production callback pointer, never a recreated JS wrapper.
const ffi = new Proxy(koffi, { get(target, property) {
  if (property !== "register") return Reflect.get(target, property);
  return (...args) => { const pointer = target.register(...args); registrations.push(pointer); return pointer; };
} });
const scenario = process.argv[2];
const binding = scenario === "initialization-guard-case" ? null : createAether({ ffi, libraryPath: process.env.AETHERUI_TEST_LIBRARY });
const address = (node) => ffi.address(node.handle);
const owners = [];
const own = (owner) => (owners.push(owner), owner);
const fields = ["whiteSpace", "overflowX", "overflowY", "cursor", "pointerEvents", "visible", "zIndex", "zIndexKind", "transitionProperties", "transitionDuration", "transitionDelay", "transitionTiming"];
const values = (style) => Object.fromEntries(fields.map((key) => [key, style[key]]));
const defaults = { whiteSpace: 0, overflowX: 0, overflowY: 0, cursor: 0, pointerEvents: 0, visible: true, zIndex: 0, zIndexKind: 0, transitionProperties: 0, transitionDuration: 0, transitionDelay: 0, transitionTiming: 0 };
const distinct = { whiteSpace: 3, overflowX: 1, overflowY: 3, cursor: 2, pointerEvents: 1, visible: false, zIndex: -7, zIndexKind: 1, transitionProperties: 3, transitionDuration: 1.25, transitionDelay: 0.5, transitionTiming: 4 };
const declarations = `white-space: pre-wrap; overflow-x: hidden; overflow-y: scroll; cursor: text; pointer-events: none; visibility: hidden; z-index: -7; transition-property: all; transition-duration: 1.25s; transition-delay: 0.5s; transition-timing-function: ease-in-out; text-shadow: 2px 3px 4px red, 5px 6px 7px blue; box-shadow: 8px 9px 10px 11px green; transform: translate(12px, 13px) rotate(30deg);`;

function binaryLayout() {
  const include = fileURLToPath(new URL("../../../native/include", import.meta.url));
  const header = readFileSync(path.join(include, "aether/aether.h"), "utf8");
  // Derive the field list from the actual C header, never the JS mirror.
  const section = header.slice(header.indexOf("/* ── ComputedStyle"));
  const body = section.slice(section.indexOf("typedef struct {") + "typedef struct {".length, section.indexOf("} aether_computed_style_t;"));
  const names = [...body.replace(/\/\*[\s\S]*?\*\//g, "").matchAll(/\b\w+\s+(\w+)\s*(?:\[\d+\])?\s*;/g)].map((match) => match[1]);
  assert.equal(names.length, 55, "review the ABI contract if the header field count changes");
  const directory = mkdtempSync(path.join(tmpdir(), "aether-js-style-layout-"));
  try {
    const source = path.join(directory, "probe.cpp"), executable = path.join(directory, process.platform === "win32" ? "probe.exe" : "probe");
    writeFileSync(source, '#include <aether/aether.h>\n#include <cstddef>\n#include <cstdio>\nint main() {\n' +
      'std::printf("size=%zu\\nalign=%zu\\n", sizeof(aether_computed_style_t), alignof(aether_computed_style_t));\n' +
      names.map((name) => `std::printf("${name}=%zu\\n", offsetof(aether_computed_style_t, ${name}));`).join("\n") + '\n}\n');
    const build = spawnSync(process.env.CXX || "c++", ["-std=c++17", "-I", include, source, "-o", executable], { encoding: "utf8", timeout: 20000 });
    assert.equal(build.error, undefined, build.error?.message);
    assert.equal(build.signal, null, `compiler signal: ${build.signal}\n${build.stderr}`);
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(executable, [], { encoding: "utf8", timeout: 5000 });
    assert.equal(run.error, undefined, run.error?.message);
    assert.equal(run.signal, null, `probe signal: ${run.signal}\n${run.stderr}`);
    assert.equal(run.status, 0, run.stderr);
    const native = Object.fromEntries(run.stdout.trim().split("\n").map((line) => { const [key, value] = line.split("="); return [key, Number(value)]; }));
    const type = binding._api.types.ComputedStyle;
    assert.equal(ffi.sizeof(type), native.size, "sizeof");
    assert.equal(ffi.alignof(type), native.align, "alignof");
    for (const name of names) assert.equal(ffi.offsetof(type, name), native[name], `offsetof(${name})`);
    assert.equal(Number(binding._api.functions.computedStyleSize()), native.size, "loaded library and current header agree");
    console.log(`Verified ${names.length} offsets, size ${native.size}, alignment ${native.align} on ${process.platform}/${process.arch}`);
  } finally { rmSync(directory, { recursive: true, force: true }); }
}

function fixture() {
  const document = own(binding.document.parse('<div id="sample">probe</div>'));
  const node = document.getElementById("sample");
  const runtime = own(binding.styleRuntime.create());
  return { document, node, runtime };
}
function styleFor(runtime, node) {
  const update = runtime.resolveSubtree(node).find((entry) => address(entry.node) === address(node));
  assert.ok(update?.style, "actual runtime update must own a copied style");
  return update.style;
}
function checkLists(style) {
  assert.equal(style.textShadows.length, 2);
  assert.equal(style.textShadows[0].offsetX, 2);
  assert.equal(style.textShadows[1].blurRadius, 7);
  assert.equal(style.boxShadows.length, 1);
  assert.equal(style.boxShadows[0].spreadRadius, 11);
  assert.equal(style.transforms.length, 2);
  assert.equal(style.transforms[0].x.value, 12);
  assert.equal(style.transforms[0].y.value, 13);
  assert.ok(Math.abs(style.transforms[1].angleRadians - Math.PI / 6) < 1e-6);
}
function runtimeCopies() {
  const { node, runtime } = fixture();
  assert.deepEqual(values(styleFor(runtime, node)), defaults);
  const saved = [];
  for (let iteration = 0; iteration < 8; iteration++) {
    const sheet = runtime.addStylesheet(`#sample { ${declarations} }`);
    const style = styleFor(runtime, node);
    assert.deepEqual(values(style), distinct);
    checkLists(style);
    saved.push({ style, snapshot: structuredClone(style) });
    assert.equal(runtime.removeStylesheet(sheet.handle), true);
    runtime.clearCache();
    assert.deepEqual(values(styleFor(runtime, node)), defaults);
  }
  for (const [z, kind] of [[23, 1], [0, 0]]) {
    const sheet = runtime.addStylesheet(`#sample { z-index: ${kind ? z : "auto"}; visibility: visible; }`);
    const style = styleFor(runtime, node);
    assert.equal(style.zIndex, z);
    assert.equal(style.zIndexKind, kind);
    assert.equal(style.visible, true);
    runtime.removeStylesheet(sheet.handle);
  }
  runtime.dispose();
  for (const { style, snapshot } of saved) assert.deepEqual(style, snapshot, "JS deep copies survive native owners and repeated frees");
}
function fragments(root) {
  const result = [], seen = new Set(), pending = [root];
  while (pending.length) {
    const box = pending.pop();
    if (!box) continue;
    const key = address(box);
    assert.ok(!seen.has(key) && seen.size < 100, "bounded acyclic layout walk");
    seen.add(key);
    if (box.text === "probe") result.push(box);
    for (let i = 0; i < box.childCount; i++) pending.push(box.child(i));
  }
  return result;
}
function measurement(throws) {
  const { document, node, runtime } = fixture();
  runtime.addStylesheet(`#sample { ${declarations} visibility: visible; }`);
  const expected = { ...distinct, visible: true };
  assert.deepEqual(values(styleFor(runtime, node)), expected);
  const map = own(runtime.createResolvedStyleMap(document));
  const observations = [];
  let measuredWidth = 37;
  const engine = own(binding.layout.create((text, style, maxWidth) => {
    // Assertions belong OUTSIDE callbacks, whose exceptions are contained.
    observations.push({ text, style, maxWidth });
    if (throws) throw new Error("intentional measurement failure");
    return { width: measuredWidth, height: 19, baseline: 14 };
  }));
  const widths = [];
  for (const width of throws ? [37] : [37, 53]) {
    measuredWidth = width;
    const before = observations.length;
    const result = own(engine.layout(document, map, 400, 200));
    assert.ok(observations.length > before, "native layout must invoke the user measurement callback");
    for (const observation of observations.slice(before)) {
      assert.equal(observation.text, "probe");
      assert.ok(Number.isFinite(observation.maxWidth));
      assert.deepEqual(values(observation.style), expected);
      checkLists(observation.style);
    }
    const boxes = fragments(result.root);
    assert.equal(boxes.length, 1, "one public text fragment");
    widths.push(boxes[0].width);
    assert.equal(boxes[0].width, throws ? 0 : width, "native fragment consumes returned width (or exception fallback)");
    result.dispose();
  }
  if (!throws) assert.equal(widths[1] - widths[0], 16);
  engine.dispose(); map.dispose(); runtime.dispose();
  for (const observation of observations) { assert.deepEqual(values(observation.style), expected); checkLists(observation.style); }
}

function callbackShim() {
  const include = fileURLToPath(new URL("../../../native/include", import.meta.url));
  const directory = mkdtempSync(path.join(tmpdir(), "aether-js-style-callback-"));
  let library;
  try {
    const source = path.join(directory, "probe.cpp");
    const output = path.join(directory, process.platform === "win32" ? "probe.dll" : process.platform === "darwin" ? "probe.dylib" : "probe.so");
    writeFileSync(source, `#include <aether/aether.h>
#ifdef _WIN32
#define PROBE_EXPORT __declspec(dllexport)
#else
#define PROBE_EXPORT
#endif
extern "C" PROBE_EXPORT aether_text_metrics_t probe(aether_text_measurer_fn measure) {
  aether_computed_style_t style{};
  style.white_space = 2; style.z_index = -19; style.z_index_kind = 1;
  style.transition_properties = 2; style.transition_duration = 2.25f;
  style.transition_delay = -0.75f; style.transition_timing = 3;
  return measure("shim", &style, 123.0f, nullptr);
}
`);
    const build = spawnSync(process.env.CXX || "c++", ["-std=c++17", process.platform === "darwin" ? "-dynamiclib" : "-shared", ...(process.platform === "win32" ? [] : ["-fPIC"]), "-I", include, source, "-o", output], { encoding: "utf8", timeout: 20000 });
    assert.equal(build.error, undefined, build.error?.message);
    assert.equal(build.signal, null, `compiler signal: ${build.signal}\n${build.stderr}`);
    assert.equal(build.status, 0, build.stderr);
    library = ffi.load(output);
    const probe = library.func("probe", binding._api.types.TextMetrics, [binding._api.types.TextMeasurerPointer]);
    const observations = [];
    const engine = own(binding.layout.create((text, style, maxWidth) => {
      observations.push({ text, style, maxWidth });
      return { width: 37, height: 19, baseline: 14 };
    }));
    const result = probe(registrations.at(-1));
    assert.deepEqual(result, { width: 37, height: 19, baseline: 14 });
    assert.equal(observations.length, 1);
    const observation = observations[0];
    assert.equal(observation.text, "shim");
    assert.equal(observation.maxWidth, 123);
    assert.deepEqual(values(observation.style), { ...defaults, visible: false, whiteSpace: 2, zIndex: -19, zIndexKind: 1, transitionProperties: 2, transitionDuration: 2.25, transitionDelay: -0.75, transitionTiming: 3 });
    engine.dispose();
    // Native CSS currently rejects negative delays; this independently proves
    // that the binding transports a valid negative ABI float without clamping.
    const throwing = own(binding.layout.create(() => { throw new Error("shim fallback"); }));
    assert.deepEqual(probe(registrations.at(-1)), { width: 0, height: 0, baseline: 0 });
    throwing.dispose();
  } finally {
    library?.unload();
    rmSync(directory, { recursive: true, force: true });
  }
}

const guardCases = ["valid", "abi-33", "abi-35", "abi-patch", "abi-major", "override-33", "override-35", "override-patch", "override-major", "expectation", "capabilities", "style-small", "style-large", "paint-small"];
function initializationGuards() {
  for (const name of guardCases) {
    // Each case needs an isolated real Koffi type registry, so that no cached
    // declarations or an earlier initialization can mask the guard under test.
    const child = spawnSync(process.execPath, [fileURLToPath(import.meta.url), "initialization-guard-case", name], { encoding: "utf8", timeout: 5000, env: process.env });
    assert.equal(child.error, undefined, `${name}: ${child.error?.message}`);
    assert.equal(child.signal, null, `${name}: signal ${child.signal}\n${child.stderr}`);
    assert.equal(child.status, 0, `${name}\n${child.stdout}\n${child.stderr}`);
  }
}
function initializationGuardCase(name) {
  assert.ok(guardCases.includes(name));
  const loaded = loadNativeLibrary({ ffi, libraryPath: process.env.AETHERUI_TEST_LIBRARY });
  const actual = loaded.library;
  let resourceCalls = 0;
  const version = { major: 0, minor: 34, patch: 0 };
  if (name.endsWith("33")) version.minor = 33;
  if (name.endsWith("35")) version.minor = 35;
  if (name.endsWith("patch")) version.patch = 1;
  if (name.endsWith("major")) version.major = 1;
  const introspection = new Set(["aether_abi_version", "aether_abi_capabilities", "aether_paint_command_size", "aether_computed_style_size"]);
  const library = { func(symbol, result, args) {
    const native = actual.func(symbol, result, args);
    if (!introspection.has(symbol)) return () => {
      resourceCalls++;
      throw new Error(`Resource function called before initialization completed: ${symbol}`);
    };
    if (symbol === "aether_abi_version") return () => (version.major << 24) | (version.minor << 16) | version.patch;
    if (symbol === "aether_abi_capabilities" && name === "capabilities") return () => 0n;
    if (symbol === "aether_computed_style_size" && name.startsWith("style-")) return () => Number(native()) + (name === "style-small" ? -1 : 1);
    if (symbol === "aether_paint_command_size" && name === "paint-small") return () => 1;
    return native;
  } };
  const options = { ffi, library };
  if (name.startsWith("override-")) options.expectedAbi = version;
  if (name === "expectation") options.expectedAbi = { major: 1, minor: 34, patch: 0 };
  if (name === "valid") options.expectedAbi = { major: 0, minor: 34, patch: 0 };
  if (name === "valid") assert.doesNotThrow(() => createAether(options));
  else {
    const error = name === "expectation" ? /does not match expectedAbi/ :
      name === "capabilities" ? /missing required capability bits/ :
      name.startsWith("style-") ? /computed style size.*requires exactly/ :
      name === "paint-small" ? /paint command size.*requires at least/ :
      /Unsupported AetherUI ABI.*supports exactly 0\.34\.0/;
    assert.throws(() => createAether(options), error);
  }
  assert.equal(resourceCalls, 0, "initialization must not invoke native resource functions");
  actual.unload();
}

try {
  if (scenario === "binary-layout") binaryLayout();
  else if (scenario === "runtime-copies") runtimeCopies();
  else if (scenario === "measurement") measurement(false);
  else if (scenario === "callback-throw") measurement(true);
  else if (scenario === "callback-shim") callbackShim();
  else if (scenario === "initialization-guards") initializationGuards();
  else if (scenario === "initialization-guard-case") initializationGuardCase(process.argv[3]);
  else throw new Error(`Unknown scenario: ${scenario}`);
  for (const owner of owners.reverse()) owner.dispose();
  console.log(`PASS ${scenario}`);
} catch (error) {
  console.error(error);
  // Exit directly rather than destroying potentially corrupted native owners.
  process.exit(1);
}
