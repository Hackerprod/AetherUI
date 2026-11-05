import assert from "node:assert/strict";
import test from "node:test";
import path from "node:path";
import {
  CAPABILITIES,
  DEFAULT_CAPABILITIES,
  SUPPORTED_ABI,
  checkAbiCompatibility,
  parseAbiVersion,
  resolveNativeLibraryCandidates,
  validateComputedStyleSize,
} from "../src/ffi.mjs";

test("decodes the packed ABI version", () => {
  const version = (0 << 24) | (3 << 16) | 7;
  assert.deepEqual(parseAbiVersion(version), { version, major: 0, minor: 3, patch: 7 });
});

const packAbi = ({ major, minor, patch }) => (major << 24) | (minor << 16) | patch;

test("accepts only the supported ABI profile and ignores unknown capability bits", () => {
  const abi = checkAbiCompatibility(
    packAbi(SUPPORTED_ABI),
    DEFAULT_CAPABILITIES | (1n << 40n),
    256,
  );
  assert.equal(abi.major, 0);
  assert.equal(abi.minor, SUPPORTED_ABI.minor);
  assert.equal(abi.patch, SUPPORTED_ABI.patch);
  assert.equal(abi.capabilities & (1n << 40n), 1n << 40n);
});

for (const version of [
  { major: 0, minor: 33, patch: 0 },
  { major: 0, minor: 35, patch: 0 },
  { major: 0, minor: 34, patch: 1 },
  { major: 1, minor: 34, patch: 0 },
]) {
  const label = `${version.major}.${version.minor}.${version.patch}`;
  test(`rejects unverified ABI ${label}, including an expectedAbi override`, () => {
    for (const expectedAbi of [undefined, version]) {
      assert.throws(
        () => checkAbiCompatibility(packAbi(version), DEFAULT_CAPABILITIES, 256, { expectedAbi }),
        /Unsupported AetherUI ABI.*supports exactly 0\.34\.0/,
      );
    }
  });
}

test("expectedAbi can restrict the known profile, never replace it", () => {
  for (const expectedAbi of [SUPPORTED_ABI, { minor: 34 }, {}]) {
    assert.doesNotThrow(() => checkAbiCompatibility(packAbi(SUPPORTED_ABI), DEFAULT_CAPABILITIES, 256, { expectedAbi }));
  }
  for (const expectedAbi of [{ major: 1 }, { minor: 33 }, { minor: 35 }, { patch: 1 }]) {
    assert.throws(
      () => checkAbiCompatibility(packAbi(SUPPORTED_ABI), DEFAULT_CAPABILITIES, 256, { expectedAbi }),
      /does not match expectedAbi/,
    );
  }
});

test("rejects a missing required capability", () => {
  assert.throws(
    () => checkAbiCompatibility((SUPPORTED_ABI.major << 24) | (SUPPORTED_ABI.minor << 16), CAPABILITIES.DOM, 256),
    /missing required capability bits/,
  );
});

test("rejects a computed-style layout mismatch", () => {
  validateComputedStyleSize(512, 512);
  assert.throws(() => validateComputedStyleSize(511, 512), /computed style size 511/);
  assert.throws(() => validateComputedStyleSize(513, 512), /computed style size 513/);
  for (const invalid of [0, -1, NaN, Infinity, 1.5]) {
    assert.throws(() => validateComputedStyleSize(invalid, 512), /invalid computed style size/);
  }
});

test("resolves the installed-prefix layout without touching the build tree", () => {
  const candidates = resolveNativeLibraryCandidates({
    platform: "win32",
    arch: "x64",
    packageRoot: "C:\\package",
    installPrefix: "C:\\aether",
  });
  assert.equal(candidates[0], path.join("C:\\aether", "bin", "aether_core.dll"));
  assert.ok(candidates.includes(path.join("C:\\package", "native", "win32-x64", "aether_core.dll")));
  assert.ok(!candidates.some((candidate) => candidate.includes("build")));
});
