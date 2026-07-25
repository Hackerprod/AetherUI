namespace AetherUI.StyleRuntime;

internal readonly record struct CompiledPropertyDeclaration(
    int PropertyId,
    CompiledStyleValue Value,
    bool Important,
    int DeclarationOrder);

internal readonly record struct DeferredShorthandDeclaration(
    string Name,
    string Source,
    bool Important,
    int DeclarationOrder);

internal readonly record struct CustomPropertyDeclaration(
    string Name,
    string Source,
    bool Important,
    int DeclarationOrder);

internal sealed record CompiledRule(
    int RuleId,
    StyleSheetHandle Sheet,
    StyleSheetLayer Layer,
    CompiledSelector Selector,
    MediaCondition Condition,
    IReadOnlyList<CompiledPropertyDeclaration> Declarations,
    IReadOnlyList<DeferredShorthandDeclaration> DeferredShorthands,
    IReadOnlyList<CustomPropertyDeclaration> CustomDeclarations,
    int SourceOrder);

internal sealed record CompiledStyleSheet(
    StyleSheetHandle Handle,
    StyleSheetLayer Layer,
    string SourceName,
    IReadOnlyList<CompiledRule> Rules,
    IReadOnlyList<StyleDiagnostic> Diagnostics);

internal sealed record InlineDeclarationSet(
    IReadOnlyList<CompiledPropertyDeclaration> Declarations,
    IReadOnlyList<DeferredShorthandDeclaration> DeferredShorthands,
    IReadOnlyList<CustomPropertyDeclaration> CustomDeclarations,
    IReadOnlyList<StyleDiagnostic> Diagnostics)
{
    public static InlineDeclarationSet Empty { get; } = new(
        Array.Empty<CompiledPropertyDeclaration>(),
        Array.Empty<DeferredShorthandDeclaration>(),
        Array.Empty<CustomPropertyDeclaration>(),
        Array.Empty<StyleDiagnostic>());
}
