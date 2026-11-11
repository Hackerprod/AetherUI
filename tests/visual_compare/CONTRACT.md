# C++ visual comparison contracts, version 1

This is a **contract-only** increment, not a visual renderer. The independent
C++20 target `aether_visual_compare` uses the C++ standard library. It does not
require .NET, Aether binaries, Windows headers or a WebView2 SDK to build these
contracts. Capture is **NOT IMPLEMENTED**. There are no PNG outputs, replacement
images, engine timing samples or claims of CSS, font, pixel or flexbox coverage.

The source includes six opt-in contract-test executables. Their CMake option
`AETHER_VISUAL_COMPARE_BUILD_CONTRACT_TESTS` defaults to `OFF`; enabling it only
adds build targets. Configuration/build do not register or launch tests. Source,
compilation, test execution and visual validation are separate milestones.

## Command line

- `--help` and `--version` are standalone informational commands without file I/O.
- `inspect --html PATH --resource-root PATH --manifest PATH --out PATH --viewport WIDTHxHEIGHT --scale SCALE`
- `capture` accepts exactly the same options as `inspect`.

All six options are required, each exactly once. Unknown options, duplicates,
missing values, invalid paths and invalid numeric syntax exit with code 2 and a
sanitized stderr message before file access. No implicit command, abbreviations,
network flag or permissive partial-capture mode exists. CLI file paths must be
absolute portable local paths, using forward slashes. Width and height are
unsigned decimal integers from 1 through 16384. Scale is exactly `1`, `1.25`,
`1.5` or `2`. Requested physical dimensions use half-up rounding of CSS size
multiplied by scale and cannot exceed 32768. These are requested dimensions;
effective dimensions remain unavailable.

`inspect` checks bytes, a manifest and listed resources. It does not parse HTML,
CSS, SVG or embedded data, discover references, resolve imports, decode images,
load fonts or prove that a manifest lists everything needed by a page. An empty
manifest does not prove self-containment. Success means only that the requested
contract inspection completed and its reports were committed.

`capture` validates arguments and prepares a fresh permitted output directory,
then reports `NOT_IMPLEMENTED`, normally exit 11. It does not open HTML, manifest
or resource inputs, check their existence, or initialize engines. Input sizes
and hashes are null, and input inspection stages remain `not_run`. Cancellation,
output I/O and internal failures can take precedence over exit 11.

## Resource manifest and file policy

`schema/resources-v1.schema.json` defines `aether.resources/1`. Root keys are
exactly `schema_version` and `entries`. Each of at most 1024 entries has exactly
`path`, `mime`, `size_bytes`, `sha256` and `role`. Roles are stylesheet, image,
font and other. MIME values are text/css, image/png, image/jpeg, image/gif,
image/svg+xml, font/ttf, font/otf, font/woff, font/woff2 and
application/octet-stream. A MIME is an inventory label, not a decoding promise.
Hashes are lowercase hexadecimal SHA-256 of all file bytes. Paths and keys must
be unique. SHA-256 follows the algorithm in NIST FIPS 180-4; its source tests
include standard known-answer vectors and chunked updates.

JSON must be valid UTF-8 with no comments, trailing content, duplicate object
keys, invalid Unicode or incomplete surrogate escapes. Parsing is bounded by
1 MiB per manifest, depth 8 and 4096 decoded UTF-8 bytes per string. Resource
sizes are nonnegative integer literals, at most 16 MiB each and 64 MiB in total.
The HTML limit is 2 MiB. Limits are checked before opening and while reading
bounded blocks, with cancellation checks between blocks. A matching declared
size does not replace the streaming limit. HTML must be valid UTF-8.

Resource paths have nonempty ASCII `[A-Za-z0-9_.-]` segments separated by `/`.
Dot segments, trailing dots/spaces, Windows device basenames (also with an
extension), colons, backslashes, percent escapes, absolute paths, drive paths,
UNC and URI forms are rejected. The explicit resource root must be absolute
and existing. HTML and manifest must be contained in its canonical path.
Inspected ancestors from the filesystem root and all path components are
checked with `symlink_status`: directory ancestors, regular file inputs and no
symlinks. Uncertain type, containment or permissions fail closed. Special files,
including FIFOs and devices, are not opened. Denied reads yield INPUT_UNREADABLE
without a retry or permission escalation.

This policy is for stable, user-controlled files. `std::filesystem` checks do
**not** prevent concurrent replacement or guarantee detection of every platform
reparse mechanism. `filesystem_race_hardening=not_implemented` is explicit. The
inspection is not a sandbox for adversarial content or a future capture engine.

## Reports and error aggregation

`schema/report-v1.schema.json` defines the closed report and its
`$defs/artifactMarker` defines the commit marker. `report.json` is UTF-8/LF in a
fixed field order, with finite numeric values only. `report.log` is a readable
summary of the same result, not an independent result schema. Unknown schema
versions must not be interpreted heuristically. Input paths and contents are
never copied to reports: the input label is `input.html`; resource labels are
`r0001` through `r1024`. Errors and stage reasons use only closed sanitized
codes. Exceptions are not serialized.

