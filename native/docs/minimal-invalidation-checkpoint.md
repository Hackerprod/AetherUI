# Minimal invalidation checkpoint

This checkpoint covers a bounded invalidation repair: immutable previous/final evidence, conservative source accounting, four impact channels, classifier/catalog checks, allocation/lifetime checks and the selected renderer/layout/scroll sentinels. It does not close the broader document/rendering milestone.

## Current focused CMake gate

The current `aether_style_diff_classifier_test` target and
`aether.style-diff-classifier` CTest entry use the inventory20 successor. It
preserves the original cases and adds independent `background_gradient` coverage.
The original19 source and the script below remain byte-identical historical
checkpoint inputs; the script retains its intentional exit-1 contract and is not
the current acceptance gate. The historical results below are not rewritten.

From the repository root, choose fresh build directories. Compilation and linking
must succeed before CTest executes the classifier and catalog:

```sh
cmake -S native -B build-classifier-debug -DAETHERUI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-classifier-debug --target aether_style_diff_classifier_test aether_style_diff_catalog_test
ctest --test-dir build-classifier-debug --output-on-failure -R '^aether\.(style-diff-classifier|style-diff-catalog)$'
cmake -S native -B build-classifier-release -DAETHERUI_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O3 -UNDEBUG' -DCMAKE_C_FLAGS_RELEASE='-O3 -UNDEBUG'
cmake --build build-classifier-release --target aether_style_diff_classifier_test aether_style_diff_catalog_test
ctest --test-dir build-classifier-release --output-on-failure -R '^aether\.(style-diff-classifier|style-diff-catalog)$'
```

The Release flags keep assertions active. On Linux the CMake classifier retains
malloc wrapping. A focused PASS does not certify the full test suite or change
the historical checkpoint outcome.

## Prerequisites

Use a complete checkout containing the native sources, tests and this script. The reviewed environment was Linux x86_64, Python 3.12.14, GCC 14.2.0 and GNU ld 2.44. Python's standard library is sufficient; no pip installation is required. The compiler must support C++20 and pthreads, and the linker must support GNU --wrap used by the allocation tests. GCC is the validated compiler; --cxx is configurable, not a claim of Clang or Windows validation.

The direct-build recipe does not invoke CMake, but reads the aether_core source list from native/CMakeLists.txt. It builds all 71 core translation units and three renderer supports from source, without inherited objects, overlays or external font assets. Do not edit the source tree during execution. Each subprocess has a 300-second timeout; this is not an overall runtime estimate.

## Run from the repository root

Choose a new output directory outside native; the script refuses an existing directory. The parent may already exist. For example, use a fresh RUN_ID for each invocation:

    RUN_ID=local-001
    python3 native/scripts/check_minimal_invalidation.py \
      --out "$PWD/artifacts/minimal-invalidation-$RUN_ID"

The example directory is local evidence, not a publication artifact. No network connection, CI activation or installed AetherUI binary is required by this command. The root is derived from the script location; there are no private workspace dependencies. To select another compiler executable, add --cxx /path/to/g++; this does not broaden validated toolchain support.

## Interpret the outcome, not just the process exit

The reviewed candidate has 25 profiles: 24 compile/link/run profiles pass, while the byte-intact original style_diff_classifier fails compilation at its historical 20-versus-19 static assertion. Its link/run stages are NOT_RUN. The inventory20 successor runs separately in normal and malloc-wrap profiles; it does not make the original a passing test. The original catalog remains intact and must pass.

The recipe intentionally returns exit 1 even after completing all selected executions. It reports PENDING_INDEPENDENT_REVIEW, not an aggregate process PASS. An independent technical acceptance does not rewrite that recorded status or turn this into an exit-0 CI command. Do not suppress exit 1 with “|| true” and describe the job as passed.

Inspect these files in the output directory:

- summary.json: completed_selected_executions must be true, gate PENDING_INDEPENDENT_REVIEW, primary_failure null and finalization_errors empty for the reviewed completed outcome.
- matrix-final-derived.json: 24 PASS profiles and one BASELINE_COMPILE_FAIL for the original classifier; compile/link/run are separate. matrix-before-execution.json is only the initial plan.
- source-integrity-final.json: unchanged must be true.
- results.json and per-step logs: actual argv, statuses and return codes. Codes that do not exist remain null. provenance.json and toolchain.log identify the recipe, invocation, flags and compiler.
- source-hashes.json: source identities captured before execution.

Any other failure, unrecognized classifier diagnostic, unexpected original-classifier compile success, timeout, launch error, interruption, missing profile or failed integrity check blocks completion. Stop and investigate; do not change assertions, skip tests or treat NOT_RUN as PASS. A missing final summary after process termination is incomplete evidence, not success. Forced termination or machine shutdown may prevent finalization.

## Patch provenance

The reviewed extraction starts from main at 40c246df59daebe063666665528ed71d6b550d53, not from wholesale replacement with the broader experimental candidate. It limits product changes to seven style-runtime paths: style_runtime_core.cpp, style_source_accounting.h, style_source_compiler.cpp, typography_evidence.h, typography_record.h, typography_composition.h and style_diff.h. The first six carry the bounded evidence/accounting repair. The seventh separately repairs the dependency catalog by adding the already registered direction and writing-mode entries; it is a product correction, not merely a test adjustment.

The intact original classifier has a stale inventory assertion. A separately reviewed inventory20 successor preserves its checks and extends explicit sidecar coverage; it is not a rewrite of the original. The recipe repair adds this successor's normal/wrap profiles and the existing CMake-required private include for auto_margins. Neither recipe change imports documentary layout or weakens assertions. This provenance identifies the reviewed local base and scope; it does not assert that the same commit is still current main or that these patches have been published.

## Limits and publication hygiene

This recipe reproduces the candidate checkpoint, not the historical main/subset experiment automatically. A failure must not be attributed to main without an equivalent baseline. The accepted local evidence is tied to the exact source bytes and toolchain used; changes during integration require appropriate revalidation.

No certification is provided here for full CMake configuration/build, the installed shared library, Windows, documentary consumers, full-page rendering, bindings or browser parity. The document-layout Y8/Y20 contradiction remains open outside this increment. Passive BreakPolicy evidence does not add CSS word-break/overflow-wrap support.

Do not publish output directories, objects, executables, raw local logs or private records. Preparing publication separately requires checking the current main and final diff, preserving later work, reviewing secrets and resource licenses, and verifying the exact published commit with a subsequent checkout reproduction. Local technical acceptance alone is not publication authorization and does not enable Actions.
