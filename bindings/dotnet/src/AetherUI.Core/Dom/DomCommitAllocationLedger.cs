#if AETHER_DOM_COMMIT_ALLOCATION_TESTING
namespace AetherUI.Dom;

// Test build only. Direct scalar bookkeeping, never an observer/delegate or a
// production diagnostic API. This type and all call sites are absent normally.
internal enum DomCommitAllocationKind { ReplaceText, InsertChild, RemoveChild, ClearChildren, Text, State, RuntimeValue, SetAttribute, RemoveAttribute, Import, Interaction }
internal enum DomCommitAllocationPhase { NativeValidation, BookkeepingValidation, NativeCommit, ManagedMaps, BookkeepingPublish, ScalarMirror }
internal struct DomCommitAllocationSnapshot
{
    internal ulong Successful, Failed, SuccessfulBytes, FailedBytes, Nested;
    internal ulong ReplaceText, InsertChild, RemoveChild, ClearChildren, Text, State, RuntimeValue, SetAttribute, RemoveAttribute, Import, Interaction;
    internal ulong NativeValidationBytes, BookkeepingValidationBytes, NativeCommitBytes, ManagedMapsBytes, BookkeepingPublishBytes, ScalarMirrorBytes;
    internal bool Invalid;
    internal readonly ulong Count(DomCommitAllocationKind kind) => kind switch
    {
        DomCommitAllocationKind.ReplaceText => ReplaceText, DomCommitAllocationKind.InsertChild => InsertChild,
        DomCommitAllocationKind.RemoveChild => RemoveChild, DomCommitAllocationKind.ClearChildren => ClearChildren,
        DomCommitAllocationKind.Text => Text, DomCommitAllocationKind.State => State,
        DomCommitAllocationKind.RuntimeValue => RuntimeValue, DomCommitAllocationKind.SetAttribute => SetAttribute,
        DomCommitAllocationKind.RemoveAttribute => RemoveAttribute, DomCommitAllocationKind.Import => Import,
        DomCommitAllocationKind.Interaction => Interaction, _ => 0,
    };
}
internal static class DomCommitAllocationLedger
{
    [ThreadStatic] private static bool _enabled;
    [ThreadStatic] private static int _depth;
    [ThreadStatic] private static long _start, _phaseStart;
    [ThreadStatic] private static bool _failed;
    [ThreadStatic] private static DomCommitAllocationSnapshot _counts;
    internal static void Reset()
    {
        if (_depth != 0) { _counts.Invalid = true; return; }
        _counts = default;
    }
    internal static void SetEnabled(bool enabled)
    {
        if (_depth != 0) { _counts.Invalid = true; return; }
        _enabled = enabled;
    }
    internal static DomCommitAllocationSnapshot Read()
    {
        var result = _counts;
        if (_depth != 0) result.Invalid = true;
        return result;
    }
    private static void Add(ref ulong target, ulong amount = 1)
    {
        if (amount > ulong.MaxValue - target) { target = ulong.MaxValue; _counts.Invalid = true; }
        else target += amount;
    }
    internal static void Begin(DomCommitAllocationKind kind)
    {
        if (!_enabled) return;
        if (_depth == int.MaxValue) { _counts.Invalid = true; return; }
        if (_depth++ == 0) { _failed = false; _start = _phaseStart = GC.GetAllocatedBytesForCurrentThread(); }
        else Add(ref _counts.Nested);
        switch (kind)
        {
            case DomCommitAllocationKind.ReplaceText: Add(ref _counts.ReplaceText); break;
            case DomCommitAllocationKind.InsertChild: Add(ref _counts.InsertChild); break;
            case DomCommitAllocationKind.RemoveChild: Add(ref _counts.RemoveChild); break;
            case DomCommitAllocationKind.ClearChildren: Add(ref _counts.ClearChildren); break;
            case DomCommitAllocationKind.Text: Add(ref _counts.Text); break;
            case DomCommitAllocationKind.State: Add(ref _counts.State); break;
            case DomCommitAllocationKind.RuntimeValue: Add(ref _counts.RuntimeValue); break;
            case DomCommitAllocationKind.SetAttribute: Add(ref _counts.SetAttribute); break;
            case DomCommitAllocationKind.RemoveAttribute: Add(ref _counts.RemoveAttribute); break;
            case DomCommitAllocationKind.Import: Add(ref _counts.Import); break;
            case DomCommitAllocationKind.Interaction: Add(ref _counts.Interaction); break;
            default: _counts.Invalid = true; break;
        }
    }
    // Attribution only. Does not change the outer interval or its zero-byte gate.
    internal static void MarkPhase(DomCommitAllocationPhase phase)
    {
        if (!_enabled || _depth == 0) return;
        var now = GC.GetAllocatedBytesForCurrentThread();
        var delta = now - _phaseStart;
        _phaseStart = now;
        if (delta < 0) { _counts.Invalid = true; return; }
        var bytes = (ulong)delta;
        switch (phase)
        {
            case DomCommitAllocationPhase.NativeValidation: Add(ref _counts.NativeValidationBytes, bytes); break;
            case DomCommitAllocationPhase.BookkeepingValidation: Add(ref _counts.BookkeepingValidationBytes, bytes); break;
            case DomCommitAllocationPhase.NativeCommit: Add(ref _counts.NativeCommitBytes, bytes); break;
            case DomCommitAllocationPhase.ManagedMaps: Add(ref _counts.ManagedMapsBytes, bytes); break;
            case DomCommitAllocationPhase.BookkeepingPublish: Add(ref _counts.BookkeepingPublishBytes, bytes); break;
            case DomCommitAllocationPhase.ScalarMirror: Add(ref _counts.ScalarMirrorBytes, bytes); break;
        }
    }
    internal static void End(bool successful)
    {
        if (!_enabled) return;
        if (_depth <= 0) { _counts.Invalid = true; return; }
        _failed |= !successful;
        if (--_depth != 0) return;
        var bytes = GC.GetAllocatedBytesForCurrentThread() - _start;
        if (bytes < 0) { _counts.Invalid = true; return; }
        if (_failed) { Add(ref _counts.Failed); Add(ref _counts.FailedBytes, (ulong)bytes); }
        else { Add(ref _counts.Successful); Add(ref _counts.SuccessfulBytes, (ulong)bytes); }
    }
}
#endif
