using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;
using AetherUI.Runtime;
using AetherUI.Windows.Graphics;
namespace AetherUI.Validator.NativeCapture;
internal static class ArtifactManifest
{
    internal static readonly JsonSerializerOptions JsonOptions=new(){WriteIndented=true};
    internal static string Hash(byte[] bytes)=>Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    internal static string HashFile(string path)=>Hash(File.ReadAllBytes(path));
    internal static void Write(string path,object data) { using var stream=new FileStream(path,FileMode.CreateNew,FileAccess.Write); JsonSerializer.Serialize(stream,data,JsonOptions); }
    internal static object LoadedIdentity()
    {
        using var process=Process.GetCurrentProcess();
        var modules=process.Modules.Cast<ProcessModule>().ToArray();
        var native=modules.Where(m=>m.ModuleName.Equals("aether_core.dll",StringComparison.OrdinalIgnoreCase)).ToArray();
        if(native.Length!=1 || !Path.GetFullPath(native[0].FileName).Equals(Path.Combine(AppContext.BaseDirectory,"aether_core.dll"),StringComparison.OrdinalIgnoreCase)) throw new InvalidOperationException("Expected real output native DLL is not loaded");
        string nativeHash=HashFile(native[0].FileName);
        if(nativeHash!=HashFile(Path.Combine(Directory.GetCurrentDirectory(),"artifacts/native/win-x64/bin/aether_core.dll"))) throw new InvalidOperationException("Loaded native differs from installed build");
        return new { native=new { file="aether_core.dll",sha256=nativeHash },
            assemblies=new[]{typeof(ArtifactManifest).Assembly,typeof(UiDocumentHost).Assembly,typeof(Direct2DRenderBackend).Assembly,typeof(AetherUI.Core.Native.NativeStyleImpact).Assembly}.Select(a=>new{ name=a.GetName().Name,sha256=HashFile(a.Location)}).ToArray(),
            graphicsModules=modules.Where(m=>m.ModuleName.Equals("d2d1.dll",StringComparison.OrdinalIgnoreCase)||m.ModuleName.Equals("dwrite.dll",StringComparison.OrdinalIgnoreCase)).Select(m=>new{ name=m.ModuleName,version=m.FileVersionInfo.FileVersion}).ToArray() };
    }
}
