import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import test from "node:test";

for (const scenario of ["binary-layout", "runtime-copies", "measurement", "callback-throw", "callback-shim", "initialization-guards"]) {
  test(`real native ComputedStyle: ${scenario}`, {
    skip: process.env.AETHERUI_TEST_NATIVE !== "1" && "set AETHERUI_TEST_NATIVE=1 to test the real ABI",
  }, () => {
    const result = spawnSync(process.execPath, [
      fileURLToPath(new URL("../scripts/computed-style-native-case.mjs", import.meta.url)), scenario,
    ], { encoding: "utf8", timeout: 45000, env: process.env });
    assert.equal(result.error, undefined, result.error?.message);
    assert.equal(result.signal, null, `native process signal: ${result.signal}\n${result.stderr}`);
    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
  });
}
