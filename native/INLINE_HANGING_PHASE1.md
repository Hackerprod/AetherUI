# Pre-wrap hanging spaces: phase 1

This change shares incremental line decisions between intrinsic height and final
inline geometry. It keeps each original measured fragment, owner and style, and
reuses its metrics at line closure. It does not change the public ABI.

## Line decisions

- P is the natural physical advance. H is the trailing hanging advance. Alignment
  uses A = P - H. Fit always uses P plus the complete next indivisible unit.
- Preserved pre-wrap spaces do not wrap solely because their own width overflows.
  The break opportunity follows the group. A following nowrap/pre unit can move
  as a whole, including across style runs; it cannot break internally.
- At a soft break the trailing pre-wrap suffix hangs in full. At a forced break
  or paragraph end only its excess beyond available width hangs. PRE stops the
  suffix scan. All accepted spaces still contribute height and baseline metrics.
- Leading and whitespace-only preserved runs remain real content. EOF does not
  add an empty line. Justification uses interior eligible whitespace and excludes
  the trailing suffix; forced and final lines are not justified.
- Measurements remain ordered within each pass. Looking ahead into the next
  indivisible unit is allowed. Closure invokes no measurement callbacks. Atomic
  layout happens once at its source position and its tree is translated if moved.

## Conservative admission

The new path is limited to horizontal LTR ASCII text with at least one pre-wrap
run. It falls back for the **entire collected sequence**, before measurement,
for tabs/other non-ASCII content, flattened inline horizontal border/padding
(including decorated empty elements), and out-of-flow block static-position
markers. Ordinary links are not excluded. There is no new RTL, Unicode line
breaking, tab, or vertical-align implementation.

CSS trailing collapse happens before hanging in Blink. This phase does not
replace AetherUI's existing collapse algorithm. Mixed normal/pre-line whitespace
and leading/trailing nowrap whitespace therefore select the complete legacy
path. A nowrap space is admitted only when it has content both before and after
within the same contiguous nowrap unit, without a forced break. PRE preserved
spaces are not collapsible. This is an intentional limitation, not a claim that
all mixed white-space cases are now corrected.

## Scroll is physical in this phase

H affects line closure and alignment only. Physical text rectangles are never
trimmed to A for painting, scroll, clipping, or transformation. Existing scroll
projection continues to consume full rectangles, also when later building with
styles or runtime presentation on the same retained result.

For 10px characters, 20px line height, and 50px available width:

| Input | Height | First-line P/H/A | Horizontal overflow |
| --- | --- | --- | --- |
| `aa    bb` (start) | 40 | 60/40/20 | 10 |
| `aa    bb` (center) | 40 | 60/40/20 | 25 |
| `aa    bb` (end) | 40 | 60/40/20 | 40 |
| `aa    ` at EOF | 20 | 60/10/50 | 10 |
| `aa    <br>bb` | 40 | 60/10/50 | 10 |

This can introduce a horizontal scrollbar where the former incorrect extra line
avoided one. CSS allows scrollable hanging spaces when useful, but this is **not
Blink's ink-only scrollbar behavior or a full PASS of WPT 021/022**. A later phase
must design any partial-overflow exclusion separately in two dimensions.

## Sources and validation

- [CSS Text 3 hanging spaces](https://www.w3.org/TR/css-text-3/#hanging)
- [CSS white-space phase 2](https://www.w3.org/TR/css-text-3/#white-space-phase-2)
- [Blink LineInfo suffix/alignment](https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/inline/line_info.cc)
- [Blink LineBreaker trailing collapse](https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/layout/inline/line_breaker.cc)
- [WPT trailing spaces 021](https://github.com/chromium/chromium/blob/main/third_party/blink/web_tests/external/wpt/css/css-text/white-space/white-space-pre-wrap-trailing-spaces-021.html)
- [WPT trailing spaces 022](https://github.com/chromium/chromium/blob/main/third_party/blink/web_tests/external/wpt/css/css-text/white-space/white-space-pre-wrap-trailing-spaces-022.html)

Native tests cover geometry and physical overflow separately, baseline envelopes,
contextual callbacks, mutation/invalid metrics, allocation failures, retained
results, and normal/with-styles/runtime builders under affine transforms and clips.
Source comparison is not a claim of an executed Chromium differential run.
