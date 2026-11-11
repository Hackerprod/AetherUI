using System.Diagnostics;
using System.Runtime.InteropServices;
using AetherUI.Events;
using AetherUI.Runtime;

namespace AetherUI.Validator;

/// <summary>
/// Native AetherUI start page for the manual validation suite. It deliberately renders through the
/// same HTML, CSS, layout, input, and Direct2D pipeline that the Validator exercises.
/// </summary>
public sealed class PageCatalogComponent : UiComponent
{
    public const string WindowTitle = "AetherUI Validator - Pages";
    private const int SwHide = 0;
    private const int SwShow = 5;

    private readonly string _pagesDirectory;
    private readonly ValidatorOptions _options;
    private readonly ValidationReport _report;
    public string InstanceWindowTitle { get; } = $"{WindowTitle} - {Guid.NewGuid():N}";

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindow(string? className, string windowTitle);

    [DllImport("user32.dll")]
    private static extern bool ShowWindow(nint windowHandle, int command);

    public PageCatalogComponent(string pagesDirectory, ValidatorOptions options, ValidationReport report)
    {
        _pagesDirectory = pagesDirectory;
        _options = options;
        _report = report;
        Directory.CreateDirectory(_pagesDirectory);
    }

    protected override void BuildRenderTree(MarkupWriter writer)
    {
        var pages = Directory.EnumerateFiles(_pagesDirectory, "*.html", SearchOption.TopDirectoryOnly)
            .OrderBy(Path.GetFileName, StringComparer.OrdinalIgnoreCase)
            .ToArray();

        writer.WriteLiteral("""
            <style>
                html, body {
                    margin: 0;
                    height: 100%;
                    overflow-y: auto;
                    background: #0b1120;
                    color: #e5edf9;
                    font-family: "Segoe UI", Arial, sans-serif;
                }

                .workspace {
                    min-height: 100%;
                    padding: 32px;
                    background: linear-gradient(145deg, #111b34, #0b1120 48%, #09101d);
                }

                .hero {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin-bottom: 26px;
                    padding: 24px;
                    background: rgba(20, 32, 59, 0.9);
                    border: 1px solid #2a3d63;
                    border-radius: 18px;
                    box-shadow: 0 14px 32px rgba(0, 0, 0, 0.25);
                }

                .eyebrow {
                    margin: 0 0 8px 0;
                    color: #7dd3fc;
                    font-size: 12px;
                    font-weight: 700;
                }

                h1 {
                    margin: 0;
                    color: #f8fbff;
                    font-size: 28px;
                    font-weight: 700;
                }

                .subtitle {
                    margin: 10px 0 0 0;
                    color: #aebed7;
                    font-size: 14px;
                }

                .count {
                    padding: 9px 12px;
                    background: #17284a;
                    border: 1px solid #36598d;
                    border-radius: 999px;
                    color: #b9dcff;
                    font-size: 13px;
                    font-weight: 700;
                }

                .section-head {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin: 0 4px 12px 4px;
                }

                .section-title {
                    color: #dce9fb;
                    font-size: 15px;
                    font-weight: 700;
                }

                .refresh, .open {
                    border: 0;
                    border-radius: 9px;
                    font-family: "Segoe UI", Arial, sans-serif;
                    font-weight: 700;
                }

                .refresh {
                    padding: 9px 14px;
                    background: #263b61;
                    color: #d9e9ff;
                }

                .open {
                    padding: 10px 16px;
                    background: #38bdf8;
                    color: #06233a;
                }

                .refresh:hover { background: #34517f; }
                .open:hover { background: #7dd3fc; }

                .pages { display: flex; flex-direction: column; }

                .page-card {
                    display: flex;
                    justify-content: space-between;
                    align-items: center;
                    margin-bottom: 11px;
                    padding: 16px 18px;
                    background: rgba(19, 31, 57, 0.94);
                    border: 1px solid #263d65;
                    border-radius: 13px;
                }

                .page-card:hover {
                    background: #1a2b4d;
                    border-color: #4a78b9;
                }

                .demo-card {
                    border-color: #7c3aed;
                    background: rgba(54, 34, 92, 0.55);
                }

                .demo-card:hover {
                    background: #3d2a73;
                    border-color: #a78bfa;
                }

                .page-name {
                    color: #f4f8ff;
                    font-size: 16px;
                    font-weight: 700;
                }

                .page-file {
                    margin-top: 5px;
                    color: #8fa6c8;
                    font-size: 12px;
                }

                .empty {
                    padding: 30px;
                    background: rgba(19, 31, 57, 0.94);
                    border: 1px dashed #395580;
                    border-radius: 13px;
                    color: #aebed7;
                    text-align: center;
                }
            </style>
            <body>
                <main class="workspace">
                    <section class="hero">
                        <div>
                            <p class="eyebrow">AETHERUI / VALIDATION SUITE</p>
                            <h1>Validation workspace</h1>
                            <p class="subtitle">Open a page to compare the native renderer with Chromium.</p>
                        </div>
            """);

        writer.WriteLiteral("<span class=\"count\">");
        writer.Write($"{pages.Length} {(pages.Length == 1 ? "page" : "pages")}");
        writer.WriteLiteral("</span></section><section class=\"pages\"><div class=\"section-head\"><span class=\"section-title\">Available pages</span><button class=\"refresh\"");
        writer.WriteEventAttribute("onclick", Refresh);
        writer.WriteLiteral(">Refresh</button></div>");

        writer.WriteLiteral("<article class=\"page-card demo-card\"><div><div class=\"page-name\">Modal Interactivo (transitions)</div><div class=\"page-file\">ModalComponent.cs — live opacity + transform demo</div></div><button class=\"open\"");
        writer.WriteEventAttribute("onclick", _ => OpenModalDemo());
        writer.WriteLiteral(">Open</button></article>");

        if (pages.Length == 0)
        {
            writer.WriteLiteral("<div class=\"empty\">No HTML pages were found in the Pages folder.</div>");
        }
        else
        {
            foreach (var pagePath in pages)
            {
                writer.WriteLiteral("<article class=\"page-card\"><div><div class=\"page-name\">");
                writer.Write(Path.GetFileNameWithoutExtension(pagePath));
                writer.WriteLiteral("</div><div class=\"page-file\">");
                writer.Write(Path.GetFileName(pagePath));
                writer.WriteLiteral("</div></div><button class=\"open\"");
                writer.WriteEventAttribute("onclick", _ => OpenPage(pagePath));
                writer.WriteLiteral(">Open</button></article>");
            }
        }

        writer.WriteLiteral("</section></main></body>");
    }

