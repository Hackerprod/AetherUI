using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;

namespace AetherUI.Runtime;

// Chromium reference (inspection, not an equivalence claim), pinned 2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9:
// MouseEventManager::RecomputeMouseHoverStateIfNeeded consumes dirty before callbacks;
// MouseEventManager::HandleRemoveSubtree invalidates a removed press target;
// Element::RemovedFrom / DetachLayoutTree distinguish atomic moves from disconnects;
// Element::SetHovered invalidates state without reconstructing the document.
public sealed partial class UiDocumentHost
{
    private static long _nextInteractionHostIdentity;
    private readonly ulong _interactionHostIdentity = NewInteractionHostIdentity();
    private InteractionSession? _interactionSession;
    private InteractionClaim? _focusClaim, _activeClaim;
    private bool _drainingInteractionLosses;
    private long _focusRequestSerial, _pressRequestSerial, _hoverRequestSerial;

    private static ulong NewInteractionHostIdentity()
    {
        while (true)
        {
            var previous = Volatile.Read(ref _nextInteractionHostIdentity);
            if (previous == long.MaxValue) throw new OverflowException("Interaction host identity exhausted.");
            if (Interlocked.CompareExchange(ref _nextInteractionHostIdentity, previous + 1, previous) == previous)
                return (ulong)(previous + 1);
        }
    }

    private sealed class InteractionClaim(InteractionSession session, ElementNode element, ElementState bit,
        NativeInteractionClaim native)
    {
        internal readonly InteractionSession Session = session;
        internal readonly ElementNode Element = element;
        internal readonly ElementState Bit = bit;
        internal readonly NativeInteractionClaim Native = native;
        // A registry belongs to a DOM publication, not to the component's latest RenderMarkup.
        internal UiComponent.EventRegistry Registry => Session.Registry;
        internal bool IsCurrent => !Session.IsDisposed && Native.IsCurrent;
    }

    private sealed class InteractionSession(DomInterop dom, UiComponent.EventRegistry registry,
        NativeInteractionOwner owner) : IDisposable
    {
        internal readonly DomInterop Dom = dom;
        internal readonly UiComponent.EventRegistry Registry = registry;
        internal readonly NativeInteractionOwner Owner = owner;
        internal readonly List<InteractionClaim> Claims = [];
        internal readonly Dictionary<ElementNode, InteractionClaim> Hover = [];
        internal InteractionClaim? Focus, Active;
        internal bool IsDisposed;
        public void Dispose()
        {
            if (IsDisposed) return;
            Owner.Retire();
            IsDisposed = true;
            foreach (var claim in Claims) claim.Native.Dispose();
            Claims.Clear(); Hover.Clear(); Focus = Active = null;
            Owner.Dispose();
        }
    }

    // All restored bits are already true in the private candidate DOM. These are
    // metadata-only claims: no live authority, DOM state or counter is published yet.
    private InteractionSession PrepareInteractionSession(DomInterop dom, UiComponent.EventRegistry registry,
        ElementNode? focused, ElementNode? active, HashSet<ElementNode> hoverChain)
    {
        var owner = NativeInteractionOwner.Prepare(dom.Native, _interactionHostIdentity);
        InteractionSession? session = null;
        try
        {
            session = new(dom, registry, owner);
            session.Claims.EnsureCapacity(hoverChain.Count + 2);
            session.Hover.EnsureCapacity(hoverChain.Count);
            if (focused is not null) session.Focus = Prepare(focused, ElementState.Focus);
            if (active is not null) session.Active = Prepare(active, ElementState.Active);
            foreach (var element in hoverChain) session.Hover.Add(element, Prepare(element, ElementState.Hover));
            return session;

            InteractionClaim Prepare(ElementNode element, ElementState bit)
            {
                // This entire candidate is private. If managed allocation fails, release
                // the handle and abort the provisional owner without exposing authority.
                var native = owner.PrepareClaim(dom.BindingCore!.GetHandleCore(element), (int)bit, (ulong)registry.Generation);
                try
                {
                    var claim = new InteractionClaim(session!, element, bit, native);
                    session!.Claims.Add(claim); return claim;
                }
                catch { native.Dispose(); throw; }
            }
        }
        catch
        {
            if (session is not null) session.Dispose();
            else { owner.Retire(); owner.Dispose(); }
            throw;
        }
    }

