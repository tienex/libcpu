/** @file
  x86/8086 system personality implementation: see X86System.h.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "X86System.h"
#include "LibCPU/X86Trap.h"
#include "LibCPU/ICpu.h"      // CPU_FLAG

namespace LibCPU {

namespace {

// 8086 register-file indices (GPRs then segment registers, in encoding order).
enum { R_AX = 0, R_CX = 1, R_DX = 2, R_SP = 4, R_SI = 6, R_DI = 7, R_ES = 8, R_CS = 9, R_SS = 10, R_DS = 11 };

// One INS/OUTS element: IsIn => IN from DX -> [ES:DI]; else [DS:SI] -> OUT to DX. Advances the
// pointer (DI or SI) by the width, honouring the direction flag.
VOID
X86StringStep (System &Sys, bool IsIn, UINT32 W)
{
    CPU_STATE *S   = Sys.State ();
    UINT16     Dx  = (UINT16) S->Reg[R_DX];
    UINT8     *Ram = Sys.PhysMem ();
    if (IsIn) {
        UINT32 A   = ((UINT32) (UINT16) S->Reg[R_ES] << 4) + (UINT16) S->Reg[R_DI];
        UINT16 Val = Sys.PortRead (Dx, W * 8);
        if (A < Sys.PhysSize ())               { Ram[A] = (UINT8) (Val & 0xFF); }
        if (W == 2 && A + 1 < Sys.PhysSize ()) { Ram[A + 1] = (UINT8) (Val >> 8); }
        S->Reg[R_DI] = (UINT16) (S->Flag[FlagDirection] ? S->Reg[R_DI] - W : S->Reg[R_DI] + W);
    } else {
        UINT32 A   = ((UINT32) (UINT16) S->Reg[R_DS] << 4) + (UINT16) S->Reg[R_SI];
        UINT16 Val = (A < Sys.PhysSize ()) ? Ram[A] : 0;
        if (W == 2 && A + 1 < Sys.PhysSize ()) { Val |= (UINT16) (Ram[A + 1] << 8); }
        Sys.PortWrite (Dx, W * 8, Val);
        S->Reg[R_SI] = (UINT16) (S->Flag[FlagDirection] ? S->Reg[R_SI] - W : S->Reg[R_SI] + W);
    }
}

// Read/write a 16-bit word in guest physical memory (bounds-checked, little-endian).
UINT16 MemRead16 (System &Sys, UINT32 Lin)
{
    UINT8 *Ram = Sys.PhysMem ();
    return (Lin + 1 < Sys.PhysSize ()) ? (UINT16) (Ram[Lin] | (Ram[Lin + 1] << 8)) : 0;
}
VOID MemWrite16 (System &Sys, UINT32 Lin, UINT16 V)
{
    UINT8 *Ram = Sys.PhysMem ();
    if (Lin + 1 < Sys.PhysSize ()) { Ram[Lin] = (UINT8) (V & 0xFF); Ram[Lin + 1] = (UINT8) (V >> 8); }
}

// The 8086 FLAGS word <-> the modeled condition flags (+ the host's interrupt-enable bit).
UINT16 PackFlags (System &Sys)
{
    CPU_STATE *S  = Sys.State ();
    UINT16     Fl = 0x0002;                                  // bit 1 reads as 1 on the 8086
    if (S->Flag[FlagCarry])     { Fl |= 0x0001; }
    if (S->Flag[FlagParity])    { Fl |= 0x0004; }
    if (S->Flag[FlagAux])       { Fl |= 0x0010; }
    if (S->Flag[FlagZero])      { Fl |= 0x0040; }
    if (S->Flag[FlagNegative])  { Fl |= 0x0080; }
    if (Sys.InterruptsEnabled ()) { Fl |= 0x0200; }
    if (S->Flag[FlagDirection]) { Fl |= 0x0400; }
    if (S->Flag[FlagOverflow])  { Fl |= 0x0800; }
    return Fl;
}
VOID UnpackFlags (System &Sys, UINT16 Fl)
{
    CPU_STATE *S = Sys.State ();
    S->Flag[FlagCarry]     = (Fl & 0x0001) ? 1 : 0;
    S->Flag[FlagParity]    = (Fl & 0x0004) ? 1 : 0;
    S->Flag[FlagAux]       = (Fl & 0x0010) ? 1 : 0;
    S->Flag[FlagZero]      = (Fl & 0x0040) ? 1 : 0;
    S->Flag[FlagNegative]  = (Fl & 0x0080) ? 1 : 0;
    Sys.SetInterruptsEnabled ((Fl & 0x0200) != 0);
    S->Flag[FlagDirection] = (Fl & 0x0400) ? 1 : 0;
    S->Flag[FlagOverflow]  = (Fl & 0x0800) ? 1 : 0;
}

// Enter an interrupt vector (real-mode IVT at physical 0): push FLAGS, CS, IP; load CS:IP from the
// vector; mask interrupts. Returns the ISR entry offset (CS travels in the register file). Shared
// by hardware IRQ delivery and the software INTO/BOUND traps.
CPU_ADDR
X86Vector (System &Sys, UINT32 Vector, CPU_ADDR ReturnPc)
{
    CPU_STATE *S  = Sys.State ();
    UINT16     Sp = (UINT16) S->Reg[R_SP];
    UINT32     SBase = (UINT32) (UINT16) S->Reg[R_SS] << 4;
    auto Push = [&] (UINT16 V) { Sp = (UINT16) (Sp - 2); MemWrite16 (Sys, SBase + Sp, V); };
    Push (PackFlags (Sys));                                 // full FLAGS (incl. IF), so IRET restores them
    Push ((UINT16) S->Reg[R_CS]);
    Push ((UINT16) ReturnPc);
    S->Reg[R_SP] = Sp;
    UINT32 Slot = Vector * 4;                               // IVT entry: [offset][segment]
    UINT16 Off  = MemRead16 (Sys, Slot);
    UINT16 Seg  = MemRead16 (Sys, Slot + 2);
    S->Reg[R_CS] = Seg;
    Sys.SetInterruptsEnabled (false);                       // interrupts masked inside the ISR
    return (CPU_ADDR) Off;
}

// Handle one x86 system trap; returns the linear/offset PC to resume at.
CPU_ADDR
X86Trap (System &Sys, UINT32 Reason, UINT32 Value, CPU_ADDR Return)
{
    CPU_STATE *S  = Sys.State ();
    UINT16     Sp = (UINT16) S->Reg[R_SP];
    UINT16     Ss = (UINT16) S->Reg[R_SS];
    UINT32     SBase = (UINT32) Ss << 4;

    switch (Reason) {
    case X86::CPU_IO_IRET: {                                 // pop IP, CS, FLAGS
        UINT16 Ip = MemRead16 (Sys, SBase + Sp);
        UINT16 Cs = MemRead16 (Sys, SBase + (UINT16) (Sp + 2));
        UINT16 Fl = MemRead16 (Sys, SBase + (UINT16) (Sp + 4));
        S->Reg[R_SP] = (UINT16) (Sp + 6);
        S->Reg[R_CS] = Cs;
        UnpackFlags (Sys, Fl);                               // restore the full FLAGS word (incl. IF)
        return (CPU_ADDR) Ip;
    }
    case X86::CPU_IO_PUSHF: {                                 // push the assembled FLAGS word
        UINT16 New = (UINT16) (Sp - 2);
        MemWrite16 (Sys, SBase + New, PackFlags (Sys));
        S->Reg[R_SP] = New;
        return Return;
    }
    case X86::CPU_IO_POPF: {                                  // pop FLAGS (incl. IF)
        UnpackFlags (Sys, MemRead16 (Sys, SBase + Sp));
        S->Reg[R_SP] = (UINT16) (Sp + 2);
        return Return;
    }
    case X86::CPU_IO_INTO:                                    // INT 4 iff overflow
        return S->Flag[FlagOverflow] ? X86Vector (Sys, 4, Return) : Return;
    case X86::CPU_IO_BOUND:                                   // INT 5 iff the frontend flagged out-of-range
        return Value ? X86Vector (Sys, 5, Return) : Return;
    case X86::CPU_IO_INSB:  X86StringStep (Sys, true,  1); return Return;
    case X86::CPU_IO_INSW:  X86StringStep (Sys, true,  2); return Return;
    case X86::CPU_IO_OUTSB: X86StringStep (Sys, false, 1); return Return;
    case X86::CPU_IO_OUTSW: X86StringStep (Sys, false, 2); return Return;
    case X86::CPU_IO_REP_INSB: case X86::CPU_IO_REP_INSW:    // REP INS/OUTS: move CX elements here, CX = 0
    case X86::CPU_IO_REP_OUTSB: case X86::CPU_IO_REP_OUTSW: {
        bool   IsIn = (Reason == (UINT32) X86::CPU_IO_REP_INSB || Reason == (UINT32) X86::CPU_IO_REP_INSW);
        UINT32 W    = (Reason == (UINT32) X86::CPU_IO_REP_INSW || Reason == (UINT32) X86::CPU_IO_REP_OUTSW) ? 2 : 1;
        for (UINT32 I = (UINT16) S->Reg[R_CX]; I > 0; I--) { X86StringStep (Sys, IsIn, W); }
        S->Reg[R_CX] = 0;
        return Return;
    }
    default:
        return Return;
    }
}

} // anonymous namespace

void
InstallX86System (System &Sys)
{
    Sys.SetArchTrap (&X86Trap);
    // Hardware IRQ n enters the real-mode vector 8 + n (the master 8259's base on the PC).
    Sys.SetIrqDeliver ([] (System &S, UINT32 Irq, CPU_ADDR Ret) { return X86Vector (S, 8 + Irq, Ret); });
    // A software interrupt (INT n) enters real-mode vector n in-guest, so the guest's own BIOS/DOS
    // handlers run. An IVT slot of 0000:0000 means no handler is installed (e.g. a demo that sets up
    // no vectors): leave the INT inert rather than branching to a meaningless address.
    Sys.SetSyscallDeliver ([] (System &S, UINT32 Vector, CPU_ADDR Ret) -> CPU_ADDR {
        UINT32 Slot = Vector * 4;
        if (MemRead16 (S, Slot) == 0 && MemRead16 (S, Slot + 2) == 0) { return Ret; }
        return X86Vector (S, Vector, Ret);
    });
}

void
X86SetIvt (System &Sys, UINT32 Vector, UINT16 Seg, UINT16 Off)
{
    UINT8 *Ram = Sys.PhysMem ();
    UINT32 Slot = Vector * 4;                                // real-mode IVT at physical 0
    if (Slot + 4 > Sys.PhysSize ()) { return; }
    Ram[Slot + 0] = (UINT8) (Off & 0xFF);
    Ram[Slot + 1] = (UINT8) (Off >> 8);
    Ram[Slot + 2] = (UINT8) (Seg & 0xFF);
    Ram[Slot + 3] = (UINT8) (Seg >> 8);
}

} // namespace LibCPU
