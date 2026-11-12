# H11 — canvas authored rule absence proof

## Scope and implementation

Based on the H10 tree published as `e92094a`. This is a CLI admission change,
not a native rendering feature. The original whole-document H3 check rejected
`#unrelated{min-height:72px}` despite there being no matching element. H11 omits
only that rule's H3-specific check after proving absence over the complete
immutable parsed DOM. The original CSS still passes through inventory,
`check_css`, function/resource/coverage gates and compilation unchanged.

The proof is in a separate translation unit to reuse native selector types,
strict list parser and matcher without conflicting with the legacy flat-style
headers used by the harness. The lexer preserves rule boundaries; it does not
strip declarations by textual property comparison. Every conditional body is
retained; no inactive-CSS exception is introduced.

A bounded DOM snapshot includes hidden/metadata nodes. Eligibility and worst-case
cost are established for the complete selector list BEFORE the first matcher
call. Descendant and general-sibling branches use a saturating recurrence rather
than a linear-time assumption. No timeout or incomplete bool result establishes
absence. Nested selectors, pseudo-elements, interaction states, attribute
substring operators and out-of-profile selectors retain the existing check.
OOM aborts; budgets never cause a rule to be dropped. The snapshot is not reused
after DOM mutations.

## Acceptance evidence

- 67 proof/profile checks, including matching list branches, escapes,
  combinators, nth-child, metadata, hidden nodes, malformed CSS, node/selector/
  rule limits and zero work budget (zero matcher queries).
- 48 allocation-failure positions: no partially constructed result published.
- 55 real CLI cases, with original-source compiled hashes, exact PNG and canvas
  geometry equality for absent rules, retained rejections for matched/inline/
  inherited/variable/conditional/state rules, global resource/function controls,
  limits and multiple canvas cases.
- PNG inspected: red empty canvas border and green flex sibling against the
  control background. No drawing/text fallback introduced.

H3's old test specifically expected absent rules to reject. H11 changes that
contract intentionally: 11 canvas-only absent declarations now pass, six global
writing-mode/direction cases still reject, and **17 matched-rule controls** were
added. Inline controls remain unchanged. H3 now has 134 cases, not 117. This is
not removal of the corresponding restrictions for actual matching styles.

## Validation and limits

Manual build and test commands are in README/build.sh and run_h11_core.sh.
No Actions/workflows, external resources, dependency installation or browser
execution were used. The exact original flexbox fixture is unmodified (SHA-256
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`) and still
rejects for unresolved support boundaries. No whole-page speed or Chromium
visual parity is claimed. `authored_css_validation=NOT_RUN` remains unchanged.

The preexisting `test_flex_static_position_lifetime` strict assertion remains
unmodified and known failing. H10 independently established that it also fails
on H9 because std::stable_sort can fall back after an optional nothrow allocation;
this is not reported as a passing test or silently included in a green suite.

Final fresh build: 71 core units + 8 auxiliary units (H11 adds one). All H11
checks above were rerun against that build. Regression results:

| Manual suite | Result |
| --- | --- |
| H3 native + 134 CLI cases (updated contract above) | PASS |
| H4 native | PASS |
| H5 native + 58 CLI cases | PASS |
| H6 native + 44 CLI cases | PASS |
| H7 native + 43 CLI cases | PASS |
| H8 native + 68 CLI cases | PASS |
| H9 native + 70 CLI cases | PASS |
| H10 native + 44 CLI cases | PASS |
| Original inventory and baseline input/domain/artifact checks | PASS |

Native scripts retain their allocation-failure injections and frozen-state
checks. The separate preexisting lifetime assertion described above is not part
of these scripts and has not been relabeled. No claim of a fully green repository
suite is made.
