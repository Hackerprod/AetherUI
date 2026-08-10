using System.Drawing;
using System.Numerics;
using System.Runtime.InteropServices;
using AetherUI.Css;
using AetherUI.Layout;
using AetherUI.Primitives;
using AetherUI.Rendering;
using SharpGen.Runtime;
using Vortice;
using Vortice.Direct2D1;
using Vortice.Direct3D;
using Vortice.Direct3D11;
using Vortice.DirectWrite;
using Vortice.DXGI;
using Vortice.Mathematics;
using DWriteFontWeight = Vortice.DirectWrite.FontWeight;
using DWriteTextAlignment = Vortice.DirectWrite.TextAlignment;
using UiFontWeight = AetherUI.Css.FontWeight;
using UiTextAlign = AetherUI.Css.TextAlign;
using UiTextMetrics = AetherUI.Layout.TextMetrics;

namespace AetherUI.Windows.Graphics;

/// <summary>
/// Direct2D renderer region: Scroll offset composition, scroll-tile rasterization and scrollbar chrome.
/// </summary>
public sealed partial class Direct2DRenderBackend
{
    private readonly ScrollTileCache _scrollTileCache = new();
    private readonly ScrollTileRenderMetrics _scrollTileMetrics = new();

    private Func<long, ScrollVisualState>? _scrollStateProvider;
    private bool _scrollTileCompositionEnabled;

    public void SetScrollOffsetProvider(Func<long, ScrollVisualOffset>? provider) =>
        _scrollStateProvider = provider is null ? null : id => new ScrollVisualState(provider(id), default);

    public void SetScrollVisualStateProvider(Func<long, ScrollVisualState>? provider) =>
        _scrollStateProvider = provider;

    public void SetScrollCompositionMode(bool enabled) =>
        _scrollTileCompositionEnabled = enabled;

    private long _scrollTileDisplayListGeneration;

    internal int LastScrollTileCacheHits => _scrollTileMetrics.CacheHits;
    internal int LastScrollTileCacheMisses => _scrollTileMetrics.CacheMisses;
    internal int LastScrollTileRasterizedCommands => _scrollTileMetrics.RasterizedCommands;
    internal int LastScrollTileEvictions => _scrollTileMetrics.Evictions;
    internal int LastScrollTileCacheBypasses => _scrollTileMetrics.Bypasses;
    internal long LastScrollTileBytes => _scrollTileCache.Bytes;

    private static int FindMatchingScrollPop(IReadOnlyList<PaintCommand> commands, int start)
    {
        var depth = 1;
        for (var i = start; i < commands.Count; i++)
        {
            switch (commands[i])
            {
                case PushScrollOffsetCommand:
                    depth++;
                    break;
                case PopScrollOffsetCommand:
                    depth--;
                    if (depth == 0)
                    {
                        return i;
                    }
                    break;
            }
        }

        throw new InvalidOperationException("The display list pushed a scroll offset without a matching pop.");
    }

