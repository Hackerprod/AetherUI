using System.Diagnostics;
using System.Reflection.Metadata;
using System.Reflection.PortableExecutable;
namespace AetherUI.Validator.NativeCapture;
internal static class RuntimeGuard
{
    private static readonly string[] Forbidden=["webview2","playwright","selenium","puppeteer","cef","chrome","chromium","msedge","firefox"];
    public static void ValidateNames(IEnumerable<string> names)
    {
        foreach(var name in names) if(Forbidden.Any(part=>name.Contains(part,StringComparison.OrdinalIgnoreCase))) throw new InvalidOperationException("Forbidden browser dependency/module identity");
    }
    public static void Check()
    {
        ValidateNames(AppDomain.CurrentDomain.GetAssemblies().Select(a=>a.GetName().Name??""));
        using var process=Process.GetCurrentProcess();
        ValidateNames(process.Modules.Cast<ProcessModule>().Select(m=>m.ModuleName));
        using var stream=File.OpenRead(typeof(RuntimeGuard).Assembly.Location);
        using var pe=new PEReader(stream); var metadata=pe.GetMetadataReader();
        foreach(var handle in metadata.MemberReferences)
        {
            var member=metadata.GetMemberReference(handle); var name=metadata.GetString(member.Name);
            string type="";
            if(member.Parent.Kind==HandleKind.TypeReference) { var reference=metadata.GetTypeReference((TypeReferenceHandle)member.Parent); type=metadata.GetString(reference.Namespace)+"."+metadata.GetString(reference.Name); }
            if((type=="System.Diagnostics.Process" && name=="Start") || name.Contains("ShellExecute",StringComparison.Ordinal) || name.Contains("EnsureCoreWebView2",StringComparison.Ordinal) || type=="AetherUI.Validator.ValidatorForm" || type=="AetherUI.Validator.Program") throw new InvalidOperationException("Forbidden executable member reference");
        }
    }
}
