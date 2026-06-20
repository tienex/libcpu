using System;
using System.Reflection.Emit;
using System.Runtime.InteropServices;

// One-time infrastructure helper: it does NOT compile source. Each call to Compile
// JITs a raw-CIL method in-process through Reflection.Emit (DynamicMethod), which is
// exactly the "use the framework's JIT, not the compiler" path.
public static class Emitter
{
    [UnmanagedCallersOnly]
    public static IntPtr Compile(IntPtr ilPtr, int ilLen, IntPtr sigPtr, int sigLen, int maxStack)
    {
        byte[] il = new byte[ilLen];
        Marshal.Copy(ilPtr, il, 0, ilLen);
        byte[] sig = new byte[sigLen];
        Marshal.Copy(sigPtr, sig, 0, sigLen);

        // arg0 is guest RAM as a native pointer (IntPtr), accessed in place via ldind/stind so the
        // (up to 640 KiB) address space is never copied; arg1 is the small CPU_STATE as a byte[].
        var dm = new DynamicMethod("insn", typeof(void),
            new[] { typeof(IntPtr), typeof(byte[]) }, typeof(Emitter).Module, true);
        DynamicILInfo info = dm.GetDynamicILInfo();
        info.SetLocalSignature(sig);
        info.SetCode(il, maxStack);
        var del = dm.CreateDelegate(typeof(Action<IntPtr, byte[]>));
        return GCHandle.ToIntPtr(GCHandle.Alloc(del));
    }

    [UnmanagedCallersOnly]
    public static void Execute(IntPtr handle, IntPtr ramPtr, int ramLen, IntPtr grfPtr, int grfLen)
    {
        var del = (Action<IntPtr, byte[]>)GCHandle.FromIntPtr(handle).Target;
        // RAM is touched in place through ramPtr -- no copy. Only the small CPU_STATE is marshalled.
        byte[] grf = new byte[grfLen];
        Marshal.Copy(grfPtr, grf, 0, grfLen);
        del(ramPtr, grf);
        Marshal.Copy(grf, 0, grfPtr, grfLen);
    }
}