    private bool TryDrawCachedScrollSegment(
        ID2D1DeviceContext target,
        DisplayList displayList,
        int segmentStart,
        int segmentEnd,
        long scrollContainerId,
        Stack<ClipScope> clipStack,
        Stack<Matrix3x2> transformStack,
        ColorRgba clearColor,
        bool allowDeviceRetry,
        out bool restarted)
    {
        restarted = false;
        if (!IsScrollSegmentCacheable(target, displayList.Commands, segmentStart, segmentEnd, clipStack, transformStack))
        {
            _scrollTileMetrics.RecordBypass();
            return false;
        }

        var viewport = clipStack.Peek().Command.Rectangle;
        if (viewport.IsEmpty)
        {
            return true;
        }

        var state = _scrollStateProvider?.Invoke(scrollContainerId) ?? default;
        var offset = state.Offset;
        var tilePixels = ScrollTileCache.TilePixelSize;
        var tileSize = tilePixels / MathF.Max(.0001f, _dpiScale);
        var visibleContent = new RectF(viewport.X + offset.X, viewport.Y + offset.Y, viewport.Width, viewport.Height);
        var firstTileX = (int)MathF.Floor(visibleContent.Left / tileSize);
        var lastTileX = (int)MathF.Floor(MathF.Max(visibleContent.Left, visibleContent.Right - .001f) / tileSize);
        var firstTileY = (int)MathF.Floor(visibleContent.Top / tileSize);
        var lastTileY = (int)MathF.Floor(MathF.Max(visibleContent.Top, visibleContent.Bottom - .001f) / tileSize);

        try
        {
            for (var tileY = firstTileY; tileY <= lastTileY; tileY++)
            {
                for (var tileX = firstTileX; tileX <= lastTileX; tileX++)
                {
                    var tileRect = new RectF(tileX * tileSize, tileY * tileSize, tileSize, tileSize);
                    var visibleInTile = Intersect(visibleContent, tileRect);
                    if (visibleInTile.IsEmpty)
                    {
                        continue;
                    }

                    var key = new ScrollTileKey(displayList.Generation, scrollContainerId,
                        FloatBits(_dpiScale), tilePixels, tileX, tileY);
                    if (_scrollTileCache.TryGet(key, out var tile))
                    {
                        _scrollTileMetrics.RecordCacheHit();
                    }
                    else
                    {
                        _scrollTileMetrics.RecordCacheMiss();
                        tile = RasterizeScrollTile(target, displayList, segmentStart, segmentEnd, tileRect,
                            clipStack, clearColor);
                        var evictions = _scrollTileCache.TryAdd(key, tile);
                        _scrollTileMetrics.RecordEvictions(evictions);
                    }

                    DrawScrollTile(target, tile.Bitmap, tileRect, visibleInTile, offset);
                }
            }

            return true;
        }
        catch (SharpGenException exception) when (IsDeviceLoss(exception.ResultCode) && allowDeviceRetry)
        {
            ReleaseDeviceResources();
            CreateDeviceResources();
            RenderCore(displayList, clearColor, allowDeviceRetry: false);
            restarted = true;
            return true;
        }
    }

    private static bool IsScrollSegmentCacheable(
        ID2D1DeviceContext target,
        IReadOnlyList<PaintCommand> commands,
        int segmentStart,
        int segmentEnd,
        Stack<ClipScope> clipStack,
        Stack<Matrix3x2> transformStack)
    {
        if (clipStack.Count == 0 || transformStack.Count != 0 || target.Transform != Matrix3x2.Identity)
        {
            return false;
        }

        for (var i = segmentStart; i < segmentEnd; i++)
        {
            switch (commands[i])
            {
                case ApplyBackdropFilterCommand:
                case DrawTextShadowCommand:
                case PushTransformCommand:
                case PopTransformCommand:
                case PushScrollOffsetCommand:
                case PopScrollOffsetCommand:
                case DrawScrollbarChromeCommand:
                    return false;
            }
        }

        return true;
    }

    private ScrollTile RasterizeScrollTile(
        ID2D1DeviceContext target,
        DisplayList displayList,
        int segmentStart,
        int segmentEnd,
        RectF tileRect,
        Stack<ClipScope> sceneClipStack,
        ColorRgba clearColor)
    {
        using var sceneTarget = target.Target
            ?? throw new InvalidOperationException("The Direct2D scene target is not initialized.");
        var sceneTransform = target.Transform;
        ID2D1Bitmap1? bitmap = null;
        var success = false;
        var restoreScene = false;

        try
        {
            SuspendClips(target, sceneClipStack);
            var boundary = EndDrawSegment(target);
            boundary.CheckError();
            restoreScene = true;

            bitmap = CreateScrollTileBitmap(target);
            target.Target = bitmap;
            BeginDrawSegment(target);
            target.Transform = Matrix3x2.CreateTranslation(-tileRect.X, -tileRect.Y);
            target.Clear(new Color4(0, 0, 0, 0));
            var rasterizedCommands = RenderScrollTileCommandRange(target, displayList.Commands, segmentStart, segmentEnd);
            var tileResult = EndDrawSegment(target);
            tileResult.CheckError();
            _scrollTileMetrics.RecordRasterizedCommands(rasterizedCommands);
            success = true;
            return new ScrollTile(bitmap);
        }
        finally
        {
            if (_isDrawSegmentActive)
            {
                try
                {
                    _ = EndDrawSegment(target);
                }
                catch
                {
                    // The original exception remains the actionable failure. Device recovery
                    // happens at the outer render boundary.
                }
            }

            target.Target = sceneTarget;
            target.Transform = sceneTransform;
            if (restoreScene)
            {
                BeginDrawSegment(target);
                RestoreClips(target, sceneClipStack);
            }
            if (!success)
            {
                bitmap?.Dispose();
            }
        }
    }

