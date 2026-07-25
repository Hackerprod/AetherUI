namespace AetherUI.StyleRuntime;

/// <summary>
/// Dynamic, Aether-native style machine. Source styles are compiled once into rule plans;
/// every subsequent update evaluates those plans directly against the live Aether tree.
/// </summary>
public sealed class AetherStyleRuntime : IDisposable
{
    private readonly StyleRuntimeOptions _options;
    private readonly StylePropertyRegistry _properties;
    private readonly SelectorMatcher _matcher = new();
    private readonly RuleIndex _index = new();
    private readonly VariableResolver _variables;
    private readonly Dictionary<long, CompiledStyleSheet> _sheets = [];
    private readonly Dictionary<long, NodeState> _nodeStates = [];
    private readonly int _ownerThreadId;
    private StyleEnvironment _environment;
    private long _nextSheetHandle = 1;
    private int _nextRuleId = 1;
    private int _nextSourceOrder = 1;
    private int _programVersion = 1;
    private bool _disposed;

    public AetherStyleRuntime(
        StylePropertyRegistry? properties = null,
        StyleRuntimeOptions? options = null,
        StyleEnvironment? environment = null)
    {
        _options = options ?? new StyleRuntimeOptions();
        _options.Validate();
        _properties = properties ?? StylePropertyRegistry.CreateDefault();
        _properties.Seal();
        _environment = (environment ?? StyleEnvironment.Default).Validate();
        _variables = new VariableResolver(_options);
        _ownerThreadId = System.Environment.CurrentManagedThreadId;
    }

    public event Action<IReadOnlyList<StyleDiagnostic>>? DiagnosticsProduced;

    public StyleEnvironment Environment => _environment;
    public StylePropertyRegistry Properties => _properties;

    public StyleSheetLoadResult AddStyleSheet(
        string source,
        StyleSheetLayer layer = StyleSheetLayer.Author,
        string sourceName = "<style>")
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(source);

        var handle = new StyleSheetHandle(_nextSheetHandle++);
        var sheet = StyleSourceCompiler.Compile(
            source,
            sourceName,
            handle,
            layer,
            _properties,
            _options,
            AllocateRuleId,
            AllocateSourceOrder);

        if (_options.ThrowOnInvalidStyleSheet &&
            sheet.Diagnostics.Any(static item => item.Severity == StyleDiagnosticSeverity.Error))
        {
            throw new StyleCompilationException($"Stylesheet '{sourceName}' contains errors.", sheet.Diagnostics);
        }

