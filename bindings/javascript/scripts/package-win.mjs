import { cpSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync, copyFileSync, statSync } from "node:fs";
import { dirname, isAbsolute, join, relative, resolve, sep } from "node:path";
import { execFileSync } from "node:child_process";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);
const packageRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const fuse = "NODE_SEA_FUSE_fce68041b7e0b5f5c1e13c7d3d8f8f9e";

function readArguments(argv) {
  const values = {};
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (!argument.startsWith("--")) throw new Error(`Unknown positional argument: ${argument}`);
    const separator = argument.indexOf("=");
    if (separator !== -1) {
      values[argument.slice(2, separator)] = argument.slice(separator + 1);
    } else {
      const key = argument.slice(2);
      const value = argv[index + 1];
      if (!value || value.startsWith("--")) throw new Error(`Missing value for --${key}.`);
      values[key] = value;
      index += 1;
    }
  }
  return values;
}

function required(values, name) {
  const value = values[name];
  if (!value) throw new Error(`Missing required option --${name}.`);
  return value;
}

function isInside(candidate, parent) {
  const normalizedCandidate = resolve(candidate);
  const normalizedParent = resolve(parent);
  return normalizedCandidate === normalizedParent || normalizedCandidate.startsWith(`${normalizedParent}${sep}`);
}

function copyTree(source, destination, excludedRoot = null) {
  mkdirSync(destination, { recursive: true });
  cpSync(source, destination, {
    recursive: true,
    force: true,
    errorOnExist: false,
    filter: (candidate) => !excludedRoot || !isInside(candidate, excludedRoot),
  });
}

function findPostjectCli() {
  let packageJson;
  try {
    packageJson = JSON.parse(readFileSync(require.resolve("postject/package.json"), "utf8"));
  } catch (error) {
    throw new Error("The Windows packager requires the dev dependency 'postject'. Run npm install first.", { cause: error });
  }
  const bin = typeof packageJson.bin === "string" ? packageJson.bin : packageJson.bin?.postject;
  if (!bin) throw new Error("The installed postject package does not expose a CLI.");
  const cli = resolve(dirname(require.resolve("postject/package.json")), bin);
  if (!existsSync(cli)) throw new Error(`The postject CLI was not found at ${cli}.`);
  return cli;
}

function generateBootstrap(entryRelative) {
  const encodedEntry = JSON.stringify(entryRelative.split(sep).join("/"));
  return `
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const applicationRoot = path.join(path.dirname(process.execPath), "resources", "app");
process.env.AETHERUI_INSTALL_PREFIX = path.join(path.dirname(process.execPath), "resources", "native");
const entry = path.join(applicationRoot, ${encodedEntry});

(async () => {
  try {
    await import(pathToFileURL(entry).href);
  } catch (error) {
    console.error(error?.stack ?? error);
    process.exitCode = 1;
  }
})();
`;
}

function main() {
  if (process.platform !== "win32") throw new Error("package-win.mjs must run on Windows with a Windows Node executable.");
  const values = readArguments(process.argv.slice(2));
  const entry = resolve(required(values, "entry"));
  const appDirectory = resolve(values["app-dir"] ?? dirname(entry));
  const outputDirectory = resolve(required(values, "out"));
  const name = values.name ?? "AetherUIApp";
  const nativeLibrary = resolve(values.native ?? join(packageRoot, "..", "..", "artifacts", "native", "win-x64", "bin", "aether_core.dll"));
  const nodeExecutable = resolve(values.node ?? process.execPath);
  if (!existsSync(entry) || !statSync(entry).isFile()) throw new Error(`Application entry was not found: ${entry}`);
  if (!existsSync(nativeLibrary)) throw new Error(`Native library was not found: ${nativeLibrary}`);
  if (!existsSync(nodeExecutable)) throw new Error(`Node executable was not found: ${nodeExecutable}`);
  if (isInside(appDirectory, outputDirectory)) throw new Error("--out cannot contain --app-dir; choose a sibling output directory.");
  const appRelativeEntry = relative(appDirectory, entry);
  if (appRelativeEntry === ".." || appRelativeEntry.startsWith(`..${sep}`) || isAbsolute(appRelativeEntry)) {
    throw new Error("--entry must be inside --app-dir so the portable bundle can include it.");
  }

  rmSync(outputDirectory, { recursive: true, force: true });
  const resources = join(outputDirectory, "resources");
  const appTarget = join(resources, "app");
  const nativeTarget = join(resources, "native", "bin");
  mkdirSync(outputDirectory, { recursive: true });
  copyTree(appDirectory, appTarget, outputDirectory);
  mkdirSync(nativeTarget, { recursive: true });
  copyFileSync(nativeLibrary, join(nativeTarget, "aether_core.dll"));

  const bindingTarget = join(appTarget, "node_modules", "@aetherui", "javascript");
  copyTree(packageRoot, bindingTarget);
  rmSync(join(bindingTarget, "node_modules"), { recursive: true, force: true });
  rmSync(join(bindingTarget, "test"), { recursive: true, force: true });
  rmSync(join(bindingTarget, "scripts"), { recursive: true, force: true });

  const koffiSource = join(packageRoot, "node_modules", "koffi");
  if (!existsSync(koffiSource)) throw new Error("The local koffi dependency is missing; run npm install first.");
  copyTree(koffiSource, join(appTarget, "node_modules", "koffi"));

  const stage = join(outputDirectory, ".sea");
  mkdirSync(stage, { recursive: true });
  const bootstrap = join(stage, "bootstrap.cjs");
  const blob = join(stage, "app.blob");
  const config = join(stage, "sea-config.json");
  writeFileSync(bootstrap, generateBootstrap(appRelativeEntry), "utf8");
  writeFileSync(config, JSON.stringify({ main: bootstrap, output: blob, disableExperimentalSEAWarning: true }, null, 2), "utf8");
  execFileSync(nodeExecutable, ["--experimental-sea-config", config], { stdio: "inherit" });

  const executable = join(outputDirectory, `${name.endsWith(".exe") ? name : `${name}.exe`}`);
  copyFileSync(nodeExecutable, executable);
  const postject = findPostjectCli();
  execFileSync(process.execPath, [postject, executable, "NODE_SEA_BLOB", blob, "--sentinel-fuse", fuse], { stdio: "inherit" });
  rmSync(stage, { recursive: true, force: true });
  console.log(`Portable Windows application written to ${executable}`);
}

try {
  main();
} catch (error) {
  console.error(error?.stack ?? error);
  process.exitCode = 1;
}