    private ID2D1Bitmap1 CreateScrollTileBitmap(ID2D1DeviceContext target)
    {
        var dpi = 96f * _dpiScale;
        return target.CreateBitmap(
            new SizeI(ScrollTileCache.TilePixelSize, ScrollTileCache.TilePixelSize),
            IntPtr.Zero,
            0,
            new BitmapProperties1(
                new Vortice.DCommon.PixelFormat(Format.B8G8R8A8_UNorm, Vortice.DCommon.AlphaMode.Premultiplied),
                dpi,
                dpi,
                BitmapOptions.Target));
    }

    private int RenderScrollTileCommandRange(
        ID2D1DeviceContext target,
        IReadOnlyList<PaintCommand> commands,
        int start,
        int end)
    {
        var rasterizedCommands = 0;
        var clipStack = new Stack<ClipScope>();
        for (var i = start; i < end; i++)
        {
            switch (commands[i])
            {
                case FillRoundedRectangleCommand fill:
                    FillRoundedGeometry(target, fill.Rectangle, fill.Radii, GetBrush(fill.Color));
                    rasterizedCommands++;
                    break;
                case FillRoundedRectangleGradientCommand gradientFill:
                    DrawGradientFill(target, gradientFill);
                    rasterizedCommands++;
                    break;
                case DrawBoxShadowCommand shadow:
                    DrawBoxShadow(target, shadow);
                    rasterizedCommands++;
                    break;
                case StrokeRoundedRectangleCommand stroke:
                    StrokeRoundedGeometry(target, stroke.Rectangle, stroke.Radii, GetBrush(stroke.Color), MathF.Max(.25f, stroke.StrokeWidth));
                    rasterizedCommands++;
                    break;
                case FillBorderSideCommand borderSide:
                    Direct2DBorderSidePainter.Draw(_d2dFactory, target, borderSide, GetBrush);
                    rasterizedCommands++;
                    break;
                case DrawTextCommand text:
                    DrawText(target, text);
                    rasterizedCommands++;
                    break;
                case DrawTextGradientCommand textGradient:
                    DrawTextGradient(target, textGradient);
                    rasterizedCommands++;
                    break;
                case DrawBackgroundImageCommand image:
                    DrawBackgroundImage(target, image);
                    rasterizedCommands++;
                    break;
                case PushClipCommand push:
                    PushClip(target, push, clipStack, transformDepth: 0);
                    break;
                case PopClipCommand:
                    if (clipStack.Count == 0)
                    {
                        throw new InvalidOperationException("A cached scroll tile popped a clip that was not active.");
                    }

                    PopClip(target, clipStack.Pop());
                    break;
            }
        }

        if (clipStack.Count != 0)
        {
            throw new InvalidOperationException("A cached scroll tile ended with an active clip.");
        }

        return rasterizedCommands;
    }

    private static void DrawScrollTile(
        ID2D1DeviceContext target,
        ID2D1Bitmap1 bitmap,
        RectF tileRect,
        RectF visibleInTile,
        ScrollVisualOffset offset)
    {
        var destination = new RectF(
            visibleInTile.X - offset.X,
            visibleInTile.Y - offset.Y,
            visibleInTile.Width,
            visibleInTile.Height);
        var source = new RectF(
            visibleInTile.X - tileRect.X,
            visibleInTile.Y - tileRect.Y,
            visibleInTile.Width,
            visibleInTile.Height);
        target.DrawBitmap(bitmap, ToRawRect(destination), 1f,
            Vortice.Direct2D1.InterpolationMode.Linear, ToRawRect(source), Matrix4x4.Identity);
    }

