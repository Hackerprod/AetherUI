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
    private readonly StringBuilder _builder;
    private readonly UiComponent _owner;

    internal MarkupWriter(UiComponent owner, int capacity = 2048)
    {
        _owner = owner;
        _builder = new StringBuilder(capacity);
    }

    public void WriteLiteral(string? value)
    {
        if (!string.IsNullOrEmpty(value))
        {
            _builder.Append(value);
        }
    }

    public void Write(object? value)
    {
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
        ValidateAttributeName(name);
        if (!name.StartsWith("on", StringComparison.OrdinalIgnoreCase) || name.Length <= 2)
        {
            throw new ArgumentException("Los eventos deben escribirse como atributos on*, por ejemplo onclick.", nameof(name));
        }

        ArgumentNullException.ThrowIfNull(handler);
        var token = _owner.RegisterEvent(handler);
        _builder.Append(' ').Append(name).Append("=\"").Append(token).Append('"');
    }

    public void WriteEventAttribute(string name, Action handler)
    {
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

    public override string ToString() => _builder.ToString();
}
