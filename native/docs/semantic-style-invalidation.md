# Final semantic style invalidation (R2)

The runtime classifies the previous frozen state against the completely resolved
candidate once per existing node. It runs after font/root and length computation,
currentColor resolution, overflow normalization and the HTML `br` display rule.
New nodes and pseudo/retirement lifecycle changes retain the existing ALL policy.

The diff does not replay declarations or applicators, deep-copy a probe, inspect
historic masks, or OR a parent's impact into a child. Every descendant is still
resolved. This is not subtree pruning or a resource cache.

## Independent results

The private `StyleDiff` has three independent kinds of result:

- `own_impact` and `inherited_value_impact` classify actual final value changes.
  The latter is information about naturally inherited families, not a mask to OR
  into descendants. Explicit `inherit` on a non-inherited property remains an own
  change of that property's family.
- `metadata_changed` and `inherited_metadata_changed` describe measure and
  symbolic-color relationships, including latent background longhand lists.
- `style_storage_equivalent` permits reuse of the previous `FrozenStyle` only
  when all its stored fields and payloads are equal. It never permits reuse of
  an old record, measure context or custom-property scope.

Unknown dependency names/flags or unsupported operation kinds set `unclassified`,
force ALL and prohibit style reuse. Tests treat this as failure for supported
input. Equality is typed and exact: `+0 == -0`, NaN is unequal, and no struct
padding or pointer address is compared. `font_family` reads at most 64 bytes;
bytes after its terminator still participate in storage equality.

## Impact families

Native bits are layout=1, paint=2, hit-test=4, composite=8, text-layout=16,
cursor=32 and inherited=64. Changes combine with bitwise OR.

| Final data | Own impact |
|---|---:|
| display, position, dimensions, inset/margin/padding/border width, overflow | 7 |
| flex, box sizing, legacy gap | 1 |
| border styles | 3 |
| color and resolved text-fill aliases, text shadows | 66 |
| background/border colors, backgrounds, text gradient, box shadows | 2 |
| border radii | 6 |
| opacity, transform origin, transforms | 12 |
| backdrop filters | 10 |
| font size | 83 |
| font family/weight, text align/transform | 82 |
| line height, white space | 81 |
| cursor | 96 |
| pointer events | 68 |
| visibility | 70 |
| z-index/value-kind | 6 |
| transition configuration | 2 |

Creating/removing the existing positioning-context predicate
`!transforms.empty() || !backdrop_filters.empty()` additionally requests layout.
A parameter change that preserves that predicate does not add layout.

Examples: opacity alone is 12, removing width and changing opacity is 15,
width+cursor is 103, background+cursor is 98, and opacity+cursor is 108.
Registered properties without observable state, such as `font-style`,
`letter-spacing`, `order` and `align-content`, do not invent visual work.

## Semantic sidecars

Percentage definiteness uses the same category as current layout readers:

```
unit == PERCENT || (unit == LINEAR && linear_percent != 0) ||
(property_dependencies & MEASURE_PERCENTAGE)
```

The nine current consumers are width, height, min-width, min-height, max-width,
max-height, flex-basis, row-gap and column-gap. A category change requests 7 for
the six dimensions, 1 for flex-basis, or 17 for contextual gaps, even if the flat
length is identical. For example, `height:calc(0% + 100px)` to
`height:calc(100px)` changes height from 0 to 100 in the tested indefinite-height
container and must request layout. A definite container does not remove this
family-level requirement.

Contextual gap values also request 17 when any typed length component changes.
Contextual line-height compares kind, value and used height and requests 81.
Number and absolute length remain distinct even when their current used heights
coincide. Font inputs and document identity/generation are metadata, not visual
work by themselves.

Other dependency flags are retained without adding work when the final value is
unchanged. Thus `16px` to `1rem` at root size 16 can have impact 0, while the next
root-size change to 20 correctly produces a dimension delta of 7. Returning to
px removes the dependency. All 86 registered dependency keys, including four
aliases and four no-ops, are catalogued with local indices independent of public
registry IDs. Only the nine percentage consumers have percentage visual policy.

