import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { mkdtempSync, rmSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

// No Aether calls: a small instrumented C allocator verifies ownership. Opt-in
// requires a C compiler and real Koffi; missing prerequisites are test failures.
test("FFI strings: raw owned pointers, borrowed snapshots, exact lifetime counts", {
  skip: process.env.AETHERUI_TEST_FFI !== "1" && process.env.AETHERUI_TEST_NATIVE !== "1" && "set AETHERUI_TEST_FFI=1 (or AETHERUI_TEST_NATIVE=1)",
}, () => {
  const directory = mkdtempSync(path.join(os.tmpdir(), "aether-strings-"));
  const library = path.join(directory, process.platform === "win32" ? "strings.dll" : process.platform === "darwin" ? "strings.dylib" : "strings.so");
  const fixture = fileURLToPath(new URL("../scripts/fixtures/string-ownership.c", import.meta.url));
  const include = fileURLToPath(new URL("../../../native/include", import.meta.url));
  try {
    const build = spawnSync(process.env.CC ?? "cc", [
      "-shared", ...(process.platform === "win32" ? [] : ["-fPIC"]), "-Wall", "-Wextra", "-Werror", "-I", include, fixture, "-o", library,
    ], { encoding: "utf8", timeout: 30000 });
    assert.equal(build.error, undefined, build.error?.message);
    assert.equal(build.signal, null, build.stderr);
    assert.equal(build.status, 0, `${build.stdout}\n${build.stderr}`);
    const result = spawnSync(process.execPath, [fileURLToPath(new URL("../scripts/ffi-strings-case.mjs", import.meta.url)), library], {
      encoding: "utf8", timeout: 30000, env: process.env,
    });
    assert.equal(result.error, undefined, result.error?.message);
    assert.equal(result.signal, null, `native shim signal: ${result.signal}\n${result.stderr}`);
    assert.equal(result.status, 0, `${result.stdout}\n${result.stderr}`);
    console.log(result.stdout.trim());
  } finally { rmSync(directory, { recursive: true, force: true }); }
});
