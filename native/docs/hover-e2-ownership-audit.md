# E2 ownership audit and acquisition protocol, revision 2

This is source analysis and proposed design, not a threaded execution test or
implementation PASS. Aether E1 source is frozen at
51b4fb53f78d01782a7de92f6ca4d4f8668dbebb. DOMsync references below use exactly
585804d5eac67232f8ea7eac829ba792e4e9e20e, read with git show. The latter is an
unaccepted dependency reference, not product imported into this branch.

## 1. Existing reference operations and all pin callers

A search of native/src finds exactly three direct accesses to layout `references`:
- `layout/layout_types.h:275`: size_t initialized to 1.
- `layout/layout_box.cpp:136-139`: LayoutOperationPin constructor increments;
  destructor calls aether_layout_result_destroy.
- `layout/layout_engine.cpp:1704-1705`: destroy decrements, deletes at zero.

The five current LayoutOperationPin call sites are:
- `layout/layout_engine.cpp::hit_with_pin`, line 1763. Failure must return null
  through the legacy wrapper, without entering cache/query or recording a miss.
- `layout/layout_engine.cpp::aether_layout_result_hit_test_input`, line 1807.
  Failure must return INVALID_STATE with existing cleared part/owner outputs.
- `rendering/display_list_builder.cpp::aether_display_list_build_with_styles`,
  line 813; and `aether_display_list_build_node_with_styles`, line 859. Failure
  must return INVALID_STATE before installing borrowed pointers in the builder.
- `animations/contextual_transitions.cpp::aether_contextual_transition_finalize_layout`,
  line 537. Acquire/check before the present pre-pin invalidation; on retain
  failure there must be no presentation write or serial change.

`layout_engine.cpp::aether_layout_layout` constructs at line 1582 and has private
failure deletes at 1632, 1679, 1684, 1691 and 1693. `layout_box.cpp::aether_layout_clone_snapshot`
constructs via unique_ptr at line 96. These are unexposed construction owners, not
additional shared retain sites. The proposed implementation replaces their direct
deletes/default unique_ptr deleter with an owned-result deleter using the checked
release primitive. Only the successful 1-to-0 release invokes delete. The private
builder owns its initial reference throughout construction and no raw output is
published on failure. Source clone borrows its original owner throughout reads.

Other layout APIs return borrowed boxes or inspect fields without incrementing
references. They still require a caller-owned live layout for the complete
operation. Atomic retain does not authorize access through a freed raw handle,
concurrent layout mutation, or retaining from a raw pointer after its owner ends.

## 2. Selected counter protocol

Use atomic<size_t> initialized to 1 for the layout and the new composite native
lease. A native retain returns status, not an unchecked increment. With an already
owned input reference, a CAS loop increments only values in [1, MAX-1]. Zero or
MAX fails with INVALID_STATE, changes no count, produces no output and transfers
no ownership. It never wraps, saturates into a pseudo-pin, or resurrects zero.
Acquire happens before dereferencing any layout fields except this protected
control, and before any operation which could release the caller's owner.

Release is noexcept: null does nothing; positive-count CAS decrements with release
ordering, and only the thread observing 1-to-0 performs the acquire fence and
storage destruction. A detectable zero-count release is a contract violation,
asserted in debug and rejected without wrapping/deleting again. This does NOT make
an already-freed raw pointer safe to examine; legacy double destroy remains invalid
caller use. SafeHandle and RAII ownership ensure valid calls release exactly once.
The private counters are not publicly writable; overflow/underflow fault tests use
an internal live test control, never a freed pointer.

LayoutOperationPin remains noexcept but records acquisition success explicitly
(null owned pointer on failure, bool/status accessor). All five callers above must
check it before further access. Its destructor decrements only an acquired pin.
There is no fake successful pin followed by an unearned decrement. New composite
create/query uses status-bearing pins. Existing shared_ptr immutable payload owners
keep their standard control-block ownership; do not reimplement their counters.

## 3. Managed acquisition must protect the original wrappers

Current public NativeLayoutResult and NativeStyleMap (`AetherUI.Core.Native.Interop/
NativeLayoutInterop.cs:153-173`, `NativeStyleMap.cs:4-19`) have IntPtr fields and
Interlocked.Exchange Dispose. Keeping an object reference or reading Handle does
not protect the pointer between that read and a native retain.

Selected change: each wrapper owns a readonly dedicated SafeHandle from initial
construction, not an IntPtr later copied into a second handle. Dispose closes that
same owner. NativeLayoutSafeHandle.ReleaseHandle calls checked layout release;
NativeStyleMapSafeHandle.ReleaseHandle destroys its uniquely owned native map
wrapper. Their native destructor closure is audited below. Owner-thread rules
continue to cover operations/mutation; closing a handle may race with an operation
only through SafeHandle's protected ownership. No native call accepts an unprotected
Handle property from these wrappers.

