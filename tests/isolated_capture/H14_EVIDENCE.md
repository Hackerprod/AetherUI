# H14: scoped variables in flex declarations

Source base: tree published as main `f01bb70`. This increment admits `var()`
only in `flex`, `flex-basis` and custom-property chains used by that profile.
No dependencies, workflows, original fixtures or public ABI changes.

## Contract

The original CSS function/resource/syntax checks remain global, including
unused fallbacks, overwritten declarations and unmatched rules. The source
profile allows only var functions, limits source bytes, nesting and calls, and
rejects resources and other functions. Other ordinary properties do not acquire
variable permission.

A private synchronous borrowed audit hook observes values at the existing
native cascade substitution points, rather than reimplementing resolution.
Resolved flex values are checked per element. The native custom-property
computation and substitutions share a bounded work budget when the audit is
opted in; ordinary callers keep their previous per-operation budgets.

The CLI uses explicit prepare/commit for variable captures. The legacy resolve
API can return zero on both failure and no changes, so it cannot establish that
an audited preparation succeeded. Preparation failure cannot publish a capture.

The limited capture profile deliberately rejects cycles, missing/incompatible
values, invalid computed custom properties and exhausted limits. The native
runtime's proper `unset` behavior is tested separately without the audit;
rejection is not a claim that these declarations are invalid CSS universally.
Math in variables and unproved unit forms remain outside this profile.

## Verification

Final rebuild and test counts are recorded after completion below. Pixel
comparisons use native explicit-value controls, not executed Chromium. The
original flexbox HTML is unchanged and remains rejected for other blockers.

Manual commands:

```sh
sh tests/isolated_capture/build.sh /tmp/h14-build
sh tests/isolated_capture/run_h14_core.sh /tmp/h14-build
python3 tests/isolated_capture/test_h14.py /tmp/h14-build/aether_capture /tmp/h14-pixels
```

References used by the reviewed design:
- https://www.w3.org/TR/css-variables-1/
- https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/css/resolver/style_cascade.cc

Final resumed build: `/tmp/h14-resume-clean-20261112`, independently fresh
objects after the interrupted earlier build; 71 core + 8 auxiliary units.
H14 native tests pass 3,786 checks, 406 transactional OOM failures and 3 source
gate OOM failures. The prior committed revision remains readable and unchanged
through failures. Native unset, shorthand priority and sheet removal are tested.
The final CLI matrix passes **64 invocations**, with exact PNG pairs for scoped
values, inheritance, nested fallbacks, case sensitivity, escapes, comments,
important declarations and competing longhands. Reports contain real process
wall time, CPU and peak RSS. Representative PNG inspected: red/blue 150px items
matching the explicit `flex:1 1 0` control.

All native H3–H13 regression scripts pass against the final objects, including
H12's lifetime sweep and negative oracles. CLI H3/H5/H6/H7/H8/H9/H10/H11/H13 and
basic preflight/domain/artifact checks pass. The original fixture hash remains
unchanged and its capture remains explicitly rejected. No Chromium execution,
full-original performance result or general variable-support claim is made.

Logs: `/tmp/h14-resume-build.log`, `/tmp/h14-resume-core.log`,
`/tmp/h14-resume-native-regression.log`, `/tmp/h14-resume-cli-regression.log`.
