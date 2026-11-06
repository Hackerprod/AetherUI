# AetherUI native core

This is the product. Everything else in this repository — `bindings/`, `examples/`, `tools/` —
consumes what is built and installed from here. The implementation is C++20; the only thing a
consumer is allowed to depend on is the C header at [`include/aether/aether.h`](include/aether/aether.h)
plus the installed binary it declares.

The core owns HTML parsing, the DOM, the CSS cascade (parser, layered cascade, selector matching
including `:has()`/`:is()`/`:not()`/nth-child, custom properties, `calc()`, media queries), layout
(block/inline/flex, intrinsic sizing, vertical scroll, hit testing), and retained display-list
generation. It does not own windows, an OS event loop, text shaping/rasterization, or a graphics API —
those are host concerns, deliberately left to each binding.

## Module map

```text
src/
  dom/            Node/ElementNode/DocumentNode, HTML parser
  css/             Legacy simplified cascade + primitives (CssLength, ColorRgba, ComputedStyle) —
                    kept for the primitive/geometry types layout and rendering still share
  style_runtime/  The real cascade engine: property registry, selector parser/matcher, rule index,
                    cascade resolution, custom-property/var() resolution, media conditions, the
                    CSS→compiled-rule source compiler, and the AetherStyleRuntime equivalent
  layout/         LayoutBox/LayoutResult and the block/inline/flex layout engine
  rendering/      DisplayList and DisplayListBuilder — the paint-command model
  primitives/     RectF/PointF/SizeF geometry shared across every module
  abi/            aether_abi_version()/aether_abi_capabilities()/aether_paint_command_size() and
                    internal C-string/error-code helpers used by every public function
```

`css/` predates `style_runtime/`: it's the original, simplified hand-rolled cascade. It has no
production consumer today — every real caller goes through `style_runtime/`. It stays because
`ComputedStyle`, `CssLength`, and `ColorRgba` are the value types layout and the display-list builder
still consume directly. Don't add new cascade logic there; extend `style_runtime/` instead.

### Transition property module

`style_runtime/transition_properties.cpp` privately owns the four transition longhands,
their parse/apply callbacks, initial values, and the single-transition shorthand. It depends
on the private property registry contract, small shared string/token utilities, and the public
value types. It does not depend on DOM, selectors, layout, rendering, animation runtime,
`style_runtime_types.h`, or global engine initialization.

The catalog calls `transition_properties_register_longhands()` after `transform-origin` and
`transition_properties_register_shorthands()` at the end of shorthand composition. The caller
provides a live, non-null registry, serializes composition, and calls `seal()` once after all
families have registered. Do not look up definitions while composition can still grow either
vector: sealing rebuilds the pointer maps. The module never seals, retains definition pointers,
or captures local state in callbacks. Duplicate or post-seal registration preserves the registry's
existing no-op behavior. Full-catalog IDs remain 82–85; an empty module-only registry uses 0–3.
The private headers and test targets are not installed or exported.

This boundary preserves existing behavior, not full CSS transition conformance. Initial values
remain `none / 0s / 0s / linear`; omitted shorthand components reset to those values, and its
expansion emits CSS tokens for ordinary longhand parsing. Lists, negative times (including delay),
and unknown shorthand targets remain unsupported. An unknown direct `transition-property`
instead succeeds with mask zero. Existing numeric parsing, including nonfinite values and
unusual `strtof` forms, is intentionally unchanged. Global keywords and `var()` remain generic
pipeline responsibilities; the existing parent-inheritance and deferred-inline-shorthand
limitations are covered by regression tests rather than changed here.

`aether.transition-property-module` links only the module, the private CSS token lexer,
and its test translation unit, with separate compile checks for each self-contained private header. It covers metadata/order,
scalar-only application, parser aliases, reallocation/lifetime, independent registries,
repeated/sealed registration, shorthand round-trips, and atomic failure. The public-ABI
`aether.transition-property-pipeline` covers resolution, reset/precedence/global semantics,
changing variables, PAINT-only updates, unchanged repeats, and shared-tokenizer numeric
results for margin, padding, inset, border, flex, gap, and overflow. The shared splitter
consumes CSS components, including adjacent tokens separated only by comments and
percentage/dimension boundaries. Comments do not count as CSS whitespace; functions
and balanced blocks remain intact. Both use explicit checks that remain active with `NDEBUG`.