    private void Refresh() => StateHasChanged();

    private void OpenPage(string pagePath) => LaunchValidator("--page", pagePath);

    private void OpenModalDemo() => LaunchValidator("--modal");

    private void LaunchValidator(params string[] arguments)
    {
        var executable = Environment.ProcessPath
            ?? throw new InvalidOperationException("The Validator executable path is unavailable.");
        var startInfo = new ProcessStartInfo(executable)
        {
            UseShellExecute = false,
            WorkingDirectory = AppContext.BaseDirectory,
        };
        if (string.Equals(Path.GetFileNameWithoutExtension(executable), "dotnet", StringComparison.OrdinalIgnoreCase))
            startInfo.ArgumentList.Add(typeof(Program).Assembly.Location);
        if (_options.Perf) startInfo.ArgumentList.Add("--perf");
        startInfo.ArgumentList.Add("--output");
        startInfo.ArgumentList.Add(_options.OutputRoot);
        if (_options.Width is int width && _options.Height is int height)
        {
            startInfo.ArgumentList.Add("--viewport");
            startInfo.ArgumentList.Add($"{width}x{height}");
        }
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        Process validator;
        try { validator = Process.Start(startInfo)
            ?? throw new InvalidOperationException("The Validator process could not be started."); }
        catch (Exception ex)
        {
            _report.Write("child-start-failed", new { message = ex.Message });
            return;
        }
        var catalogWindow = FindWindow(null, InstanceWindowTitle);
        if (catalogWindow != nint.Zero)
        {
            ShowWindow(catalogWindow, SwHide);
        }

        _ = RestoreCatalogWhenClosedAsync(validator, catalogWindow);
    }

    private async Task RestoreCatalogWhenClosedAsync(Process validator, nint catalogWindow)
    {
        try { await validator.WaitForExitAsync(); }
        catch (Exception ex) { _report.Write("child-wait-failed", new { message = ex.Message }); }
        finally { validator.Dispose(); }
        if (catalogWindow != nint.Zero)
        {
            ShowWindow(catalogWindow, SwShow);
        }
    }
}
