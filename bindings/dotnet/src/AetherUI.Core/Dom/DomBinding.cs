namespace AetherUI.Dom;

// All methods except MutationCompleted are owner-thread, non-reentrant operations.
// EnsureCurrent may stage/import inputs, but must never dispatch user notifications.
internal interface IDomBinding
{
    void EnsureCurrent();
    IDomPreparedEdit PrepareEdit(DomEdit edit);
    void MutationCompleted();
}

// Dispose releases staging only: no user code, notifications, or fallible bookkeeping.
internal interface IDomPreparedEdit : IDisposable
{
    // Revalidate all native/mirror bases before the first write. On success publish
    // native inputs, binding maps and preallocated host bookkeeping, without callbacks
    // or allocation. A failure must leave this edit unpublished. Never throw after write.
    void Commit();
}

internal enum DomEditKind { InsertChild, RemoveChild, ClearChildren, ReplaceText, SetText, SetAttribute, RemoveAttribute, SetState, SetRuntimeValue }

internal readonly record struct DomEdit(
    DomEditKind Kind, Node Target, Node? Child = null, Node? SourceParent = null,
    int Index = 0, string? Name = null, string? Text = null, ElementState State = default);

internal readonly record struct DocumentVersionChange(DocumentNode? Document, long Version, long InputVersion)
{
    internal void Publish() => Document?.PublishVersions(Version, InputVersion);
}

internal static class DomNames
{
    internal static string Canonicalize(string name)
    {
        ArgumentNullException.ThrowIfNull(name);
        var first = -1;
        for (var i = 0; i < name.Length; i++)
            if (name[i] is >= 'A' and <= 'Z') { first = i; break; }
        if (first < 0) return name;
        var chars = name.ToCharArray();
        for (var i = first; i < chars.Length; i++)
            if (chars[i] is >= 'A' and <= 'Z') chars[i] = (char)(chars[i] + ('a' - 'A'));
        return new string(chars);
    }
}
