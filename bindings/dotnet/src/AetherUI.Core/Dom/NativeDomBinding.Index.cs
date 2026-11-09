namespace AetherUI.Dom;

internal sealed partial class NativeDomBinding
{
    private static IDomPreparedBookkeeping PrepareBookkeeping(NativeDomBinding owner, DomEdit edit,
        IReadOnlyList<(Node Node, NativeDomNodeRecord Record)>? created = null, IReadOnlyCollection<Node>? materializedRoots = null)
    {
        if (edit.Kind is not (DomEditKind.InsertChild or DomEditKind.RemoveChild or DomEditKind.ClearChildren or DomEditKind.ReplaceText))
            return owner._host.Prepare(edit, import: false);
        var elements = new List<IntPtr>();
        Visit(owner.Document);
        return owner._host.PrepareStructure(edit, elements);

        void Visit(Node node)
        {
            if (node is ElementNode)
            {
                var handle = node.NativeRecordCore?.Handle ?? IntPtr.Zero;
                if (handle == IntPtr.Zero && created is not null)
                    foreach (var pair in created) if (ReferenceEquals(pair.Node, node)) { handle = pair.Record.Handle; break; }
                if (handle == IntPtr.Zero) throw new InvalidOperationException("The candidate DOM index has an unmapped element.");
                elements.Add(handle);
            }
            if (ReferenceEquals(node, edit.Target))
            {
                if (edit.Kind == DomEditKind.ClearChildren) return;
                if (edit.Kind == DomEditKind.ReplaceText) { if (edit.Child is not null) Visit(edit.Child); return; }
                if (edit.Kind == DomEditKind.InsertChild)
                {
                    var position = 0;
                    foreach (var child in node.ChildrenCore)
                    {
                        if (ReferenceEquals(child, edit.Child)) continue;
                        if (position++ == edit.Index) Visit(edit.Child!);
                        Visit(child);
                    }
                    if (position == edit.Index) Visit(edit.Child!);
                    return;
                }
            }
            foreach (var child in node.ChildrenCore)
            {
                if (materializedRoots?.Contains(child) == true) continue;
                if (ReferenceEquals(child, edit.Child) &&
                    (ReferenceEquals(node, edit.SourceParent) || edit.Kind == DomEditKind.RemoveChild && ReferenceEquals(node, edit.Target))) continue;
                Visit(child);
            }
        }
    }
}
