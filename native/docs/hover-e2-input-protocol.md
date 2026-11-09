# E2 discrete-action admission and bounded settling, revision 2

Proposal only. This completes review items B/C for the E2 ON experimental route;
it is not existing runtime behavior or implementation evidence. E2 OFF keeps its
historical assertions. The source model is UiDocumentHost.Pointer/Input/Scroll/
Render/Publication plus the separately accepted DOMsync interaction contract.

## 1. Admission precedes every possible callback

Each enumerated discrete action has an owned descriptor: original checked sequence,
kind (press/release/wheel-X/wheel-Y/key/text/leave), immutable arguments, host/session
identity, captured pending-prefix descriptor and progress, state and failure reason.
No raw target/box is authority. Event targets are resolved from current certified
inputs at execution; a reserved press does not pin authority to an obsolete hit.

Before EnsureVisualTree, DOM loss notifications, prefix settling, RenderRequested
or any action effect, the public entry does argument/owner-thread/disposed checks,
preflights the next sequence, and prepares the descriptor, queue capacity and any
independent prefix-lease owner. Queue admission publishes descriptor+sequence and
seals its exact older prefix by no-fail field writes. Only then may it invoke work
which can call user code. Replaying that descriptor never assigns another sequence.
The native prefix lease is retained under a protected source borrow and installed
in its own already allocated SafeHandle; sharing a managed reference to a handle
that the publisher may Dispose is insufficient.

Proposed bounded resource rule: E2 owns a 64-slot queue of admitted but not-yet-
terminal discrete descriptors, allocated when E2 is enabled on the host. The small
slots/sequence data are independent of document size. A descriptor and any owned
SafeHandle/payload staging are allocated BEFORE admission and prefix callbacks;
text arguments keep the supplied immutable string, not a document snapshot.
If full, OOM, retain overflow or sequence exhaustion occurs before admission, the
new call throws the corresponding resource/state error and is NOT accepted. Old
pending point, prior actions, claims, offsets and sequence remain unchanged. It
must not first flush a prefix and only later discover there is no action slot.
The capacity rejection is explicit experimental behavior, not silent input loss or
coalescing; it requires this design approval and a boundary test. Normal moves do
not allocate a discrete descriptor or consume these slots.

A queued descriptor can own one sealed older prefix plus its lease. Global Bnext
holds the latest compatible newer move. A later admitted action seals that Bnext
into its own descriptor; it cannot overwrite the earlier action's prefix. Thus
retention is one published lease, one global pending lease, at most 64 queued-prefix
leases and active stack scopes, not the one-prefix bound stated in E2 revision 1.
These independent owners may share native snapshots but must be released on every
terminal path. Peak retained layouts/maps/offset pages is measured, including the
worst blocked queue. Index ownership remains layout -> index, with no self-pin.

## 2. States and the exact first effect

Preparation is private and not admitted. Public states are:
- Queued: descriptor is durable; none of THIS discrete action's effects started.
  Its older hover/lifecycle prefix may already have made separately acknowledged
  effects. A blocked reason is attached to Queued, not a fake completion.
- Started: immediately before the first action-owned observable effect. This
  includes committed focus/state/claim/capture/scroll/text/default changes, native
  edit commit, a dispatcher/handler invocation or externally visible notification,
  whether or not any event handler is registered. It does NOT mean merely "first
  callback". Diagnostic counters, pure validation and private staging are not an
  action effect. Once Started, never transition back to Queued.
- Completed: its normal branch finished, including a legitimate no-op/no-target
  action. A pure no-op may go Queued -> Completed without pretending a side effect.
- Cancelled: terminal, with a precise reason (host/session replaced or disposed,
  authority lost, reentrant supersession, or exception after start). Partial effects
  already performed remain; this is not rollback or a successful completed action.

A started action maintains a small nonallocating phase marker as it advances its
existing branch, so instrumentation/fault tests distinguish default changes from
callbacks. A branch/effect is marked entered BEFORE calling code that may throw or
reenter. After a callback or committed default throws, terminal Cancelled means no
automatic resumption/replay of later effects. For example, a mouseup handler that
throws does not cause mouseup or a later click to be replayed on recovery. Normal
existing exception semantics are preserved once action effects start.

