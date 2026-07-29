namespace AetherUI.Runtime;

/// <summary>
/// Observable host-pipeline work. These are operation counts rather than wall-clock samples so
/// performance tests remain stable across machines and drivers.
/// </summary>
public readonly record struct UiDocumentHostMetrics(
    long StyleResolutions,
    long LayoutBuilds,
    long DisplayListBuilds,
    long ScrollCompositions);
