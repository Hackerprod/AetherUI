# E2 proposal evidence, not implementation validation

Date: 2026-11-09. Frozen product base: E1 + CI follow-up
`51b4fb53f78d01782a7de92f6ca4d4f8668dbebb`.
Only technical documents are changed by this proposal branch. E1 stays on its own
frozen branch. The external probe below does not change production code or claim
that proposed E2 behavior exists. DOMsync WIP is not linked into this execution.

## Existing P1/P2 observations

`bindings/dotnet/tests/AetherUI.InputTraceTests/InputEContractBaseline.cs` and
`hover-e1-proposal.md` preserve the previously run real .NET E0 observations:
CursorAt(B) independently returns Text but returns A's Pointer while move A is
pending; move A/down A/move B/up A gives down/up/click before B mouseover at Render.
E1 ON and OFF preserve those observations. The proposed enabled corrections are
listed separately in E2, not substituted for historical assertions.

## Additional P3 observations

Executed a small external .NET 10 executable against the same already built Core
assembly and separately copied native E1 OFF/ON libraries. Both runs exited 0 and
printed the same two observations. This is recorded output, not a comprehensive
assertion suite, independent review, E2 test pass, or OS/presentation measurement.

Exact stdout for each native mode:

```text
CASE stationary-scroll: trace=[] Ahover=True Bhover=False cursor=Text NeedsRender=False
CASE mouseout-relocates-visible-B: trace=[a:mouseout,b:mouseover] Ahover=False Bhover=True cursor=Default NeedsRender=False
```

The first case uses public SetScrollOffset after hover settled, then Render without
a new physical move. The second uses an outgoing handler and the existing internal
known-state write/invalidation methods via reflection, like the existing input
trace fixtures. It activates `#b:invalid { margin-top:80px }`, a layout change, not
visibility or pointer-events. Its exact query after Render returns Default while
B remains hovered. No claim is made about generic transforms from this fixture.

## Reproduction and binary identity

Project was built with SDK 10.0.201, `-c Release -m:1 -p:UseSharedCompilation=false`,
DOTNET_CLI_HOME set to an isolated temporary home. Build: 0 warnings, 0 errors.
The source below references the existing own Core assembly, so the probe build
cannot change production sources/outputs. For each serial run, copy the chosen
native .so to the probe output directory before starting dotnet, compare bytes and
hash, then launch Probe.dll. The prior process has exited before replacing its
copy; no loaded library is truncated and no other worker's output is touched.

SHA256:
- Program.cs: `db15ba7aa02f5dc89c6de9e78082b9ebbf597c299da67d65fd106d5261b02e0d`
- Probe.csproj: `39090a04b3d565b392decf638ebb014585ac2b69182c249d2d39da3d1720ad94`
- Core assembly: `89681dd74376642aba8a1340959a42fc0c9cc8e49bc59cbb87a39d9b1888bfad`
- Probe assembly: `5910fd17887ec8a3acc86ee356a92a4799f6a47c905dca8ec92be108de70a7ed`
- Native E1 OFF: `936023cbac8ffce91f3c0b73aa1fea7af783bc053cf9fee66ade42fc57ced39b`
- Native E1 ON: `a1cd1b238aeb0b3db5dcf672c507bc5eacc1079424353fc4bbf0358b645af5af`

OFF refers to HIT_ACCELERATION; contextual/state/work instrumentation flags remain
as recorded in E1's build report. This is not an all-features-OFF baseline.

External artifacts: sibling `e2-evidence-probe/` directory, build.log, off.log,
on.log and their .hash files. Recreate its location beside `repo/` to use the
relative assembly path below. Full source is embedded here for review/reproduction.

```xml
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable></PropertyGroup><ItemGroup><Reference Include="AetherUI.Core"><HintPath>../repo/bindings/dotnet/src/AetherUI.Core/bin/Release/net10.0/AetherUI.Core.dll</HintPath></Reference></ItemGroup></Project>
```

```csharp
using AetherUI.Css;
using AetherUI.Dom;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using AetherUI.Runtime;
using System.Reflection;
foreach (var scroll in new[] { true, false }) {
 var c=new Component(scroll); using var r=new NullRenderer();
 using var h=new UiDocumentHost(c,new ApproximateTextMeasurer(),r,new UiHostOptions{Width=200,Height=200});
 h.Render(); h.PointerMoved(new(20,20)); h.Render(); c.Trace.Clear();
 if(scroll) { h.SetScrollOffset(h.Document!.GetElementById("p")!,new(0,80)); h.Render(); }
 else { c.OnOut=()=>{ var b=h.Document!.GetElementById("b")!; typeof(UiDocumentHost).GetMethod("SetElementState",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(h,[b,ElementState.Invalid,true]); typeof(UiDocumentHost).GetMethod("InvalidateVisual",BindingFlags.Instance|BindingFlags.NonPublic)!.Invoke(h,[false]); }; h.PointerMoved(new(20,100)); h.Render(); }
 Console.WriteLine($"CASE {(scroll?"stationary-scroll":"mouseout-relocates-visible-B")}: trace=[{string.Join(",",c.Trace)}] Ahover={(h.Document!.GetElementById("a")!.State & ElementState.Hover)!=0} Bhover={(h.Document!.GetElementById("b")!.State & ElementState.Hover)!=0} cursor={h.CursorAt(scroll?new(20,20):new(20,100))} NeedsRender={h.NeedsRender}");
}
sealed class Component(bool scroll):UiComponent {
 public List<string> Trace{get;}=[]; public Action? OnOut;
 protected override void BuildRenderTree(MarkupWriter w){
 w.WriteLiteral("<style>body{margin:0}#a,#b{width:80px;height:80px}#a{cursor:pointer}#b{cursor:text}#b:invalid{margin-top:80px}"+(scroll?"#p{width:100px;height:80px;overflow-y:auto}":"")+"</style><body><div id='p'>");
 foreach(var id in new[]{"a","b"}) { w.WriteLiteral($"<div id='{id}'");foreach(var type in new[]{"mouseover","mouseout"})w.WriteEventAttribute("on"+type,e=>{Trace.Add(id+":"+type);if(id=="a"&&type=="mouseout")OnOut?.Invoke();});w.WriteLiteral("></div>");} w.WriteLiteral("</div></body>");
 }
}
sealed class NullRenderer:IRenderBackend {public void Resize(int w,int h,float dpi){} public void Render(DisplayList l,ColorRgba c){} public void Dispose(){} }
```

## Additional Chromium source identity

Pinned revision: `2d4034e6c4e0a2881aa4539c4ba4604e9b45ecc9`.
The GitHub connector returned full content and Git blob identities; local
`git hash-object` reproduced each. Files are stored in sibling `chromium-e2/`.

- `core/paint/paint_layer.cc`: Git blob
  `d590f86a93e3f296250949347eb53f0b6c151fab`, SHA256
  `661419d857ae0e83d00bad8c88341cd6d0d0e41cb139d7284d9431bf96ff44c3`.
- `core/paint/box_fragment_painter.cc`: Git blob
  `b03abaf104d483f3c09e152fb42a43b82186bf5b`, SHA256
  `df3c0350cdb35d345461834342032630b1ba49b5fe9838976d3d8fd3a6696ba1`.

Exact source links/functions and what is not translated into Aether semantics are
in the proposal. No Chromium execution, Windows/macOS execution, performance
improvement or implementation approval is inferred from these sources/probes.