Background position/size lists retain unused elements without requiring paint;
projection onto active layers is classified through their actual layer fields.
CurrentColor references are compared independently from RGBA. Changing a symbolic
text-shadow relation sets inherited metadata, not an inherited paint mask when
RGBA stayed equal. A child's full resolution observes the new relationship.
Dormant URL/gradient payloads also prevent inappropriate style reuse without
requesting paint when the active background representation is unchanged.

## Exhaustiveness and transaction boundary

- `AETHER_STYLE_SCALAR_FIELDS` derives `StyleField`; each field plus font-family
  needs an explicit policy specialization.
- The six-payload and fourteen-sidecar manifests drive comparison calls. Adding
  an entry requires both policy and comparator specializations; there is no
  generic fallback reader or implicit metadata policy.
- Independent tests enumerate every registered property, alias, no-op and
  shorthand target, check the nine percentage policies and mutate every scalar,
  payload member and sidecar family with independently written expected masks.

The classifier uses stack dependency arrays and borrowed views, with no dynamic
allocation, parser/applicator calls, registry/DOM access or callbacks. Snapshot
owners remain pinned. All freezing, array reservations and update construction
remain in prepare. The commit implementation is unchanged: validation, prepared
publication and the existing diagnostic replay. Publication does not allocate.

An impact of zero never discards new scopes, symbolic references, latent lists,
measure dependencies or context. Historic mask transport fields now store the
current delta deterministically and are not read as invalidation authority.

## External invalidation remains the host's responsibility

The same URL can load another image size; font metrics, viewport/layout inputs,
scroll and animation ticks can change without a CSS delta. The host must preserve
those invalidations and OR them with style work. Style impact zero does not clear
external work or establish pixel equality. No resource, parser, inheritance,
layout policy or public ABI changed here.

## Validation entry points

- `aether.style-diff-classifier`: complete typed field/payload/sidecar mutations,
  no-op, NaN/signed-zero, dormant storage and bounded font-family cases; zero
  classifier allocation calls, including first invocation.
- `aether.style-diff-catalog`: exact registry/dependency coverage and explicit
  aliases/no-ops/percentage consumers.
- `aether.style-semantic-diff`: both public APIs; historical 30-case oracle,
  scalar families and mixed/removal cases, percentage geometry, snapshots,
  metadata, currentColor, inheritance, class/media/variable paths, pseudo
  lifecycle and external resource/layout invalidations.
- `aether.style-semantic-diff-oom`: allocation-failure sweeps of metadata-only and
  percentage-only changes; old revisions, maps, sidecars and generated nodes
  survive each failed prepare. Successful commit makes zero operator-new calls.
- Existing frozen, revision, malloc-fault, layout, paint, transform, origin,
  transition, visibility and installed-consumer tests remain applicable.

`tests/benchmark_semantic_style_diff.cpp` is an optional standalone Linux/glibc
benchmark. Build it with the same optimized core/compiler for baseline and
candidate. It records raw timing and allocator calls/bytes for simple/rich,
1/1000-node, no-op/isolated/mixed fixtures; initialization, legacy resolve,
contextual prepare and commit are separate, with 30 samples after ten warmups.
Commit timing includes release of the previous revision when no outside owner
pins it. Pinning the old revision for a diagnostic moves those frees out of the
commit call; it does not change the publication algorithm.

## Chromium comparison

The design was compared read-only with Chromium commit
`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`, especially
[`ComputedStyle::VisualInvalidationDiff`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/style/computed_style.cc#L723-L817)
and its
[field-comparison generator](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/build/scripts/core/style/templates/computed_style_base.cc.tmpl#L79-L83).
The transferable principle is to classify the full computed-value difference.
Native masks, containing-block rules and sidecars remain AetherUI's contract;
this work does not claim Blink parity or execute Chromium.
