using System.Reflection;
using System.Runtime.ExceptionServices;
using AetherUI.Core.Native;
using AetherUI.Dom;
using AetherUI.Events;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;

internal static class HostInteractionClaimsTests
{
    internal static void Run()
    {
        PublicStateDoesNotAcquireHostFocus();
        RemovedPressCannotClickAfterOverflow();
        LostFocusUsesFrozenTargetOnlyHandler();
        CleanupPreservesNewNativeBitStamp();
        AdoptionPreservesDestinationAuthority();
        BlurReentryKeepsNewFocus();
        BlurReentryCanClearFocus();
        SameTargetReparentReconcilesHover();
        RemovedHoverSuppressesOut();
        LossCallbackCannotSendOutForOldHover();
        MouseUpReentryKeepsNewPress();
        GetterDefersLossAndNotification();
        ScrollCaptureSurvivesDisconnect();
        ReplacementDiscardsOldLossAndRestoresClaims();
        ClaimPreparationFailurePreservesAuthority();
        CallbackLossesWaitForTheNextHostEntry();
        OneBatchCanDeliverSeveralPriorLosses();
        ThrowingLossCallbackRestoresEntryBudget();
        InternalHoverDoesNotRequestAnotherFrame();
        InteractionCommitPreservesPriorAndPreparedNotifications();
        LaterCallbackNotificationSurvivesInteractionCommit();
        Console.WriteLine("PASS host interaction claims, exact losses, continuity, reentry and capture");
    }

    private static void PublicStateDoesNotAcquireHostFocus()
    {
        using var test = new Fixture();
        test.A.State |= ElementState.Focus | ElementState.Active | ElementState.Hover;
        test.Host.Render();
        Check(test.Host.FocusedElement is null, "Public State became host focus.");
        test.Host.PointerReleased(new(10, 10));
        Check(!test.Component.Trace.Contains("a:click"), "Public Active became a pressed claim.");
    }

    private static void RemovedPressCannotClickAfterOverflow()
    {
        using var test = new Fixture();
        test.Host.PointerPressed(new(10, 10));
        Check(ReferenceEquals(test.Host.FocusedElement, test.A), "Initial press failed to focus target.");
        var handle = test.Dom.GetNativeHandle(test.A);
        var parent = test.Dom.GetNativeHandle(test.Left);
        Check(NativeMethods.NodeRemoveChild(parent, handle) == 0, "Native remove failed.");
        for (var i = 0; i < 160; ++i) test.Dom.Native.SetAttribute(parent, "data-overflow", i.ToString());
        Check(NativeMethods.NodeAppendChild(parent, handle) == 0, "Native reattach failed.");
        test.Host.PointerReleased(new(10, 10));
        Check(!test.Component.Trace.Contains("a:click") && !test.Component.Trace.Contains("a:up"), "Removed press resurrected after overflow.");
        Check(test.Host.FocusedElement is null && test.Component.Trace.Count(x => x == "a:blur") == 1, "Focus continuity was not lost exactly once.");
        Check(!test.Component.Trace.Contains("left:blur"), "Loss blur bubbled through a reattached tree.");
    }

