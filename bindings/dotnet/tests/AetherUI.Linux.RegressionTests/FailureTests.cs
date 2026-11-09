using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using AetherUI.Linux;
using AetherUI.Linux.Graphics;
using AetherUI.Primitives;
using AetherUI.Rendering;
internal static class FailureTests
{
    internal static bool MissingDependencyMode(string[] args)
    {
        if (args.Length != 1 || !args[0].StartsWith("--missing-", StringComparison.Ordinal))
            return false;
        var missing = args[0] == "--missing-cairo" ? "libcairo.so.2" : "libpangocairo-1.0.so.0";
        NativeLibrary.SetDllImportResolver(typeof(CairoRenderBackend).Assembly, (name, _, _) => name == missing ? throw new DllNotFoundException($"Injected missing {missing}") : IntPtr.Zero);
        try
        {
            using var renderer = new CairoRenderBackend(new(new MemoryFramePresenter()), new(8, 8, 1));
            throw new InvalidOperationException("Missing dependency was not reported");
        }
        catch (Exception ex) when (ex is PlatformNotSupportedException or InvalidOperationException && ex.InnerException is DllNotFoundException)
        {
            if (!ex.Message.Contains("Cairo", StringComparison.Ordinal))
                throw;
            Console.WriteLine("PASS missing dependency: " + missing);
        }
        return true;
    }
    internal static void Run()
    {
        static void Check(bool yes, string why)
        {
            if (!yes)
                throw new InvalidOperationException("Failure: " + why);
        }
        var memory = new MemoryFramePresenter();
        var fail = false;
        var calls = 0;
        void Allocate(int w, int h)
        {
            calls++;
            if (fail)
                throw new OutOfMemoryException("Injected allocation failure before native surface creation");
        }
        fail = true;
        try
        {
            using var bad = new CairoRenderBackend(new(memory), new(8, 8, 1), Allocate);
            throw new InvalidOperationException("Constructor failure missed");
        }
        catch (OutOfMemoryException) { }
        Check(memory.CurrentFrame is null, "Constructor must not present");
        fail = false;
        using var backend = new CairoRenderBackend(new(memory), new(8, 8, 1), Allocate);
        backend.Render(new(Array.Empty<PaintCommand>()), ColorRgba.White);
        var old = memory.CurrentFrame;
        fail = true;
        try
        {
            backend.Resize(16, 16, 1);
            throw new InvalidOperationException("Resize failure missed");
        }
        catch (OutOfMemoryException) { }
        Check(ReferenceEquals(old, memory.CurrentFrame), "Resize failure frame");
        try
        {
            backend.Render(new(Array.Empty<PaintCommand>()), ColorRgba.Black);
            throw new InvalidOperationException("Render failure missed");
        }
        catch (OutOfMemoryException) { }
        Check(ReferenceEquals(old, memory.CurrentFrame), "Render failure frame");
        fail = false;
        backend.Render(new(Array.Empty<PaintCommand>()), ColorRgba.White);
        Check(memory.CurrentFrame!.PixelWidth == 8, "Resize failure keeps old viewport");
        var nth = 0;
        using var nested = new CairoRenderBackend(new(memory), new(8, 8, 1), (w, h) => { if (++nth == 3) throw new OutOfMemoryException("Injected effect allocation failure"); });
        old = memory.CurrentFrame;
        try
        {
            nested.Render(new(new PaintCommand[] { new DrawBoxShadowCommand(new(2, 2, 2, 2), default, new(0, 0, 2, 0, ColorRgba.Black, false)) }), default);
            throw new InvalidOperationException("Effect failure missed");
        }
        catch (OutOfMemoryException) { }
        Check(ReferenceEquals(old, memory.CurrentFrame), "Effect allocation failure no partial frame");
        foreach (var dependency in new[] { "cairo", "pango" })
        {
            var start = new ProcessStartInfo(Environment.ProcessPath!) { RedirectStandardOutput = true, RedirectStandardError = true, UseShellExecute = false };
            if (Path.GetFileNameWithoutExtension(Environment.ProcessPath) == "dotnet")
                start.ArgumentList.Add(Assembly.GetExecutingAssembly().Location);
            start.ArgumentList.Add("--missing-" + dependency);
            using var process = Process.Start(start)!;
            var stdout = process.StandardOutput.ReadToEnd();
            var stderr = process.StandardError.ReadToEnd();
            process.WaitForExit();
            Check(process.ExitCode == 0 && stdout.Contains("PASS missing dependency", StringComparison.Ordinal), stdout + stderr);
        }
        Console.WriteLine("PASS failure injection: Create/Resize/Render/effect allocation rollback and isolated missing Cairo/Pango. Not a real system OOM test.");
    }
}
