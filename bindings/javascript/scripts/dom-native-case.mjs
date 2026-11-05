import assert from "node:assert/strict";
import { createAether, NativeStatusError } from "../src/index.mjs";
import { loadKoffi } from "../src/ffi.mjs";

// Run only via dom-native.test.mjs. On failure exit without traversing or
// destroying a potentially corrupted graph. The parent also bounds execution.
const owners = [];
const own = (value) => (owners.push(value), value);
let ffi;
// Koffi may return a new External object for the same native pointer. Compare
// addresses, never JavaScript object identity, for both DOM and layout handles.
const handle = (view) => view == null ? null : ffi.address(view.handle);
const children = (node) => Array.from({ length: node.childCount }, (_, i) => node.child(i));
const ids = ["root", "left", "deep", "leaf", "right", "last", "flex", "pseudo", "named"];
const measure = (text) => ({ width: text.length * 8, height: 16, baseline: 12 });

function collect(root) {
  const nodes = [], visited = new Set(), pending = [root];
  while (pending.length) {
    const node = pending.pop();
    if (!node) continue;
    assert.ok(nodes.length < 200, "bounded DOM traversal");
    assert.ok(!visited.has(handle(node)), "DOM cycle or duplicate child");
    visited.add(handle(node));
    nodes.push(node);
    const list = children(node);
    for (const child of list) assert.equal(handle(child.parent), handle(node), "reciprocal DOM edge");
    pending.push(...list);
  }
  return nodes;
}
function snapshot(document, nodes) {
  // Flat structural getters first, before recursive text/ID getters.
  const structure = nodes.map((node) => ({
    handle: handle(node), parent: handle(node.parent), children: children(node).map(handle),
  }));
  for (const row of structure) assert.equal(new Set(row.children).size, row.children.length);
  return {
    version: document.version, structure,
    text: nodes.map((node) => node.textContent),
    ids: ids.map((id) => handle(document.getElementById(id))),
  };
}
function reject(document, nodes, parent, child, operation = "append", observable = () => null) {
  const before = snapshot(document, nodes), layoutBefore = observable();
  assert.throws(() => parent[`${operation}Child`](child), (error) => {
    assert.ok(error instanceof NativeStatusError);
    assert.equal(error.status, -2);
    assert.equal(error.operation, `aether_node_${operation}_child`);
    return true;
  });
  // If an old core accepts the operation, assert.throws exits before any
  // recursive getter or destructor can follow the newly introduced cycle.
  const flat = nodes.map((node) => ({ handle: handle(node), parent: handle(node.parent), children: children(node).map(handle) }));
  assert.deepEqual(flat, before.structure);
  assert.deepEqual(snapshot(document, nodes), before);
  assert.deepEqual(observable(), layoutBefore);
}
function boxTree(result) {
  const boxes = [], pending = [result.root], seen = new Set();
  while (pending.length) {
    const box = pending.pop();
    if (!box) continue;
    assert.ok(boxes.length < 200 && !seen.has(handle(box)), "bounded layout traversal");
    seen.add(handle(box));
    boxes.push(box);
    for (let i = box.childCount - 1; i >= 0; i--) pending.push(box.child(i));
  }
  return boxes;
}
function layoutSnapshot(result, builder) {
  const list = builder.build(result);
  try {
    return {
      boxes: boxTree(result).map((b) => ({ node: handle(b.element), kind: b.kind, rect: b.borderBox, text: b.text, children: b.childCount })),
      paintOrder: Array.from({ length: result.paintOrderCount }, (_, i) => handle(result.paintOrderBox(i))),
      // Raw command bytes contain transient pointers; compare decoded values.
      commands: list.toArray().map(({ raw, ...command }) => command),
    };
  } finally { list.dispose(); }
}

