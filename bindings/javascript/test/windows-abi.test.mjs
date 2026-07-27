import test from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { createWindowsApi } from "../src/desktop/windows.mjs";

test("Win32 declarations load against the real system DLLs", (t) => {
  if (process.platform !== "win32") return t.skip("Win32-only test");
  let koffi;
  try {
    koffi = createRequire(import.meta.url)("koffi");
  } catch {
    return t.skip("koffi is not installed");
  }
  const api = createWindowsApi(koffi);
  assert.ok(api.user32.fillRect);
  assert.ok(api.user32.drawText);
  assert.equal(api.ffi.sizeof(api.types.Vertex), 16);
  assert.equal(api.ffi.sizeof(api.types.WindowClass), process.arch === "ia32" ? 48 : 80);
});
