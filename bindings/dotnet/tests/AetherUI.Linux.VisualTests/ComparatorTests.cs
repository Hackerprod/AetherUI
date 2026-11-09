using System;
using System.Buffers.Binary;
using System.IO;
using System.IO.Compression;
using System.Text;

namespace VisualValidation;

public static class ComparatorTests
{
    public static void Run()
    {
        var baseline = new Raster(2, 2, new byte[] {
            12, 34, 56, 255, 20, 40, 60, 128,
            0, 0, 0, 0, 90, 100, 110, 255 });
        Raster Clone() => new(baseline.Width, baseline.Height, (byte[])baseline.Pixels.Clone());
        Comparison Compare(Raster? actual) => RasterTools.Compare(baseline, actual, "reference-1", "candidate-1");
        void Changed(string name, Raster actual, int pixels, int error)
        {
            Comparison result = Compare(actual);
            Check(result.Status == Verdict.FAIL && result.ChangedPixels == pixels && result.MaxError == error, name);
        }

        Comparison identical = Compare(Clone());
        Check(identical.Status == Verdict.PASS && identical.ChangedPixels == 0 && identical.MaxError == 0, "identical, distinct identities");
        Raster color = Clone(); color.Pixels[0]++;
        Changed("single channel change", color, 1, 1);
        Raster displaced = Clone();
        Array.Copy(baseline.Pixels, 4, displaced.Pixels, 0, 4);
        Array.Copy(baseline.Pixels, 0, displaced.Pixels, 4, 4);
        Changed("pixel displacement", displaced, 2, 127);
        Raster swapped = Clone();
        (swapped.Pixels[0], swapped.Pixels[2]) = (swapped.Pixels[2], swapped.Pixels[0]);
        Changed("red blue channel swap", swapped, 1, 44);
        Raster alpha = Clone(); alpha.Pixels[7]--;
        Changed("wrong alpha", alpha, 1, 1);
        byte[] whitePixels = new byte[baseline.Pixels.Length]; Array.Fill(whitePixels, (byte)255);
        Changed("white blank capture", new Raster(2, 2, whitePixels), 4, 255);
        Check(Compare(baseline).Status == Verdict.BLOCKED, "same raster object with distinct identities");
        Check(Compare(new Raster(2, 2, baseline.Pixels)).Status == Verdict.BLOCKED, "aliased pixel buffers");
        Check(Compare(null).Status == Verdict.BLOCKED, "missing actual");
        Check(RasterTools.Compare(null, baseline, "a", "b").Status == Verdict.BLOCKED, "missing expected");
        Check(Compare(new Raster(1, 4, (byte[])baseline.Pixels.Clone())).Status == Verdict.FAIL, "dimension mismatch");
        Check(RasterTools.Compare(baseline, Clone(), "same", "same").Status == Verdict.BLOCKED, "same capture identity");
        Check(RasterTools.Compare(baseline, Clone(), "", "candidate").Status == Verdict.BLOCKED, "missing identity");
        Check(Compare(new Raster(2, 2, new byte[4])).Status == Verdict.BLOCKED, "malformed capture");
        Check(RasterTools.Aggregate() == Verdict.NOT_RUN, "empty mandatory suite");
        Check(RasterTools.Aggregate(Verdict.PASS, Verdict.PASS) == Verdict.PASS, "all pass");
        Check(RasterTools.Aggregate(Verdict.PASS, Verdict.NOT_RUN) == Verdict.NOT_RUN, "unrun mandatory test");
        Check(RasterTools.Aggregate(Verdict.PASS, Verdict.NOT_RUN, Verdict.BLOCKED) == Verdict.BLOCKED, "blocked priority");
        Check(RasterTools.Aggregate(Verdict.BLOCKED, Verdict.NOT_RUN, Verdict.FAIL) == Verdict.FAIL, "fail priority");
        Check(RasterTools.Aggregate(Verdict.FAIL, Verdict.BLOCKED) == Verdict.FAIL, "fail priority independent of order");
        Check(RasterTools.Unpremultiply(64, 128) == 128, "rounded unpremultiply");
        Check(RasterTools.Unpremultiply(1, 2) == 128, "half rounds up");
        Check(RasterTools.Unpremultiply(200, 0) == 0, "transparent RGB cleared");
        Check(RasterTools.Unpremultiply(255, 255) == 255, "opaque channel unchanged");
        Raster diff = RasterTools.Diff(baseline, alpha);
        Check(diff.Pixels[4] == 1 && diff.Pixels[5] == 1 && diff.Pixels[6] == 1 && diff.Pixels[7] == 255, "alpha difference visible");
        Raster overlay = RasterTools.Overlay(baseline, color);
        Check(overlay.Pixels[0] == 13 && overlay.Pixels[3] == 255, "rounded premultiplied overlay");
        VerifyPng();
    }

    private static void VerifyPng()
    {
        string path = Path.Combine(Path.GetTempPath(), $"visual-comparator-{Guid.NewGuid():N}.png");
        try
        {
            RasterTools.WritePng(path, new Raster(2, 1, new byte[] { 32, 64, 96, 128, 99, 88, 77, 0 }));
            byte[] png = File.ReadAllBytes(path);
            Check(png.AsSpan(0, 8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }), "PNG signature");
            using var idat = new MemoryStream();
            bool header = false, end = false;
            for (int offset = 8; offset < png.Length;)
            {
                int count = BinaryPrimitives.ReadInt32BigEndian(png.AsSpan(offset, 4));
                string type = Encoding.ASCII.GetString(png, offset + 4, 4);
                ReadOnlySpan<byte> data = png.AsSpan(offset + 8, count);
                // Independently validate the chunk CRC against the standard polynomial.
                uint crc = uint.MaxValue;
                foreach (byte value in png.AsSpan(offset + 4, count + 4))
                {
                    crc ^= value;
                    for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ ((crc & 1) * 0xedb88320u);
                }
                Check((crc ^ uint.MaxValue) == BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(offset + 8 + count, 4)), $"PNG {type} CRC");
                if (type == "IHDR")
                {
                    header = true;
                    Check(count == 13 && BinaryPrimitives.ReadInt32BigEndian(data[..4]) == 2 &&
                        BinaryPrimitives.ReadInt32BigEndian(data.Slice(4, 4)) == 1 && data[8] == 8 && data[9] == 6, "PNG RGBA header");
                }
                if (type == "IDAT") idat.Write(data);
                if (type == "IEND") { end = true; Check(count == 0, "PNG empty end"); }
                offset += count + 12;
            }
            Check(header && end, "PNG structural chunks");
            idat.Position = 0;
            using var zlib = new ZLibStream(idat, CompressionMode.Decompress);
            using var raw = new MemoryStream();
            zlib.CopyTo(raw);
            Check(raw.ToArray().AsSpan().SequenceEqual(new byte[] { 0, 191, 128, 64, 128, 0, 0, 0, 0 }), "PNG filter, BGRA conversion, rounded unpremultiply and zero-alpha RGB");
        }
        finally { if (File.Exists(path)) File.Delete(path); }
    }

    private static void Check(bool success, string name)
    {
        if (!success) throw new InvalidOperationException($"Comparator self-test failed: {name}");
    }
}
