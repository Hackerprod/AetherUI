# Frozen ancestor-ID v3 experiment harnesses

These files implement the already reviewed v3 schedule. They do not establish a
performance PASS. Keep engine probes, counted copies, sanitizer binaries and all
smoke observations outside both timed campaigns. No Chrome, Windows, raster or
font-shaping comparison is represented.

## Fixed files

`frozen_protocol.json`, `control_inputs_manifest.json` and all 20 files under
`control-inputs/` are unchanged copies of the preregistered v3 artifacts. The
manifest's original artifact paths are provenance references; the tools resolve
their basenames under `control-inputs/`. Do not rewrite the manifest.

- P invokes the unchanged `aether_flexbox_page_test` for each fresh process. Its
  original fixture and pipeline/report definitions remain intact. Destruction
  is included in complete process elapsed time but not independently observed.
- `public_control.cpp` runs W1440 and S00–S16. It includes the original test's
  public helpers with its main renamed, and adds only the prescribed control
  entry point. Setup/initial resolution, mutations, readback, validation and frees
  are outside resolver timers. W has five warmups/eight measured resolutions;
  S has five/sixteen. It records every call and verifies real nonzero updates,
  toggled root opacity and stable target widths. S13 verifies the prescribed
  alternating 17px/auto result after ID/reparent changes.
- `private_control.cpp` runs Q00–Q02 with runtime-loaded DOM and selector text.
  Each complete operation constructs a new context, makes exactly one query,
  and destroys the context before the end timestamp. Initial negative, separate
  fresh-context positive sanity, five negative warmups and sixteen negative
  measured operations are distinct. Every measured result is false and the
  false-count signature is sixteen. Candidate builds enable the context via its
  boolean constructor; baseline builds use the original default constructor.

Q is a private matcher target, not the shared public pipeline. Its parser and
matcher are separate translation units compiled with Release flags and no LTO.
The shared public library is used for ordinary DOM input setup. Do not compile B
headers against A's matcher: each arm must use its own source include paths.

## Build and smoke

First build standard Release shared A/B outside this tool, retaining exact trees,
compile commands, compiler/linker versions and binary hashes. Then, from a repo:

```sh
python3 native/tests/ancestor_id_filter/build_harnesses.py \
  --source BASELINE_REPO --library-dir BASELINE_BUILD --output NEW_HARNESS_A
python3 native/tests/ancestor_id_filter/build_harnesses.py \
  --source CANDIDATE_REPO --library-dir CANDIDATE_BUILD --output NEW_HARNESS_B --candidate
python3 native/tests/ancestor_id_filter/verify_controls.py \
  --A NEW_HARNESS_A --B NEW_HARNESS_B --fixture ORIGINAL_FIXTURE --output NEW_SMOKE_DIRECTORY
python3 -m unittest discover -s native/tests/ancestor_id_filter -p test_protocol_tools.py -v
```

`build_harnesses.py` compiles only these small harnesses, not the core. It records
commands, compiler version, flags, source and binary hashes in the private output
folder. The public original harness binary must come from each standard build.
Use a fresh output folder on every diagnostic revision; preserve failures.

Smoke verifies 21 controls on both arms, with equal semantic signatures of target
widths, root opacity, update counts, private booleans and operation counts. These
signatures are stronger than checking successful exits, but they do not replace
the separately required complete public style/display equivalence at all three
viewports. Smoke durations are not campaign samples and must not be compared as
performance evidence.

For counted S/Q verification, `generate_counted_controls.py OUTPUT` makes external
copies using `ancestor_id_counted/counters.h`. Link the public copy against the
separate counted shared library, and the private copy against the generated
counted matcher plus selector parser. `verify_counted_controls.py --harnesses DIR
--output NEW_DIR` checks all 20 inputs and stores per-operation counts. These
binaries are never eligible as A/B binaries. The counts cover shape admission,
exact RHS counts, early positive visits, first-negative continuation, bounded
collection and copies, not heap allocation accounting. The separate focused
allocator tests remain mandatory.

## Campaign configuration

Use a private JSON configuration, not tracked runtime logs. Required shape:

- `fixture`: exact original HTML path; SHA-256 is verified by the runner.
- `arms.A` and `.B`: paths `P`, `public`, `private`; matching `sha256` object
  containing all three binary hashes; optional `env` dictionary for loader paths.
- `expected_P`: P1440/P390/P768 objects with `geometry_sha256` and `counters`.
  Required counters: dom_nodes, case_inventory, css_rules, style_updates,
  layout_boxes, display_commands, layout_text_measure_calls,
  layout_text_measure_bytes, paint_text_measure_calls, paint_text_measure_bytes.
  Obtain them from independently validated baseline runs, never fill from a
  failing candidate. Additional expected counters can also be required.
- `expected_controls`: W1440/S00–S16/Q00–Q02 semantic signatures from the verified
  A/B smoke output `expected_controls.json`.
- `readiness`: explicit true values for independent_candidate_review_pass,
  semantic_equivalence_pass, counted_controls_pass, scratch_memory_pass,
  sanitizers_pass, no_local_heavy_work and parent_campaign_authorized. These are
  attestations backed by the corresponding separate evidence, not tests that the
  runner can perform automatically.
- Include source identities, shared library/object hashes, environment identity,
  versions/flags, exact independent review and validation evidence in additional
  configuration fields. The entire configuration is preserved verbatim in the
  campaign folder.

```sh
python3 native/tests/ancestor_id_filter/run_campaigns.py --config CONFIG.json --output NEW_CAMPAIGNS
# Prints the complete plan without executing the engine.
python3 native/tests/ancestor_id_filter/run_campaigns.py --config CONFIG.json --output NEW_CAMPAIGNS --execute
python3 native/tests/ancestor_id_filter/analyze_campaigns.py NEW_CAMPAIGNS
```

The runner refuses an existing output folder. It executes exactly 1080 measured
processes and 192 excluded warmups, with fixed campaign/cell/block order and a
30-second timeout per process. It retains stdout/stderr, reports, geometry, status,
commands, timestamps, semantic signatures and all warmup observations. Failure or
missing work stops without retry. Never delete, resume or replace an affected
campaign. A new execution requires the parent-approved new preregistration.

The analyzer validates complete schedule identity, treats processes as units,
uses the fixed 10000 block bootstrap draws/seed/percentiles and reports each
campaign independently. All required point and interval gates must pass; no
pooling, trimming, extension, substituted estimator or slow-pair rerun is allowed.
It refuses to overwrite an existing analysis. Its unit tests use synthetic
numbers only and do not run an engine benchmark.