    // Called inside the component's no-callback publication immediately after the
    // style binding commits. Validation was done before that first publication.
    private void PublishInteractionSession(InteractionSession session, IntPtr binding)
    {
        session.Owner.Activate(binding);
        _interactionSession = session;
        _focusClaim = session.Focus;
        _activeClaim = session.Active;
    }

    private void DisposeInteractionOwner()
    {
        var session = _interactionSession;
        _interactionSession = null;
        _focusClaim = _activeClaim = null;
        session?.Dispose();
    }

    private InteractionClaim AcquireInteractionClaim(ElementNode element, ElementState bit)
    {
        var session = _interactionSession ?? throw new InvalidOperationException("The host has no interaction owner.");
        var binding = session.Dom.BindingCore!;
        session.Claims.EnsureCapacity(session.Claims.Count + 1);
        if (bit == ElementState.Hover) session.Hover.EnsureCapacity(session.Hover.Count + 1);
        using var edit = binding.PrepareInteractionAcquire(session.Owner, element, bit, (ulong)session.Registry.Generation);
        var claim = new InteractionClaim(session, element, bit, edit.Claim);
        edit.Commit();
        session.Claims.Add(claim); // Capacity and absence were prepared before commit.
        return claim;
    }

    private bool ReleaseInteractionClaim(InteractionClaim claim)
    {
        if (claim.Session.IsDisposed) return false;
        // A loss belongs to the next private lifecycle batch; normal release must not
        // acknowledge it or manufacture a blur using the node's current attributes.
        if (claim.Native.TryReadLoss(out _)) return false;
        var info = claim.Native.Read();
        using var edit = claim.Session.Dom.BindingCore!.PrepareInteractionCleanup(claim.Element, claim.Native, info.Generation);
        var changed = edit.StateChanged;
        edit.Commit();
        claim.Session.Claims.Remove(claim);
        claim.Native.Dispose();
        return changed;
    }

    private readonly record struct InteractionContinuity(DocumentNode? Document, NativeDomBinding? Binding,
        ulong Identity, ulong DisconnectSerial)
    {
        internal static InteractionContinuity Capture(DocumentNode? document, ElementNode element)
        {
            var record = element.NativeRecordCore;
            return new(document, record?.Owner, record?.Identity ?? 0, record?.DisconnectSerial ?? 0);
        }
        internal bool Matches(UiDocumentHost host, ElementNode element)
        {
            if (host._disposed || !ReferenceEquals(Document, host._document)) return false;
            if (Binding is null) return ReferenceEquals(element.OwnerDocument, Document);
            element.BindingCore?.EnsureCurrent();
            var record = element.NativeRecordCore;
            return record is { Attached: true } && ReferenceEquals(record.Owner, Binding) &&
                ReferenceEquals(element.BindingCore, Binding) && record.Identity == Identity &&
                record.DisconnectSerial == DisconnectSerial;
        }
    }

    private sealed record LossDelivery(InteractionClaim Claim, NativeInteractionLoss Loss, string? Handler,
        UiEvent Event);

