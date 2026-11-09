using AetherUI.Layout;

namespace AetherUI.Rendering;

/// <summary>
/// Physical surface dimensions in pixels and the scale from device-independent pixels (DIPs)
/// to physical pixels. Commands and text metrics remain in DIPs. Validation and zero-size
/// lifecycle are module-specific: this value does not impose a common suspension policy.
/// </summary>
public readonly record struct RenderViewport(int PixelWidth, int PixelHeight, float DpiScale);

/// <summary>
/// A single owned renderer and text pipeline. Dispose the instance once, not its interfaces
/// separately. Optional interfaces such as <see cref="IIncrementalRenderBackend"/> remain
/// available on the same concrete instance.
/// </summary>
public interface IRenderBackendWithText : IRenderBackend, ITextMeasurer;

/// <summary>
/// Explicitly selected platform drawing factory. A target is borrowed and must outlive the
/// returned renderer. The caller owns the renderer until successfully transferring it to a
/// host. Create, resize, measure, render and dispose belong to the same owner thread; a module
/// documents whether it actively rejects violations. A failed creation releases partial resources.
/// </summary>
public interface IRenderModule<in TTarget>
{
    string Id { get; }
    IRenderBackendWithText Create(TTarget target, RenderViewport viewport);
}