    private void DrawScrollbarChrome(ID2D1DeviceContext target, ScrollbarChrome chrome)
    {
        var thumbBrush = GetBrush(new ColorRgba(.5f, .5f, .5f, .55f));
        var cornerBrush = GetBrush(new ColorRgba(.5f, .5f, .5f, .28f));
        if (chrome.VerticalVisible) DrawScrollbarThumb(target, chrome.VerticalThumb, thumbBrush);
        if (chrome.HorizontalVisible) DrawScrollbarThumb(target, chrome.HorizontalThumb, thumbBrush);
        if (chrome.VerticalVisible && chrome.HorizontalVisible && !chrome.Corner.IsEmpty)
        {
            target.FillRectangle(ToRawRect(chrome.Corner), cornerBrush);
        }
    }

    private static void DrawScrollbarThumb(ID2D1DeviceContext target, RectF thumb, ID2D1Brush brush)
    {
        if (thumb.IsEmpty) return;
        var radius = MathF.Min(thumb.Width, thumb.Height) / 2f;
        target.FillRoundedRectangle(ToRoundedRectangle(thumb, radius), brush);
    }

    private static int FloatBits(float value) => BitConverter.SingleToInt32Bits(value);

    private readonly record struct ScrollTileKey(
        long DisplayListGeneration,
        long ScrollContainerId,
        int DpiBits,
        int TilePixels,
        int TileX,
        int TileY);

    private sealed class ScrollTile : IDisposable
    {
        internal ScrollTile(ID2D1Bitmap1 bitmap)
        {
            Bitmap = bitmap;
            Bytes = checked(bitmap.PixelSize.Width * bitmap.PixelSize.Height * 4L);
        }

        internal ID2D1Bitmap1 Bitmap { get; }
        internal long Bytes { get; }

        public void Dispose() => Bitmap.Dispose();
    }

    private sealed class ScrollTileCache : IDisposable
    {
        internal const int TilePixelSize = 512;
        private const long BudgetBytes = 64L * 1024L * 1024L;
        private readonly Dictionary<ScrollTileKey, LinkedListNode<Entry>> _entries = new();
        private readonly LinkedList<Entry> _lru = new();

        internal long Bytes { get; private set; }

        internal bool TryGet(ScrollTileKey key, out ScrollTile tile)
        {
            if (_entries.TryGetValue(key, out var node))
            {
                _lru.Remove(node);
                _lru.AddFirst(node);
                tile = node.Value.Tile;
                return true;
            }

            tile = null!;
            return false;
        }

        internal int TryAdd(ScrollTileKey key, ScrollTile tile)
        {
            if (tile.Bytes > BudgetBytes)
            {
                tile.Dispose();
                return 0;
            }

            if (_entries.TryGetValue(key, out var existing))
            {
                Bytes -= existing.Value.Tile.Bytes;
                existing.Value.Tile.Dispose();
                _lru.Remove(existing);
                _entries.Remove(key);
            }

            var node = new LinkedListNode<Entry>(new Entry(key, tile));
            _lru.AddFirst(node);
            _entries.Add(key, node);
            Bytes += tile.Bytes;

            var evictions = 0;
            while (Bytes > BudgetBytes && _lru.Last is { } last)
            {
                _lru.RemoveLast();
                _entries.Remove(last.Value.Key);
                Bytes -= last.Value.Tile.Bytes;
                last.Value.Tile.Dispose();
                evictions++;
            }

            return evictions;
        }

        internal void Clear()
        {
            foreach (var entry in _lru)
            {
                entry.Tile.Dispose();
            }

            _entries.Clear();
            _lru.Clear();
            Bytes = 0;
        }

        public void Dispose() => Clear();

        private readonly record struct Entry(ScrollTileKey Key, ScrollTile Tile);
    }

    private sealed class ScrollTileRenderMetrics
    {
        internal int CacheHits { get; private set; }
        internal int CacheMisses { get; private set; }
        internal int RasterizedCommands { get; private set; }
        internal int Evictions { get; private set; }
        internal int Bypasses { get; private set; }

        internal void Reset()
        {
            CacheHits = 0;
            CacheMisses = 0;
            RasterizedCommands = 0;
            Evictions = 0;
            Bypasses = 0;
        }

        internal void RecordCacheHit() => CacheHits++;
        internal void RecordCacheMiss() => CacheMisses++;
        internal void RecordRasterizedCommands(int count) => RasterizedCommands += Math.Max(0, count);
        internal void RecordEvictions(int count) => Evictions += Math.Max(0, count);
        internal void RecordBypass() => Bypasses++;
    }

}
