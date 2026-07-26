namespace AetherUI.Diagnostics;

public class AetherException : Exception
{
    public AetherException(string message) : base(message)
    {
    }

    public AetherException(string message, Exception innerException) : base(message, innerException)
    {
    }
}

public sealed class MarkupParseException : AetherException
{
    public MarkupParseException(string message, int position)
        : base($"{message} (posición {position}).")
    {
        Position = position;
    }

    public int Position { get; }
}
