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

### Stylesheet order and lifetime limits

The style runtime compares declarations by important, inline, engine layer, selector
specificity, global rule position, then declaration position within that rule. A later
successfully added stylesheet wins an otherwise equal tie. This does not make the last
sheet win over important declarations, higher layers, or more specific selectors.
Engine layers retain AetherUI's existing ordering, including for important declarations;
they are not a general implementation of CSS `@layer`.

Rule positions continue across additions. Selectors in one list have distinct rule IDs
but share a source position. Media activation only changes eligibility, never position.
Removing a sheet, clearing caches, and rebuilding the index do not renumber survivors.
Removing and re-adding a sheet places it after existing sheets, even with the same name
or text. The .NET reload path uses this remove/add contract; multiple API calls are not
a single transaction.

Each runtime can assign rule IDs 1 through `INT32_MAX-1` and sheet handles 1 through
`INT64_MAX-1`, cumulatively over its lifetime. The maximum value is reserved as an
exhausted next-ID sentinel. Removed IDs and handles are not recycled. An empty or
recovered-invalid sheet that emits no rules can still be added with exhausted rule
capacity if a handle remains. Inactive media rules consume IDs like other rules.
Stylesheet and inline source text must fit in `INT32_MAX` bytes, excluding the null
terminator. These guards also bound declaration positions and public rule counts.

Controlled capacity failures return `AETHER_ERROR_INVALID_STATE` and emit
`AETHER-CSS-LIMIT` through an installed diagnostic callback. A rejected sheet leaves
existing sheets, index, cached styles, counters, and output arguments unchanged before
delivering diagnostics. Any explicit changes made by a diagnostic callback are separate
operations and are not undone by the rejected call.
Compilation stages the whole sheet, including nested media, before publication.
An oversized inline block is ignored in full with the same diagnostic; resolution
continues with stylesheet declarations, inheritance, and initial values. The resolution
ABI returns updates rather than the inline compiler's private status. An exhausted
runtime can continue resolving and removing sheets; create a new runtime to reset its
lifetime counters. This contract covers controlled compilation/capacity errors, not a
general strong exception guarantee for every allocation failure.

Diagnostics from `add_stylesheet` are collected with owned payloads, then delivered
synchronously in order after the entire add has committed or its staging has been
discarded. Successful outputs, counters, index, and cache invalidation are complete
before the first callback. A callback can add another sheet, which receives later
global positions, or remove the just-added sheet. The outer call does not restore
counters, reinsert sheets, or invalidate styles resolved by callbacks afterward.

Each add captures its callback and user data at entry. Changing the registration
affects new calls, including nested adds, but not remaining messages in the current
batch. Keep captured callbacks and user data valid until their active calls return.
Message strings remain borrowed only for each callback's duration; copy them to retain
them. A null callback avoids collecting messages. Batches are local to calls, and
nested delivery finishes before the next outer message. Callbacks are never invoked
from the buffer destructor. This contract is specific to adding sheets: it does not
make every diagnostic during style resolution reentrant, permit destruction of an
active runtime, or permit exceptions from user callbacks to cross the C ABI.

Media compilation uses a per-call arena that owns allocated nodes rather than shared
edges. Aborts delete each new node once, including operands discarded by constant
folding. Success deletes unreachable local nodes and transfers reachable nodes to the
existing raw-pointer contract. This change does not repair the historical ownership
of reachable media graphs in successfully added sheets. Compiler memory tests explicitly
clean their successful graphs so they can isolate rollback and orphan cleanup.

`aether.stylesheet-source-order` exercises the public cascade and lifecycle.
`aether.stylesheet-source-order-compiler` covers metadata, priorities, boundary guards,
rollback, and shared media graphs. `aether.stylesheet-source-order-runtime` builds a
private copy of the core for counter/cache/map checks and injects a reduced source-size
predicate only in its compiler translation unit, exercising inline rejection without
allocating gigabytes. It changes no production limit or installed ABI. All new checks
remain active with `NDEBUG`. Run the focused compiler test with ASan and leak detection
as well as UBSan; passing a run with leak detection disabled is not leak validation.
`aether.stylesheet-diagnostic-reentrancy` additionally checks the public diagnostic
transaction boundary, nested additions/removals, batch payloads, and callback capture.
The white-box runtime test covers those operations near ID/handle/source limits and
checks that maps and cached states computed during callbacks survive the outer return.

### Stylesheet block boundaries and EOF

The source compiler keeps typed component boundaries for parentheses, brackets and
braces. A mismatched closing token is content, not a close for an ancestor. Strings,
URLs, comments and escapes pass through the component lexer, so punctuation inside
them cannot expose a false sibling rule. A real opening brace may end implicitly at
EOF; a prelude without a real opening brace still produces no rule. The original and
comment-masked source views retain identical byte offsets, preserving original
selector comments and escape terminators.

A per-compilation index stores each brace's body end separately from its next source
position. Real closes are excluded from the body and consumed by the next position;
EOF uses the source end for both, without inventing a character or computing EOF+1.
One typed token pass builds the index. A shared forward-only cursor visits each extent
at most once, including ignored blocks. An explicit frame stack visits media bodies
in depth-first source order, eliminating repeated ancestor scans and native-stack
recursion. Entering a child advances its parent's source position, not the index
cursor past that child's extents. IDs, selector-group positions, inactive rules,
compilation-local ownership and transactional publication keep the contracts above.