To verify the isolated target before building Core, use a fresh build directory:

```sh
cmake -S native -B artifacts/build/transition-isolation \
  -DCMAKE_BUILD_TYPE=Release -DAETHERUI_BUILD_TESTS=ON
cmake --build artifacts/build/transition-isolation \
  --target aether_transition_property_module_test --verbose
ctest --test-dir artifacts/build/transition-isolation \
  -R '^aether.transition-property-module$' --output-on-failure
```

Then build and run the complete suite and installed package consumer as described below.
The isolated test does not replace whole-engine, ABI, or installation validation.

## ABI contract

The current ABI is **0.1.0**: flat C functions, opaque handles, fixed-width value types, integer
status codes, and plain C callbacks. No C++ classes or STL containers are part of the public interface.
Do not assume that every allocation failure is translated into a status code; in particular, DOM
append does not currently guarantee rollback if its final child-vector allocation fails.

Every consumer must inspect the loaded library before creating a single handle:

```c
#include <aether/aether.h>

uint32_t version = aether_abi_version();
if (AETHER_ABI_VERSION_GET_MAJOR(version) != AETHER_ABI_VERSION_MAJOR) {
    /* refuse to load: major-version mismatch is a breaking-change signal */
}

aether_capabilities_t capabilities = aether_abi_capabilities();
size_t paint_command_size = aether_paint_command_size();
```

`aether_paint_command_size()` exists specifically so a binding never hard-codes
`sizeof(aether_paint_command_t)`. The display-list command union grows as paint features are added;
allocate command buffers from this call, not from a literal.

The ABI advertises DOM, style-runtime, layout, display-list, render-plan and input-hit-test capabilities through
`aether_abi_capabilities()`. Treat unset bits as "not available in this build," and ignore bits you
don't recognize — a newer library may set capability bits an older binding has never heard of.

**Ownership.** Every `aether_*_create`/`aether_*_parse_*` that returns an owning handle has an exact
matching `aether_*_destroy`/`aether_*_free`. Nodes, layout boxes, and computed-style views obtained
from an owning handle are *borrowed* — they are valid only while their owner is alive, and become
invalid the instant the owner is destroyed or the document is reparsed. Copy out anything you need to
outlive the owner (text, gradient stop arrays) before releasing it.

**Threading.** Handle access is serialized in ABI 0.1: no two threads may operate on the same
document, style runtime, layout result, or display list concurrently. Callbacks (text measurement,
scroll-offset queries) run synchronously on the calling thread during the native call that triggered
them, must remain valid for as long as they're registered, and must not let an exception, panic, or
longjmp unwind through native stack frames — catch everything at the callback boundary and return a
safe fallback value instead.

See the per-binding READMEs under `bindings/` for how each language enforces these rules in its own
idiom (`SafeHandle` in .NET, `AutoCloseable` in Java, an explicit `Close()` plus cgo pointer rules in
Go).

### ID lookup

`aether_doc_get_element_by_id` returns the first connected element with the exact,
nonempty ID in current tree preorder: a parent precedes its descendants, and an
entire subtree precedes the next sibling. Duplicate IDs are not rejected or
rewritten. Visibility does not affect the result. Case, whitespace and UTF-8
identity are preserved; HTML entities have already been decoded by the parser.
No match, an empty ID, a null ID or a null document returns null.

The borrowed result reflects moves, disconnect/reconnect, ID changes and reparsing
on the next query. Generated pseudo-elements and layout-only wrappers never win;
an authored element named `aether-anonymous-flex-item` still participates. Lookup
does not change nodes, attributes, relationships or document version. It rebuilds
the index on each call, without adding a version-based cache or new lifetime rules.

`aether.dom-id-order` checks these rules through the public ABI with explicit
checks active under `NDEBUG`, independently enumerated expected handles, and
before/after DOM snapshots. `aether.dom-mutation-integrity` also checks duplicate
lookup around real pseudo-elements and flex layouts, including rejected mutations.

### Structural DOM mutation

