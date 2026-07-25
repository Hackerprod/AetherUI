namespace AetherUI.StyleRuntime;

public delegate bool StyleValueParser<T>(string source, out T value);
public delegate bool StyleShorthandExpander(string source, StyleShorthandWriter writer);

public sealed class StyleShorthandWriter
{
    private readonly List<(string Name, string Value)> _items = [];

    public void Set(string propertyName, string value)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(propertyName);
        ArgumentException.ThrowIfNullOrWhiteSpace(value);
        _items.Add((propertyName.Trim().ToLowerInvariant(), value.Trim()));
    }

    internal IReadOnlyList<(string Name, string Value)> Items => _items;
}

internal enum CompiledValueMode : byte
{
    Value,
    Initial,
    Inherit,
    Unset,
    Deferred,
}

internal readonly record struct CompiledStyleValue(
    CompiledValueMode Mode,
    object? Value,
    string? DeferredSource)
{
    public static CompiledStyleValue Typed(object? value) => new(CompiledValueMode.Value, value, null);
    public static CompiledStyleValue Initial => new(CompiledValueMode.Initial, null, null);
    public static CompiledStyleValue Inherit => new(CompiledValueMode.Inherit, null, null);
    public static CompiledStyleValue Unset => new(CompiledValueMode.Unset, null, null);
    public static CompiledStyleValue Deferred(string source) => new(CompiledValueMode.Deferred, null, source);
}

internal sealed class StylePropertyDefinition
{
    public required int Id { get; init; }
    public required string Name { get; init; }
    public required bool Inherited { get; init; }
    public required StyleImpact Impact { get; init; }
    public required object? InitialValue { get; init; }
    public required Func<string, (bool Success, object? Value)> Parse { get; init; }
    public required Action<AetherStyleBuilder, object?> Apply { get; init; }
    public required Func<AetherComputedStyle, object?> Read { get; init; }
}

/// <summary>
/// Registry of native Aether style capabilities. Each property is connected once to a typed
/// parser, initial value, inheritance rule, invalidation impact and computed-style destination.
/// </summary>
public sealed class StylePropertyRegistry
{
    private readonly Dictionary<string, StylePropertyDefinition> _properties =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, StyleShorthandExpander> _shorthands =
        new(StringComparer.OrdinalIgnoreCase);
    private readonly List<StylePropertyDefinition> _ordered = [];
    private bool _sealed;

    public int PropertyCount => _ordered.Count;

    public static StylePropertyRegistry CreateDefault()
    {
        var registry = new StylePropertyRegistry();
        BuiltinStyleProperties.Register(registry);
        return registry;
    }

    public void Register<T>(
        string name,
        bool inherited,
        StyleImpact impact,
        T initialValue,
        StyleValueParser<T> parser,
        Action<AetherStyleBuilder, T> apply,
        Func<AetherComputedStyle, T> read)
    {
        EnsureMutable();
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(parser);
        ArgumentNullException.ThrowIfNull(apply);
        ArgumentNullException.ThrowIfNull(read);

        var normalized = name.Trim().ToLowerInvariant();
        if (_properties.ContainsKey(normalized) || _shorthands.ContainsKey(normalized))
        {
            throw new InvalidOperationException($"Style property '{normalized}' is already registered.");
        }

        var definition = new StylePropertyDefinition
        {
            Id = _ordered.Count,
            Name = normalized,
            Inherited = inherited,
            Impact = inherited ? impact | StyleImpact.Inherited : impact,
            InitialValue = initialValue,
            Parse = source =>
            {
                if (parser(source, out var value))
                {
                    return (true, (object?)value);
                }

                return (false, null);
            },
            Apply = (builder, value) => apply(builder, (T)value!),
            Read = style => read(style),
        };

        _properties.Add(normalized, definition);
        _ordered.Add(definition);
    }

    /// <summary>
    /// Registers a native engine extension such as -aether-window-drag-region without adding
    /// a field to the core style records.
    /// </summary>
    public void RegisterExtension<T>(
        string name,
        bool inherited,
        StyleImpact impact,
        T initialValue,
        StyleValueParser<T> parser,
        string? extensionKey = null)
    {
        var key = string.IsNullOrWhiteSpace(extensionKey) ? name.Trim().ToLowerInvariant() : extensionKey!;
        Register(
            name,
            inherited,
            impact,
            initialValue,
            parser,
            (builder, value) => builder.SetExtension(key, value),
            style => style.GetExtension(key, initialValue));
    }

    public void RegisterShorthand(string name, StyleShorthandExpander expander)
    {
        EnsureMutable();
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(expander);

        var normalized = name.Trim().ToLowerInvariant();
        if (_properties.ContainsKey(normalized) || !_shorthands.TryAdd(normalized, expander))
        {
            throw new InvalidOperationException($"Style property or shorthand '{normalized}' is already registered.");
        }
    }

    internal IReadOnlyList<StylePropertyDefinition> Definitions => _ordered;

    internal bool TryGet(string name, out StylePropertyDefinition definition) =>
        _properties.TryGetValue(name, out definition!);

    internal bool TryGetShorthand(string name, out StyleShorthandExpander expander) =>
        _shorthands.TryGetValue(name, out expander!);

    internal void Seal() => _sealed = true;

    private void EnsureMutable()
    {
        if (_sealed)
        {
            throw new InvalidOperationException("The property registry is sealed after a runtime starts using it.");
        }
    }
}