A stack-only disposable borrow acquires DangerousAddRef on the existing SafeHandle,
checks success, then obtains its pointer. Failed AddRef/closed owner never calls
DangerousGetHandle/native code. The borrow releases only if AddRef succeeded. No
object, closure or per-point allocation is required. Do not implement this as
"check IsClosed then get raw handle". Close concurrent with borrow either wins
before acquisition (failure, no dereference) or waits for the successful borrow's
release to run native cleanup.

Composite creation steps, before any native read:
1. Reserve managed destination wrapper/required staging; initialize native output
   to null. Borrow original layout, optional current map, DOM owner and offset
   snapshot handles. Acquisition order is fixed; partial failure releases acquired
   borrows in reverse and invokes no native create. Absent map is distinct from
   a disposed or explicitly empty map.
2. Invoke native composite create while ALL borrows remain held. Native initializes
   out=null before work, checked-retains layout, copies immutable map owners from
   the protected map wrapper, obtains authentic owner/root/offset ownership and
   captures provenance. It cannot invoke user code. Mutation stays owner-thread.
3. Native failure releases only successfully acquired owners and leaves out null.
   Native success transfers one complete composite owner. Install it in the
   already reserved SafeHandle without allocation; revalidate host publication
   authority. If superseded, release candidate without publishing it.
4. Release original borrows in finally. Subsequent queries borrow the composite
   SafeHandle for the complete native call and projection. They do not reacquire
   layout from the old wrapper. Final release waits for those borrows to finish.

Conversion of consumers is part of E2a, not an optional future audit. Current
wrapper consumers are NativeLayoutInterop (PrepareTransitionContext/Layout/HitTest/
HitTestInput); NativeLayoutResult getters/Read/GetChild; NativeDisplayListInterop
(Build/BuildNode); StyleRuntimeInterop (CreateResolvedStyleMap/UpdateResolvedStyleMap);
and StylePublicationInterop.PreparedPublication (delta, style and map preparation).
All operations borrow their source map/layout for the full native call(s), including
multi-field NativeLayoutResult.Read, and acquire new native outputs directly into
owned handles with failure cleanup. Compile-time removal/private restriction of
raw Handle access exposes omissions. Runtime/factory handles of unrelated types
retain their own reviewed lifetime contracts; this is not a blanket retrofit.

A borrowed NativeLayoutBox is still only valid with its actual owning layout and
current box lifetime. GetChild cannot legitimize a foreign/freed box. E2's new
status query returns identity/value data, never such a raw box as a retained result.

## 4. Destruction closure in frozen E1 source

The following is the actual ownership path, not just names for a future audit.
Implicit destructors mean destruction of listed members only; no hidden callback
is assumed. Allocation hooks supplied by the C++ runtime are outside Aether's
application-callback API. All live mutation and queries remain owner-thread.

1. `layout_box.cpp::~aether_layout_result_s` (29-45) clears local non-owning
   paint_order/primary_boxes. For abandoned private trees it severs child vectors,
   deletes registry boxes, then deletes root. It invokes no engine or callback.
   `~aether_layout_box_s` (48-53) calls owner->release_box before recursively deleting
   children. `release_box` (21-27) calls unregister_box, unlinks the LOCAL intrusive
   allocation registry, clears box owner and decrements LOCAL live_box_count.
2. `layout_types.h::unregister_box` (343-350) resets the local hit accelerator and
   advances/saturates the local presentation serial, erases the pointer from local
   paint/primary indices. It does not inspect DOM raw node fields; the pointer is
   used only as an index key. LayoutBoxRegistry::unlink mutates other boxes owned
   by this same dying result. At final count zero no operation or other owner can
   read/write that result; live re-layout deletion remains owner-thread as today.
3. `layout_types.h` result members: effective_context_owners releases an immutable
   ContextOwnersSnapshot; assets releases LayoutAssets; build_overlay (only during
   construction) releases private MutableLayoutOverlay; primary/paint/anonymous
   maps and registry contain borrowed addresses only and do not delete DOM nodes.
   A box's text/control strings and vectors are local values. Its BoxStyleSnapshot
   releases FrozenStyle; visual_identity releases only a weak token reference.
4. `layout/hit_acceleration.h::HitAcceleration` has no custom destructor. Its box
   and range vectors contain borrowed boxes/scalars; it never deletes boxes or
   touches their owner. selected_ releases MapVersionRef; three Epoch owners and
   cached_identity_ release shared identity controls. cached_target_ is borrowed.
   No index->layout strong edge exists. Offset owner in E2 will follow item 10.
