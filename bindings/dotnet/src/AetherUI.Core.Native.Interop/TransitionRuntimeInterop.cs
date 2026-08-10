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

    public TransitionRuntimeInterop()
    {
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
        ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
        Throw(NativeMethods.TransitionRuntimeObserve(_runtime, node, before, after, nowMs));
    }

    /// <summary>Advances every active transition to <paramref name="nowMs"/>; returns true while any transition remains active.</summary>
    public bool Tick(long nowMs)
    {
        ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
        return NativeMethods.TransitionRuntimeTick(_runtime, nowMs) != 0;
    }

    public bool HasActive
    {
        get
        {
            ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
            return NativeMethods.TransitionRuntimeHasActive(_runtime) != 0;
        }
    }

    public bool TryGetOpacityOverride(IntPtr node, out float opacity)
    {
        ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
        return NativeMethods.TransitionRuntimeOpacityOverride(_runtime, node, out opacity) != 0;
    }

    public bool TryGetTransformOverride(IntPtr node, out NativeTransformMatrix matrix)
    {
        ObjectDisposedException.ThrowIf(_runtime == IntPtr.Zero, this);
        return NativeMethods.TransitionRuntimeTransformOverride(_runtime, node, out matrix) != 0;
    }

    public void Dispose()
    {
        if (_runtime != IntPtr.Zero)
        {
            NativeMethods.TransitionRuntimeDestroy(_runtime);
            _runtime = IntPtr.Zero;
        }

        GC.SuppressFinalize(this);
    }

    private static void Throw(int status)
    {
        if (status != 0) throw new InvalidOperationException($"Native AetherUI transition call failed with status {status}.");
    }
}
