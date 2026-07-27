import assert from "node:assert/strict";
import test from "node:test";
import {
  CAPABILITIES,
  DEFAULT_CAPABILITIES,
  SUPPORTED_ABI,
  checkAbiCompatibility,
  parseAbiVersion,
  resolveNativeLibraryCandidates,
} from "../src/ffi.mjs";

test("decodes the packed ABI version", () => {
  const version = (0 << 24) | (3 << 16) | 7;
  assert.deepEqual(parseAbiVersion(version), { version, major: 0, minor: 3, patch: 7 });
});

test("accepts a newer minor ABI and ignores unknown capability bits", () => {
  const abi = checkAbiCompatibility(
    (SUPPORTED_ABI.major << 24) | (SUPPORTED_ABI.minor + 1 << 16),
    DEFAULT_CAPABILITIES | (1n << 40n),
    256,
  );
  assert.equal(abi.major, 0);
  assert.equal(abi.minor, SUPPORTED_ABI.minor + 1);
  assert.equal(abi.capabilities & (1n << 40n), 1n << 40n);
});

test("rejects a missing required capability", () => {
  assert.throws(
    () => checkAbiCompatibility((SUPPORTED_ABI.major << 24) | (SUPPORTED_ABI.minor << 16), CAPABILITIES.DOM, 256),
    /missing required capability bits/,
  );
});

test("resolves the installed-prefix layout without touching the build tree", () => {
  const candidates = resolveNativeLibraryCandidates({
    platform: "win32",
    arch: "x64",
    packageRoot: "C:\\package",
    installPrefix: "C:\\aether",
  });
  assert.equal(candidates[0], "C:\\aether\\bin\\aether_core.dll");
  assert.ok(candidates.includes("C:\\package\\native\\win32-x64\\aether_core.dll"));
  assert.ok(!candidates.some((candidate) => candidate.includes("build")));
});
