import assert from "node:assert/strict";
import { createAether, PaintCommandTag } from "../src/index.mjs";

const owners = [];
const own = (value) => (owners.push(value), value);
const text = "árbol中文🙂";
const source = "asset://ejemplo/ñ/图片🙂.png";
const font = "á字体🙂";
const retained = [];
const observations = [];
const diagnostics = [];
const measure = (value, style) => {
  observations.push({ text: value, style });
  return { width: value.length * 8, height: 19, baseline: 13 };
};
function boxes(root) {
  const result = [], pending = [root];
  while (pending.length) {
    const box = pending.pop();
    if (!box) continue;
    assert.ok(result.length < 100, "bounded layout traversal");
    result.push(box);
    for (let i = 0; i < box.childCount; i++) pending.push(box.child(i));
  }
  return result;
}
try {
  const binding = createAether({ libraryPath: process.env.AETHERUI_TEST_LIBRARY });
  const scenario = process.argv[2];
  if (scenario === "dom") {
    const document = own(binding.document.parse(`<main id="main" data-empty=""><span id="text">${text}</span><div id="empty"></div></main>`));
    const main = document.getElementById("main"), span = document.getElementById("text");
    main.setAttribute("data-ñ", text);
    for (let i = 0; i < 2000; i++) {
      assert.equal(main.tagName, "main");
      assert.equal(main.getAttribute("missing"), null);
      assert.equal(main.getAttribute("data-empty"), "");
      assert.equal(main.getAttribute("data-ñ"), text);
      assert.deepEqual(new Map(main.attributes.map(({ name, value }) => [name, value])), new Map([["id", "main"], ["data-empty", ""], ["data-ñ", text]]));
      assert.equal(span.child(0).textContent, text);
      // C ABI node text is not descendant-concatenating DOM textContent.
      assert.equal(main.textContent, "");
    }
    const runtime = own(binding.styleRuntime.create());
    runtime.resolveSubtree(document.documentElement);
    const map = own(runtime.createResolvedStyleMap(document));
    const engine = own(binding.layout.create(measure));
    const result = own(engine.layout(document, map, 640, 480));
    const all = boxes(result.root);
    assert.ok(all.some((box) => box.text === text), "layout returns owned Unicode text");
    assert.ok(all.some((box) => box.text === null), "non-text boxes return NULL");
    retained.push(...all.map((box) => box.text));
  } else if (scenario === "paint") {
    for (let iteration = 0; iteration < 20; iteration++) {
      const document = binding.document.parse(`<main><div id="plain">${text}</div><div id="gradient">${text}</div></main>`);
      const runtime = binding.styleRuntime.create();
      runtime.addStylesheet(`main, div { display: block; } div { width: 250px; height: 40px; font-family: "${font}"; }
        #plain { background-image: url("${source}"); text-shadow: 1px 2px 3px red; }
        #gradient { background: linear-gradient(red, blue); background-clip: text; }`);
      runtime.resolveSubtree(document.documentElement);
      const map = runtime.createResolvedStyleMap(document);
      const engine = binding.layout.create(measure);
      const result = engine.layout(document, map, 640, 480);
      const builder = binding.displayList.create(measure);
      const list = builder.build(result);
      const commands = list.toArray();
      for (const tag of [PaintCommandTag.DRAW_TEXT, PaintCommandTag.DRAW_TEXT_GRADIENT, PaintCommandTag.DRAW_TEXT_SHADOW, PaintCommandTag.DRAW_BACKGROUND_IMAGE]) {
        const emitted = commands.filter((command) => command.tag === tag);
        assert.ok(emitted.length > 0, `real native emitter produced tag ${tag}`);
        for (const command of emitted) {
          if (tag === PaintCommandTag.DRAW_BACKGROUND_IMAGE) assert.equal(command.source, source);
          else { assert.equal(command.text, text); assert.equal(command.fontFamily, font); }
        }
      }
      // Repeated reads must produce independent copies before each command free.
      const repeated = list.toArray();
      assert.deepEqual(repeated.map(({ raw, ...command }) => command), commands.map(({ raw, ...command }) => command));
      retained.push(...commands);
      list.dispose(); builder.dispose(); result.dispose(); engine.dispose(); map.dispose(); runtime.dispose(); document.dispose();
    }
    for (const command of retained) {
      if ([4, 5, 9].includes(command.tag)) assert.equal(command.text, text);
      if (command.tag === 10) assert.equal(command.source, source);
    }
  } else if (scenario === "callbacks") {
    const document = own(binding.document.parse(`<main id="subject">${text}</main>`));
    const runtime = own(binding.styleRuntime.create({ onDiagnostic: (diagnostic) => diagnostics.push(diagnostic) }));
    runtime.addStylesheet(`#subject { background-image: url("${source}"); font-family: "${font}"; width: calc(100% / 0); }`);
    const updates = runtime.resolveSubtree(document.documentElement);
    const style = updates.find((update) => update.node?.getAttribute("id") === "subject")?.style;
    assert.ok(style);
    assert.equal(style.backgroundLayers[0].imageSource, source);
    assert.equal(style.fontFamily, font);
    const map = own(runtime.createResolvedStyleMap(document));
    const engine = own(binding.layout.create(measure));
    own(engine.layout(document, map, 640, 480));
    retained.push(style);
  } else { throw new Error(`Unknown scenario ${scenario}`); }
  for (const owner of owners.reverse()) owner.dispose();
  assert.ok(observations.length > 0, "production measurement callback invoked");
  assert.ok(observations.some((item) => item.text === text), "Unicode callback text survives native owner disposal");
  if (scenario === "callbacks") {
    assert.equal(retained[0].backgroundLayers[0].imageSource, source);
    assert.equal(retained[0].fontFamily, font);
    assert.ok(observations.some((item) => item.style?.fontFamily === font));
    assert.ok(observations.some((item) => item.style?.backgroundLayers.some((layer) => layer.imageSource === source)));
    assert.equal(diagnostics.length, 1);
    assert.equal(diagnostics[0].code, "AETHER-CSS-CALC");
    assert.equal(diagnostics[0].property, "width");
    assert.equal(diagnostics[0].value, "calc(100% / 0)");
    assert.equal(typeof diagnostics[0].message, "string");
    assert.ok(diagnostics[0].message.length > 0);
  }
  console.log(`PASS native strings ${scenario}: ${observations.length} measurement calls`);
} catch (error) { console.error(error); process.exit(1); }
