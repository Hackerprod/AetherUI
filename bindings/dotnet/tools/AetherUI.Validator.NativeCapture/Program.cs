namespace AetherUI.Validator.NativeCapture;
internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        string? output=null; bool claimed=false;
        try
        {
            if(args.Length==3 && args[0]=="self-test" && args[1]=="--output")
            {
                output=args[2]; NativeCaptureRunner.ClaimOutput(output); claimed=true;
                RuntimeGuard.Check(); HiddenCaptureWindow.ConfigureDpi();
                int exit=CaptureSelfTests.Run(output); RuntimeGuard.Check(); return exit;
            }
            if(args.Length!=13 || args[0]!="capture") throw new ArgumentException("Expected capture with page, hash, output, viewport, dpi and driver");
            var values=new Dictionary<string,string>(StringComparer.Ordinal);
            for(int i=1;i<args.Length;i+=2) if(!values.TryAdd(args[i],args[i+1])) throw new ArgumentException("Duplicate argument");
            string Get(string key)=>values.TryGetValue(key,out var value)?value:throw new ArgumentException("Missing argument");
            if(values.Keys.Any(k=>k is not ("--page" or "--expected-sha256" or "--output" or "--viewport" or "--dpi" or "--driver")) || Get("--viewport")!="1440x900" || Get("--dpi")!="96" || Get("--driver")!="warp") throw new ArgumentException("Unsupported capture parameters");
            string page=Get("--page"), markup=NativeCaptureRunner.ValidateInput(page,Get("--expected-sha256"));
            output=Get("--output"); NativeCaptureRunner.ClaimOutput(output); claimed=true;
            RuntimeGuard.Check(); HiddenCaptureWindow.ConfigureDpi();
            return NativeCaptureRunner.Run(page,markup,output);
        }
        catch(Exception error)
        {
            int exit=error is ArgumentException?2:4;
            // Never touch a preexisting output directory, including when a claim failed.
            if(claimed && output is not null && !File.Exists(Path.Combine(output,"manifest.json")))
                ArtifactManifest.Write(Path.Combine(output,"manifest.json"),new{schema="aether-native-capture-v1",status="failed-before-run",exitCode=exit,error=error.GetType().Name,visualParity="not-run"});
            Console.Error.WriteLine($"Native capture failed: {error.GetType().Name}; exit={exit}");
            return exit;
        }
    }
}
