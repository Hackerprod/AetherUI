namespace AetherUI.Validator;

public sealed record ValidatorOptions(string? HtmlPath, bool Perf, bool Modal, string OutputRoot,
    int? Width, int? Height, bool CaptureReference, int SettleMilliseconds, bool Help)
{
    public static ValidatorOptions Parse(string[] args)
    {
        bool perf = false, modal = false, capture = false, help = false;
        string? path = null;
        var output = Path.Combine(Environment.CurrentDirectory, "artifacts", "validator");
        int? width = null, height = null;
        int settle = 500;
        for (int i = 0; i < args.Length; i++)
        {
            string Value() => ++i < args.Length ? args[i] : throw new ArgumentException($"{args[i - 1]} requires a value.");
            switch (args[i].ToLowerInvariant())
            {
                case "--perf": perf = true; break;
                case "--modal": modal = true; break;
                case "--capture-reference": capture = true; break;
                case "--help": case "-h": help = true; break;
                case "--page":
                    if (path is not null) throw new ArgumentException("Specify only one HTML file.");
                    path = Value(); break;
                case "--output": output = Path.GetFullPath(Value()); break;
                case "--settle-ms":
                    if (!int.TryParse(Value(), out settle) || settle < 0 || settle > 60000)
                        throw new ArgumentException("--settle-ms must be 0..60000.");
                    break;
                case "--viewport":
                    var parts = Value().ToLowerInvariant().Split('x');
                    if (parts.Length != 2 || !int.TryParse(parts[0], out var w) || !int.TryParse(parts[1], out var h)
                        || w < 100 || h < 100 || w > 8192 || h > 8192)
                        throw new ArgumentException("--viewport must be WIDTHxHEIGHT, each 100..8192 physical client pixels.");
                    width = w; height = h; break;
                default:
                    if (args[i].StartsWith('-')) throw new ArgumentException($"Unknown option: {args[i]}");
                    if (path is not null) throw new ArgumentException("Specify only one HTML file.");
                    path = args[i]; break;
            }
        }
        if (modal && path is not null) throw new ArgumentException("--modal and --page cannot be combined.");
        if (capture && path is null) throw new ArgumentException("--capture-reference requires --page.");
        if (path is not null)
        {
            path = Path.GetFullPath(path);
            if (!File.Exists(path)) throw new FileNotFoundException("HTML input does not exist.", path);
        }
        return new(path, perf, modal, output, width, height, capture, settle, help);
    }
}