Media condition evaluation also uses an explicit stack. Per-call memoization handles
shared DAGs without repeatedly evaluating a shared node, and is discarded before a
later environment change. Three-valued AND/OR/NOT preserve unknown until the final
match comparison. Leaf conditions avoid allocating traversal state. This does not
cache results across rules or fix ownership of successful media graphs.

The boundary behavior is checked against Chromium source at
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/tree/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71):
[`CSSTokenizer::BlockEnd`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_tokenizer.cc#L66-L86)
only pops the matching type;
[`UncheckedSkipToEndOfBlock`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_parser_token_stream.cc#L51-L74)
stops at EOF; and
[`ConsumeStyleRule`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_parser_impl.cc#L3081-L3091)
requires a real opening brace. Chromium's streaming guards and nested rule objects
are not copied here: AetherUI uses an offset index and emits flattened conditioned
rules. Media AND/OR/NOT retain Chromium's
[`KleeneValue` tables](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/kleene_value.h#L19-L52),
but Chromium's recursive visitor short-circuits and records dependency flags, while
AetherUI's pure evaluator keeps evaluating both operands as before. This is structural
recovery, not full CSS EOF/value-grammar conformance.
It does not synthesize closing text for calc(), var(), strings or property parsers,
and it does not add CSS nesting, unsupported at-rules or new media features.

`aether.stylesheet-eof` checks public rule counts, computed widths and environment
changes. `aether.stylesheet-eof-compiler` checks DFS metadata, original selector text,
rollback and reentrant compilation. Their closed/open/mixed deep variants use 10,000
real media conditions, not constant-folded `all`. `aether.stylesheet-block-index`
compares the production index with a bounded recursive component oracle over 22
fixed and 200,000 deterministic adversarial inputs, counts token/extent visits, and
checks offset limits without allocating a 2 GiB source. Its deep variant uses up to
100,000 levels. `aether.media-condition-iterative` checks ternary truth tables,
environment changes and deep shared DAGs. Every new check remains active under NDEBUG.
For small native-stack validation on POSIX, run the deep executables in a shell with
`ulimit -s 256`; no platform-specific stack API is added to the library or tests.

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
pipeline responsibilities; the existing explicit-inherit limitation for non-inherited properties remains covered
by regression tests. Inline pending shorthands now share the atomic declaration pipeline.

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

### Ordinary HTML tag openings

An ordinary start tag begins only when an ASCII letter immediately follows `<`.
Otherwise the parser emits that `<` as text and processes the next source byte
normally, preserving whitespace and later real tags. Entity-decoded text is not
parsed again as markup. Every accepted start-tag name is canonicalized from ASCII
`A`–`Z` to `a`–`z` before creating the element, including unknown names such as
`X-CuStOm` and `A_B:C.D9`. The existing name-tail grammar remains unchanged,
as do comment, declaration, processing-instruction, end-tag and raw
`style`/`script` handling. This is a bounded correction, not general HTML5
conformance; malformed end tags and incomplete valid start tags retain their
existing simplified behavior.

`aether.html-tag-open` uses public ABI checks active under `NDEBUG` for ASCII
boundaries, actual EOF, reconsumption, complete trees/attributes, parent relations,
ID lookup, reparsing and the real style runtime.

This intentionally changes the observable result of `aether_element_get_tag_name`:
HTML parsed as `<DiV>` now returns `div`, not the original spelling. The binary C
ABI, exported types, signatures and version are unchanged. Attribute names follow
the separate ASCII identity contract below: `id` and `ID` name one attribute,
whose first parsed value wins. Attribute values, ID/class values, text and raw-text bytes
are not case-folded. This is ASCII normalization, not locale-dependent or Unicode
case folding, and adds no XML, SVG/MathML namespace or custom-element semantics.

The normalization boundary follows Chromium's tokenizer at
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/html/parser/html_tokenizer.cc#L361-L425):
the start-tag initial and remaining name characters use ASCII lowercase. This is
a source comparison of token identity, not a claim of Chromium's full grammar,
tree construction, or the web DOM's `Element.tagName` presentation contract.

`aether.html-tag-names` checks literal getter results, nonfirst HTML-root selection,
BODY lookup, all four focusable controls, all void elements, mixed/stray closes,
attribute and ID preservation, complete and long names, and replacement/version
lifecycle through the public C ABI. `aether.html-tag-name-consumers` additionally
checks native control classification, actual geometry and display-list text,
BR line-break semantics and author overrides, and the HTML canvas background.
Both use explicit checks active under `NDEBUG`. The historical tag-open, end-tag
and raw-text corpora retain their inputs and tree/text checks with canonical name
expectations. Run these with the complete native suite, C11 smoke test and
installed package consumer. Native execution does not establish .NET execution,
Chromium execution or visual equivalence.

### HTML attribute-name identity and duplicates

Attribute names in the native HTML DOM are canonical ASCII lowercase: only bytes
`A`–`Z` become `a`–`z`. The parser, C `get_attribute` / `has_attribute` /
`set_attribute` APIs, and CSS attribute-selector compiler share that identity.
Enumeration returns canonical keys in the existing sorted map order. Get and has
are read-only; an absent get returns null, while a present empty value returns an
owned empty string and has remains true. The setter replaces the one canonical
entry and preserves literal value bytes, without decoding HTML entities.

Within each opening tag, the parser keeps the first source occurrence for an
ASCII-equivalent name, including empty and boolean values. `DATA-X=First
data-x=Later` stores only `data-x=First`. Later values are fully consumed and
decoded but do not replace or merge the first value. The entire opening is still
discarded if EOF arrives before its syntactic terminator. ID lookup, classes,
selectors, inline style and control attributes read the same canonical storage.

CSS selector names are canonicalized **after escape decoding**: `[DATA-X]`,
`[DaTa-X]` and `[\44 ATA-\58 ]` all query `data-x`. Values retain their existing
comparison rules: `[DATA-X=Value]` differs from `[DATA-X=value]`; `i`/`I` and
`s`/`S` flags apply only to values. ID and class values, URLs, text, raw text,
custom-property names and non-ASCII bytes are not folded. This does not broaden
the HTML name grammar, add namespaces/XML/SVG/MathML, or change the existing
value matcher (including its locale-sensitive `i` handling).

**Compatibility:** enumeration and formerly case-distinct native attribute keys
change observably, but ABI versions, structures, signatures and lifetimes do not.
Reparse original HTML to obtain canonical first-wins storage; a sorted old map
cannot recover the source order of competing spellings. Attribute setters still
do not bump document version or automatically invalidate style. Explicitly call
`aether_style_runtime_resolve_subtree` on the affected style root after mutation;
include siblings/ancestors for relational selectors. That root is also the
inheritance root, so resolving a smaller subtree does not recover external
ancestor inheritance. `disabled` attribute presence does not set `:disabled`
state bits, and host inline style does not apply directly to generated pseudos.

`aether.html-attribute-case` covers parser/API identity, byte preservation,
duplicate casing/empty values, EOF, versioning and reparses.
`aether.selector-attribute-case` covers decoded AST identity, all seven operators,
value flags, metadata, composition, pseudo hosts and invalid-list recovery.
`aether.html-attribute-case-runtime` covers real cascade/layout and mutation in the
same runtime and stylesheet, explicit root resolution, retained snapshots,
pseudo lifecycle, ID preorder and exact style impacts against lowercase controls.
All use checks active under `NDEBUG`. Historical duplicate/tag/raw-text/pseudo
inputs are retained with the newly canonical expected names. Run these with the
complete native suite, C11 ABI smoke and the installed-package consumer.

Scope is native. Managed .NET `ElementNode` still uses Unicode
`OrdinalIgnoreCase`/`ToLowerInvariant`, including its public `Attributes` view,
and can conflate non-ASCII names that native code keeps distinct. Its managed
projection and attribute synchronization were not changed or validated by these
native tests. Native success is not .NET, browser or visual validation.

The identity boundaries follow Chromium source at
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/html/parser/html_tokenizer.cc#L835-L921):
its tokenizer lowercases names before
[first-wins deduplication](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/html/parser/atomic_html_token.h#L333-L379),
and its [CSS compiler](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_selector_parser.cc#L1512-L1565)
folds decoded names in HTML documents. AetherUI has no HTML/XML mode distinction;
this is its simplified HTML contract, not empirical Chromium conformance.

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
behavior (including vertical tab), and self-closing behavior for non-void
elements remain unchanged.

An opening tag is committed only after its syntactic `>` or `/>` terminator.
EOF before that terminator discards the entire opening, including all attributes,
without adding a node or text. A `>` inside a quoted value is data, not a
terminator. Already completed openings remain in the tree even without an end
tag, including completed `style` and `script` openings with text through EOF.
This does not expand name/attribute grammar or change raw-text/end-tag rules.

## HTML unmatched end tags

An end tag with no matching open element leaves the insertion context intact.
A matching tag closes the nearest open element with that name and its open
descendants, using ASCII case-insensitive comparison against the DOM's canonical
start-tag names. Previously closed, void and self-closed elements cannot
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
O(depth × closes) visits. End-tag token consumption and end-tag EOF recovery are unchanged.

## HTML style and script text

`style` and `script` names are matched ASCII case-insensitively when entering
their text mode and recognizing the matching end tag. Public DOM tag names are
canonical ASCII lowercase. Their content is one literal text child (or no child
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

## Multiple IDs in a compound selector

Every ID occurrence in a compound is retained and matched as a conjunction.
`#target#target` matches `id="target"` and has specificity `(2,0,0)`.
`#target#other` is equally valid syntax and has the same specificity, but cannot
match one element. IDs are case-sensitive after CSS escape decoding. Repetition
is not deduplicated, and IDs separated by a combinator constrain different nodes.

Impossible branches remain in logical selector lists and compiled rules.
`:is(#never#other,.item)` can match `.item` with specificity `(2,0,0)`;
`:not(#target#other)` matches an element whose ID is `target`.
`:is`, `:not` and `:has` retain their maximum-branch specificity, while `:where`
contributes zero without losing its dependencies. Existing strict/forgiving list
policies and the prohibition on pseudo-elements inside `:has` are unchanged.

The index uses the first direct ID of the rightmost compound, never an ID from
an alternative, ancestor or descendant. A candidate still passes the full matcher.
Changing an ID requires explicit resolution of a subtree containing every affected
subject, including ancestors for `:has` and sibling subjects. This adds no automatic
attribute invalidation. Generated `::before`/`::after` subjects use the host identity.
The public C ABI, DOM ID lookup and stylesheet ordering/capacity contracts are unchanged.

`aether.selector-multiple-ids-parser` checks full AST vectors, decoded identity,
specificity, dependencies and recovery. `aether.selector-multiple-ids-compiler`
checks publication, anchors, candidates and transactional limits.
`aether.selector-multiple-ids` checks the public ABI, cascade, repeated ID mutations,
incremental state changes, stylesheet lifecycle and real generated content.
Checks stay active under `NDEBUG`; size cases include 0, 1, 2, 3, 32 and 1024 IDs.
The public test observes fresh updates for changed subjects and keeps only scalar
snapshots for unchanged subjects; `aether_style_map_get` is currently a stub.

The semantics were compared with Chromium source at
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/tree/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71):
[`ConsumeCompoundSelector`/`ConsumeId`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_selector_parser.cc#L1316-L1326)
retain successive simple selectors,
[`SpecificityForOneSelector`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/css_selector.cc#L245-L270)
counts each ID, and
[`SelectorChecker::CheckOne`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/selector_checker.cc#L1538-L1540)
compares each ID with the element's ID. This is source-level reference, not a
Chromium execution or visual comparison. Chromium's packed specificity saturation,
quirks mode and broader selector grammar are not imported.

## CSS identifier starts in selectors

Identifier admission uses the source spelling before escape decoding. A name
starts with an ASCII letter, `_`, a valid non-ASCII scalar or an escape. A leading
`-` must be followed by one of those starts or another `-`. Digits and dashes
remain valid later in a name. Thus `.123`, `#123`, `.-1`, `-` and `[data-x=123]`
are invalid selectors; `.\31 23`, `#\31 23`, `.\-1`, `\-` and
`[data-x="123"]` remain valid. Numeric HTML IDs, classes and attribute values
are unchanged. Quoted values retain their existing string rules.

A literal `-->` at the start of an ident is a CDC token, so `.-->a` is invalid.
The hash path is different: `#-->a` remains ID `--`, a child combinator and type
`a`. Comments or escapes prevent CDC formation: `.--/**/>a` and `.-\->a` are
valid, while `.--\>a` is one class named `-->a`. CDC is not searched inside an
already-started name; `.a-->b` still means class `a--` followed by child type `b`.

Admission neither joins comments nor changes structural recovery. `./**/name`
is valid but `#/**/name` is not a hash ID. Escaped punctuation stays name data,
including in a rejected branch. UTF-8 validation, hex escape terminators,
replacement scalars and the private selector API's trailing-escape behavior
remain unchanged. The parser does not import custom-property restrictions:
`--` is a valid general identifier.

The public selector-list API still discards invalid branches and requires a
survivor. IS/WHERE discard invalid branches and may remain present but empty;
NOT/HAS reject an invalid argument branch. Rejected branches contribute no
specificity, dependencies, anchors, rule IDs or source positions. A valid
never-matching `:is(.123)` still publishes one rule. For example,
`:is(#123,.item)` has only class specificity, while `:is(#\31 23,.item)` retains
the valid ID branch and its specificity. No AST, matcher, ABI or binding change
is needed.

The source comparison uses Chromium revision
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_parser_idioms.cc#L74-L86):
`NextCharsAreIdentifier` defines the start rule,
[`Hash` and `HyphenMinus`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_tokenizer.cc#L89-L115)
distinguish hash ID from contextual CDC, and
[`ConsumeId`/`ConsumeClass`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_selector_parser.cc#L1484-L1508)
require those token kinds. This is a bounded source comparison, not Chromium
execution, a full tokenizer implementation, stylesheet-list policy parity or
visual conformance.

`aether.selector-identifier-start-parser` checks source/decoded distinctions,
UTF-8, EOF, comments, CDC ASTs and metadata recovery.
`aether.selector-identifier-start-compiler` checks publication, contiguous IDs,
shared order/media/declarations, indexes and capacity.
`aether.selector-identifier-start-runtime` checks the public ABI, cascade,
mutations, repeated state transitions and stylesheet/generated-content lifetime.
All checks remain active under `NDEBUG`; CMake/CTest, direct native builds and
binding execution are separate validation stages.

## Pseudo-selector name identity

Supported pseudoclass names are ASCII case-insensitive after CSS escape decoding:
`:hover`, `:HOVER` and `:\48 OVER` have the same meaning. This applies recursively
inside selector functions and preserves the original top-level selector source.
Decoded pseudo names and HTML attribute names each follow their own ASCII
normalization boundary. Classes, IDs, attribute values and custom-property names
retain their existing identity and comparison rules.
Pseudoelement names `::before` and `::after` already follow the same ASCII rule.

`aether.pseudo-name-parser` checks all 22 existing names, recursive AST fields,
specificity, dependencies, strict/forgiving list policies, escapes and negatives.
`aether.pseudo-name-runtime` checks public-ABI matching, cascade precedence,
generated styles and repeated state deltas without recompiling stylesheets.
Both keep checks active under `NDEBUG`. Runtime transitions use only the four
public states; parser coverage of other states does not imply HTML state derivation.
This does not add names, single-colon pseudoelements, Unicode case-folding,
relative `:has()` arguments, or `nth-child(... of ...)` support.

## Comments in selectors

CSS comments separate tokens without becoming whitespace. `.card/**/.active`
remains one compound selector, while `.card/**/ .active` retains its real
space and descendant relation. Comments do not merge names, hashes, function
names, attribute operators or numbers: `.ca/**/rd`, `#/**/host`,
`:is/**/(.card)` and `[x~/**/=a]` are rejected. A class delimiter and its
identifier may be separated (`./**/card`), as may colon tokens and a pseudo name.
Quoted attribute values and escaped punctuation remain literal data.

The stylesheet compiler retains the original selector bytes across nested
`@media` blocks alongside its existing, offset-preserving comment mask for
rule discovery, media and declarations. This correction does not change value,
custom-property, shorthand, inline-style or importance parsing. Selector lists
use strict `:not()`/`:has()` and forgiving `:is()`/`:where()` policies.
An+B keeps checked int32 arithmetic and existing syntax; comments cannot join
separate numeric tokens. For example `+/**/n` is valid but `+/**/2` and
`-/**/n` are not. An unfinished comment consumes through EOF, so it cannot
manufacture a closing function or a new stylesheet rule.

`aether.selector-comments-parser` checks recursive ASTs, specificity,
dependencies, token boundaries, escapes, An+B and bounded adversarial input.
`aether.selector-comments-compiler` checks rule metadata, nested media and
unchanged declarations. `aether.selector-comments-runtime` checks public-ABI
matching, mutations, cascade parity and generated pseudo-elements. Checks remain
active with `NDEBUG`. This adds no selector syntax, public ABI or global parser
work/depth limit, and does not establish general CSS or browser conformance.

## Strict lists in `:has()`

Within AetherUI's existing selector subset, every argument branch of `:has()`
must parse completely. An unknown pseudo, malformed attribute or escape, empty
branch, invalid An+B, or dangling combinator rejects the whole `:has()`. No
partial AST, specificity, dependencies or rule is retained. This applies after
pseudo-name escape decoding and ASCII case folding, including `:HAS()`.

Each nested function still owns its list policy. `:has(:is(.ready,:unknown))`
is valid because `:is()` discards the invalid branch. In
`:is(:has(.ready,:unknown),.safe)`, only `.safe` survives; `:where()` recovers
the same way with zero specificity. `:not()` remains strict. Empty or entirely
invalid `:is()`/`:where()` arguments are valid lists with zero branches and never
match. Their nested AST list remains present, with zero specificity and no
nested dependencies. Thus `:not(:is())` is valid and matches eligible subjects,
while `:has(:is())` is valid but never matches. `:not()` and `:has()` themselves
still reject empty arguments or any invalid branch. The top-level selector-list
API remains forgiving but requires at least one valid selector; `:is(),.safe`
retains both selectors and compiles both rules, even though the first never matches.
This extends the previously documented scope of the strict-HAS cycle.

Valid `:has()` lists use the maximum specificity of all their branches, even a
branch that does not match. Descendant anchoring, logical DOM boundaries,
generated-node exclusion and state-change resolution are unchanged. After DOM
mutations, explicitly resolve a common ancestor of the affected subjects.
A sheet whose selectors are all rejected still has its normal handle/lifetime;
adding or removing it can clear style caches and produce legitimate updates.

This is atomic list acceptance, not full browser selector conformance. Leading
relative combinators (`:has(> .child)`, `+`, `~`) remain unsupported, including
when mixed with valid branches. Internal combinators (`:has(.mid > .leaf)`)
remain supported. Existing nested `:has()` behavior is preserved, unlike
Chromium's nesting restriction; no new pseudo names, namespace syntax,
`:scope`, or `nth-child(... of ...)` support is added.
The list-acceptance comparison is source-based against Chromium revision
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_selector_parser.cc):
`ConsumeRelativeSelectorList` rejects a failed branch, and `kPseudoHas` checks
the whole list before publishing. This comparison does not require or imply
executing Chromium or validating rendering parity.

`aether.has-strict-list-parser` checks recursive ASTs, metadata, transactional
output, protected delimiters and malformed branches. `aether.has-strict-list`
checks public-ABI rule counts, cascade, recovery, media, stylesheet lifecycle,
DOM mutations and repeated state transitions. `aether.has-strict-list-compiler`
checks preserved declarations, rule IDs, media conditions, index buckets and
caller-owned rules after rejected blocks. All keep checks active under
`NDEBUG`; existing relative-anchor and generated-selector tests remain separate
controls. These bounded checks do not establish unlimited recursion depth or
global linear-time parsing. No public ABI, export or dependency is added.

### Pseudo-elements inside `:has()`

The parser rejects `::before` and `::after` anywhere inside a `:has()` argument,
including within nested `:is()`, `:where()`, `:not()` and currently supported
nested `:has()` functions. Rejection follows the existing local list policy:
`:has(::before)` and `:has(:not(::before))` are invalid, while
`:has(:is(::before,.ready))` retains only `.ready`. An all-rejected `:is()` or
`:where()` remains a present, empty argument, so `:has(:is(::before))` compiles
a never-matching rule and `:has(:not(:is(::before)))` requires a real DOM
descendant. Generated content cannot satisfy that requirement.

Rejected branches contribute no specificity, dependencies or index entries.
For example, `.card:has(:is(#never::before,.ready))` has specificity `(0,2,0)`.
A valid but impossible branch such as `#never:is(::before)` instead recovers
to `#never:is()` and retains its ID specificity and dependencies; `:where()`
suppresses its specificity but not its dependencies. The top-level list remains
forgiving, so `.card:has(::before),.safe` still publishes the `.safe` rule.

The restriction belongs only to the argument. `.card:has(.ready)::before` and
`::after` still generate content normally. Existing behavior of logical pseudos
outside HAS, nested HAS, CSS escapes, comments and quoted attribute data is
unchanged. This is not a general pseudo-element grammar change or full browser
selector conformance; single-colon pseudos and leading relative combinators
remain outside this correction.

The source comparison uses Chromium revision
[`7fe04cd3b6456bd7baec869f6a5efaebd6e79b71`](https://github.com/chromium/chromium/blob/7fe04cd3b6456bd7baec869f6a5efaebd6e79b71/third_party/blink/renderer/core/css/parser/css_selector_parser.cc#L1710-L1721):
HAS unconditionally establishes a pseudo-element prohibition, and the parser
rejects pseudo-elements under that context. Chromium restores the previous
context on exit; AetherUI passes a private context by value to each subparser.
The comparison does not imply Chromium execution, its additional nested-HAS
restriction, or visual parity.

`aether.has-pseudo-element-parser` checks recursive ASTs, metadata, protected
tokens, bounded nesting, transactional output and independent concurrent parser
calls. `aether.has-pseudo-element-compiler` checks declarations, rule IDs, media
conditions and index publication. `aether.has-pseudo-element` checks the public
ABI, cascade, mutations, state changes and stylesheet/generated-content lifetime.
Generated pseudos are inspected through current style updates and their text
children, not public child counts or handles retained across resolutions. All
checks stay active under `NDEBUG`; no matcher, public ABI or binding is changed.

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

## Unspanned table cell completion

Phase-1 automatic table columns allocate a common row margin height to their
collected cells. Each cell keeps its native resolved margins and decorations:
its final border height is the row height minus its top/bottom margins, with a
padding/border floor. Negative margins can therefore make a border box taller
than its row. Relative offsets move the visual cell, not the following row.
Ordinary in-flow children keep their natural size and top placement.

Authored height and min/max-height determine the natural row contribution.
The subsequent table allocation is authoritative and is not capped again by
max-height. A cell naturally capped at20 beside a cell80 finishes at80; a
min-height100 cell makes the row100. Generic blocks and flex items retain their
existing height clamps.

Native percentage semantics are intentionally bounded: auto height or min-height
alone does not establish an in-flow percentage reference. An authored definite
height keeps its original reference even after row allocation: height40 with an
in-flow height50% child still gives child20 in a row80. Percentage/calc cell
heights resolve only against an originally definite incoming containing height.
Absolute descendants instead use the final padding box; prepared effective-fixed
owners are reconstructed against final geometry. This is a native compatibility
rule, not a claim of browser table conformance.

Cells retain their in-flow subtree and finish direct positioned children, scroll
ranges and relative placement once after row sizing. Their scroll viewport uses
the final content height, retaining original raw flow extent. Table-related
prepared fixed reconstruction repairs affected ancestor ranges without sampling
offset callbacks or changing stored offsets. Explicit hit/input consumers still
sample and clamp requested offsets; display lists emit host-managed scroll scopes.
Contextual visual overflow sampling remains a separate later pass.

Positioned measurement callbacks can now occur after sibling cells' in-flow
callbacks. There is no callback ordering guarantee; measurement callbacks must
remain deterministic under the existing contract. Row completion does not repeat
in-flow nested-table layout. It uses row-local continuation records and, when
positioned boxes were indexed, linear pointer scratch to restore cell flow then
completion paint-index ordering. No persistent layout cache is introduced.

Spans, row-group sizing, table/row authored-height distribution, border collapse
or spacing, anonymous table construction, baseline/vertical alignment,
fragmentation and vertical writing remain outside this phase.

## Atomic shorthand declarations

The declaration compiler stages every longhand expansion before publishing the group.
An invalid component rejects the whole declaration at parsing time, retaining earlier
cascade candidates. Registered shorthands declare their complete destination set, checked
when the registry is sealed; missing or duplicate destinations cannot produce partial styles.
The six box shorthands (`padding`, `margin`, `inset`, `border-width`, `border-style`, and
`border-color`) accept global keywords only as their whole value, never mixed with components.

A syntactically valid `var()` shorthand participates with its original priority even when
substitution later fails. Each group resolves and validates once per node. Failed groups
offer `unset` to every destination; higher-priority longhands survive, while lower-priority
candidates do not reappear. Stylesheet and inline declarations use the same path. Defined
empty custom properties substitute zero tokens without activating a fallback.

Pending declaration validation consumes the whole original value, including unused nested
fallbacks, before cascade participation. Malformed `var()` calls, bad strings/URLs,
unmatched closing blocks, forbidden top-level punctuation and restricted top-level brace
combinations are rejected at parsing time. This does not parse the final property grammar
prematurely: `bogus var(--p)` is pending and becomes invalid only after substitution.
Importance belongs to the original declaration, not to substituted text.

`aether.shorthand-atomicity` verifies the production public ABI, priorities, variable scopes,
inline styles, repeated mutation and numerical layout. `aether.shorthand-compiler` checks
staging, destination integrity, modes and pending preservation with an adversarial private
registry. `aether.pending-declaration-syntax` checks token validation, protected strings/URLs,
empty fallbacks and bounded-stack deep nesting. All three use checks active with `NDEBUG`.
Existing transition pipeline tests now require inline pending values to resolve and update.

This preserves the supported property grammars, units, initial values, explicit `inherit`
limitation for non-inherited properties, and existing `revert`-as-`initial` behavior. It does
not change the public ABI or require browser execution. The current custom-property scope
is independent of ordinary computed values, so offering staged values or `unset` at their
original priority is equivalent to pending candidates in this runtime.

### Empty forgiving arguments

Only `:is()` and `:where()` permit a valid zero-branch argument list. Empty
branches, comments-only bodies and syntactically invalid branches are discarded.
Balanced outer parentheses remain mandatory; this does not repair unclosed
functions, strings or comments. A valid but impossible branch, such as
`#ghost:is()`, remains in its parent list and retains its ID specificity and
dependencies. A rejected branch such as `#ghost:hover:unknown` contributes neither.
`:where()` still suppresses specificity, not dependencies of retained branches.

The native `aether.forgiving-empty-list-parser` test checks the real AST, matching,
metadata, strict boundaries, protected delimiters and repeated bounded nesting.
`aether.forgiving-empty-list-compiler` checks rule IDs, shared group source order,
media conditions, declaration preservation and index buckets.
`aether.forgiving-empty-list` checks the public ABI, cascade, state-delta resolution
on subjects/ancestors/descendants, explicit resolution after DOM mutations,
stylesheet add/remove/readd and generated content. These checks remain active
with `NDEBUG`; generated-node lifetimes are not inferred from update counts.

Historical HAS, NOT, pseudo-name, escape, comment and nth tests retain their original
corpora with updated expectations. The .NET integration runner registers
`ForgivingEmptyListTests`, checking rule counts, computed opacity, nested
composition, strict controls, cascade and repeated resolution. Execution results
for native and .NET tests must be reported separately; adding these tests does
not establish .NET execution, Chromium execution or visual equivalence. No new
DOM invalidation contract, public ABI or global recursion limit is introduced.

### Inline capitalization continuity

Native `text-transform: capitalize` carries a stack-local word-start context across
ordinary inline descendants, adjacent text nodes and style changes. Measured,
stored and painted fragments retain their original owner/style partitions; DOM
text is not rewritten. Empty nodes and `display:none` are transparent. Hidden
text still participates. Out-of-flow descendants do not affect surrounding
capitalization; nested positioned content retains its existing flattened output.

This is a bounded native contract, not complete browser casing parity. Explicit
breaks, blocks, atomic inline boxes and opaque controls separate sequences.
Nested block-in-inline geometry and nested positioned geometry remain unchanged.
Direct discarded whitespace advances capitalization but still has no emitted
space box. Split fragments remain separate wrapping/min-content tokens: under
the regression metrics `Hel` + `lo` has max-content 41 and min-content 27, whereas
unsplit `Hello` has both 41. Soft wrapping does not restart capitalization.
Casing remains the existing byte-oriented, whitespace-delimited algorithm, not
Unicode/language/punctuation-aware title casing.

Focused regression targets are `aether_inline_capitalization_test`,
`aether_inline_capitalization_intrinsic_test` and `aether_text_transform_test`.
Run `ctest --test-dir <build> --output-on-failure -R 'inline-capitalization|text-transform'`
after building those targets. The integration test uses only the public ABI;
the intrinsic supplement reuses the existing private test core objects.

## Out-of-flow flex static placement

Each absolute or fixed child with both insets auto on an axis aligns its margin
box independently within its immediate flex parent's final content box. Final
height clamps and the originally resolved padding determine that rectangle.
Row/column, start/center/end, align-self, stretch-as-start and the sole-item
space-between fallback retain their existing native semantics, including negative
alignment residuals. Out-of-flow siblings do not participate in flex distribution.

The static rectangle is separate from the positioning containing block. Absolute
percentage sizes, percentage margins and explicit insets still use the padding
block. Fixed children use the viewport, or the completed effective transform
owner in prepared layout. Rebuilt fixed descendants take their static anchor
from the surviving immediate parent's completed content rectangle, even when a
different ancestor supplies their sizing/inset block.

Later parent placement and relative movement carry direct flex fixed children's
auto axes, while explicit viewport inset axes remain anchored. Ordinary
descendants inherit the allowed movement once; nested fixed barriers retain their
per-axis behavior. This is a bounded native contract, not a general rewrite of
non-flex fixed positioning or a claim of full browser CSS conformance.

`aether.flex-static-position` exercises public geometry and consumers in legacy
and freshly prepared layouts with active checks under `NDEBUG`.
`aether.flex-static-position-lifetime` shares the existing private core object
target and checks replacement reachability, indexes and allocation-failure
recovery. Registry checks and sanitizer runs with leak detection disabled do not
establish leak freedom. No installed header, ABI or capability changes are needed.

### Background position grammar

`background-position` and positions inside `background` share the native one- or
two-component parser. Keyword pairs are normalized by axis (`center left` equals
`left center`); numeric pairs retain X/Y order. Thus `left 10px` and `10px top`
are supported, but `top 10px`, `10px left`, `left right`, and `top bottom` are not.
Three/four-component edge-offset forms are not supported. Native lengths,
percentages, contextual units and supported linear `calc()` expressions retain
their coefficients; this is not a claim of full browser CSS numeric conformance.

Invalid literal positions are ignored before cascade comparison, preserving an
earlier valid declaration. A selected variable-containing declaration that becomes
invalid after substitution instead uses the existing initial-value behavior; it
does not restore an earlier declaration or use a defined variable's fallback.
CSS-wide handling is unchanged, including native `revert` meaning initial.
Position lists repeat modulo their length over image layers. Contextual positions
are stored independently of image count and inherit as computed measures.
Invalid background shorthands publish none of their five target declarations.

### Physical corner radius pairs

The four physical `border-*-radius` longhands accept one or two supported
length/percentage components. A single component supplies both axes; a pair is
horizontal then vertical. Slash-separated values, a third component, negative
literals and malformed components reject the whole declaration. Typed length
`calc()` expressions retain existing semantics, including internal division and
negative computed values clamped by used-radius geometry. Percentages use box
width horizontally and height vertically. Relative units compute in the existing
font/root/viewport context.

Ordinary invalid declarations leave earlier eligible values intact. An invalid
selected variable result becomes initial zero, not an earlier declaration or the
fallback of a defined variable. Explicit inheritance copies the selected computed
corner. The aggregate `border-radius` parser and independent registry-winner
architecture are unchanged: a selected physical corner overrides that corner of
the aggregate in either declaration text order. This is native compatibility,
not browser shorthand expansion or a general cascade correction.

`aether.corner-radius-pipeline`, `aether.corner-radius-context` and
`aether.corner-radius-geometry` exercise public grammar, deferred values,
context/lifecycle, aggregate controls and headless display-list geometry. The
existing `aether.transform-origin-dispatch` also checks corner parser aliases,
atomic application and registration/inheritance callbacks. These checks remain
active under NDEBUG. Display-list evidence is not pixel/browser parity; this
change does not add rounded native hit testing or change public ABI/bindings.

### Opacity numeric policy

Native opacity accepts a whole CSS number or adjacent percentage, with surrounding
CSS whitespace/comments. A private C locale converts to finite binary64; percentages
are divided by 100 and clamped to [0,1] before narrowing to float. Finite underflow
is accepted and zero is canonical positive zero. Binary64 overflow, NaN/Infinity,
hexadecimal syntax, trailing tokens and calc/math are rejected. Float round-trip
normalization preserves representable tiny values rather than rounding to six decimals.

An invalid literal preserves an earlier valid declaration; an admitted deferred
winner invalid after variable substitution leaves the native non-inherited initial 1.
Opacity still has no explicit-inheritance/CSS-wide callback: literal initial/unset/
revert and unsupported inherit yield native 1, and substituted CSS-wide words reject
to that same initial value. This is not browser origin rollback or full inheritance.

The active public `aether.opacity-pipeline` regression covers inline/sheet literal
and variable routes. Registry, transition and contextual controls remain in their
existing executables. Validation receipts distinguish headless command evidence from
pixels/browser parity and report unavailable locale/leak coverage explicitly.

The legacy transition runtime still suppresses opacity deltas at or below 1e-4
and interpolates in float without snapping the final sample. For example, an
endpoint parsed as 1e-7 remains exact in computed style, while a legacy 1-to-1e-7
transition finishes at 2^-23 through existing cancellation rounding. This parser
change does not alter transition precision or eligibility.

### Flex intrinsic horizontal widths

Wrapping rows keep distinct minimum and preferred widths: the minimum uses the
widest child contribution, while the preferred width sums the child contributions
and horizontal gaps. Nowrap rows retain the sum for both modes; columns retain
the existing widest-child horizontal reduction. Container padding and borders
are added once outside this reduction. Automatic flex minimums still request
minimum-content sizing, so wrapping containers may shrink below their preferred
width without shrinking below the widest contribution.

This is a bounded contribution model, not complete CSS Flexbox intrinsic sizing.
It does not add column-wrap preferred-width layout, reverse directions, writing
modes, constrained shrink-to-fit rules, or comprehensive child flex-basis/min/max
contribution constraints. Existing nonnegative-contribution flooring and deferred
percentage-gap resolution are unchanged. The public `aether.flex-intrinsic-width`
test covers fixed cards, decorations, signed margins with positive outer widths,
gaps, exclusions, parent clamps, atomic-inline propagation and result/style reuse.

### Absolute block insertion anchors

Direct absolute children with native BLOCK, FLEX or TABLE display use their
insertion point in an ordinary flow parent's content, independently on each
auto-inset axis. Explicit insets and the positioning containing block still
control sizing and their respective axes. The parent may itself be static;
deferred table-cell completion reconstructs anchors from the completed content
rectangle. Flex parents retain their separate static-placement rules.

Flow collection keeps private scalar insertion records. A pending inline run
receives a zero-consumption marker: it neither flushes the line nor creates a
box, contributes width/height, changes capitalization, nor adds measurement
callbacks. Prefix line metrics exclude later content. Parent-start collapse
moves only records dependent on its moved first block; form-control centering
applies the existing content translation. An absolute alone never consumes a
pending block margin. Existing whitespace-only flush and centering/collapse
limitations are unchanged, as are inline, pseudo-element and fixed paths.

Records are construction-only and are cleared during positioned completion,
including deferred cells. Snapshot cloning intentionally leaves the new vector
empty rather than copying its capacity. The private box gains one vector object;
records contain two node identities and two scalar offsets, with O(A) retained
capacity for A eligible children. Inline items gain one marker index. Actual
platform sizes, allocation traces and capacity observations belong to validation
receipts, not a public ABI promise. No public headers or capability bits change.

`aether.block-static-position` uses public geometry, hit, paint and scroll APIs
with literal expectations and active Release checks. Its lifetime supplement
reuses the authentic private core object pool for allocation-failure, publication,
clone, callback and document-lifetime checks. These tests define a bounded native
contract, not browser conformance, leak freedom or a performance claim.
