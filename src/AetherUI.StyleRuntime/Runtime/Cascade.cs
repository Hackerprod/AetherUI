namespace AetherUI.StyleRuntime;

internal readonly record struct CascadePriority(
    bool Important,
    bool Inline,
    StyleSheetLayer Layer,
    Specificity Specificity,
    int SourceOrder,
    int DeclarationOrder) : IComparable<CascadePriority>
{
    public int CompareTo(CascadePriority other)
    {
        var result = Important.CompareTo(other.Important);
        if (result != 0)
        {
            return result;
        }

        result = Inline.CompareTo(other.Inline);
        if (result != 0)
        {
            return result;
        }

        result = Layer.CompareTo(other.Layer);
        if (result != 0)
        {
            return result;
        }

        result = Specificity.CompareTo(other.Specificity);
        if (result != 0)
        {
            return result;
        }

        result = SourceOrder.CompareTo(other.SourceOrder);
        return result != 0 ? result : DeclarationOrder.CompareTo(other.DeclarationOrder);
    }
}

internal struct CascadeEntry
{
    public bool IsSet;
    public CascadePriority Priority;
    public CompiledStyleValue Value;

    public void Offer(CascadePriority priority, CompiledStyleValue value)
    {
        if (!IsSet || priority.CompareTo(Priority) >= 0)
        {
            IsSet = true;
            Priority = priority;
            Value = value;
        }
    }
}

internal struct CustomCascadeEntry
{
    public bool IsSet;
    public CascadePriority Priority;
    public string Source;

    public void Offer(CascadePriority priority, string source)
    {
        if (!IsSet || priority.CompareTo(Priority) >= 0)
        {
            IsSet = true;
            Priority = priority;
            Source = source;
        }
    }
}