The fixed stage order is arguments, output_prepare, input_read, manifest_parse,
resource_validation, aether_capture, webview2_capture, comparison, coverage,
output_commit. Stage states are not_run, passed, failed, blocked,
not_implemented or cancelled. After an inspection failure, subsequent inspection
stages stay not_run; output closure may still run. Engines always expose
implementation_status=not_implemented, runtime_version=null,
capture_status=not_run, content_status=not_evaluated and metrics=null.
Comparison is not_run with null metrics. Coverage is not_evaluated with null
expected/evaluated counts. No fixture metadata is promoted to evaluated coverage.
References and font availability are not_evaluated; decoding is not_implemented.
Only the host's optional steady-clock inspection duration is recorded.

Exit codes are:

| Code | Meaning |
| --- | --- |
| 0 | Requested contract scope complete, subject to matching marker |
| 2 | Invalid arguments, without file I/O or reports |
| 3 | Dependency/ABI/platform; reserved, not evaluated here |
| 4 | Invalid, missing or unreadable input, manifest or resource |
| 5 | Unsupported render operation; reserved |
| 6 | Navigation/readiness timeout; reserved |
| 7 | Render/capture failure; reserved |
| 8 | Comparison failure; reserved |
| 9 | Coverage incomplete; reserved |
| 10 | Explicit permission/capability policy block |
| 11 | Not implemented |
| 12 | Cancelled |
| 13 | Output I/O failure |
| 14 | Internal failure |

Priority is 13, 14, 10, 12, 2, 3, 4, 5, 6, 7, 8, 9, 11, 0. The first discovered
error at the highest priority is primary. The error list preserves discovery
order and removes identical `(code, stage, resource_label, detail_code)` tuples.
Reserved engine codes do not mean that any engine operation has been run.

## Candidate reports and commit marker

The output directory must not exist, and its immediate parent must already
exist. All existing parent ancestors must be non-symlink directories. The tool
creates only that final directory, never missing ancestors; existing outputs are
not reused, deleted or cleaned. An output failure has no automatic fallback
location. A partial directory may remain.

The output protocol writes, flushes and closes `report.log.tmp` and
`report.json.tmp`, checking each operation, then promotes each report separately.
It hashes the closed final report bytes. Only after both reports are ready does
it write, flush, close and promote `manifest.json.tmp` to `manifest.json`.
The marker rename is the final filesystem operation of the publication protocol.
The marker has schema `aether.visual-compare.artifacts/1`, commit_state=complete,
run_id, requested_scope, overall_status, exit_code, and exactly the `report.log`
and `report.json` artifacts with relative name, byte size and hash. It does not
hash itself. The report's own artifacts array is empty, avoiding hash cycles.

Every report says `commit.state=requires_marker`. Temporary files and promoted
reports are candidates until the marker is validated. Consumers must reject a
missing, incomplete or inconsistent marker, and recheck both report hashes,
sizes, run ID, schema, scope and overall result. A candidate may contain
`overall.status=passed` yet provide **no valid success evidence** without its
matching marker. A marker's `complete` says the reports are committed, not that
capture or inspection succeeded.

On an output failure, stderr and exit 13 are authoritative. There is no promise
to rewrite an already promoted candidate, repair a failed commit or produce a
valid marker after a failed write. This is neither atomic publication of the
whole file set nor a guarantee of power-loss durability. Consumers always
revalidate content.

SIGINT only sets a `sig_atomic_t` flag; its handler performs no I/O. Cancellation
is sampled between phases/blocks. The last sample, immediately before report
serialization, freezes the reported result. Cancellation before that boundary
is reported as cancelled when possible. After it, the fixed result is published
or fails without a valid marker; a later signal cannot revoke a completed
result. Forceful process termination may leave no report. Tests inject a token
rather than send signals. No console-window handler is installed.

## Future visual implementation

Capture interfaces describe request generation, cancellation, approved assets
and explicit pixel-buffer ownership. They do not instantiate an adapter or
claim runtime lifecycle correctness. A future implementation must reject stale
generations and validate that captured pixels really exist. Host callbacks,
stream lifetimes and platform shutdown need independent implementation/testing.

A native visual build will still require Windows, suitable Aether and platform
SDKs, WebView2 Runtime/loader, system libraries and an explicitly identified C++
toolchain/architecture. Depending on linkage, a Visual C++ Redistributable may
be needed. Avoiding .NET does not mean a self-contained executable; CRT linkage
is not changed or chosen by this increment. Invisible capture, fresh raster
pixels, rendering support, SVG text, comparison and full fixture coverage remain
future work. The public flexbox fixture's declared 403 cases are provenance, not
a result. No claim of visual validation follows from compiling these contracts.