All fallible preparation possible for a default effect happens before that effect
starts; native typed commit remains no-fail as required by DOMsync. If preparation
fails while Queued, retain the descriptor for recovery. If it fails after an earlier
action effect, cancel the action rather than repeat the earlier effect. Host Dispose
or session/document replacement cancels unstarted descriptors explicitly and drops
their leases without callbacks into the replacement registry. A terminal reason is
counted, not turned into a cache miss or hidden successful delivery.

## 3. Prefix progress, ordering and reentrancy

Prefix effects are separate from action effects. Capture/detach the eligible older
move prefix before callbacks and store its point/sequence/certificate in the action.
A completed boundary dispatch is acknowledged exactly. A dispatch which begins and
throws is also consumed, never retried; lifecycle/state cleanup and a fresh query
can still be owed. Do not reconstruct old mouseout/over from a fresh registry.
DOMsync claim-generation/disconnect/request guards remain authoritative.

If a prefix callback throws before action start, the admitted action stays Queued,
with its original sequence and consumed prefix progress. Propagate the exception
from the original call, mark automatic retry blocked and leave Bnext intact. This
is an explicit promise that the accepted, unstarted action can run at a later safe
entry; callers can distinguish admission failure (no descriptor) from an admitted
operation blocked before action execution in diagnostics/tests. No accepted action
is silently dropped just because the public call unwound on prefix failure.

Recovery picks the oldest eligible Queued descriptor before any newer queued action
or newer move's hover. It revalidates host/session and settles only its remaining
older prefix/required cleanup; it does not replay consumed callbacks. It then
queries the action's OWN point with current inputs, and starts exactly once.
A newer physical move may immediately compute its cursor, as required, while its
hover remains Bnext. Its newer position must not replace the old action's sealed
prefix or be drained before that action. The action does not manufacture a move at
its down/up coordinates.

If a callback in an UNSTARTED action's prefix calls another discrete API, that new
descriptor is admitted behind the older one and cannot execute past it. This makes
the previously vague prefix reentrancy rule explicit. Once an action is Started,
nested explicit input may retain the current DOMsync request/claim takeover rules
(new press can supersede the outer started press). It still obtains a descriptor
before its own prefix work. It cannot pass some other already Queued older action.
This preserves existing started-action reentrancy without claiming that a newer
nested press can silently erase an unstarted accepted press.

Only one attempt of a particular queued action is made by automatic recovery in an
outer entry. Failure blocks that entry's automatic queue drain. On success, later
actions that were already admitted at the entry's captured cutoff may proceed in
sequence and under the SAME remaining budget. Callback-created queued Bnext is not
added to that cutoff. New explicit reentrant calls are user recursion, not license
for an internal drain loop to refresh the cutoff.

PointerLeft is also admitted in sequence. When eligible, its own unseen prefix is
terminally discarded; it emits out only for committed hover, never over immediately
before leave. It cannot delete an older queued action's sealed prefix or overtake
that older accepted action. Dispose/session replacement is the explicit terminal
cancellation rule above. Captured scrollbar moves remain synchronous and preserve
every admitted point; they are not placed in the ordinary coalesced-move slot.

## 4. One budget for the outer entry, including lifecycle

An allocation-free host execution scope is entered by the outermost public
Render/input/recovery-capable operation. It stores depth, eight remaining hover
passes, the captured automatic queue cutoff, lifecycleBatchTaken, reset state and
automaticRetryBlocked. Nested EnsureVisualTree, pending/barrier drains, RenderCore,
style-recovery callbacks and their nested host entries use this SAME scope; they
cannot initialize another eight passes or capture a larger automatic queue prefix.
Budget initialization happens only at depth zero for a new external entry.

One pass is consumed BEFORE attempting pending hover application/requery plus its
resulting style/geometry settlement. Every such automatic pass decrements the same
counter, including when called by a barrier rather than Render. A failed pass still
consumes its unit. Pure certified cursor queries do not themselves spend a hover
pass, but cannot invoke a hidden new settling loop. EnsureVisualTree's existing
bounded style-recovery allowance stays separate and cannot reset the hover budget.

