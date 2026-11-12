# H4 contextual comparison math: implementation evidence

Base: public main `e442720a401ccc36406ffa5b80a39cb1009abe94`.
Implementation is pending independent code review. No Actions or remote push used.

## Contract

Private immutable typed trees preserve calc/min/max/clamp branches until the
layout percentage basis is known. Scope is padding (four longhands and atomic
shorthand) and flex-basis (including explicit grow/shrink/basis shorthand).
Number and length types are distinct; function Number zero is not coerced to
length. px, %, em, rem, vw, vh are supported. Unsupported units/functions,
non-finite arithmetic, divide-by-zero, excess nodes/depth/source are rejected.
Limits: source 16KiB, 256 created nodes, depth32. This is not complete CSS Math.

Font/viewport units compute before inheritance; percentages remain deferred.
Frozen measures own trees, delta distinguishes unchanged/set/erase, and style
diff compares structure. Literal/initial/unset erases trees. Public flat copy
returns INVALID_STATE without changing output; owning clone and get_update do
not project through flat data. A NaN private scalar marker detects lost context.
There is no public ABI struct change.

Layout carries frozen owners into snapshots. Padding has Used, IntrinsicCyclic
(percent terms zero inside the expression), and Indefinite phases. Both flex
paths retain percent-basis indefiniteness and use content rather than choosing a
branch prematurely. Only final used padding/basis is clamped nonnegative.

The source harness admits function tokens only within parsed, tested property
contexts. Selectors, other properties, unknown functions, at-rules, resources,
text and SVG retain their separate restrictions. Diagnostic inventory remains
lexical observation, not a certificate.

## Verified on rebuilt native core

71 core translation units rebuilt with C++20/O1, plus native frame/preparation/
software-renderer CLI support. Manual only; no extra graphics dependency.

- AST: 121 checks PASS, including escaped identifiers, malformed/type/budget/
  division/nonfinite rejection and no output mutation on failure.
- Context gate: 24 checks PASS.
- State/ABI: probe tri-state, frozen lifetime, flat sentinel and clone PASS.
- H4 ownership/OOM: get_update clones survive runtime/updates/document; parse14,
  freeze14, clone1 allocation fault sites; runtime prepare literal→tree215 fault
  sites, ordinary and aligned allocation. No failed prepare publishes; previous
  generation/revision remains literal7. Successful prepare does not publish;
  allocation-free commit publishes next generation while retained prior stays.
- H4 layout: padding used/cyclic/indefinite, lost context rejection, row basis
  200→80,500→150,800→180; column defined500→150; indefinite content37 rather
  than height90 or premature clamp80. Frozen map after runtime destruction PASS.
- Core integration: header bases1000→24,1228→24,1280→50,1600→210; nested basis;
  clamp inverted bounds; invalid shorthand atomicity; tree replacement/initial/
  unset; inherited max(3em,10%) uses parent20px font, not child40px PASS.
- Existing calc and calc trivia tests PASS; computed measures PASS; scratch
  41426 checks PASS; flex definite-basis716 cases PASS.
- H3 native core PASS; H3 CLI117 cases PASS; H1 input/preflight/domain checks and
  H2 inventory regression PASS.

Controlled `fixtures/h4_math.html` produces real 1280x800 PNG and report.log.
`test_h4_pixels.py` checks exact green content boundaries at header x50/y28 and
blue flex child width150. Inspected pixels visually as well. These are geometric
assertions, not screenshot equality against Chromium. Timing/RSS in report.log
are one local run, not a benchmark against Chrome.

Original fixture unchanged:
`eab17055687df3296d8b911b0217dfb0429c306a6aa57ab49895937a6a34b837`.
Original remains blocked by independent unsupported content; CSS eligibility
Unknown, authored CSS validation NOT_RUN, Chromium NOT_RUN. Do not report all403
Flexbox cases as passed. LeakSanitizer could not run under the environment's
ptrace restrictions; no sanitizer PASS claimed.

## Reproduce

From repo root, in a new build directory:

```
sh tests/isolated_capture/build.sh /tmp/h4-build
sh tests/isolated_capture/run_h4_core.sh /tmp/h4-build
```

Link the new `native/tests/test_contextual_math_state.cpp`, `test_h4_layout.cpp`,
`test_h4_ownership_oom.cpp` individually against `/tmp/h4-build/core/*.o` with
`c++ -std=c++20 -O1 -Inative/include ... -pthread`.
`test_contextual_math.cpp` is standalone. `test_h4_gate.cpp` links core objects.

```
/tmp/h4-build/aether_capture --geometry tests/isolated_capture/fixtures/h4_math.html /tmp/h4-image 1280 800 1
python3 tests/isolated_capture/test_h4_pixels.py /tmp/h4-image/aether.png
python3 tests/isolated_capture/test_h3.py /tmp/h4-build/aether_capture
python3 tests/isolated_capture/test_inventory.py /tmp/h4-build/aether_capture
```

Chromium architecture reference used by the approved proposal:
https://chromium.googlesource.com/chromium/src/+/f92e277933365aa393f19125780c286ba84362ee/third_party/blink/renderer/core/css/css_math_expression_node.cc
CSS clamp/min/max: https://drafts.csswg.org/css-values-4/#comp-func

Additional direct applicator gate: `test_h4_applicators.cpp` PASS checks legacy
checked and void routes reject without mutation, malformed syntax rejects,
and contextual probes set/erase trees without mutating their base.