With live handles, an initially coherent editable DOM forest and serialized access,
`aether_node_append_child` rejects self-insertion and inserting an ancestor beneath its descendant
with `AETHER_ERROR_INVALID_STATE`, before disconnecting the child. Both append and remove also reject
an endpoint that is a generated pseudo-element, its descendant, an anonymous layout wrapper, or a
descendant of that wrapper. The check uses internal provenance, not tag spelling or style aliases.
Remove requires both membership in the parent's child list and the matching child-to-parent link.
Null arguments retain `AETHER_ERROR_NULL_ARG` precedence. These rejections leave parents, child order,
contents, IDs and document version unchanged.

Valid append still moves the child to the end: moving upward, moving between branches, reordering in
the same parent and reattaching a disconnected subtree remain supported. Authored hosts with generated
children and authored text referenced by layout wrappers remain mutable. Layout getters still expose
their existing borrowed nodes; a mutation is never silently redirected from a wrapper to its host.
Old layout results remain snapshots after valid authored mutations, not automatically updated views.

These structural guards do not validate freed handles, extend a wrapper's lifetime beyond its layout
result, make other setters immutable, repair an already corrupt tree, implement cross-document
adoption, change ownership of disconnected nodes, or provide allocation-failure rollback. Release
layout results before their borrowed style maps and documents, and do not retain synthetic handles
across owner destruction or style resolution that regenerates pseudo-elements.

`aether.dom-mutation-integrity` covers the public C ABI, real wrappers/pseudo-elements returned by
layout, and deterministic geometry/display-list consumers without a browser. Its bounded checks and
explicit failures stay active in Release. `aether.dom-mutation-boundary` complements it with private
provenance and nonreciprocal-remove fixtures. The JavaScript native integration regression is described
in its binding README; negotiation-only/mock tests do not establish native DOM integrity.

## Build, test, and install

The install tree — not the build tree — is the consumption contract. Never point a consumer at
`native/build/` or a private CMake cache directory.

**Windows (MSVC):**

```powershell
cmake -S native -B artifacts/build/native/win-x64 -A x64 `
  -DAETHERUI_BUILD_TESTS=ON `
  -DCMAKE_INSTALL_PREFIX=artifacts/native/win-x64
cmake --build artifacts/build/native/win-x64 --config Release --parallel
ctest --test-dir artifacts/build/native/win-x64 -C Release --output-on-failure
cmake --install artifacts/build/native/win-x64 --config Release
```

**Linux / macOS:**

```sh
cmake -S native -B artifacts/build/native/$(uname -m) \
  -DCMAKE_BUILD_TYPE=Release \
  -DAETHERUI_BUILD_TESTS=ON \
  -DCMAKE_INSTALL_PREFIX=artifacts/native/$(uname -m)
cmake --build artifacts/build/native/$(uname -m) --parallel
ctest --test-dir artifacts/build/native/$(uname -m) --output-on-failure
cmake --install artifacts/build/native/$(uname -m)
```

`AETHERUI_BUILD_TESTS` defaults to CMake's own `BUILD_TESTING`; pass it explicitly if your generator
doesn't set that automatically. Two test binaries run under CTest:

- `aether.smoke` (`tests/test_smoke.cpp`) — a C++ compile/link smoke test against the installed target.
- `aether.abi-c` (`tests/test_abi_c.c`) — a **C11** consumer, compiled and linked with no C++ in its
  translation unit, proving the header is genuinely C-clean and not "C++ wearing an `extern "C"` hat."

`tests/package_consumer/` is a separate, standalone CMake project (its own `CMakeLists.txt`, not part
of the main build) that runs `find_package(AetherUI CONFIG REQUIRED)` against an *installed* tree and
links `AetherUI::Core` — this is the regression test for the install/export step itself, not for the
library's behavior. Run it after `cmake --install` to catch a broken `AetherUIConfig.cmake` or a
missing installed header before a downstream binding does.

## Installed package

CMake consumers use the exported imported target:

```cmake
find_package(AetherUI CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AetherUI::Core)
```

pkg-config consumers (non-CMake C/C++ builds, most cgo setups) use:

```sh
pkg-config --cflags --libs aetherui-core
```

