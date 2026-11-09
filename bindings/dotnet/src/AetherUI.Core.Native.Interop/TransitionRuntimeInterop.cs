using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

/// <summary>
/// Managed wrapper over the native CSS transition runtime (ABI 0.33.0). The runtime observes
/// computed-style changes for a node and advances interpolated opacity/transform overrides as the
/// host clock ticks. The display list builder consumes those overrides per element when the runtime
/// is attached via <see cref="NativeDisplayListInterop.SetTransitionRuntime"/>.
/// </summary>
public sealed class TransitionRuntimeInterop : IDisposable
{
    private IntPtr _runtime;
    private readonly int _ownerThread = Environment.CurrentManagedThreadId;

    public TransitionRuntimeInterop()
    {
        // Before the runtime is created/exposed: no caller can yet have captured
        // spans or native payload pointers for its first prepared publication.
        NativeMethods.TransitionPublication.EnsurePrepared();
        NativeAbiContract.EnsureCompatible();
        Throw(NativeMethods.TransitionRuntimeCreate(out _runtime));
    }

    public IntPtr Handle => _runtime;

    public bool IsDisposed => _runtime == IntPtr.Zero;

    /// <summary>
    /// Starts (or supersedes) a transition for <paramref name="node"/> when the new style's
    /// transition duration/property bits match a value change versus the before style. Both
    /// <paramref name="before"/> and <paramref name="after"/> must be pointers to native-layout
    /// <c>aether_computed_style_t</c> copies valid for the duration of this call.
    /// </summary>
    public void Observe(IntPtr node, IntPtr before, IntPtr after, long nowMs)
    {
        EnsureOwner();
        Throw(NativeMethods.TransitionRuntimeObserve(_runtime, node, before, after, nowMs));
    }

    /// <summary>Advances every active transition to <paramref name="nowMs"/>; returns true while any transition remains active.</summary>
    public bool Tick(long nowMs)
    {
        EnsureOwner();
        Throw(TryTick(nowMs, out var active));
        return active;
    }

    public bool HasActive
    {
        get
        {
            EnsureOwner();
            return NativeMethods.TransitionRuntimeHasActive(_runtime) != 0;
        }
    }

    public bool TryGetOpacityOverride(IntPtr node, out float opacity)
    {
        EnsureOwner();
        return NativeMethods.TransitionRuntimeOpacityOverride(_runtime, node, out opacity) != 0;
    }

    public bool TryGetTransformOverride(IntPtr node, out NativeTransformMatrix matrix)
    {
        EnsureOwner();
        return NativeMethods.TransitionRuntimeTransformOverride(_runtime, node, out matrix) != 0;
    }

    public void Dispose()
    {
        if (_runtime != IntPtr.Zero && Environment.CurrentManagedThreadId != _ownerThread)
            throw new InvalidOperationException("Transition runtime must be disposed on its owner thread.");
        if (_runtime != IntPtr.Zero)
        {
            NativeMethods.TransitionRuntimeDestroy(_runtime);
            _runtime = IntPtr.Zero;
        }

        GC.SuppressFinalize(this);
    }

    /// <summary>Pure status query. The token is a certificate, not a node/storage lease.</summary>
    public int TryReadToken(out NativeTransitionToken token)
    {
        token = default;
        if (_runtime == IntPtr.Zero) return -4;
        if (Environment.CurrentManagedThreadId != _ownerThread) return -2;
        return NativeMethods.TransitionRuntimeReadToken(_runtime, out token);
    }

    public NativeTransitionToken ReadToken()
    {
        Throw(TryReadToken(out var token));
        return token;
    }

    public bool IsCurrent(NativeTransitionToken token)
        => TryReadToken(out var current) == 0 && current.AllowsPublication && current == token;

    /// <summary>
    /// Samples the full current transition set. On failure active is false only
    /// as an initialized output, never as evidence that sampling succeeded.
    /// Success alone acknowledges the exact captured native TickError.
    /// </summary>
    public int TryTick(long nowMs, out bool active)
    {
        active = false;
        if (_runtime == IntPtr.Zero) return -4;
        if (Environment.CurrentManagedThreadId != _ownerThread) return -2;
        var status = NativeMethods.TransitionRuntimeTryTick(_runtime, nowMs, out var nativeActive);
        if (status == 0) active = nativeActive != 0;
        return status;
    }

    /// <summary>
    /// Fallible staging only. All observations use one timestamp. Native copies
    /// endpoints and owns leases before this method returns; style buffers may
    /// then be freed. An empty batch shares native state without cloning it.
    /// </summary>
    public unsafe PreparedPublication PreparePublication(
        ReadOnlySpan<NativeTransitionObservation> observations,
        ReadOnlySpan<IntPtr> retiredNodes, long nowMs)
    {
        NativeMethods.TransitionPublication.EnsurePrepared();
        EnsureOwner();
        // Allocate both managed owners before asking native to produce a handle.
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("transition.prepare.publication-wrapper");
#endif
        var prepared = new PreparedPublication(this);
        try
        {
            fixed (NativeTransitionObservation* entries = observations)
            fixed (IntPtr* retired = retiredNodes)
            {
#if AETHER_DOM_FAULT_TESTING
                AetherUI.Core.Native.ManagedDomFault.Before("transition.prepare.native-call-aggregate");
#endif
                var status = NativeMethods.TransitionRuntimePrepare(_runtime, entries,
                    (nuint)observations.Length, retired, (nuint)retiredNodes.Length, nowMs, out var handle);
                prepared.Initialize(handle);
                Throw(status);
            }
            return prepared;
        }
        catch { prepared.Dispose(); throw; }
    }

    /// <summary>
    /// A candidate can outlive runtime disposal safely. Validate/TryCommit are
    /// status-only, allocation-free calls for the owner-thread publication
    /// segment. A successful commit preserves the public runtime handle and
    /// retains replaced storage here until disposal after publication.
    /// </summary>
    public sealed class PreparedPublication : IDisposable
    {
        private readonly TransitionRuntimeInterop _owner;
        private readonly TransitionCandidateHandle _handle;
        private bool _committed;
        internal PreparedPublication(TransitionRuntimeInterop owner)
        {
            _owner = owner;
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("transition.prepare.candidate-handle");
#endif
            _handle = new TransitionCandidateHandle(owner);
        }
        internal void Initialize(IntPtr handle) => _handle.Initialize(handle);
        private int AccessStatus => _handle.IsClosed || _handle.IsInvalid || _owner.IsDisposed ? -4 :
            Environment.CurrentManagedThreadId != _owner._ownerThread || _committed ? -2 : 0;
        public int Validate()
        {
            var status = AccessStatus;
            return status != 0 ? status : NativeMethods.TransitionPublication.Validate(_handle);
        }
        public int TryCommit(out NativeTransitionToken token)
        {
            token = default;
            var status = AccessStatus;
            if (status != 0) return status;
            status = NativeMethods.TransitionPublication.Commit(_handle, out token);
            if (status == 0) _committed = true;
            return status;
        }
        public void Dispose() => _handle.Dispose();
    }

    private void EnsureOwner()
    {
        ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
        if (Environment.CurrentManagedThreadId != _ownerThread)
            throw new InvalidOperationException("Transition runtime requires its owner thread.");
    }

    private static void Throw(int status)
    {
        if (status == -3) throw new OutOfMemoryException("Native transition staging could not allocate storage.");
        if (status == -4) throw new ObjectDisposedException(nameof(TransitionRuntimeInterop));
        if (status != 0) throw new InvalidOperationException($"Native AetherUI transition call failed with status {status}.");
    }
}
