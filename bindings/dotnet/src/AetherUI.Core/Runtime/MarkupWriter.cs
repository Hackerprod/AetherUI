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
    private readonly Dictionary<string, UiEventHandler> _events;
    private long _nextEventId;
    private bool _sealed;

    internal MarkupWriter(int capacity = 2048)
    {
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.event-map");
#endif
        _events = new(StringComparer.Ordinal);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.builder");
#endif
        _builder = new StringBuilder(capacity);
    }

    public void WriteLiteral(string? value)
    {
        VerifyWritable();
        if (!string.IsNullOrEmpty(value))
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("markup.literal-append-aggregate");
#endif
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

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.format-call-aggregate");
#endif
        var text = value switch
        {
            IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
            _ => value.ToString(),
        };

        if (!string.IsNullOrEmpty(text))
        {
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("markup.text-encode-aggregate");
#endif
            var encoded = WebUtility.HtmlEncode(text);
#if AETHER_DOM_FAULT_TESTING
            AetherUI.Core.Native.ManagedDomFault.Before("markup.text-append-aggregate");
#endif
            _builder.Append(encoded);
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

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.attribute-prefix-append-aggregate");
#endif
        _builder.Append(' ').Append(name);
        if (value is true)
        {
            return;
        }

#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.format-call-aggregate");
#endif
        var text = value switch
        {
            IFormattable formattable => formattable.ToString(null, CultureInfo.InvariantCulture),
            _ => value.ToString(),
        };
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.attribute-encode-aggregate");
#endif
        var encoded = WebUtility.HtmlEncode(text ?? string.Empty);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.attribute-value-append-aggregate");
#endif
        _builder.Append("=\"").Append(encoded).Append('"');
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
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.event-token-format-aggregate");
#endif
        var token = $"aether:{_nextEventId:x}";
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.BeforeDictionaryInsert(_events, token, "markup.event-map-growth");
#endif
        _events.Add(token, handler);
        _nextEventId = next;
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.event-append-aggregate");
#endif
        _builder.Append(' ').Append(name).Append("=\"").Append(token).Append('"');
    }

    public void WriteEventAttribute(string name, Action handler)
    {
        VerifyWritable();
        ArgumentNullException.ThrowIfNull(handler);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.action-adapter-aggregate");
#endif
        WriteEventAttribute(name, AdaptAction(handler));
    }

    // Keep allocation of the Action closure/delegate inside this aggregate operation so
    // its test-only fault point precedes it, rather than a compiler-generated entry local.
    private static UiEventHandler AdaptAction(Action handler) => _ => handler();

    private static void ValidateAttributeName(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.attribute-validation-aggregate");
#endif
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
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.registry");
#endif
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
#if AETHER_DOM_FAULT_TESTING
        AetherUI.Core.Native.ManagedDomFault.Before("markup.result-string");
#endif
        return _builder.ToString();
    }
}