Both are generated from `cmake/AetherUIConfig.cmake.in` and `cmake/aetherui-core.pc.in` at configure
time and installed under `<prefix>/lib/cmake/AetherUI/` and `<prefix>/lib/pkgconfig/` respectively —
private headers and build-tree binaries (`native/build/`) are never a distribution interface, only
`<prefix>/include/aether/aether.h` and the installed library are.

See [`examples/cpp/abi-info`](../examples/cpp/abi-info/) for the minimal working consumer: it links
against the installed package, calls `aether_abi_version()`/`aether_abi_capabilities()`/
`aether_paint_command_size()`, and fails the build if the loaded ABI's major version doesn't match
what it was compiled against. Copy that project's `CMakeLists.txt` as the starting point for a new
native or cgo consumer.

## HTML attribute values

Slashes are data in unquoted HTML attribute values, including a slash directly
before `>`: `src=logo.svg/>` has the value `logo.svg/`, while `src=logo.svg />`
has the value `logo.svg` followed by the parser's self-closing marker. URLs and
paths are not normalized. The parser extracts the raw value before decoding
HTML entities once; decoded whitespace, quotes, and `>` never become delimiters.

`aether.html-unquoted-attributes` checks the complete attribute map through the
public ABI, including real versus accidentally invented `disabled` attributes,
child relationships, production cascade matches, and reparsing. Its checks also
run with `NDEBUG`. The slash boundary follows HTML tokenization, but this is not
a claim of general HTML5 conformance: the existing entity decoder, `std::isspace`
behavior (including vertical tab), incomplete-start-tag recovery at EOF, and
self-closing behavior for non-void elements remain unchanged.

## HTML unmatched end tags

An end tag with no matching open element leaves the insertion context intact.
A matching tag closes the nearest open element with that name and its open
descendants, using ASCII case-insensitive comparison while preserving the DOM's
original tag spelling. Previously closed, void and self-closed elements cannot
satisfy a later end tag. The document sentinel is never removed.

`aether.html-end-tags` checks literal public-ABI trees, parent/ancestor relations,
child order, text/comments, repeated closes, document boundaries, reparsing and
bounded deep nesting. A numerical style check resolves from body and verifies
the retained container's descendant rule. Checks remain active with `NDEBUG`.

This preserves AetherUI's simplified matched-tag behavior, not full HTML tree
construction. HTML scope barriers, special end-tag rules (such as `p` and `br`),
implied elements, formatting adoption, tables, templates and foreign content
remain outside this recovery fix. Searching uses O(depth) node visits and O(1)
auxiliary space, plus name-comparison cost; repeated unmatched closes can take
O(depth × closes) visits. Token consumption and EOF recovery are unchanged.

## HTML style and script text

`style` and `script` names are matched ASCII case-insensitively when entering
their text mode and recognizing the matching end tag. Public DOM tag names keep
their original spelling. Their content is one literal text child (or no child
when empty): entities, apparent markup, UTF-8 and CR/LF bytes are preserved.
A matching `</name` ends the text only when followed by `>`, `/`, or HTML
whitespace (space, tab, LF, FF, CR). Suffixes such as `</styleX>` and a name at
EOF without a delimiter remain text. Once a delimiter is present, an unfinished
end tag is consumed by the existing end-tag parser. A real closing tag inside a
CSS or JavaScript string still closes the element.

`aether.html-raw-text` verifies the original regression corpus, all 5,120 opening
and closing case combinations, exact DOM/text, byte boundaries, EOF, overlapping
false candidates, and replacement of a previously parsed document through the
public ABI. The scanner is linear and uses constant auxiliary space apart from
the resulting text.

This is a bounded contract, not full HTML tokenization. ScriptData escaped and
double-escaped states, RCDATA (`title`/`textarea`), other raw-text elements,
foreign content and browser newline normalization are not implemented here.
The existing end-tag consumer does not honor quoted `>` in attributes. Existing
start-tag self-closing and attribute behavior remain unchanged. The C-string
input ABI does not support embedded NUL. Parsing does not execute scripts or
automatically apply the embedded styles; hosts supply CSS to the style runtime.

## Pseudo-selector name identity

