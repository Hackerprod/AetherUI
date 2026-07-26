# AetherUI Java binding

**Status: not implemented.** This directory defines the integration contract a Java module must
satisfy, not working code yet. It must be built entirely on the installed C ABI and must not expose or
depend on C++ classes, private native headers, or a CMake build tree.
[`bindings/dotnet/`](../dotnet/) is the reference implementation of the same contract in a different
language — when a lifetime or ownership question isn't obvious from this document, that binding is the
one to mirror (its `NativeDomDocument`/`NativeLayoutResult`/`StyleRuntimeInterop` classes are the
concrete precedent for the `AutoCloseable` wrappers described below).

## Intended scope

The binding will expose document parsing, the style runtime, layout, and display-list generation
through idiomatic Java objects. Rendering is intentionally out of scope for the core binding:
applications may consume display lists headlessly (server-side layout, diffing, snapshot testing) or
provide a Java/platform-specific text-measurement and rendering backend layered on top — the same
separation `AetherUI.Windows` maintains from the .NET core binding.

The compatibility baseline is **JNI** — it's available on every supported JDK and every native
platform this project targets. A Foreign Function & Memory (FFM, `java.lang.foreign`) implementation
may be added later for JDKs where it's stable, but both implementations, if both ever exist, must
expose one public Java API and preserve identical lifetime semantics; callers should never need to know
which native binding strategy is loaded underneath.

### Sketch of the target shape

Not an API commitment — illustrative, to make the ownership contract concrete before anyone writes JNI:

```java
package dev.aetherui;

public final class Document implements AutoCloseable {
    public static Document parse(String html) { /* aether_doc_create + aether_doc_parse_html */ }
    @Override public void close() { /* aether_doc_destroy */ }
}

public final class StyleRuntime implements AutoCloseable {
    public StyleRuntime(Options options, Environment environment) { /* aether_style_runtime_create */ }
    public long addStylesheet(String css, Layer layer, String sourceName) { /* ...add_stylesheet */ }
    @Override public void close() { /* aether_style_runtime_destroy */ }
}

public final class LayoutEngine implements AutoCloseable {
    public LayoutEngine(TextMeasurer measurer) { /* aether_layout_create, retains the callback */ }
    public LayoutResult layout(Document doc, StyleRuntime styles, float width, float height) { ... }
    @Override public void close() { /* aether_layout_destroy */ }
}

public final class LayoutResult implements AutoCloseable {
    // Borrowed node/box views obtained from this result become unusable after close().
    public Optional<Node> hitTest(float x, float y) { ... }
    @Override public void close() { /* aether_layout_result_destroy */ }
}

@FunctionalInterface
public interface TextMeasurer {
    TextMetrics measure(String text, ComputedStyleView style, float maxWidth);
}
```

Every owning type is `AutoCloseable`, so `try (var doc = Document.parse(html)) { ... }` is the natural
shape — deliberately parallel to `SafeHandle`'s deterministic-disposal role in the .NET binding.

## Native library contract

The binding targets an installed `aether_core` library and its public header — never a copy vendored
into the Java module's own source tree:

```text
native/include/aether/aether.h
<install-prefix>/include/aether/aether.h
<install-prefix>/lib/cmake/AetherUI/
<install-prefix>/lib/pkgconfig/aetherui-core.pc
```

At startup (a static initializer or an explicit `AetherUI.load()` call) it must load the library built
for the current OS and architecture, then validate:

```text
aether_abi_version()
aether_abi_capabilities()
aether_paint_command_size()
```

Bindings require a compatible ABI major/minor pair, check only the capability bits they actually use,
ignore unknown bits from a newer library, and must never hard-code the byte size of a paint command —
always read it from `aether_paint_command_size()`, since the native command union grows as paint
features are added and a stale constant becomes a silent out-of-bounds read.

## Java API and ownership model

The public API uses `AutoCloseable` for every owning handle — documents, style runtimes, layout
engines, layout results, display lists. A `java.lang.ref.Cleaner` registration may exist as a
last-resort safety net for abandoned resources (logging a warning, not silently freeing on GC), but it
cannot replace deterministic `close()`: native objects have a dependency order (a style runtime must
outlive every layout built from it) that GC timing does not respect, and relying on a `Cleaner` for
ordering will eventually double-free or use-after-free something.

Nodes and layout boxes are borrowed views. They must retain (or otherwise validate against) their
owner, and every method on a borrowed view must throw a clear exception — not crash the JVM — once that
owner has been closed. Re-parsing a document invalidates every wrapper for the previous DOM tree; the
binding should track a generation counter on `Document` and check it on each borrowed-view access
rather than trusting the caller to discard old references correctly.

Strings and gradient-stop arrays returned inside display-list commands are copied into Java-owned
values (`String`, `float[]`) before `aether_paint_command_free()` runs. No Java code ever calls a
C allocator's `free` directly — native memory is released exclusively through the matching
`aether_*_free`/`aether_*_destroy` function for what allocated it.

## Callback and threading policy

Native callbacks (text measurement, scroll-offset queries) are synchronous, invoked on whatever native
thread is currently inside the call that triggered them. The binding must keep every callback
implementation and its associated user-data object reachable for as long as native code can invoke it
(a `GlobalRef` in JNI terms), catch every `Throwable` inside the callback body, and return a documented
safe fallback value — a Java exception must never be allowed to propagate across a JNI boundary back
into C++, since that corrupts the native call stack rather than raising a catchable error on either
side.

ABI 0.1 treats each handle graph as serialized: concurrent calls against the same document, style
runtime, layout result, or display list from two threads are unsupported by the native core. The Java
API should either enforce thread confinement (reject a call from a thread other than the one that
created the handle) or serialize access internally and document that choice explicitly — silently
allowing concurrent access and hoping the native side tolerates it is not an acceptable default.

## Packaging and validation requirements

The eventual Maven/Gradle distribution should separate pure-Java classes from platform-specific native
artifacts, selecting the correct native binary by OS and architecture at either build or resolution
time (classifier artifacts or a runtime OS/arch detection step) — mirroring how the .NET binding scopes
native assets per RID under `runtimes/<rid>/native/`. It must not compile or vendor a private copy of
the C++ engine as part of an application's own build.

Before the binding is published it needs automated checks for: ABI negotiation against the pinned
native version, native-handle cleanup ordering (no leaks, no use-after-close), callback exception
containment (a measurement callback that throws must not crash the JVM), display-list command decoding
against a fixture covering every paint-command variant, and a headless HTML/CSS/layout/display-list
smoke test run on every platform the release actually ships a native artifact for.
