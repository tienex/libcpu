/** @file
  BuildRegisterLayout -- flatten a UPCL register_file AST into a concrete layout.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#include "RegisterLayout.h"

namespace LibCPU {
namespace Upcl {

// The bit width of one splitter field: an entry-specific type (#i3 TOP) wins, else the
// splitter's element type (the #i8 in `-> #i8 ( ah : al )`), else nothing.
static UINT32
BindWidth (Splitter CONST *pSplit, BitBind CONST *pBind)
{
    if (pBind->SubType != nullptr) { return pBind->SubType->Width; }
    if (pSplit->FieldType != nullptr) { return pSplit->FieldType->Width; }
    return 0;
}

// True when a bind composes the register from OTHER registers (pc's `seg <- cs` /
// `off <-> ip`) or hardwires an expression, rather than aliasing this register's own
// storage bits. Such binds describe evaluation, not layout.
static bool
IsComposition (BitBind CONST *pBind)
{
    return !pBind->SrcBind.empty() || pBind->Bidi || pBind->HardExpr != nullptr;
}

// Decompose one colon-list splitter (`( a : b : ... )`) into sub-registers / flags.
// The binds are listed MSB-first, so we walk the register from its top bit down.
static void
SplitColonList (RegisterLayout *pLayout, RegPhys CONST &Phys, Splitter CONST *pSplit)
{
    UINT32 Bit = Phys.Width;        // start above the most-significant bit

    for (BitBind CONST *pBind : pSplit->Binds) {
        UINT32 Width = BindWidth (pSplit, pBind);
        UINT32 Lo    = (Width <= Bit) ? (Bit - Width) : 0;
        Bit          = Lo;

        // A hardwired constant (the 0s padding the flags word) occupies bits but has no name.
        if (pBind->IsConst || pBind->Name.empty()) { continue; }

        if (Width == 1) {
            // A single bit is a flag -- even one mapped from another bit (A <- C).
            RegFlag Flag;
            Flag.Name   = pBind->Name;
            Flag.Parent = Phys.Index;
            Flag.Bit    = Lo;
            Flag.Meta   = pBind->MetaMap;
            pLayout->Flags.push_back (Flag);
            continue;
        }

        // A composition bind (pc's seg/off) names no storage of its own. Record the PC's
        // field-to-source mapping (off -> ip, seg -> cs) so pc.off resolves to the offset
        // register, then skip it as a storage field.
        if (IsComposition (pBind)) {
            if (Phys.IsPc && !pBind->SrcBind.empty ()) { pLayout->PcFields[pBind->Name] = pBind->SrcBind; }
            continue;
        }

        RegSub Sub;
        Sub.Name   = pBind->Name;
        Sub.Parent = Phys.Index;
        Sub.Lo     = Lo;
        Sub.Width  = Width;
        pLayout->Subs.push_back (Sub);
    }
}

// How many copies a declaration expands to: `N ** <type> <name>` repeats N times,
// everything else is a single register.
static UINT32
RepeatCount (RegDecl CONST *pDecl)
{
    if (pDecl->RepeatCount != nullptr && pDecl->RepeatCount->Kind == ExprInt) {
        UINT64 Count = pDecl->RepeatCount->Int;
        if (Count >= 1) { return (UINT32) Count; }
    }
    return 1;
}

RegisterLayout
BuildRegisterLayout (Arch *pArch)
{
    RegisterLayout Layout;

    if (pArch == nullptr || pArch->RegFile == nullptr) { return Layout; }

    for (Group CONST *pGroup : pArch->RegFile->Groups) {
        for (RegDecl CONST *pDecl : pGroup->Regs) {
            UINT32 Width = (pDecl->VType != nullptr) ? pDecl->VType->Width : 0;
            UINT32 Count = RepeatCount (pDecl);

            for (UINT32 Copy = 0; Copy < Count; ++Copy) {
                RegPhys Phys;
                Phys.Name  = (Count > 1) ? (pDecl->Name + std::to_string (Copy)) : pDecl->Name;
                Phys.Index = (UINT32) Layout.Phys.size ();
                Phys.Width = Width;

                if (pDecl->Binding != nullptr) {
                    Phys.IsPc  = (pDecl->Binding->Meta == "PC");
                    Phys.IsPsr = (pDecl->Binding->Meta == "PSR");
                }

                Layout.PhysIndex[Phys.Name] = Phys.Index;
                Layout.Phys.push_back (Phys);

                // Decompose a colon-list splitter into sub-registers / flags. Union
                // splitters (the FPU sw/cw words) are nested bit maps not needed by the
                // integer core -- the physical word is captured, decomposition deferred.
                if (pDecl->Binding != nullptr && pDecl->Binding->Split != nullptr
                    && !pDecl->Binding->Split->Union) {
                    SplitColonList (&Layout, Layout.Phys.back (), pDecl->Binding->Split);
                }
            }
        }
    }

    return Layout;
}

} // namespace Upcl
} // namespace LibCPU