5. `layout/layout_assets.h::LayoutAssets` releases base MapVersionRef, immutable
   FrozenSyntheticStore, default FrozenStyle and document identity. Its methods
   read tokens while alive but destructor does not. MutableLayoutOverlay and
   FrozenSyntheticStore release their local index/ordered owners. SyntheticEntry
   releases FrozenMeasures, FrozenStyle and shared_ptr<aether_node_s> synthetic
   node. `layout_flex.cpp::create_anonymous_flex_item` creates the anonymous shared
   node (around line 53); its parent/children/anonymous_flex_owner are borrowed.
   `dom_types.h::~aether_node_s` marks its own identity dead/unattached, releases
   value members and generated_storage, not raw children/parent. This is a real
   token write, not literally pure byte freeing. Any reader of that synthetic
   node must keep the assets/payload owner, so last node storage release excludes
   such readers. A token alone cannot grant permission to read that node.
6. `css/style_map_types.h::aether_style_map_s` owns only current MapVersionRef;
   `style_resolver.cpp::aether_style_map_destroy` deletes this unique wrapper.
   The safe borrow excludes concurrent wrapper destruction; owner-thread excludes
   concurrent writes of its mutable current shared_ptr. Copying different shared_ptr
   objects sharing one immutable version is safe; racing the SAME shared_ptr field
   against a writer is not made safe by its control block.
7. `css/style_map_version.h::MapVersion` releases backing, revision, document token
   and projection_source. `immutable_style_backing.h::ImmutableStyleBacking` owns
   an immutable radix tree; nodes release child refs and FrozenNodeRecord refs.
   ContextOwnersSnapshot/context_owners.h releases a PersistentIndex of identity
   refs and lineage only; observer counters run on build/lookup, not destruction.
   `style_revision.h::style_revision_s` releases its weak self, identities, immutable
   context owners, measurement document, backing, immutable entry/change/retired
   arrays and persistent directory/manifest/impacts. There is no callback/destructor
   body or mutable runtime back-reference in these owners.
8. `css/frozen_node_record.h` record releases payload/measures. Payload releases
   style, measure payload, custom-property scopes, color metadata, identity controls
   and authentic generated_leaf storage. Raw node_ is never deleted as a raw pointer.
   FrozenRetiredNode retains generated storage/token, with the same final-owner rule.
   Shared generated nodes may outlive their DOM attachment; explicit retirement
   and physical storage deletion are separate. E2 never turns a retained generated
   token into a live event target after retirement.
9. `css/frozen_style.h` FrozenStyle/StylePayload/Gradient/backgrounds/measure owners
   contain immutable arrays, strings/scalars and shared immutable document measures,
   with no runtime callback. `immutable_array.h::Storage::~Storage` destroys const
   owned elements in reverse order then frees its private Slot[]. `frozen_c_string.cpp::
   Storage::~Storage` destroys its char array and calls the fixed new_delete_resource,
   not a caller-supplied allocator. `style_runtime/frozen_runtime_metadata.cpp::ScopeDelete`
   is significant: it maintains a THREAD-LOCAL pending/deleting chain to reclaim
   FrozenScopeControl iteratively. It allocates nothing and dispatches no callbacks;
   only controls whose shared ownership ended enter that thread's chain. Parent
   scope shared ownership, local immutable properties/index and FrozenColorMetadata
   arrays form the rest of that closure. This TLS chain is not an owner-thread
   scheduling queue and requires no later event-loop drain.
10. E2 offsets do not exist yet. Their permitted concrete representation is an
    immutable PersistentIndex<OffsetEntry> (`css/persistent_index.h`), with each
    entry holding only float values, identity/disconnect and authentic storage/owner
    shared pins. Tree nodes own children, never historical roots or callbacks.
    Default destruction releases those values/owners; no user deleter or native
    lookup during destruction. Offset creation validates this representation,
    rather than accepting an arbitrary custom "read-only source" object.

### Member destruction order, not just the ownership graph

The result destructor BODY deletes boxes while assets and other members are still
alive. The first release_box resets hit_acceleration; repeated resets see empty.
If there are no boxes, the remaining accelerator is destroyed later as a member.
After the body, members are destroyed in reverse declaration order: local registry/
value containers, anonymous/paint/primary maps, build_overlay, assets, effective
context owners, then the early-declared hit_acceleration and counter/scalars.
No member destructor reads another already-destroyed result member. In particular,
a surviving accelerator only releases its own maps/tokens; it does not inspect
boxes or assets after their lifetime ended.