Supported pseudoclass names are ASCII case-insensitive after CSS escape decoding:
`:hover`, `:HOVER` and `:\48 OVER` have the same meaning. This applies recursively
inside selector functions and preserves the original top-level selector source.
Only the decoded pseudo name is folded. Classes, IDs, attribute names and values,
and custom-property names retain their existing identity and comparison rules.
Pseudoelement names `::before` and `::after` already follow the same ASCII rule.

`aether.pseudo-name-parser` checks all 22 existing names, recursive AST fields,
specificity, dependencies, strict/forgiving list policies, escapes and negatives.
`aether.pseudo-name-runtime` checks public-ABI matching, cascade precedence,
generated styles and repeated state deltas without recompiling stylesheets.
Both keep checks active under `NDEBUG`. Runtime transitions use only the four
public states; parser coverage of other states does not imply HTML state derivation.
This does not add names, single-colon pseudoelements, Unicode case-folding,
relative `:has()` arguments, or `nth-child(... of ...)` support.

## Empty-element selectors

`:empty` matches an element with no direct DOM element children and no nonempty
text children. Comments, zero-length text and generated `::before`/`::after`
content do not affect the result. Whitespace text, including a single space,
does prevent matching, as does an element with `display:none`. Comments and
empty text remain observable through the DOM API; generated nodes remain
excluded from public child enumeration.

After a DOM mutation, explicitly resolve a subtree that includes every affected
subject. For ancestor `:has()` or sibling dependencies, resolve their common
ancestor, not only the changed leaf. This does not add automatic DOM invalidation
or change the generation/lifetime of pseudo-elements.

`aether.selector-empty` covers the public ABI, nested selectors, generated content
across repeated passes, and append/remove/move transitions. `aether.selector-empty-text`
adds a private fixture for zero-length text, evaluated by the production runtime
without extending the ABI. Both tests keep their checks active with `NDEBUG`.

## Structural selectors and generated content

Structural selectors use the logical DOM, not the mixed storage consumed by
layout and paint. `::before` and `::after`, including their entire generated
subtrees, are excluded from element positions, sibling combinators and `:has()`
searches. Text and comments do not occupy element indices; real elements with
`display:none` still do. `:empty` retains its separate text-aware rule above.

The subject of a pseudo-element rule is different from a traversal candidate:
`.item:first-child::before` checks the originating element's DOM position while
the cascade still selects the `before` rule. Only the matcher entry normalizes
this subject. Anonymous flex wrappers are not DOM subjects and are not aliases
for their style origins. Generated content remains available to layout/paint,
and the public DOM ABI is unchanged.

Adding/removing pseudo rules or changing their `content` must not require an
extra resolution to stabilize ancestor `:has()` matches. After moving real
elements, resolve a common ancestor containing all affected subjects. Updates
are deltas: retain copied DOM style values across batches, and never retain a
generated-node pointer across resolutions.

`aether.generated-selector-metamorphic`, `aether.generated-selector-lifecycle`
and `aether.generated-selector-origin` cover mutations, repeated resolution,
state changes and the pseudo's own style through the public ABI, with checks
active under `NDEBUG`. `aether.selector-dom-boundary` checks private malformed
origins and synthetic-node guards without adding ABI entry points.
`aether.generated-selector-display-list` checks generated text in a headless
display list; it does not replace visual renderer or binding validation.
This contract does not add new selector syntax or change disconnected-node
compatibility.

## Custom properties

Custom-property names use decoded CSS identifier values with exact case and Unicode
identity: `--Brand` and `--brand` are distinct, while `--j`, `--\6a` and `--\6A`
name the same property. Declarations, inline styles, inheritance and `var()` share
this identity. Standard property and `var` function names remain case-insensitive.
Values retain their source spelling. Custom-only changes refresh inherited scopes
even when the ancestor's concrete style does not change.

`aether.custom-property-case` checks this through the public ABI, including escaped
names, cascading, recomputation and snapshot lifetime. `aether.css-variable-strings`
also covers the private decoder and resolver budgets. Private scopes must use
decoded keys; source spellings are not a compatibility alias. This does not add
`@property`, full `revert` semantics, or deferred inline shorthands, and does not
normalize Unicode.

