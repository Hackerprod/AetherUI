using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Core.Native;

// Real native parse/source compilation/rebind and managed cache publication. No mock parser.
// Allocation-failure probes are separate host tests, not a claim of native OOM injection.
internal static class DomStyleBindingInteropTests
{
    [DllImport("aether_core", EntryPoint = "aether_style_runtime_add_stylesheet", CallingConvention = CallingConvention.Cdecl)]
    private static extern int AddSheet(IntPtr runtime, [MarshalAs(UnmanagedType.LPUTF8Str)] string source,
        int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, out long handle, out int rules);

    internal static void Run()
    {
        AbortAndStaleBasesPreservePublishedRuntime();
        RebindPreservesForeignSheetsAndRemovedSource();
        Console.WriteLine("PASS native DOM style rebind, cache staging and foreign sheets");
    }

    private static void AbortAndStaleBasesPreservePublishedRuntime()
    {
        using var a = NativeDomDocument.Parse("<style>#a{width:70px}</style><button id='a'>A</button>");
        using var b = NativeDomDocument.Parse("<style>#b{width:90px}</style><button id='b'>B</button>");
        using var runtime = new StyleRuntimeInterop(200, 200, 1);
        using (var initial = runtime.PrepareDomBinding(a)) { initial.Validate(); initial.Commit(); }
        using (var styles = runtime.PreparePublication(null, 200, 200, 1))
        { Check(styles.Commit(), "Initial style commit failed."); using var map = styles.Publish(); }
        var document = Field<IntPtr>(runtime, "_document");
        var cache = Field<object>(runtime, "_styles");
        var index = Field<object>(runtime, "_elementIndex");
        var elements = Field<object>(runtime, "_elements");
        var revision = Field<IntPtr>(runtime, "_managedPublicationRevision");
        var slot = runtime.ReadDomSourceSlot();
        using (var aborted = runtime.PrepareDomBinding(b))
        {
            aborted.Validate();
            Check(Field<IntPtr>(runtime, "_document") == document, "Preparation changed runtime document.");
        }
        AssertA();
        using (var stale = runtime.PrepareDomBinding(b))
        {
            b.SetAttribute(b.EnumerateElementHandles().Last(), "class", "native-newer");
            Throws<InvalidOperationException>(stale.Commit);
        }
        AssertA();
        using (var staleOld = runtime.PrepareDomBinding(b))
        {
            a.SetAttribute(a.EnumerateElementHandles().Last(), "class", "old-native-newer");
            Throws<InvalidOperationException>(staleOld.Commit);
        }
        AssertA();
        Throws<InvalidOperationException>(() => runtime.Load("<div>forbidden</div>", "", ""));
        AssertA();
        using (var accepted = runtime.PrepareDomBinding(b)) accepted.Commit();
        Check(Field<IntPtr>(runtime, "_document") == b.Handle, "Successful rebind did not publish B.");
        Check(!ReferenceEquals(Field<object>(runtime, "_styles"), cache), "B inherited A's projection cache.");
        Check(Field<IntPtr>(runtime, "_managedPublicationRevision") == IntPtr.Zero, "A's revision remained published for B.");
        using var bStyles = runtime.PreparePublication(null, 200, 200, 1);
        Check(bStyles.Commit(), "Rebound document failed real native resolve.");
        using var bMap = bStyles.Publish();

        void AssertA()
        {
            Check(Field<IntPtr>(runtime, "_document") == document && runtime.ReadDomSourceSlot() == slot,
                "Failed preparation changed document or source slot.");
            Check(ReferenceEquals(Field<object>(runtime, "_styles"), cache) &&
                ReferenceEquals(Field<object>(runtime, "_elementIndex"), index) &&
                ReferenceEquals(Field<object>(runtime, "_elements"), elements) &&
                Field<IntPtr>(runtime, "_managedPublicationRevision") == revision,
                "Failed rebind changed runtime projection bookkeeping.");
        }
    }

    private static void RebindPreservesForeignSheetsAndRemovedSource()
    {
        const string markup = "<style>button{width:70px}</style><button>A</button>";
        using var a = NativeDomDocument.Parse(markup);
        using var same = NativeDomDocument.Parse(markup);
        using var changed = NativeDomDocument.Parse("<style>button{width:90px}</style><button>B</button>");
        using var runtime = new StyleRuntimeInterop(200, 200, 1);
        using (var initial = runtime.PrepareDomBinding(a)) initial.Commit();
        var handle = Field<IntPtr>(runtime, "_runtime");
        Check(AddSheet(handle, "button{height:55px}", 2, "foreign.css", out var foreign, out _) == 0,
            "Failed to add foreign stylesheet.");
        var source = runtime.ReadDomSourceSlot();
        Check(NativeMethods.StyleRuntimeRemoveStyleSheet(handle, source.Handle) != 0, "Could not remove bound source slot.");
        using (var preserved = runtime.PrepareDomBinding(same)) preserved.Commit();
        var absent = runtime.ReadDomSourceSlot();
        Check(!absent.Present && absent.Handle == source.Handle, "Same-source rebind resurrected a removed source slot.");
        using (var reinstalled = runtime.PrepareDomBinding(changed)) reinstalled.Commit();
        Check(runtime.ReadDomSourceSlot().Present, "Changed-source rebind did not reinstall source.");
        Check(Field<IntPtr>(runtime, "_runtime") == handle, "Rebind replaced the native runtime.");
        Check(NativeMethods.StyleRuntimeRemoveStyleSheet(handle, foreign) != 0, "Rebind removed a foreign stylesheet.");
    }

    private static T Field<T>(object value, string name) => (T)(value.GetType()
        .GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)?.GetValue(value)
        ?? throw new Exception($"Missing field {name}"));
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Throws<T>(Action action) where T : Exception
    {
        try { action(); } catch (T) { return; }
        throw new Exception($"Expected {typeof(T).Name}.");
    }
}