LayoutAssets releases its members in reverse too. At the DOMsync reference revision,
dom_pin is declared last and therefore releases BEFORE document identity, defaults,
synthetic and base. This is not permission to traverse borrowed synthetic children
while destroying synthetic storage: their destructor never traverses those links.
Base/payloads that can read live DOM retain their own root/relay; if none remain,
remaining identity controls are just tokens and no destructor dereferences them.
FrozenNodePayload's trailing owner-relay/lease members release before generated_leaf
and identity/style values; generated storage destruction uses no raw style_origin/
parent access. Therefore the closure does not rely on incidental member ordering
to protect such a dereference. Any future destructor adding one invalidates this
inventory and requires an owning pin/order change before it is accepted.

Nothing in items 1-9 licenses background query/mutation. Final destruction can run
on the releasing thread only because each live query/writer has a protected owned
layout/map/assets chain and exclusive final storage cannot still have such a user.
No optimization may keep a naked box or payload beyond that ownership scope.

## 5. DOMsync-owned closure and explicit dependency limit

Frozen E1 alone does NOT own all editable DOM storage. E2 host/composite capability
must reject it as insufficient rather than use an identity token as a storage pin.
The inspected 585804d ownership additions are concrete:
- LayoutAssets has `shared_ptr<const EditableDomPin> dom_pin` in layout_assets.h.
  EditableDomPin (dom_types.h:158-171) owns vectors of live owner relays, editable
  storages and generated shared storage. No finalizer walks raw parent/children.
- FrozenNodePayload owns editable_lease, document_lease, live_owner_pin and owner
  leases/relays; FrozenRetiredNode copies the same owners (frozen_node_record.h,
  style_revision.h). These cover historical style-origin/parent paths, not just node
  identity. Their default destructor decrements those owned references.
- EditableNodeStorage (dom_types.h:85-88) owns one unique node. DomNodeOwnerPin
  (90-92) owns current_root; identities have only weak storage/owner-relay links.
  Last relay destruction releases current_root. An adoption writer retains the
  relay in its staged moving_relays/adopt_relays before mutating current_root
  (node.cpp and dom_edit.cpp). Therefore its last destruction cannot overlap that
  writer. Finalizers never read/update current_root, only destroy the last object.
- Document registry owns editable storages but not itself. Explicit
  document_node.cpp::aether_doc_destroy checks owner thread, marks retirement and
  clears live registry, then releases the external legacy owner. E2 release MUST
  NOT call that function as its SafeHandle cleanup; it only drops retained owners.
- HOWEVER, `dom_types.h:243-255::~aether_doc_s` on last physical root release can
  set owner_control->alive=false, call aether_dom_retire_node over root/registry and
  dispose unmanaged children if no explicit retirement occurred. aether_node_s's
  destructor also writes identity flags. Calling this closure "no logical token
  writes" was inaccurate. The protocol allows only this final, no-reader cleanup,
  not independent retirement while any legitimate owner/relay survives.

Why that distinction matters: a layout count of zero excludes users of that
layout, not users of a shared DOM root. Root/relay shared ownership independently
excludes the latter. A legitimate DOM reader/writer/claim or generated-parent reader
must retain the real root/relay for the entire access. If any such user remains,
release of the E2 pin cannot be the last root/relay release. If it is last, there
must be no live interaction hook capable of observing or writing another live
registry. Root retirement may then touch only its exclusively owned dying domain.
Token-only readers, weak.lock promotion followed by unpinned access, or raw
current_root writers would invalidate the proof and block E2 capability.

The inspected interaction hook has a concrete ownership guard too:
`585804d:dom/dom_interaction.cpp::DomInteractionRegistry` owns a strong `document`
storage; each claim owns node storage plus live_owner relay and weak registry;
`aether_dom_interaction_retire_node` (137 onward) acts only after locking those weak
claims/registry references. A still-live registry therefore prevents the root from
being last-released. During final registry destruction its weak references cannot
be promoted; its document member is released only as its owners are unwound. This
supports the closure argument, but does not substitute for the complete DOMsync
consumer/root-pin gate. E2 does not install its own finalizer callback hook.

This is precisely the pending independent DOMsync global lifetime gate, not a proof
that 585804d already passes it. E2 must consume a named accepted DOMsync revision
with that property; this proposal does not approve 585804d or weaken its gate. If
that accepted revision changes the closure or relay protocol, update this inventory
before code. E2a lifetime tests include last release with a still-live other layout,
map, root holder, relay/adoption writer and claim; explicit doc retirement; GC last
release with no dispatcher; and sanitizer runs. The inventory fixes the proposed
protocol now, while implementation evidence remains a later independent gate.
