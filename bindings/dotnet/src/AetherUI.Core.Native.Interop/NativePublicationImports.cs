using System.Diagnostics.CodeAnalysis;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AetherUI.Core.Native;

// Linking can allocate or invoke an application DllImportResolver. It is real,
// fallible preparation, never a dummy call or a successful-commit exemption.
internal static class NativePublicationImports
{
    // No allocating/fallible type initializer. Only the CAS winner owns reset.
    // Never hold a lock or wait while a resolver can reenter or use another thread.
    internal static void EnsurePrepared(ref int state,
        [DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicMethods | DynamicallyAccessedMemberTypes.NonPublicMethods)] Type group,
        int expectedImports)
    {
        if (Volatile.Read(ref state) == 2) return;
        var observed = Interlocked.CompareExchange(ref state, 1, 0);
        if (observed == 2) return;
        if (observed != 0)
            throw new InvalidOperationException("Publication imports are being prepared. Retry after that preparation finishes.");
        var complete = false;
        try
        {
            var methods = group.GetMethods(BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
            var count = 0;
            foreach (var method in methods)
                if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0) ++count;
            if (expectedImports <= 0 || count != expectedImports)
                throw new InvalidOperationException("Publication import metadata is unavailable or does not match its declared group.");
            foreach (var method in methods)
                if ((method.Attributes & MethodAttributes.PinvokeImpl) != 0) Marshal.Prelink(method);
            Volatile.Write(ref state, 2);
            complete = true;
        }
        finally
        {
            // Unsupported runtimes, missing metadata/exports and OOM all fail
            // preparation. There is no unsupported-as-zero or partial-ready path.
            if (!complete) Volatile.Write(ref state, 0);
        }
    }
}
