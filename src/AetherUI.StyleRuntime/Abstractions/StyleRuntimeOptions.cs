namespace AetherUI.StyleRuntime;

public sealed class StyleRuntimeOptions
{
    /// <summary>Throws when a stylesheet contains an error instead of returning diagnostics.</summary>
    public bool ThrowOnInvalidStyleSheet { get; init; }

    /// <summary>Checks that all runtime calls are made from the thread that created the runtime.</summary>
    public bool EnforceThreadAffinity { get; init; } = true;

    /// <summary>Maximum nesting accepted while resolving var() references.</summary>
    public int MaximumVariableDepth { get; init; } = 32;

    /// <summary>Maximum expanded character count produced by one var() resolution.</summary>
    public int MaximumVariableExpansionLength { get; init; } = 64 * 1024;

    internal void Validate()
    {
        if (MaximumVariableDepth is < 1 or > 256)
        {
            throw new ArgumentOutOfRangeException(nameof(MaximumVariableDepth));
        }

        if (MaximumVariableExpansionLength is < 256 or > 4 * 1024 * 1024)
        {
            throw new ArgumentOutOfRangeException(nameof(MaximumVariableExpansionLength));
        }
    }
}
