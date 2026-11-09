using AetherUI.Rendering;
using AetherUI.Linux.Graphics;
namespace AetherUI.Linux;

/// <summary>Synchronous borrowed BGRA8 premultiplied, top-down frame. Copy pixels before returning.</summary>
public interface ILinuxFramePresenter
{
    void Present(int pixelWidth, int pixelHeight, int stride, ReadOnlySpan<byte> bgraPixels);
}
public sealed record LinuxSoftwareRenderTarget(ILinuxFramePresenter Presenter);
/// <summary>Retains an atomic owned copy. The renderer never owns or disposes this presenter.</summary>
public sealed class MemoryFramePresenter : ILinuxFramePresenter
{
    public sealed record Frame(int PixelWidth, int PixelHeight, int Stride, ReadOnlyMemory<byte> Pixels);
    private Frame? _frame;
    public Frame? CurrentFrame => Volatile.Read(ref _frame);
    public void Present(int pixelWidth,int pixelHeight,int stride,ReadOnlySpan<byte> bgraPixels)
    {
        if(pixelWidth<=0 || pixelHeight<=0 || stride!=checked(pixelWidth*4) || bgraPixels.Length!=checked(stride*pixelHeight)) throw new ArgumentException("Invalid BGRA8 frame.");
        var frame=new Frame(pixelWidth,pixelHeight,stride,bgraPixels.ToArray());
        Volatile.Write(ref _frame,frame);
    }
}
public sealed class LinuxCairoRenderModule : IRenderModule<LinuxSoftwareRenderTarget>
{
    public string Id => "linux.cairo.software";
    public IRenderBackendWithText Create(LinuxSoftwareRenderTarget target,RenderViewport viewport) => new CairoRenderBackend(target,viewport);
}
/// <summary>The presenter threw; it may already have copied or published pixels. No automatic retry occurred.</summary>
public sealed class FramePresentationException : Exception
{
    public FramePresentationException(Exception innerException) : base("Linux frame presentation failed; external presentation outcome is uncertain.",innerException) { }
}
