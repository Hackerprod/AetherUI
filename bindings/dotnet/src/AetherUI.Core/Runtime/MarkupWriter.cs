using System.Globalization;
using System.Net;
using System.Text;
using AetherUI.Events;

namespace AetherUI.Runtime;

/// <summary>
/// Receives generated render instructions and produces safe HTML markup.
/// Dynamic values are HTML encoded; only generated literal fragments are emitted verbatim.
/// </summary>
public sealed class MarkupWriter
{
    // The writer's mutable storage is adopted by an immutable EventRegistry at Seal.
    // All writes and sealing share this thread, even for an unbound component. A retained
    // writer must not begin a cross-thread write that could complete after publication.
    private readonly int _ownerThreadId = Environment.CurrentManagedThreadId;
    private readonly StringBuilder _builder;
    private readonly Dictionary<string, UiEventHandler> _events = new(StringComparer.Ordinal);
    private long _nextEventId;
    private bool _sealed;

    internal MarkupWriter(int capacity = 2048)
    {
        _builder = new StringBuilder(capacity);
    }

    public void WriteLiteral(string? value)
    {
        VerifyWritable();
        if (!string.IsNullOrEmpty(value))
        {
            _builder.Append(value);
        }
    }

    public void Write(object? value)
    {
        VerifyWritable();
        if (value is null)
        {
            return;
        }

        var text = value switch
        {
            IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
            _ => value.ToString(),
        };

        if (!string.IsNullOrEmpty(text))
        {
            _builder.Append(WebUtility.HtmlEncode(text));
        }
    }

    public void WriteAttribute(string name, object? value)
    {
        VerifyWritable();
        ValidateAttributeName(name);
        if (value is null || value is false)
        {
            return;
        }

        _builder.Append(' ').Append(name);
        if (value is true)
        {
            return;
        }

        var text = value switch
        {
            IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
            _ => value.ToString(),
        };
        _builder.Append("=\"").Append(WebUtility.HtmlEncode(text ?? string.Empty)).Append('"');
    }

    public void WriteEventAttribute(string name, UiEventHandler handler)
    {
        VerifyWritable();
        ValidateAttributeName(name);
        if (!name.StartsWith("on", StringComparison.OrdinalIgnoreCase) || name.Length <= 2)
        {
            throw new ArgumentException("Los eventos deben escribirse como atributos on*, por ejemplo onclick.", nameof(name));
        }

        ArgumentNullException.ThrowIfNull(handler);
        var next = checked(_nextEventId + 1);
        var token = $"aether:{_nextEventId:x}";
        _events.Add(token, handler);
        _nextEventId = next;
        _builder.Append(' ').Append(name).Append("=\"").Append(token).Append('"');
    }

    public void WriteEventAttribute(string name, Action handler)
    {
        VerifyWritable();
        ArgumentNullException.ThrowIfNull(handler);
        WriteEventAttribute(name, _ => handler());
    }

    private static void ValidateAttributeName(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (name.Any(static character =>
                !char.IsLetterOrDigit(character) && character is not '-' and not '_' and not ':'))
        {
            throw new ArgumentException("El nombre del atributo contiene caracteres no permitidos.", nameof(name));
        }
    }

    internal void Seal()
    {
        VerifyOwnerThread();
        _sealed = true;
    }

    internal UiComponent.EventRegistry CreateEventRegistry(long generation)
    {
        VerifyOwnerThread();
        if (!_sealed) throw new InvalidOperationException("The render writer is not sealed.");
        return new UiComponent.EventRegistry(_events, generation);
    }

    private void VerifyWritable()
    {
        VerifyOwnerThread();
        if (_sealed) throw new InvalidOperationException("The render writer is sealed.");
    }

    private void VerifyOwnerThread()
    {
        if (_ownerThreadId != Environment.CurrentManagedThreadId)
            throw new InvalidOperationException("The render writer belongs to another thread.");
    }

    public override string ToString()
    {
        VerifyOwnerThread();
        return _builder.ToString();
    }
}
