using AetherUI.Events;

namespace AetherUI.Runtime;

/// <summary>
/// Base type generated for every .cshtml view. Component state remains alive between renders.
/// </summary>
public abstract class UiComponent : IDisposable
{
    private readonly Dictionary<string, UiEventHandler> _events = new(StringComparer.Ordinal);
    private long _nextEventId;
    private bool _disposed;

    public event EventHandler? Invalidated;

    public string RenderMarkup()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        _events.Clear();
        _nextEventId = 0;
        var writer = new MarkupWriter(this);
        BuildRenderTree(writer);
        return writer.ToString();
    }

    protected abstract void BuildRenderTree(MarkupWriter writer);

    protected void StateHasChanged()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        Invalidated?.Invoke(this, EventArgs.Empty);
    }

    internal string RegisterEvent(UiEventHandler handler)
    {
        var token = $"aether:{_nextEventId++:x}";
        _events.Add(token, handler);
        return token;
    }

    internal bool TryGetEventHandler(string token, out UiEventHandler handler) =>
        _events.TryGetValue(token, out handler!);

    protected virtual void Dispose(bool disposing)
    {
        if (!disposing || _disposed)
        {
            return;
        }

        _events.Clear();
        _disposed = true;
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }
}