        _sheets.Add(handle.Value, sheet);
        RebuildIndex();
        PublishDiagnostics(sheet.Diagnostics);
        return new StyleSheetLoadResult(handle, sheet.Diagnostics, sheet.Rules.Count);
    }

    public bool RemoveStyleSheet(StyleSheetHandle handle)
    {
        VerifyAccess();
        if (!_sheets.Remove(handle.Value))
        {
            return false;
        }

        RebuildIndex();
        return true;
    }

    public IReadOnlyList<StyleUpdate> ResolveSubtree(IAetherStyleNode root)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(root);
        EnsureAncestorsResolved(root);
        var updates = new List<StyleUpdate>();
        ResolveSubtreeCore(root, updates);
        return updates;
    }

    public StyleUpdate ResolveNode(IAetherStyleNode node)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(node);
        EnsureAncestorsResolved(node);
        return ComputeAndStore(node);
    }

    public IReadOnlyList<StyleUpdate> NotifyMutation(IAetherStyleNode node, StyleMutation mutation)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(node);

        if (mutation.Kind is StyleMutationKind.Full or StyleMutationKind.Environment)
        {
            return ResolveSubtree(GetRoot(node));
        }

        if (_index.Dependencies.HasFlag(SelectorDependency.Descendant))
        {
            return ResolveSubtree(GetRoot(node));
        }

        if (mutation.Kind is StyleMutationKind.Children or StyleMutationKind.Parent ||
            _index.Dependencies.HasFlag(SelectorDependency.Sibling) ||
            _index.Dependencies.HasFlag(SelectorDependency.Structure))
        {
            return ResolveSubtree(node.StyleParent ?? node);
        }

        if (_index.Dependencies.HasFlag(SelectorDependency.Ancestor))
        {
            return ResolveSubtree(node);
        }

        EnsureAncestorsResolved(node);
        var updates = new List<StyleUpdate>();
        var update = ComputeAndStore(node);
        if (update.Impact != StyleImpact.None)
        {
            updates.Add(update);
        }

        if (update.Impact.HasFlag(StyleImpact.Inherited))
        {
            foreach (var child in node.StyleChildren)
            {
                ResolveSubtreeCore(child, updates);
            }
        }

        return updates;
    }

    public IReadOnlyList<StyleUpdate> SetEnvironment(StyleEnvironment environment, IAetherStyleNode root)
    {
        VerifyAccess();
        var validated = environment.Validate();
        if (validated == _environment)
        {
            return Array.Empty<StyleUpdate>();
        }

        _environment = validated;
        return ResolveSubtree(root);
    }

    public bool TryGetComputedStyle(long nodeId, out AetherComputedStyle style)
    {
        VerifyAccess();
        if (_nodeStates.TryGetValue(nodeId, out var state) && state.ProgramVersion == _programVersion)
        {
            style = state.Style;
            return true;
        }

        style = null!;
        return false;
    }

    public AetherComputedStyle GetComputedStyle(IAetherStyleNode node)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(node);
        if (_nodeStates.TryGetValue(node.StyleNodeId, out var state) && state.ProgramVersion == _programVersion)
        {
            return state.Style;
        }

        return ResolveNode(node).Current;
    }

    public void DetachSubtree(IAetherStyleNode root)
    {
        VerifyAccess();
        ArgumentNullException.ThrowIfNull(root);
        foreach (var child in root.StyleChildren)
        {
            DetachSubtree(child);
        }

        _nodeStates.Remove(root.StyleNodeId);
    }

    public void ClearCache()
    {
        VerifyAccess();
        _nodeStates.Clear();
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        VerifyThread();
        _disposed = true;
        _nodeStates.Clear();
        _sheets.Clear();
    }

    private StyleUpdate ComputeAndStore(IAetherStyleNode node)
    {
        NodeState? parentState = null;
        if (node.StyleParent is not null &&
            _nodeStates.TryGetValue(node.StyleParent.StyleNodeId, out var cachedParent) &&
            cachedParent.ProgramVersion == _programVersion)
        {
            parentState = cachedParent;
        }

        _nodeStates.TryGetValue(node.StyleNodeId, out var existing);
        var previous = existing?.Style;
        var inline = GetInlineDeclarations(node, existing);
        var candidates = _index.GetCandidates(node);
        var matched = new List<CompiledRule>(candidates.Count);
        foreach (var rule in candidates)
        {
            if (rule.Condition.Matches(_environment) && _matcher.Matches(rule.Selector, node))
            {
                matched.Add(rule);
            }
        }

        var customResult = BuildCustomPropertyScope(parentState?.CustomProperties, matched, inline);
        var cascade = new CascadeEntry[_properties.PropertyCount];
        foreach (var rule in matched)
        {
            OfferRuleDeclarations(rule, customResult.Scope, cascade);
        }

        OfferInlineDeclarations(inline, customResult.Scope, cascade);

        var builder = new AetherStyleBuilder();
        InitializeBuilder(builder, parentState?.Style);
        for (var propertyId = 0; propertyId < cascade.Length; propertyId++)
        {
            if (!cascade[propertyId].IsSet)
            {
                continue;
            }

            var definition = _properties.Definitions[propertyId];
            ApplyValue(builder, definition, cascade[propertyId].Value, parentState?.Style, customResult.Scope);
        }

        NormalizeComputedValues(builder, parentState?.Style);
        var current = builder.Build();
        var impact = StyleDiff.Compare(previous, current, _properties.Definitions);
        if (HasInheritedChange(previous, current) ||
            existing is not null && !DictionaryEquals(existing.CustomLocalValues, customResult.LocalValues))
        {
            impact |= StyleImpact.Inherited;
        }

        var state = existing ?? new NodeState();
        state.Style = current;
        state.CustomProperties = customResult.Scope;
        state.CustomLocalValues = customResult.LocalValues;
        state.InlineSource = node.InlineStyleText;
        state.InlineDeclarations = inline;
        state.ProgramVersion = _programVersion;
        _nodeStates[node.StyleNodeId] = state;
        return new StyleUpdate(node, previous, current, impact);
    }

    private CustomScopeResult BuildCustomPropertyScope(
        CustomPropertyScope? parent,
        IReadOnlyList<CompiledRule> matched,
        InlineDeclarationSet inline)
    {
        var winners = new Dictionary<string, CustomCascadeEntry>(StringComparer.Ordinal);
        foreach (var rule in matched)
        {
            foreach (var declaration in rule.CustomDeclarations)
            {
                var priority = new CascadePriority(
                    declaration.Important,
                    Inline: false,
                    rule.Layer,
                    rule.Selector.Specificity,
                    rule.SourceOrder,
                    declaration.DeclarationOrder);
                if (!winners.TryGetValue(declaration.Name, out var current))
                {
                    current = default;
                }

                current.Offer(priority, declaration.Source);
                winners[declaration.Name] = current;
            }
        }

        foreach (var declaration in inline.CustomDeclarations)
        {
            var priority = new CascadePriority(
                declaration.Important,
                Inline: true,
                StyleSheetLayer.Author,
                new Specificity(1, 0, 0),
                int.MaxValue,
                declaration.DeclarationOrder);
            if (!winners.TryGetValue(declaration.Name, out var current))
            {
                current = default;
            }

            current.Offer(priority, declaration.Source);
            winners[declaration.Name] = current;
        }

        var local = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var pair in winners)
        {
            var source = pair.Value.Source.Trim();
            if (source.Equals("inherit", StringComparison.OrdinalIgnoreCase) ||
                source.Equals("unset", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            local[pair.Key] = source.Equals("initial", StringComparison.OrdinalIgnoreCase) ||
                source.Equals("revert", StringComparison.OrdinalIgnoreCase)
                ? CustomPropertyScope.InvalidValue
                : pair.Value.Source;
        }

        return new CustomScopeResult(new CustomPropertyScope(parent, local), local);
    }

    private void OfferRuleDeclarations(
        CompiledRule rule,
        CustomPropertyScope customScope,
        CascadeEntry[] cascade)
    {
        foreach (var declaration in rule.Declarations)
        {
            var priority = new CascadePriority(
                declaration.Important,
                Inline: false,
                rule.Layer,
                rule.Selector.Specificity,
                rule.SourceOrder,
                declaration.DeclarationOrder);
            cascade[declaration.PropertyId].Offer(priority, declaration.Value);
        }

        foreach (var shorthand in rule.DeferredShorthands)
        {
            var priority = new CascadePriority(
                shorthand.Important,
                Inline: false,
                rule.Layer,
                rule.Selector.Specificity,
                rule.SourceOrder,
                shorthand.DeclarationOrder);
            OfferDeferredShorthand(shorthand.Name, shorthand.Source, priority, customScope, cascade);
        }
    }

    private void OfferInlineDeclarations(
        InlineDeclarationSet inline,
        CustomPropertyScope customScope,
        CascadeEntry[] cascade)
    {
        foreach (var declaration in inline.Declarations)
        {
            var priority = new CascadePriority(
                declaration.Important,
                Inline: true,
                StyleSheetLayer.Author,
                new Specificity(1, 0, 0),
                int.MaxValue,
                declaration.DeclarationOrder);
            cascade[declaration.PropertyId].Offer(priority, declaration.Value);
        }

        foreach (var shorthand in inline.DeferredShorthands)
        {
            var priority = new CascadePriority(
                shorthand.Important,
                Inline: true,
                StyleSheetLayer.Author,
                new Specificity(1, 0, 0),
                int.MaxValue,
                shorthand.DeclarationOrder);
            OfferDeferredShorthand(shorthand.Name, shorthand.Source, priority, customScope, cascade);
        }
    }

    private void OfferDeferredShorthand(
        string name,
        string source,
        CascadePriority priority,
        CustomPropertyScope customScope,
        CascadeEntry[] cascade)
    {
        if (!_properties.TryGetShorthand(name, out var shorthand) ||
            !_variables.TryResolve(source, customScope, out var resolved))
        {
            return;
        }

        var writer = new StyleShorthandWriter();
        if (!shorthand(resolved, writer))
        {
            return;
        }

        foreach (var expanded in writer.Items)
        {
            if (!_properties.TryGet(expanded.Name, out var definition) ||
                !TryCreateRuntimeValue(definition, expanded.Value, out var value))
            {
                continue;
            }

            cascade[definition.Id].Offer(priority, value);
        }
    }

    private void ApplyValue(
        AetherStyleBuilder builder,
        StylePropertyDefinition definition,
        CompiledStyleValue compiled,
        AetherComputedStyle? parent,
        CustomPropertyScope customScope)
    {
        object? value;
        switch (compiled.Mode)
        {
            case CompiledValueMode.Value:
                value = compiled.Value;
                break;
            case CompiledValueMode.Initial:
                value = definition.InitialValue;
                break;
            case CompiledValueMode.Inherit:
                value = parent is null ? definition.InitialValue : definition.Read(parent);
                break;
            case CompiledValueMode.Unset:
                value = definition.Inherited && parent is not null
                    ? definition.Read(parent)
                    : definition.InitialValue;
                break;
            case CompiledValueMode.Deferred:
                if (compiled.DeferredSource is null ||
                    !_variables.TryResolve(compiled.DeferredSource, customScope, out var resolved))
                {
                    return;
                }

                if (!TryCreateRuntimeValue(definition, resolved, out var runtimeValue))
                {
                    return;
                }

                ApplyValue(builder, definition, runtimeValue, parent, customScope);
                return;
            default:
                return;
        }

        definition.Apply(builder, value);
    }

    private void InitializeBuilder(AetherStyleBuilder builder, AetherComputedStyle? parent)
    {
        foreach (var definition in _properties.Definitions)
        {
            var value = definition.Inherited && parent is not null
                ? definition.Read(parent)
                : definition.InitialValue;
            definition.Apply(builder, value);
        }
    }

    private void NormalizeComputedValues(AetherStyleBuilder builder, AetherComputedStyle? parent)
    {
        var parentFontSize = parent?.Text.FontSize.Pixels ?? _environment.RootFontSize;
        if (!float.IsFinite(parentFontSize) || parentFontSize <= 0f)
        {
            parentFontSize = _environment.RootFontSize;
        }

        var resolvedFontSize = builder.FontSize.Resolve(new StyleLengthResolveContext(
            parentFontSize,
            parentFontSize,
            _environment.RootFontSize,
            _environment.ViewportWidth,
            _environment.ViewportHeight));
        if (!float.IsFinite(resolvedFontSize) || resolvedFontSize < 0f)
        {
            resolvedFontSize = parentFontSize;
        }

        builder.FontSize = StyleLength.PixelsValue(resolvedFontSize);
        var resolvedLineHeight = builder.LineHeight.Resolve(new StyleLengthResolveContext(
            resolvedFontSize,
            resolvedFontSize,
            _environment.RootFontSize,
            _environment.ViewportWidth,
            _environment.ViewportHeight));
        if (!float.IsFinite(resolvedLineHeight) || resolvedLineHeight < 0f)
        {
            resolvedLineHeight = resolvedFontSize * 1.2f;
        }

        builder.LineHeight = StyleLength.PixelsValue(resolvedLineHeight);
        if (builder.Color.IsCurrentColor)
        {
            builder.Color = parent?.Text.Color ?? StyleColor.Black;
        }

        builder.BackgroundColor = builder.BackgroundColor.ResolveCurrentColor(builder.Color);
        builder.BorderColor = new StyleEdges<StyleColor>(
            builder.BorderColor.Top.ResolveCurrentColor(builder.Color),
            builder.BorderColor.Right.ResolveCurrentColor(builder.Color),
            builder.BorderColor.Bottom.ResolveCurrentColor(builder.Color),
            builder.BorderColor.Left.ResolveCurrentColor(builder.Color));

        if (builder.BoxShadows.Count > 0 && builder.BoxShadows.Any(static shadow => shadow.Color.IsCurrentColor))
        {
            builder.BoxShadows = builder.BoxShadows
                .Select(shadow => shadow with { Color = shadow.Color.ResolveCurrentColor(builder.Color) })
                .ToArray();
        }

        builder.Opacity = Math.Clamp(builder.Opacity, 0f, 1f);
    }

    private InlineDeclarationSet GetInlineDeclarations(IAetherStyleNode node, NodeState? existing)
    {
        var source = node.InlineStyleText;
        if (string.IsNullOrWhiteSpace(source))
        {
            return InlineDeclarationSet.Empty;
        }

        if (existing is not null && string.Equals(existing.InlineSource, source, StringComparison.Ordinal))
        {
            return existing.InlineDeclarations;
        }

        var compiled = StyleSourceCompiler.CompileInline(
            source,
            _properties,
            _options,
            $"inline:{node.StyleNodeId}");
        PublishDiagnostics(compiled.Diagnostics);
        return compiled;
    }

    private bool TryCreateRuntimeValue(
        StylePropertyDefinition definition,
        string source,
        out CompiledStyleValue value)
    {
        var normalized = source.Trim();
        if (normalized.Equals("initial", StringComparison.OrdinalIgnoreCase) ||
            normalized.Equals("revert", StringComparison.OrdinalIgnoreCase))
        {
            value = CompiledStyleValue.Initial;
            return true;
        }

        if (normalized.Equals("inherit", StringComparison.OrdinalIgnoreCase))
        {
            value = CompiledStyleValue.Inherit;
            return true;
        }

        if (normalized.Equals("unset", StringComparison.OrdinalIgnoreCase))
        {
            value = CompiledStyleValue.Unset;
            return true;
        }

        var parsed = definition.Parse(normalized);
        if (parsed.Success)
        {
            value = CompiledStyleValue.Typed(parsed.Value);
            return true;
        }

        value = default;
        return false;
    }

    private bool HasInheritedChange(AetherComputedStyle? previous, AetherComputedStyle current)
    {
        if (previous is null)
        {
            return true;
        }

        foreach (var definition in _properties.Definitions)
        {
            if (definition.Inherited && !Equals(definition.Read(previous), definition.Read(current)))
            {
                return true;
            }
        }

        return false;
    }

    private void ResolveSubtreeCore(IAetherStyleNode node, List<StyleUpdate> updates)
    {
        var update = ComputeAndStore(node);
        if (update.Impact != StyleImpact.None)
        {
            updates.Add(update);
        }

        foreach (var child in node.StyleChildren)
        {
            ResolveSubtreeCore(child, updates);
        }
    }

    private void EnsureAncestorsResolved(IAetherStyleNode node)
    {
        var missing = new Stack<IAetherStyleNode>();
        var current = node.StyleParent;
        while (current is not null)
        {
            if (_nodeStates.TryGetValue(current.StyleNodeId, out var state) &&
                state.ProgramVersion == _programVersion)
            {
                break;
            }

            missing.Push(current);
            current = current.StyleParent;
        }

        while (missing.Count > 0)
        {
            ComputeAndStore(missing.Pop());
        }
    }

    private void RebuildIndex()
    {
        _index.Rebuild(_sheets.Values);
        _programVersion++;
    }

    private int AllocateRuleId() => _nextRuleId++;
    private int AllocateSourceOrder() => _nextSourceOrder++;

    private void PublishDiagnostics(IReadOnlyList<StyleDiagnostic> diagnostics)
    {
        if (diagnostics.Count > 0)
        {
            DiagnosticsProduced?.Invoke(diagnostics);
        }
    }

    private void VerifyAccess()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        VerifyThread();
    }

    private void VerifyThread()
    {
        if (_options.EnforceThreadAffinity && System.Environment.CurrentManagedThreadId != _ownerThreadId)
        {
            throw new InvalidOperationException("AetherStyleRuntime must be used from its owning UI thread.");
        }
    }

    private static IAetherStyleNode GetRoot(IAetherStyleNode node)
    {
        while (node.StyleParent is not null)
        {
            node = node.StyleParent;
        }

        return node;
    }

    private static bool DictionaryEquals(
        IReadOnlyDictionary<string, string> left,
        IReadOnlyDictionary<string, string> right)
    {
        if (left.Count != right.Count)
        {
            return false;
        }

        foreach (var pair in left)
        {
            if (!right.TryGetValue(pair.Key, out var value) || !string.Equals(pair.Value, value, StringComparison.Ordinal))
            {
                return false;
            }
        }

        return true;
    }

    private sealed class NodeState
    {
        public AetherComputedStyle Style { get; set; } = AetherComputedStyle.Initial;
        public CustomPropertyScope CustomProperties { get; set; } =
            new(null, new Dictionary<string, string>(StringComparer.Ordinal));
        public IReadOnlyDictionary<string, string> CustomLocalValues { get; set; } =
            new Dictionary<string, string>(StringComparer.Ordinal);
        public string? InlineSource { get; set; }
        public InlineDeclarationSet InlineDeclarations { get; set; } = InlineDeclarationSet.Empty;
        public int ProgramVersion { get; set; }
    }

    private readonly record struct CustomScopeResult(
        CustomPropertyScope Scope,
        IReadOnlyDictionary<string, string> LocalValues);
}
