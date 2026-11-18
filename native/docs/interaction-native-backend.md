# Private native interaction backend (G0-M1)

`InteractionNativeDocument` retains one parsed DOM and native style runtime. A
scene retains the document and the exact style revision/map it was laid out
against. Document never retains scenes. Candidate scene failure destroys layout,
paint, prepared frame, masks and bridge; replacing the confirmed scene releases
its resources when its last shared owner leaves.

## Allocation and lifetime inventory

Included in the shared OwnedBudget domain, via allocator-rebound allocate_shared:
Document/State, StyleBundle, Scene/State, native Bridge and their control blocks.
Control ordinal vector and seed-view vector use OwnedAllocator. Temporary HTML
null termination and stylesheet strings use OwnedString. Paint-list and builder
object storage is reserved before allocation at their concrete sizeof, covering
the newly added fixed focus evidence (no dynamic focus metadata). Font source
bytes/immutable owners and alias objects are accounted by the source bridge;
there is one byte storage charge and strong ownership across all native aliases.

Excluded by G0-M1: existing DOM/parser/runtime storage, Inventory and its initial
authored strings, runtime revision/map storage, layout engine/results, style
handles, derived owner/occurrence lookup maps, legacy ControlPresentation strings,
FontProduct and ShapedRun instances, legacy masks, paint/preparation vector and
command storage. These are bounded by existing admission/work limits and retained
only by document or scene ownership; they are not new caches or authoritative
runtime input storage. Existing FontProvider work and cache bounds remain active.
A scene owns only derived runtime display strings: normal text or password bullets
and placeholders. It never receives plaintext runtime password values. Static
button labels are read from the immutable inventory, not state-machine values.
The scene's FontProducts and ShapedRuns refer to the same immutable source byte
owner, not a font-byte copy. No event reparses HTML.

The 256 MiB quota covers instrumented storage only, not total process/session
memory or RSS. This document does not claim accounting of excluded STL storage.

## Resize publication

The validator first shapes every candidate display string. Resize then allocates
an owned StyleBundle, prepares a native style transaction for the new viewport,
and creates a private pending map. No layout or hit-test consumes that map before
commit. The unchanged commit routine checks owner/thread/generation/sheet/DOM
provenance; failure discards the map and transaction. After success only noexcept
shared-pointer swap/destruction remains before the state machine's no-throw swap.
This backend creates its runtime with null diagnostic callback and user data;
`stylesheet_diagnostics_s::replay` immediately returns in this specific route.
This is not a claim that general runtime commits are callback-free.

Zero viewport retains authoritative zero dimensions and does not render; the
native contextual environment uses max(1, dimension) solely because native style
resolution requires positive measurements. Restoration prepares the actual
positive viewport before any layout. Strict admitted CSS has no media queries.

## Decorations

Default native/static and legacy builder calls keep a null interaction pointer.
Only the private interaction entry records one focused owner, border rectangle
and exact command index. Finalization allows the existing radius-2, alpha-.75
focus stroke only at that index with exact geometry/color. Other geometry keeps
square/opaque profile-2 validation. Input caret is a clipped solid rectangle based
on native advance, or zero advance for empty/placeholder, with no host callback.