try {
  ffi = loadKoffi();
  const api = createAether({ libraryPath: process.env.AETHERUI_TEST_LIBRARY });
  const document = own(api.document.parse('<main id="root"><section id="left"><div id="deep"><b id="leaf">leaf</b></div></section><aside id="right"></aside><i id="last">last</i></main>'));
  const get = (id) => { const node = document.getElementById(id); assert.ok(node, id); return node; };
  const root = get("root"), left = get("left"), deep = get("deep"), leaf = get("leaf"), right = get("right"), last = get("last");
  const scenario = process.argv[2];
  if (scenario === "hierarchy") {
    const nodes = collect(document.documentElement);
    for (let repeat = 0; repeat < 3; repeat++) {
      reject(document, nodes, root, root);
      reject(document, nodes, left, root);
      reject(document, nodes, leaf, root);
      reject(document, nodes, leaf.child(0), leaf.child(0));
    }
    root.removeChild(left);
    for (let repeat = 0; repeat < 2; repeat++) {
      reject(document, nodes, leaf, left);
      reject(document, nodes, deep, left);
      reject(document, nodes, left, left);
    }
    assert.equal(document.getElementById("left"), null);
    root.appendChild(left);
    root.appendChild(leaf); // descendant upward
    right.appendChild(leaf); // across branches
    assert.equal(handle(leaf.parent), handle(right));
    root.appendChild(last); // middle to end
    assert.equal(handle(root.child(root.childCount - 1)), handle(last));
    const ordered = children(root).map(handle);
    root.appendChild(last); // already last
    assert.deepEqual(children(root).map(handle), ordered);
    root.removeChild(left);
    root.appendChild(left);
    assert.equal(handle(deep.parent), handle(left));
    assert.equal(handle(get("deep")), handle(deep));
    collect(document.documentElement);
    const styles = own(api.styleRuntime.create({ environment: { viewportWidth: 640, viewportHeight: 480 } }));
    // This API copies resolved states; it does not resolve styles itself.
    const updates = styles.resolveSubtree(document.documentElement);
    const map = own(styles.createResolvedStyleMap(document));
    const engine = own(api.layout.create(measure));
    assert.ok(updates.find((update) => handle(update.node) === handle(root))?.style, "authored root has a resolved style copy");
    const result = own(engine.layout(document, map, 640, 480));
    assert.ok(result.root);
  } else if (scenario === "synthetic") {
    document.parse('<main id="root"><div id="flex">authored<span id="right">right</span></div><div id="pseudo">body</div><aether-anonymous-flex-item id="named">named</aether-anonymous-flex-item></main>');
    const host = get("flex"), pseudoHost = get("pseudo"), target = get("right"), named = get("named"), main = get("root");
    const styles = own(api.styleRuntime.create({ environment: { viewportWidth: 640, viewportHeight: 480 } }));
    styles.addStylesheet('#flex { display: flex; width: 300px; } #pseudo::before { content: "generated"; display: block; }');
    // This API copies resolved states; it does not resolve styles itself.
    const updates = styles.resolveSubtree(document.documentElement);
    const map = own(styles.createResolvedStyleMap(document));
    const engine = own(api.layout.create(measure));
    assert.equal(updates.find((update) => handle(update.node) === handle(host))?.style?.display, 4, "host resolved to AETHER_DISPLAY_FLEX");
    const first = own(engine.layout(document, map, 640, 480));
    const second = own(engine.layout(document, map, 640, 480));
    const builder = own(api.displayList.create(measure));
    const wrapperFor = (result) => boxTree(result).map((b) => b.element).find((n) => n?.isElement && n.tagName === "aether-anonymous-flex-item" && handle(n.parent) === handle(host));
    const wrapper = wrapperFor(first), other = wrapperFor(second);
    assert.ok(wrapper && other, "real anonymous flex wrappers");
    assert.notEqual(handle(wrapper), handle(other));
    const text = wrapper.child(0);
    assert.ok(text, "wrapper aliases authored text");
    assert.equal(text.textContent, "authored");
    assert.equal(handle(text.parent), handle(host));
    assert.equal(handle(other.child(0)), handle(text));
    const pseudo = boxTree(first).map((b) => b.element).find((n) => n?.isElement && handle(n.parent) === handle(pseudoHost) && handle(n) !== handle(pseudoHost) && n.child(0)?.textContent === "generated");
    assert.ok(pseudo, "pseudo obtained from public layout box");
    assert.equal(handle(pseudo.parent), handle(pseudoHost));
    assert.ok(boxTree(first).find((box) => handle(box.element) === handle(pseudo)), "generated pseudo participates in resolved layout");
    const generatedText = pseudo.child(0);
    assert.ok(generatedText);
    assert.equal(handle(generatedText.parent), handle(pseudo));
    assert.equal(generatedText.textContent, "generated");
    const nodes = [...new Map([...collect(document.documentElement), wrapper, other, pseudo, generatedText].map((n) => [handle(n), n])).values()];
    const observe = () => [layoutSnapshot(first, builder), layoutSnapshot(second, builder)];
    for (let repeat = 0; repeat < 2; repeat++) {
      for (const [parent, child] of [[text, wrapper], [wrapper, target], [target, wrapper], [wrapper, wrapper], [wrapper, other], [main, pseudo], [pseudo, target], [main, generatedText], [generatedText, target]]) {
        reject(document, nodes, parent, child, "append", observe);
      }
      for (const [parent, child] of [[wrapper, text], [wrapper, wrapper], [other, text], [main, wrapper], [pseudoHost, pseudo], [pseudo, generatedText], [main, generatedText], [generatedText, target]]) {
        reject(document, nodes, parent, child, "remove", observe);
      }
    }
    // Authored hosts and text aliases remain editable; old layout is a snapshot.
    target.appendChild(text);
    assert.equal(handle(text.parent), handle(target));
    host.appendChild(text);
    main.appendChild(pseudoHost);
    main.appendChild(named); // matching wrapper tag is not synthetic provenance
    assert.equal(handle(get("pseudo")), handle(pseudoHost));
    assert.equal(handle(get("named")), handle(named));
    collect(document.documentElement);
  } else if (scenario === "cross-document") {
    const other = own(api.document.parse('<div id="other"></div>'));
    for (const method of ["appendChild", "removeChild"]) {
      assert.throws(() => root[method](other.getElementById("other")), /same document/);
    }
  } else if (scenario === "stale") {
    document.parse('<main id="fresh"></main>');
    const fresh = document.getElementById("fresh");
    for (const method of ["appendChild", "removeChild"]) {
      assert.throws(() => fresh[method](root), /reparsed/);
      assert.throws(() => root[method](fresh), /reparsed/);
    }
  } else if (scenario === "disposed") {
    document.dispose();
    for (const method of ["appendChild", "removeChild"]) assert.throws(() => root[method](left), /disposed/);
  } else { throw new Error(`Unknown scenario ${scenario}`); }
  for (const owner of owners.reverse()) owner.dispose();
  console.log(`PASS ${scenario}`);
} catch (error) {
  console.error(error);
  process.exit(1);
}
