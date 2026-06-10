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

        var dm = new DynamicMethod("insn", typeof(void),
            new[] { typeof(byte[]), typeof(byte[]) }, typeof(Emitter).Module, true);
        DynamicILInfo info = dm.GetDynamicILInfo();
        info.SetLocalSignature(sig);
        info.SetCode(il, maxStack);
        var del = dm.CreateDelegate(typeof(Action<byte[], byte[]>));
        return GCHandle.ToIntPtr(GCHandle.Alloc(del));
    }

    [UnmanagedCallersOnly]
    public static void Execute(IntPtr handle, IntPtr ramPtr, int ramLen, IntPtr grfPtr, int grfLen)
    {
        var del = (Action<byte[], byte[]>)GCHandle.FromIntPtr(handle).Target;
        byte[] ram = new byte[ramLen];
        Marshal.Copy(ramPtr, ram, 0, ramLen);
        byte[] grf = new byte[grfLen];
        Marshal.Copy(grfPtr, grf, 0, grfLen);
        del(ram, grf);
        Marshal.Copy(ram, 0, ramPtr, ramLen);
        Marshal.Copy(grf, 0, grfPtr, grfLen);
    }
}
