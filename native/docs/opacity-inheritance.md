# Explicit opacity inheritance

`opacity: inherit` copies only the parent's computed opacity float. Opacity is
still not inherited by default: an undeclared child remains at 1. A chain of
explicit `inherit` declarations copies the same computed float at each level;
it does not square the value during style resolution. Ancestor alpha continues
to multiply during painting, in the existing compositor path.

The checked numeric parser and registration order are unchanged. The property
opts into the existing explicit-inheritance and CSS-wide callbacks. Literal,
escaped and case-insensitive keywords, and keywords produced by `var()` fallback,
use the same runtime machinery as other opt-in properties. The inheritance hook
does not serialize, parse, clamp, allocate, or acquire a locale. It copies the
already computed value, including positive zero and representable tiny values.

`initial` and `unset` resolve to 1. Root inheritance with no parent also resolves
to 1. This keeps native compatibility for `revert` (treated as initial); it does
not implement cascade-origin rollback or `revert-layer`.

A custom property declared as `--x: inherit` inherits that custom property. For
example, with `--x: 50%` on the parent, `opacity: var(--x)` computes to .5, not the
parent's opacity. Invalid literal numbers leave lower valid declarations eligible;
an invalid winning variable substitution resolves to initial rather than reviving
a lower declaration or selecting the fallback of a defined invalid variable.

The contextual revision API observes ancestors outside the requested subtree.
Legacy `resolve_subtree` keeps its isolated inheritance root: resolving a child
with `opacity: inherit` directly gives 1, while resolving its containing tree sees
the parent. Generated `::before` and `::after` use their host as the parent. Retained
maps, revisions, handles, clones, and exported style copies remain snapshots.

## Tests

- `aether.opacity-pipeline`: literals, variables and fallback, escaped keywords,
  percentages, clamping, positive zero, tiny values, invalid winners, and separated
  compile/apply numeric locales where a comma-decimal locale is installed.
- `aether.transform-origin-dispatch`: the exact hook allowlist and opacity registry
  checks, including byte isolation and no locale calls in explicit inheritance.
- `aether.opacity-inheritance`: cascade order/specificity/important, winning and
  losing inheritance, parent mutation, chains, siblings, reparenting, isolated
  removal after repeated no-ops, contextual/legacy boundaries, pseudo-elements,
  retained snapshots, emitted paint alpha, and transition endpoints/interior.

The paint assertion inspects native display-list commands. It is not a browser
screenshot comparison or a claim of complete browser compositing equivalence.

## Existing invalidation limitation

Registration still specifies `COMPOSITE | HITTEST` (12), without `LAYOUT`.
Effective mutations and isolated removal covered by the tests emit that mask,
with no update on unchanged resolves. This is not a general invalidation guarantee.
The existing runtime can reuse historical declared impacts for removed properties.
The following were reproduced on baseline `dfbdcf3`, before this change:

1. Set `width:100px;color:red;opacity:.4`; resolve the full tree; resolve twice
   unchanged; replace with `width:100px;color:red`. Opacity becomes 1 and width
   stays 100px, but the update mask is 79 rather than 12 (excess layout invalidation).
2. Set the same initial style; resolve; resolve unchanged; replace with
   `color:red;opacity:.5`. Width becomes auto and opacity becomes .5, but the update
   mask is 12 and misses layout invalidation.
3. With only `opacity:.4`, resolve, resolve unchanged, then remove the declaration:
   the mask is 12 as expected.

Both over- and under-invalidation precede explicit opacity inheritance. This change
keeps that general runtime behavior untouched rather than adding a property-specific
mask override. Correcting removal impacts across scalar and contextual properties
requires separate work.