    private void DrainInteractionLosses()
    {
        if (_disposed || _drainingInteractionLosses || _styleOperationDepth != 0 || _interactionSession is not { } session) return;
        // Snapshot before any callback. Decode every borrowed handler while native owns
        // it, then retain only managed strings, exact record scalars and registry pins.
        List<LossDelivery>? pending = null;
        foreach (var claim in session.Claims)
        {
            if (!claim.Native.TryReadLoss(out var loss)) continue;
            if (loss.Claim.HostIdentity != _interactionHostIdentity || loss.Claim.RegistryIdentity != (ulong)claim.Registry.Generation ||
                loss.Claim.NodeIdentity != claim.Native.Read().NodeIdentity)
                throw new InvalidOperationException("Interaction loss does not match its pinned registry and target.");
            var handler = loss.HandlerPresent != 0 ? NativeDomCodec.Decode(loss.Handler) : null;
            loss.Handler = default; // Never retain/use a native string borrow beyond acknowledgment.
            (pending ??= []).Add(new(claim, loss, handler, new UiEvent("blur", claim.Element)
            {
                CurrentTarget = claim.Element,
                Value = claim.Element.RuntimeValueCore ?? claim.Element.GetAttributeCore("value"),
            }));
        }
        if (pending is null)
        {
            ValidateScrollbarContinuity();
            return;
        }
        var batch = pending.ToArray();
        _drainingInteractionLosses = true;
        try
        {
            foreach (var delivery in batch)
            {
                if (_disposed || session.IsDisposed || !ReferenceEquals(session, _interactionSession)) break;
                var claim = delivery.Claim;
                if (!claim.Native.TryReadLoss(out var current) || current.RecordId != delivery.Loss.RecordId ||
                    current.RecordSerial != delivery.Loss.RecordSerial || current.Claim.Generation != delivery.Loss.Claim.Generation)
                    throw new InvalidOperationException("The exact interaction loss was superseded before delivery.");
                // The host guard suppresses notification trampolines until all old authority
                // is retired and the exact native record is acknowledged. No callback here.
                ++_styleOperationDepth;
                try
                {
                    using (var edit = session.Dom.BindingCore!.PrepareInteractionCleanup(claim.Element, claim.Native,
                        delivery.Loss.Claim.Generation, delivery.Loss.RecordId, delivery.Loss.RecordSerial))
                        edit.Commit();
                    claim.Native.Acknowledge(delivery.Loss);
                    session.Claims.Remove(claim);
                    if (ReferenceEquals(_focusClaim, claim))
                    {
                        _focusClaim = null; session.Focus = null; _focused = null;
                    }
                    if (ReferenceEquals(_activeClaim, claim))
                    {
                        _activeClaim = null; session.Active = null; _active = null;
                    }
                    if (session.Hover.TryGetValue(claim.Element, out var hover) && ReferenceEquals(hover, claim))
                    {
                        session.Hover.Remove(claim.Element); _hoverChain.Remove(claim.Element);
                        if (ReferenceEquals(_hovered, claim.Element)) _hovered = null;
                    }
                    claim.Native.Dispose();
                    _hasPendingPointerTarget = false; _pendingPointerTarget = null;
                    if (_hasPointerPosition && !_scrollbarPointerCaptured)
                    { _pendingPointerMove = _lastPointerPosition; _hasPendingPointerMove = true; }
                }
                finally { LeaveStyleOperation(); }
                if (_disposed || session.IsDisposed || !ReferenceEquals(session, _interactionSession)) break;
                // Always target-only, including remove+reattach and source -> adopt -> focus
                // at a destination. The old registry/token cannot bubble into either tree.
                if (claim.Bit == ElementState.Focus && delivery.Handler is { Length: > 0 } token &&
                    claim.Registry.TryGetEventHandler(token, out var callback)) callback(delivery.Event);
                if (_disposed || session.IsDisposed || !ReferenceEquals(session, _interactionSession)) break;
                EnsureDomInputsCurrent();
            }
            ValidateScrollbarContinuity();
        }
        catch
        {
            RejectStyleProjection(); _styleRetryBlocked = true;
            throw; // No automatic retry, and unacknowledged records remain pending.
        }
        finally { _drainingInteractionLosses = false; }
    }

    private bool HasCurrentHoverClaim(ElementNode? element) =>
        element is not null && _interactionSession is { } session &&
        session.Hover.TryGetValue(element, out var claim) && claim.IsCurrent;

    private bool HoverChainIsCurrent(ElementNode? target)
    {
        var count = 0;
        for (var current = target; current is not null; current = current.ParentCore as ElementNode)
        {
            if (!_hoverChain.Contains(current) || (_interactionSession is { } session &&
                (!session.Hover.TryGetValue(current, out var claim) || !claim.IsCurrent))) return false;
            count++;
        }
        return count == _hoverChain.Count;
    }
}
