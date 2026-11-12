# T1.1 native text: approved design, implementation blocked

## Status

This checkpoint records the independently reviewed T1.1 design. It does **not**
implement a text provider or enable native text. No text PNG has been produced.
The existing software rasterizer remains unchanged and continues rejecting text.

Read-only preflight on 2026-11-12 found neither HarfBuzz nor FreeType development
metadata or headers. No dependency installation was attempted for this task.
No fake provider, copied system headers, or mock test is a substitute for the
required real HTML-to-image acceptance test.

Run `python3 tests/visual_compare/scripts/check_native_text_dependencies.py`.
Exit 2 means blocked. Exit 0 means only expected pkg-config metadata is present,
not that the provider compiles, renders, or passes. The command never installs
anything and always records rendering as NOT_RUN.

## Reviewed implementation contract

- Explicit native mode, mutually exclusive with contextual and legacy callbacks.
  Conflicts fail rather than silently choosing a provider. Legacy mode is unchanged.
- A native measurement returns metrics and a strong reference to an immutable
  ShapedRun. Both fragment-building routes, direct text boxes, layout snapshot
  clones and display-list rebuilds must retain it. Audit every route before enabling.
- Each text layout box stores the run and relative baseline. Paint sidecar entries
  are indexed with their commands and retain run, font instance and absolute baseline.
  Native capture reads commands and sidecar together; the public C command ABI alone
  cannot recover the run. Preparation must not infer identity from matching metrics.
- Changed typographic style requires relayout. Color-only changes may reuse the run.
  Copies, destruction and visual-update paths must preserve or invalidate the sidecar
  explicitly, never retain stale command indices.
- Layout positions each text fragment with its recorded ascent; baseline is box top
  plus run ascent. CSS line height remains a layout decision, not bitmap height.
  Painting must neither reshape nor remeasure.
- A font instance includes exact bytes/hash, face index, size and raster configuration.
  hb-ft and FreeType share that instance on the owner thread without concurrent mutation.
  Reset FreeType transforms before shaping; use consistent no-hinting/no-embedded-bitmap
  load flags. Coordinates are 26.6, with explicit tested rounding for negatives and
  fractional-phase carry, and one Y-axis conversion. Final phase must be in cache keys.
- FreeType creates grayscale glyph masks; Aether composites them using its own surface
  and source-over implementation. This is **not** a wholly original typography engine.
- Initial text clips must be integer-aligned rectangles. Reject fractional/curved text
  clips rather than multiplying independent antialiased coverages and claiming exactness.
- Bound masks, caches, allocations and work; frames retain resources after cache eviction.
  Any preparation/raster failure preserves the previously committed frame.

## Deliberate initial scope

Real HTML with horizontal simple Latin, regular/bold fixed fonts, left/start alignment,
normal/nowrap/pre whitespace and wrapping through existing layout. Reject unsupported
spacing, justification, bidi, fallback, variable/color fonts, synthetic styles, vertical
writing and text effects. Do not silently replace fonts or omit missing glyphs.
Fragment-level shaping is not general cross-node/script shaping.

## Dependency and distribution gate

Reviewed targets: HarfBuzz 15.1.0 and FreeType 2.15.0; Noto Sans regular/bold test fonts.
Before materialization/build, record exact release/file hashes, build options, source
notices and font hashes/licenses. Use FreeType's FTL option, not an assumed blanket
license; preserve relevant notices and font OFL. Version strings alone do not establish
integrity or distribution compliance. Dependencies require an authorized installation
path; this checkpoint supplies none and does not bypass environment restrictions.

## Acceptance gate (not run)

1. Compile/link actual provider and generate PNG from HTML paragraphs, wrapping and flex.
2. Counter proves zero shaping/measurement during painting; run/font identity is retained.
3. Exercise both fragmentation paths, snapshot clones, list copies/rebuilds and destruction
   of DOM/layout; eviction cannot invalidate a prepared frame.
4. Verify baseline, spaces, bold, clipping, missing glyphs, invalid UTF-8 and explicit rejects.
5. Reopen real PNG, report shaping/layout/mask preparation/composition separately.
6. Inventory remaining unsupported commands in flexbox_runtime_validation.html.
7. Independent code and execution review; Chromium comparison only if actually executed.

A read-only dependency report or dependency-free contract tests cannot satisfy this gate.
