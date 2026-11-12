# R3 text requests, not text rendering

This pure component owns validated text requests. It does not shape, measure,
render, choose fallback fonts or implement a cache/provider/ABI bridge. R2 still
rejects Text; existing C# backends remain unchanged.

Intrinsic requests explicitly mean no wrap. They carry used_line_height in DIPs
(not a multiplier) and optional max width. Width zero differs from absent width.
Alignment and height are absent, not fabricated defaults. Paint requests retain
width, height, wrap and alignment but used_line_height is absent because the
paint command does not supply it. Purpose and every optional field participate
in equality. Position/color/CTM do not; the complete paint input nevertheless
passes the original R1 validation before strings are copied.

Strings are owned and byte-preserved: no Unicode, whitespace, line-break,
family-case or fallback normalization. Limits remain 65536 bytes text, family
less than64 bytes, UTF8 without NUL, nonempty family, finite positive font size,
weight1..1000. Intrinsic used line height is finite>=0, max width when present
finite>=0. Empty text remains valid without invented metrics. Invalid input
returns a typed error; bad_alloc during construction returns OUT_OF_MEMORY.
No partial request escapes. Request is copy-only and not assignable; passing
an rvalue invokes copying and leaves the source valid. Copies outside factories
may allocate/throw. Getter references last only during the owning request life.

The Text branch of R1 validate_command was extracted mechanically to
validate_text; both R1 and paint factory call it. Error precedence and string
budget updates only on success are unchanged. Intrinsic validation operates on
its actual fields instead of fabricating a paint command with dummy geometry.

Float equality is exact numeric equality without tolerance, not bitwise
serialization or a binary cache key. This component is not installed or linked
into production yet.

Exact request equality is NOT layout equivalence or a sufficient cache key.
Future reuse must additionally match provider instance/configuration, font
availability generation, direction, language, script, features and shaping
context. Missing ABI data are not guessed. A real provider must retain its
owned native layout for measurement and drawing of the SAME request; intrinsic
and wrapped requests may require different layouts and reshaping. R3 does not
claim or enforce that future provider implementation yet.

The existing historical ABI returns width/height/baseline, no layout handle or
error. Contextual layout has used_line_height in DIPs. Some callers use width0
for intrinsic work; R3 does not declare it a universal ABI sentinel or convert
it. C# Pango intrinsic Measure uses nowrap; Draw can wrap/align. DirectWrite
creates separate layouts for Measure and Draw. No heuristic baseline or empty
text result is added here.

## Pure verification

From repository root:

```bash
N=native/src/rendering/preparation
/usr/bin/c++ -std=c++20 -Wall -Wextra -Werror -pedantic -UNDEBUG \
 -I native/include -I "$N/include" "$N/paint_snapshot.cpp" \
 native/src/rendering/text/text_request.cpp native/tests/test_text_request.cpp \
 -o /tmp/aether-text-request-test
/tmp/aether-text-request-test
```

Tests cover ownership, source lifetime, exact request fields, optional values,
UTF8/NUL/limits, preserved RTL/combining/whitespace bytes, R1 helper parity and
budgets, and one-shot bad_alloc simulation through test-only global allocation
replacement (not real process OOM). No production allocation hook was added.
R0/R1/R2 pure regression commands are in their prior documents.

No CMake integration or SDK compilation is claimed. Real Pango requires SDK
Cairo/Pango and permitted rendering validation; DirectWrite/Direct2D requires
Windows SDK and compatible execution environment. No fake ABI or runtime load.

## References

- bindings/dotnet/src/AetherUI.Core/Layout/ITextMeasurer.cs
- bindings/dotnet/src/AetherUI.Linux/Graphics/PangoTextLayout.cs
- bindings/dotnet/src/AetherUI.Windows/Graphics/Direct2DRenderBackend.cs
- native/include/aether/aether_contextual_layout.h
- https://chromium.googlesource.com/chromium/src/+/e459c1e7c797c2bade8d35ed20c9cbff9b4837a6/third_party/blink/renderer/platform/fonts/README.md
- https://chromium.googlesource.com/chromium/src/+/8c22cfa391fd04cfa94c8f020f12fd39caf93f02/third_party/blink/renderer/platform/fonts/shaping/shaping_line_breaker.h

Chromium shares font machinery between measurement and paint, but reuse depends
on font context and line-breaking safety. These sources do not justify heuristic
shaping or equivalence of requests with different layout constraints.