DOMsync lifecycle notifications consume at most ONE captured lifecycle-loss batch
in this outer scope. Set lifecycleBatchTaken before the first notification. Exact
loss records/claims are acknowledged using the accepted DOMsync rules; callbacks
produce new lifecycle Bnext but no second automatically enlarged loss batch. Pure
freshness checks/import/preparation may report that publication is not yet valid;
that is not permission to drain another notification batch. An action whose
required authority/lifecycle cannot be certified within that one batch stays Queued
and blocks newer automatic work, rather than executing against stale claims.

This rule does not try to prohibit arbitrarily recursive code written by the user.
It does prevent automatically generated Bnext, reentrant Render/Ensure and internal
recovery from obtaining a fresh budget in the same call stack. An explicit nested
input inherits the remaining automatic budget even if its existing synchronous
non-hover action branch is allowed to run by the ordering rules.

## 5. Exhaustion, reset and later recovery

On the first request for a ninth pass, call the existing ResetUnstableHover policy
ONCE for this outer scope. It clears only the unstable batch's hover authority and
acknowledges only that prefix, never newer Bnext. No extra hover settling is hidden
inside reset. Its final cursor may use a currently certified pure query; if none
exists, default/defer is safer than building another settlement budget.

For a reserved unstarted action, successful reset seals its old prefix as
SuppressedUnstable, a terminal prefix disposition. The action itself remains
Queued, automaticRetryBlocked=true for the rest of this outer entry. It is NOT
silently completed/cancelled and cannot run after exhaustion in that entry. On a
later external Render or new-input recovery entry it gets a fresh overall budget,
but the SAME suppressed prefix is NOT reopened to oscillate for another eight
passes. Validate current lifecycle, perform the action's fresh exact query and
then start it, before newer queued Bnext. A future genuine move or independent
geometry/visibility/scroll change may create a new pending prefix normally.

If reset/claim cleanup itself fails, its consumed boundary callbacks remain
consumed, cleanup remains explicitly owed and the action stays blocked Queued.
Later recovery retries only authoritative cleanup/current validation, never the
old boundary dispatch. No stale target or miss is certified from exhaustion.
New DOM loss records and input Bnext remain pending for a later permitted entry.

At error/exhaustion, suppress RequestRender for the blocked work and do not set
NeedsRender solely to run it again. A later external call to Render permits one
bounded attempt, matching Render's existing `_styleRetryBlocked=false` entry
policy; internal reentrant Render cannot clear the active scope's blocked state.
A previously scheduled external paint may therefore make one later bounded attempt,
but a failed attempt cannot schedule another. Successful recovery may process its
pre-existing bounded queue prefix; callback-created Bnext gets at most one later
processing opportunity if progress is allowed, never a self-sustaining idle loop.

## 6. Required fault/order traces

- Fail descriptor, SafeHandle, retain or capacity admission before any prefix
  callback: no callback/action effect, old queue/pending/sequence/authority unchanged.
- Fill all 64 slots while blocked: the next call rejects BEFORE prefix drain;
  recover accepted actions in original order and release all retained owners.
- Prefix mouseout throws; original action still Queued, nested/new Bnext survives;
  next external entry does not repeat mouseout and starts original action first.
- Action with no handlers commits focus/capture/scroll/text and later faults:
  Started was marked before its first effect; no duplicate default on recovery.
- Callback creates action while older action is Queued versus already Started:
  verify the distinct sequencing/takeover rules, exact terminal disposition.
- Render -> barrier -> Ensure -> callback Render/Ensure/geometry invalidation:
  total automatic hover attempts <= 8, one reset, one lifecycle batch, unchanged
  queue cutoff. Include several distinct actions in the original queue prefix.
- Exhausted original prefix is sealed, action waits, next outer entry executes it
  with a fresh query instead of replaying another eight passes of that prefix.
- Reset cleanup OOM, lifecycle Bnext, lost claims, host replacement and Dispose:
  no stale events, no erased newer work, explicit cancellation/recovery and no
  frames while idle. Measure queue/lease retention and admitted/rejected actions.
