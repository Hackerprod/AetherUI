import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";

// Opt-in is explicit: once requested, missing koffi/library/ABI is a failure,
// never a skip or a mock substitute. Each case gets a separate native process.
for (const scenario of ["id-order", "hierarchy", "synthetic", "cross-document", "stale", "disposed"]) {
  test(`real native DOM: ${scenario}`, {
    skip: process.env.AETHERUI_TEST_NATIVE !== "1" && "set AETHERUI_TEST_NATIVE=1 to run against the real native library",
  }, () => {
    const result = spawnSync(process.execPath, [
      fileURLToPath(new URL("../scripts/dom-native-case.mjs", import.meta.url)), scenario,
    ], { encoding: "utf8", timeout: 30000, env: process.env });
    assert.equal(result.error, undefined, result.error?.message);
    assert.equal(result.signal, null, `native process signal: ${result.signal}\n${result.stderr}`);
    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
  });
}