### Computed custom-property inheritance

Custom properties are computed on the element that declares them, including
variables with no local consumer. A child inherits terminal tokens, invalidity,
or a work-limit failure, never the ancestor's unresolved `var()` instructions.
For example, a parent's `--base:20px; --alias:var(--base)` leaves an inherited
`--alias` at `20px` when a child changes `--base`. Redeclaring `--alias` in the
child computes it with the child's variables. Relative units and `currentColor`
remain tokens for the eventual ordinary-property consumer.

Local `initial` (and the existing simplified `revert`) blocks the ancestor's
value; `inherit` and `unset` retain the parent result. A valid empty value does
not select fallback. Cycles are detected in references actually resolved, so an
unused fallback does not create a cycle, while cycle participants cannot rescue
themselves with their own fallback. A consumer outside the cycle can recover.
Semantic failures do not stop inspection of later references in the declaration,
which is necessary to detect overlapping cycles.

Each local winner gets an independent budget of 1,048,576 logical work units,
shared by its dependencies and chosen fallbacks, without memoization. Each
ordinary consumer gets the same independent budget. Exhaustion is a hard result:
it does not select fallback, and remains hard when inherited until a successful
new generation replaces it. The default depth is 32 and expansion limit is 65536
serialized bytes, including token separators and normalized escape tails. A
named custom-property root starts at depth 1; inherited/computed terminals use
one substitution level without replaying their historical dependency depth.
Limits do not bound total document CPU/RSS: N local winners can each use their
own budget, and stored local values can total N times the expansion limit.

Runtime scopes are complete owned snapshots with shared parent snapshots and
iterative release of exclusively owned parent chains. They refresh even when
concrete style is unchanged. The existing incremental state-change API can take
a resolved parent's current snapshot; `resolve_subtree` still treats its supplied
root as an inheritance root. Allocation failures follow the existing native-core
exception policy, not a new OOM-recovery guarantee: a partial scope is never
returned as successful, and allocation exceptions are not translated here.

`aether.inherited-custom-properties` covers the public ABI, mutation, cascade,
shorthands, generated content and style snapshots. `aether.computed-custom-property-scope`
covers terminal ownership, token stability, exact work/depth/length limits and
long-chain release with checks active under `NDEBUG`. This does not add CSS-wide
keywords generated by substitution, registered properties, or full revert semantics.

## Visibility

`visibility:hidden` preserves layout and overflow geometry. It suppresses the
node's own paint and hit eligibility, while descendants with computed
`visibility:visible` remain visible and interactive within the existing ancestor
transform, clip, scroll and opacity scopes. Visibility is inherited. Changing
only its computed value on a stable DOM topology requires paint/hit updates,
not layout; pass a current style map for live hit testing and display lists.
The hit APIs without a style map intentionally use the layout's captured styles.
`aether_layout_result_hit_test_input` (capability bit 5) returns the frontmost
eligible content, thumb, track or corner plus a borrowed native owner, in reverse
local paint order. Own chrome precedes children and uses snapshot geometry with
live thumb offsets. A null offset callback uses snapshot scroll offsets for this
new API; the three DOM hit APIs retain their previous behavior. Outputs are
initialized to NONE/null before argument validation; a valid miss is OK/NONE/null.
The query preserves generated owners even when a binding cannot project them.

`visibility:collapse` is currently an alias of `hidden` for all displays,
including table rows/cells and flex items. CSS table/flex geometric collapse is
not implemented. Tables retain their existing unspanned phase-1 layout, and
flattened inline ancestors and table groups do not gain new paint scopes.
DOM hit testing retains the existing rectangular clipping, border-box candidates (including border and padding, excluding margins),
and traversal order; this change does not add inverse-transform hit testing.
Hit queries accept logical input coordinates without implicit viewport rejection
or clamping. Eligible boxes outside the viewport remain queryable, subject to
existing clips and scroll offsets. This coordinate domain differs from a browser's
`Document.elementFromPoint` query.
Generated pseudo-elements are still recreated by the runtime, so their identity
changes require layout even when triggered by a visibility declaration.
Canvas background propagation remains separate from a box's own visibility.
