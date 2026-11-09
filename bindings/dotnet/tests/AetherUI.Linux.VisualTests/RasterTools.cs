using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace VisualValidation;

/// <summary>Tightly packed, row-major BGRA8 premultiplied pixels.</summary>
public sealed record Raster(int Width, int Height, byte[] Pixels);
public enum Verdict { PASS, FAIL, BLOCKED, NOT_RUN }
public sealed record Comparison(Verdict Status, string Reason, int ChangedPixels, int MaxError);

public static class RasterTools
{
    public static Comparison Compare(Raster? expected, Raster? actual, string expectedIdentity, string actualIdentity)
    {
        if (expected is null || actual is null)
            return new(Verdict.BLOCKED, "Expected or actual capture is missing.", 0, 0);
        if (ReferenceEquals(expected, actual) || ReferenceEquals(expected.Pixels, actual.Pixels))
            return new(Verdict.BLOCKED, "Self-comparison or aliased pixel buffers are not evidence.", 0, 0);
        if (string.IsNullOrWhiteSpace(expectedIdentity) || string.IsNullOrWhiteSpace(actualIdentity) ||
            string.Equals(expectedIdentity, actualIdentity, StringComparison.Ordinal))
            return new(Verdict.BLOCKED, "Distinct, nonempty capture identities are required.", 0, 0);
        if (!IsValid(expected) || !IsValid(actual))
            return new(Verdict.BLOCKED, "Capture dimensions or pixel buffer are invalid.", 0, 0);
        if (expected.Width != actual.Width || expected.Height != actual.Height)
            return new(Verdict.FAIL, $"Dimensions differ: {expected.Width}x{expected.Height} vs {actual.Width}x{actual.Height}.", 0, 0);
        int changed = 0, maximum = 0;
        for (int i = 0; i < expected.Pixels.Length; i += 4)
        {
            bool different = false;
            for (int c = 0; c < 4; c++)
            {
                int error = Math.Abs(expected.Pixels[i + c] - actual.Pixels[i + c]);
                different |= error != 0;
                maximum = Math.Max(maximum, error);
            }
            if (different) changed++;
        }
        return changed == 0
            ? new(Verdict.PASS, "BGRA8 premultiplied pixels match exactly.", 0, 0)
            : new(Verdict.FAIL, $"{changed} pixels differ; maximum channel error {maximum}.", changed, maximum);
    }

    /// <summary>Every mandatory result must pass. An empty suite is never a pass.</summary>
    public static Verdict Aggregate(params Verdict[] verdicts)
    {
        ArgumentNullException.ThrowIfNull(verdicts);
        if (verdicts.Length == 0) return Verdict.NOT_RUN;
        bool blocked = false, notRun = false;
        foreach (Verdict verdict in verdicts)
        {
            if (verdict == Verdict.FAIL) return Verdict.FAIL;
            blocked |= verdict == Verdict.BLOCKED;
            notRun |= verdict != Verdict.PASS && verdict != Verdict.BLOCKED;
        }
        return blocked ? Verdict.BLOCKED : notRun ? Verdict.NOT_RUN : Verdict.PASS;
    }

    /// <summary>Opaque difference visualization. Alpha differences are also visible in RGB.</summary>
    public static Raster Diff(Raster expected, Raster actual)
    {
        RequirePair(expected, actual);
        byte[] pixels = new byte[expected.Pixels.Length];
        for (int i = 0; i < pixels.Length; i += 4)
        {
            int alphaError = Math.Abs(expected.Pixels[i + 3] - actual.Pixels[i + 3]);
            for (int c = 0; c < 3; c++)
                pixels[i + c] = (byte)Math.Max(alphaError, Math.Abs(expected.Pixels[i + c] - actual.Pixels[i + c]));
            pixels[i + 3] = 255;
        }
        return new(expected.Width, expected.Height, pixels);
    }

    /// <summary>Equal-weight blend in premultiplied space for displacement inspection.</summary>
    public static Raster Overlay(Raster expected, Raster actual)
    {
        RequirePair(expected, actual);
        byte[] pixels = new byte[expected.Pixels.Length];
        for (int i = 0; i < pixels.Length; i++)
            pixels[i] = (byte)((expected.Pixels[i] + actual.Pixels[i] + 1) / 2);
        return new(expected.Width, expected.Height, pixels);
    }

    public static byte Unpremultiply(byte channel, byte alpha) => alpha == 0
        ? (byte)0 : (byte)Math.Min(255, (channel * 255 + alpha / 2) / alpha);

    /// <summary>Write a straight-alpha RGBA PNG, without external image libraries.</summary>
    public static void WritePng(string path, Raster raster)
    {
        RequireValid(raster);
        using var stream = File.Create(path);
        stream.Write(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 });
        byte[] header = new byte[13];
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(0, 4), raster.Width);
        BinaryPrimitives.WriteInt32BigEndian(header.AsSpan(4, 4), raster.Height);
        header[8] = 8;
        header[9] = 6;
        WriteChunk(stream, "IHDR", header);
        using var compressed = new MemoryStream();
        using (var zlib = new ZLibStream(compressed, CompressionLevel.Optimal, leaveOpen: true))
        {
            byte[] row = new byte[checked(raster.Width * 4 + 1)];
            for (int y = 0; y < raster.Height; y++)
            {
                row[0] = 0;
                for (int x = 0; x < raster.Width; x++)
                {
                    int source = (y * raster.Width + x) * 4;
                    int target = 1 + x * 4;
                    byte alpha = raster.Pixels[source + 3];
                    row[target] = Unpremultiply(raster.Pixels[source + 2], alpha);
                    row[target + 1] = Unpremultiply(raster.Pixels[source + 1], alpha);
                    row[target + 2] = Unpremultiply(raster.Pixels[source], alpha);
                    row[target + 3] = alpha;
                }
                zlib.Write(row);
            }
        }
        WriteChunk(stream, "IDAT", compressed.ToArray());
        WriteChunk(stream, "IEND", Array.Empty<byte>());
    }

    private static bool IsValid(Raster raster) => raster.Width > 0 && raster.Height > 0 &&
        raster.Pixels is not null && (long)raster.Width * raster.Height * 4 == raster.Pixels.Length;

    private static void RequireValid(Raster raster)
    {
        ArgumentNullException.ThrowIfNull(raster);
        if (!IsValid(raster)) throw new ArgumentException("Invalid raster dimensions or BGRA8 buffer.", nameof(raster));
    }

    private static void RequirePair(Raster expected, Raster actual)
    {
        RequireValid(expected);
        RequireValid(actual);
        if (expected.Width != actual.Width || expected.Height != actual.Height)
            throw new ArgumentException("Visualization requires matching raster dimensions.");
    }

    private static void WriteChunk(Stream stream, string name, byte[] payload)
    {
        byte[] type = Encoding.ASCII.GetBytes(name);
        Span<byte> number = stackalloc byte[4];
        BinaryPrimitives.WriteInt32BigEndian(number, payload.Length);
        stream.Write(number);
        stream.Write(type);
        stream.Write(payload);
        uint crc = 0xffffffff;
        foreach (byte b in type) crc = UpdateCrc(crc, b);
        foreach (byte b in payload) crc = UpdateCrc(crc, b);
        BinaryPrimitives.WriteUInt32BigEndian(number, crc ^ 0xffffffff);
        stream.Write(number);
    }

    private static uint UpdateCrc(uint crc, byte value)
    {
        crc ^= value;
        for (int i = 0; i < 8; i++) crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xedb88320u : crc >> 1;
        return crc;
    }
}