    private static void LostFocusUsesFrozenTargetOnlyHandler()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        test.A.SetAttribute("onblur", test.B.GetAttribute("onblur"));
        var handle = test.Dom.GetNativeHandle(test.A); var parent = test.Dom.GetNativeHandle(test.Left);
        Check(NativeMethods.NodeRemoveChild(parent, handle) == 0, "Remove failed.");
        test.Dom.Native.SetAttribute(handle, "onblur", test.Left.GetAttribute("onblur")!);
        Check(NativeMethods.NodeAppendChild(parent, handle) == 0, "Reattach failed.");
        test.Host.Render();
        Check(test.Component.Trace.Count(x => x == "b:blur") == 1, "First disconnect did not freeze the current handler.");
        Check(!test.Component.Trace.Contains("left:blur") && !test.Component.Trace.Contains("a:blur"), "Loss redirected or bubbled to newer attributes/topology.");
    }

    private static void CleanupPreservesNewNativeBitStamp()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        var handle = test.Dom.GetNativeHandle(test.A); var parent = test.Dom.GetNativeHandle(test.Left);
        Check(NativeMethods.NodeRemoveChild(parent, handle) == 0, "Remove failed.");
        test.Dom.Native.SetState(handle, (int)ElementState.Invalid);
        test.Dom.Native.SetState(handle, (int)(ElementState.Invalid | ElementState.Focus));
        Check(NativeMethods.NodeAppendChild(parent, handle) == 0, "Reattach failed.");
        test.Host.Render();
        Check(test.Host.FocusedElement is null && test.A.State.HasFlag(ElementState.Focus) && test.A.State.HasFlag(ElementState.Invalid), "Old cleanup erased a newer CSS bit write.");
        Check(test.Component.Trace.Count(x => x == "a:blur") == 1, "New CSS state hid the old blur.");
    }

    private static void AdoptionPreservesDestinationAuthority()
    {
        using var source = new Fixture(); using var destination = new Fixture();
        Focus(source.Host, source.A);
        var moved = source.A;
        destination.Left.AppendChild(moved);
        destination.Host.Render();
        Focus(destination.Host, moved);
        source.Host.Render();
        Check(ReferenceEquals(destination.Host.FocusedElement, moved) && moved.State.HasFlag(ElementState.Focus), "Source cleanup cleared destination focus.");
        Check(source.Host.FocusedElement is null && source.Component.Trace.Count(x => x == "a:blur") == 1, "Source loss was not delivered from its old registry.");
        Check(!source.Component.Trace.Contains("left:blur") && !destination.Component.Trace.Any(x => x.EndsWith(":blur")), "Adopted loss bubbled or used destination handlers.");
    }

    private static void BlurReentryKeepsNewFocus()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        test.Component.Blur = _ => Focus(test.Host, test.B);
        var target = test.A; test.Left.RemoveChild(target); test.Left.AppendChild(target);
        test.Host.Render();
        Check(ReferenceEquals(test.Host.FocusedElement, test.B) && test.B.State.HasFlag(ElementState.Focus), "Old blur cleanup overwrote reentrant focus.");
        Check(test.Component.Trace.Count(x => x == "a:blur") == 1, "Reentrant drain delivered blur twice.");
    }

    private static void BlurReentryCanClearFocus()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        test.Component.Blur = _ => Invoke(test.Host, "Focus", [null]);
        Focus(test.Host, test.B);
        Check(test.Host.FocusedElement is null && !test.B.State.HasFlag(ElementState.Focus), "Explicit reentrant focus clear lost to the outer request.");
    }

    private static void SameTargetReparentReconcilesHover()
    {
        using var test = new Fixture();
        Hover(test.Host, test.A);
        var token = test.A.NativeRecordCore!.Identity; var disconnect = test.A.NativeRecordCore.DisconnectSerial;
        test.Right.AppendChild(test.A); // Atomic same-document move, not Remove + Append.
        test.Host.Render();
        Hover(test.Host, test.A);
        Check(test.A.NativeRecordCore!.Identity == token && test.A.NativeRecordCore.DisconnectSerial == disconnect, "Atomic move lost node continuity.");
        Check(!test.Left.State.HasFlag(ElementState.Hover) && test.Right.State.HasFlag(ElementState.Hover) && test.A.State.HasFlag(ElementState.Hover), "Same target kept its old hover ancestors.");
        Check(test.Component.Trace.Count(x => x == "a:over") == 1 && !test.Component.Trace.Contains("a:out"), "Reparent invented boundary events.");
    }

    private static void RemovedHoverSuppressesOut()
    {
        using var test = new Fixture(); Hover(test.Host, test.A);
        test.Left.RemoveChild(test.A);
        test.Host.PointerLeft();
        Check(!test.Component.Trace.Contains("a:out"), "Retired/disconnected outgoing target received mouseout.");
        Check(!test.Left.State.HasFlag(ElementState.Hover), "Surviving old hover ancestor was not cleaned.");
    }

    private static void LossCallbackCannotSendOutForOldHover()
    {
        using var test = new Fixture(); Focus(test.Host, test.A); Hover(test.Host, test.A);
        test.Component.Blur = _ => test.Host.PointerLeft();
        var target = test.A; test.Left.RemoveChild(target); test.Left.AppendChild(target);
        test.Host.Render();
        Check(!test.Component.Trace.Contains("a:out"), "Nested PointerLeft used a reattached wrapper to revive a lost hover claim.");
    }

    private static void MouseUpReentryKeepsNewPress()
    {
        using var test = new Fixture();
        test.Component.MouseUp = _ => test.Host.PointerPressed(new(10, 50));
        test.Host.PointerPressed(new(10, 10)); test.Host.PointerReleased(new(10, 10));
        Check(ReferenceEquals(Field<ElementNode?>(test.Host, "_active"), test.B) && test.B.State.HasFlag(ElementState.Active), "Old release cleared a reentrant press.");
        Check(!test.Component.Trace.Contains("a:click"), "Superseded release still clicked its old target.");
    }

    private static void GetterDefersLossAndNotification()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        var calls = 0; test.Host.RenderRequested += (_, _) => calls++;
        var handle = test.Dom.GetNativeHandle(test.A); var parent = test.Dom.GetNativeHandle(test.Left);
        Check(NativeMethods.NodeRemoveChild(parent, handle) == 0, "Remove failed.");
        Check(NativeMethods.NodeAppendChild(parent, handle) == 0, "Reattach failed.");
        _ = test.A.State;
        Check(calls == 0 && !test.Component.Trace.Contains("a:blur"), "Getter imported by executing callbacks.");
        test.A.State |= ElementState.Invalid;
        Check(!test.Component.Trace.Contains("a:blur"), "Public setter drained lifecycle records outside a host safe point.");
        test.Host.Render();
        Check(test.A.State.HasFlag(ElementState.Invalid) && test.Component.Trace.Count(x => x == "a:blur") == 1, "Loss cleanup replayed a stale state mask.");
    }

    private static void ScrollCaptureSurvivesDisconnect()
    {
        using var test = new Fixture();
        var scroll = test.Host.Document!.GetElementById("scroll")!;
        test.Host.SetScrollOffset(scroll, new(0, 15));
        var identity = scroll.NativeRecordCore!.Identity;
        scroll.SetAttribute("class", "unchanged-scroll"); test.Host.Render();
        Check(scroll.NativeRecordCore.Identity == identity && test.Host.GetScrollOffset(scroll).Y == 15, "Surviving wrapper lost its scroll offset.");
        var type = typeof(UiDocumentHost).GetNestedType("ScrollbarDrag", BindingFlags.NonPublic)!;
        var axis = typeof(UiDocumentHost).GetNestedType("ScrollAxis", BindingFlags.NonPublic)!;
        var drag = Activator.CreateInstance(type, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic, null,
            [scroll, Enum.ToObject(axis, 1), 0f], null)!;
        Set(test.Host, "_scrollbarDrag", drag); Set(test.Host, "_scrollbarPointerCaptured", true);
        var parent = scroll.Parent!; parent.RemoveChild(scroll); parent.AppendChild(scroll);
        test.Host.Render();
        Check(Field<object?>(test.Host, "_scrollbarDrag") is null && Field<bool>(test.Host, "_scrollbarPointerCaptured"), "Disconnect lost scrollbar capture or kept a stale drag.");
        test.Host.PointerReleased(new(10, 10));
        Check(!Field<bool>(test.Host, "_scrollbarPointerCaptured") && !test.Component.Trace.Contains("a:click"), "Scrollbar release became a DOM click.");
    }

    private static void ReplacementDiscardsOldLossAndRestoresClaims()
    {
        using var test = new Fixture(); Focus(test.Host, test.A);
        var old = test.A;
        test.Left.RemoveChild(old); test.Left.AppendChild(old);
        test.Component.Invalidate(); test.Host.Render();
        var restored = test.Host.Document!.GetElementById("a")!;
        Check(!ReferenceEquals(old, restored) && ReferenceEquals(test.Host.FocusedElement, restored), "Replacement lost its restore-by-id policy.");
        Check(!test.Component.Trace.Contains("a:blur"), "Replacement delivered old loss into the new DOM.");
        restored.Parent!.RemoveChild(restored); test.Host.Render();
        Check(test.Component.Trace.Count(x => x == "a:blur") == 1 && test.Host.FocusedElement is null, "Restored focus had no activated claim.");
    }

    private static void ClaimPreparationFailurePreservesAuthority()
    {
        using var test = new Fixture();
        var token = NativeDomSync.ReadToken(test.Dom.Native); var state = test.A.State;
        test.Host.DomSynchronizationProbe = () => throw new OutOfMemoryException("claim bookkeeping");
        try { Focus(test.Host, test.A); throw new InvalidOperationException("Expected managed OOM."); }
        catch (OutOfMemoryException) { }
        finally { test.Host.DomSynchronizationProbe = null; }
        Check(test.Host.FocusedElement is null && test.A.State == state && NativeDomSync.ReadToken(test.Dom.Native) == token, "Failed claim preparation published state or focus.");
    }

    private static void CallbackLossesWaitForTheNextHostEntry()
    {
        using var test = new Fixture();
        var a = test.A; var b = test.B; var left = test.Left; var right = test.Right;
        var deliveries = 0;
        void ReplaceFocusWithDisconnected(ElementNode next, ElementNode parent)
        {
            if (++deliveries > 5) throw new InvalidOperationException("Lifecycle callbacks escaped their host-entry batch.");
            if (next.Parent is null) parent.AppendChild(next);
            Focus(test.Host, next);
            parent.RemoveChild(next);
        }
        test.Component.Blur = _ => ReplaceFocusWithDisconnected(b, right);
        test.Component.BlurB = _ => ReplaceFocusWithDisconnected(a, left);
        Focus(test.Host, a); left.RemoveChild(a);
        for (var entry = 1; entry <= 4; entry++)
        {
            test.Host.Render();
            Check(deliveries == entry, "A callback-created loss was drained again in the same outer Render entry.");
            Check(test.Host.FocusedElement is null, "A newly disconnected focus remained eligible after the entry.");
            var beforeQuery = deliveries;
            Check(test.Host.NeedsRender && deliveries == beforeQuery, "The next batch was hidden or NeedsRender delivered it.");
        }
        Check(Field<int>(test.Host, "_interactionEntryDepth") == 0, "Lifecycle entry scope did not unwind.");
    }

    private static void OneBatchCanDeliverSeveralPriorLosses()
    {
        using var test = new Fixture();
        var a = test.A; var b = test.B; var left = test.Left; var right = test.Right;
        var generated = false;
        test.Component.Blur = _ =>
        {
            if (generated) return;
            generated = true;
            Focus(test.Host, b); right.RemoveChild(b);
            left.AppendChild(a); Focus(test.Host, a); left.RemoveChild(a);
        };
        Focus(test.Host, a); left.RemoveChild(a);
        test.Host.Render();
        Check(test.Component.Trace.Count(x => x.EndsWith(":blur", StringComparison.Ordinal)) == 1,
            "New records extended the first batch.");
        Check(test.Host.NeedsRender, "Two legitimate pending records were hidden.");
        test.Host.Render();
        Check(test.Component.Trace.Count(x => x == "a:blur") == 2 && test.Component.Trace.Count(x => x == "b:blur") == 1,
            "The next batch was incorrectly limited to a single blur instead of all prior records.");
        Check(!test.Component.Trace.Contains("left:blur"), "Batched loss delivery bubbled.");
    }

    private static void ThrowingLossCallbackRestoresEntryBudget()
    {
        using var test = new Fixture();
        var a = test.A; var b = test.B; var left = test.Left; var right = test.Right;
        test.Component.Blur = _ =>
        {
            Focus(test.Host, b); right.RemoveChild(b);
            throw new FormatException("after acknowledged loss");
        };
        Focus(test.Host, a); left.RemoveChild(a);
        try { test.Host.Render(); throw new InvalidOperationException("Expected callback failure."); }
        catch (FormatException) { }
        Check(Field<int>(test.Host, "_interactionEntryDepth") == 0 && !Field<bool>(test.Host, "_drainingInteractionLosses"),
            "A throwing callback leaked the outer entry or drain guard.");
        Check(!test.Host.NeedsRender, "Callback failure incorrectly enabled automatic retry.");
        test.Host.Render();
        Check(test.Component.Trace.Count(x => x == "a:blur") == 1 && test.Component.Trace.Count(x => x == "b:blur") == 1,
            "Explicit next entry failed to deliver its own pending batch or repeated an acknowledged loss.");
    }

    private static void InternalHoverDoesNotRequestAnotherFrame()
    {
        using var test = new Fixture();
        var requests = 0;
        test.Host.RenderRequested += (_, _) => requests++;
        Hover(test.Host, test.A);
        Check(!Field<bool>(test.Host, "_domNotificationPending"), "Internal hover produced a public DOM trampoline notification.");
        test.Host.Render();
        Check(requests == 0, "A settled internal hover requested another frame.");
        test.A.State |= ElementState.Invalid;
        Check(requests == 1, "Suppressing an internal claim notification suppressed the public mutator trampoline.");
    }

    private static void InteractionCommitPreservesPriorAndPreparedNotifications()
    {
        using (var test = new Fixture())
        {
            test.Dom.Native.SetAttribute(test.Dom.GetNativeHandle(test.A), "data-before", "native");
            Check(test.A.GetAttribute("data-before") == "native" && Field<bool>(test.Host, "_domNotificationPending"),
                "Native import did not set up the prior pending notification.");
            Focus(test.Host, test.A);
            Check(Field<bool>(test.Host, "_domNotificationPending"), "An interaction commit consumed a prior import notification.");
        }
        using (var test = new Fixture())
        {
            // The preparation probe is before the first publication. A pending flag
            // produced here must be captured after staging, not at helper entry.
            test.Host.DomSynchronizationProbe = () => Set(test.Host, "_domNotificationPending", true);
            Focus(test.Host, test.A);
            test.Host.DomSynchronizationProbe = null;
            Check(Field<bool>(test.Host, "_domNotificationPending"), "The interaction helper restored a pre-preparation notification bit.");
        }
    }

    private static void LaterCallbackNotificationSurvivesInteractionCommit()
    {
        using var test = new Fixture();
        test.Component.MouseOver = _ =>
        {
            test.Dom.Native.SetAttribute(test.Dom.GetNativeHandle(test.A), "data-callback", "newer");
            test.A.GetAttribute("data-callback");
        };
        Hover(test.Host, test.A);
        Check(test.A.GetAttribute("data-callback") == "newer" && Field<bool>(test.Host, "_domNotificationPending"),
            "An old interaction commit consumed a newer callback notification.");
    }

    private static void Focus(UiDocumentHost host, ElementNode element) => Invoke(host, "Focus", element);
    private static void Hover(UiDocumentHost host, ElementNode element) => Invoke(host, "UpdateHoverTarget", element, new PointF(10, 10), false);
    private static void Invoke(object value, string method, params object?[] args)
    {
        try { value.GetType().GetMethod(method, BindingFlags.Instance | BindingFlags.NonPublic)!.Invoke(value, args); }
        catch (TargetInvocationException error) when (error.InnerException is not null) { ExceptionDispatchInfo.Capture(error.InnerException).Throw(); }
    }
    private static T Field<T>(object value, string name) => (T)value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.GetValue(value)!;
    private static void Set(object value, string name, object? field) => value.GetType().GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)!.SetValue(value, field);
    private static void Check(bool condition, string message) { if (!condition) throw new InvalidOperationException(message); }

    private sealed class Fixture : IDisposable
    {
        internal readonly Component Component = new();
        internal readonly Renderer Renderer = new();
        internal readonly UiDocumentHost Host;
        internal DomInterop Dom => Field<DomInterop>(Host, "_nativeDom");
        internal ElementNode A => Host.Document!.GetElementById("a")!;
        internal ElementNode B => Host.Document!.GetElementById("b")!;
        internal ElementNode Left => Host.Document!.GetElementById("left")!;
        internal ElementNode Right => Host.Document!.GetElementById("right")!;
        internal Fixture()
        {
            Host = new(Component, new ApproximateTextMeasurer(), Renderer, new UiHostOptions { Width = 240, Height = 240 });
            Host.Render();
        }
        public void Dispose() => Host.Dispose();
    }
    private sealed class Component : UiComponent
    {
        internal readonly List<string> Trace = [];
        internal Action<UiEvent>? Blur, BlurB, MouseUp, MouseOver;
        internal void Invalidate() => StateHasChanged();
        protected override void BuildRenderTree(MarkupWriter writer)
        {
            writer.WriteLiteral("<style>body{margin:0}button{display:block;width:60px;height:40px;padding:0;border:0}#scroll{width:60px;height:40px;overflow:scroll}#content{height:200px}</style><body><div id='left'");
            writer.WriteEventAttribute("onblur", () => Trace.Add("left:blur"));
            writer.WriteLiteral("><button id='a' type='button'");
            writer.WriteEventAttribute("onblur", e => { Trace.Add("a:blur"); Blur?.Invoke(e); });
            writer.WriteEventAttribute("onmouseover", e => { Trace.Add("a:over"); MouseOver?.Invoke(e); });
            writer.WriteEventAttribute("onmouseout", () => Trace.Add("a:out"));
            writer.WriteEventAttribute("onmouseup", e => { Trace.Add("a:up"); MouseUp?.Invoke(e); });
            writer.WriteEventAttribute("onclick", () => Trace.Add("a:click"));
            writer.WriteLiteral(">A</button></div><div id='right'><button id='b' type='button'");
            writer.WriteEventAttribute("onblur", e => { Trace.Add("b:blur"); BlurB?.Invoke(e); });
            writer.WriteLiteral(">B</button></div><div id='scroll'><div id='content'>scroll</div></div></body>");
        }
    }
    private sealed class Renderer : IRenderBackend
    {
        public void Resize(int width, int height, float dpiScale) { }
        public void Render(DisplayList displayList, ColorRgba color) { }
        public void Dispose() { }
    }
}
