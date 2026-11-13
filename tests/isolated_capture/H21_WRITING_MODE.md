# H21 capture profile: vertical flex geometry

The native sidecar stores horizontal-tb, vertical-rl and vertical-lr independently
of physical width/height. Capture admission is deliberately narrower than native
CSS parsing: only these three literal lowercase writing-mode values are admitted,
not CSS-wide keywords, escapes, variables, aliases or sideways modes.

Any authored writing-mode activates the closed H20 property/value contract across
all declarations, including inactive and unmatched rules. H20 numeric bounds and
physical property spellings are unchanged. No generic acceptance of unknown CSS.

Computed vertical boxes require definite physical pixel width and height and pixel
margins. Only shallow nowrap flex containers with empty block children of the same
computed writing-mode are proved. Orthogonal children, vertical block/root layout,
nested flex, hidden children, contents, text, generated/replaced content, intrinsic
or percentage dimensions, auto margins, scrolling and effects remain rejected.
Horizontal declarations retain previous horizontal geometry behavior. Canvas
capture remains rejected whenever writing-mode is authored. The original fixture
is not rewritten, and rejected captures must never emit a success PNG.

`test_h21.py BINARY OUTPUT` checks the 24 mode/direction/flex combinations plus
fail-closed source and computed adversaries. Native layout tests supply independent
coordinate and sizing assertions. Chromium execution is NOT_RUN.

## Executed capture validation (2026-11-13)

Clean-build CLI: H21 46 checks; H20 1219 checks. Historical CLI regressions:
H3 134, H5 58, H6 44, H7 43, H8 68, H9 70, H10 44, H11 55,
H13 38 scenarios / 59 CLI runs, H14 64, H15 32 scenarios / 50 runs,
H16 33, H17 21, H18 53, H19 62. All passed. The H8 and H20 old
vertical-keyword rejection cases now use sideways-rl, which remains unsupported;
new H21 tests explicitly cover admitted vertical geometry and rejected orthogonal
geometry. No reduction in old parser rejection coverage for escapes or unknown
property names. H3 canvas writing-mode rejection tests remain unchanged.
