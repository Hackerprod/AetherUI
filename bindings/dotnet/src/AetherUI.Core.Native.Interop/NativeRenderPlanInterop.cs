namespace AetherUI.Core.Native;

public static class NativeRenderPlanInterop
{
    public static uint VisualInvalidationFromStyleImpact(uint impact) =>
        NativeMethods.VisualInvalidationFromStyleImpact(impact);

    public static uint VisualInvalidationForScroll(bool displayListContainsPatchedChunks) =>
        NativeMethods.VisualInvalidationForScroll(displayListContainsPatchedChunks ? 1 : 0);

    public static NativeRenderPlan Evaluate(NativeRenderPlanInput input)
    {
        var status = NativeMethods.RenderPlanEvaluate(input, out var plan);
        if (status != 0)
            throw new InvalidOperationException($"AetherUI native render-plan evaluation failed with status {status}.");
        return plan;
    }
}
