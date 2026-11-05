using AetherUI.Dom;

namespace AetherUI.Tests;

// Managed parity only: DocumentNode uses its own index, not DocGetElementById.
internal static class DomIdOrderTests
{
    internal static void Run()
    {
        var document = new DocumentNode();
        var body = new ElementNode("body");
        document.AppendChild(body);
        var branch = new ElementNode("section");
        var first = Element("dup");
        var nested = Element("dup");
        var second = Element("dup");
        var third = Element("dup");
        body.AppendChild(branch);
        branch.AppendChild(first);
        first.AppendChild(nested);
        body.AppendChild(new TextNode("gap"));
        body.AppendChild(new CommentNode("gap"));
        body.AppendChild(second);
        body.AppendChild(third);
        Expect("dup", body.Children[0].Children[0]);
        var version = document.Version;
        Expect("dup", first);
        Expect("missing", null);
        if (document.Version != version) throw new InvalidOperationException("Lookup mutated version.");
        body.AppendChild(branch);
        Expect("dup", body.Children[2]);
        body.AppendChild(second);
        Expect("dup", third);
        body.AppendChild(second);
        Expect("dup", third);
        branch.AppendChild(third);
        Expect("dup", first);
        first.SetAttribute("id", "other");
        Expect("dup", nested);
        Expect("other", first);
        nested.SetAttribute("id", "");
        Expect("dup", third);
        first.SetAttribute("id", "dup");
        Expect("other", null);
        Expect("dup", first);
        body.RemoveChild(branch);
        Expect("dup", second);
        first.SetAttribute("id", "detached");
        Expect("detached", null);
        body.RemoveChild(second);
        Expect("dup", null);
        body.AppendChild(branch);
        body.AppendChild(second);
        Expect("dup", third);
        Expect("detached", first);
        // A managed rebuild, not a native parser or borrowed-handle lifetime test.
        body.ClearChildren();
        foreach (var id in new[] { "A", "A", "a", " A ", " ", "é", "é", "e\u0301" })
            body.AppendChild(Element(id));
        Expect("dup", null);
        Expect("detached", null);
        foreach (var (id, index) in new[] { ("A", 0), ("a", 2), (" A ", 3), (" ", 4), ("é", 5), ("e\u0301", 7) })
            Expect(id, body.Children[index]);
        Expect("", null);
        try { document.GetElementById(null!); }
        catch (ArgumentNullException) { return; }
        throw new InvalidOperationException("Null ID must throw ArgumentNullException.");

        void Expect(string id, Node? expected)
        {
            if (!ReferenceEquals(document.GetElementById(id), expected))
                throw new InvalidOperationException($"Wrong managed winner for ID '{id}'.");
        }
    }

    private static ElementNode Element(string id)
    {
        var element = new ElementNode("div");
        element.SetAttribute("id", id);
        return element;
    }
}
