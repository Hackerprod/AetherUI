# Interaction owned-storage infrastructure

The public interaction API uses this private storage infrastructure. ABI minor 38
adds the API; existing rendering profiles and platform policy are unchanged.
The 256 MiB ceiling covers instrumented storage only, under G0-M1. It is not
a bound on total session memory or RSS. Legacy DOM, style, layout, preparation
and font caches remain explicitly excluded; see the per-component inventories.

`rendering/html_ltr/owned_budget.h` provides owner-thread reservation accounting.
The default maximum is 256 MiB. The separately allocated ledger is charged by
`sizeof(State)` before its allocation; intrusive references let reservations
outlive the original budget handle without a separate unaccounted control block.
Reservation transfer is move-only. Shrinking cannot increase a reservation.

`OwnedBuffer<T>` is restricted to trivial payloads, with explicit element capacity.
It checks multiplication and reserves `count * sizeof(T)` before `new T[count]`.
The old buffer remains charged while its replacement is allocated. On failure the
old bytes are unchanged and the candidate reservation is returned. Storage is
released before its associated charge. Allocator metadata/rounding and RSS are
not logical owned bytes. This is not a general STL/PMR allocator.

`interaction_frame.h` adds private exact-capacity BGRA frame storage. A candidate
frame is limited to dimensions 0..8192 and 64 MiB; zero dimensions suspend without
replacing the prior frame. A caller must implement state/sequence advancement and
atomic publication: this class only stores supplied metadata. The ABI coordinator publishes state and frame metadata only after success.

The initial private units cover overflow, reservation failure, old/new capacity,
allocation failure, recovery, retained-domain lifetime and 10,000 frame swaps.
Those swaps are not the required 10,000 edit/render/backspace/render interaction
cycles. Integrated edit/render stress is a separate gate. No complete 256 MiB session
guarantee is claimed. Legacy storage is not instrumented by this ledger.

The shared UTF-8 decoder and scalar repertoire predicate retain the authored
HTML admission rules. The runtime text path separately validates the
bounded UTF-8 payload before repertoire decisions and never parses text as HTML.
Payloads above 1024 bytes fail the work/input bound before scanning. Password
payloads never reach logs or native shaped runs; derived runs contain bullets
or a placeholder only.

The parser pending-node RAII fix is independent of interaction. It keeps new text
and comment nodes privately owned until attachment succeeds. It does not promise
rollback of the legacy DOM parser after an error.
