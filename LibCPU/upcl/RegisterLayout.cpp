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

CHAR8 CONST *
ReservationBitName ()
{
    return "__llbit";
}

CHAR8 CONST *
MmuResultName ()
{
    return "__mmu_pa";       // the physical address the MMU table-walk (%PA) produces
}

CHAR8 CONST *
ReservationAddrName ()
{
    return "__lladdr";
}

// Does this expression tree contain an interlocked memory access (%LL load-linked or %SC
// store-conditional)?
static bool
ExprUsesInterlock (Expr *pExpr)
{
    if (pExpr == nullptr) { return false; }
    if (pExpr->Kind == ExprStoreCond) { return true; }
    if (pExpr->Kind == ExprMem && pExpr->Linked) { return true; }
    for (Expr *A : pExpr->Args) { if (ExprUsesInterlock (A)) { return true; } }
    return false;
}

static bool
StmtUsesInterlock (Stmt *pStmt)
{
    if (pStmt == nullptr) { return false; }
    if (ExprUsesInterlock (pStmt->Lhs) || ExprUsesInterlock (pStmt->Rhs)
        || ExprUsesInterlock (pStmt->Cond)) {
        return true;
    }
    for (Stmt *S : pStmt->Body) { if (StmtUsesInterlock (S)) { return true; } }
    for (Stmt *S : pStmt->Then) { if (StmtUsesInterlock (S)) { return true; } }
    for (Stmt *S : pStmt->Else) { if (StmtUsesInterlock (S)) { return true; } }
    for (Stmt *S : pStmt->Init) { if (StmtUsesInterlock (S)) { return true; } }
    for (Stmt *S : pStmt->Step) { if (StmtUsesInterlock (S)) { return true; } }
    return false;
}

bool
ArchUsesInterlock (Arch *pArch)
{
    if (pArch == nullptr) { return false; }
    for (Insn *pInsn : pArch->Insns) {
        for (Stmt *S : pInsn->Semantics) { if (StmtUsesInterlock (S)) { return true; } }
    }
    for (JumpInsn *pJump : pArch->Jumps) {
        for (Stmt *S : pJump->Pre) { if (StmtUsesInterlock (S)) { return true; } }
        for (Stmt *S : pJump->Action) { if (StmtUsesInterlock (S)) { return true; } }
    }
    return false;
}

RegisterLayout
BuildRegisterLayout (Arch *pArch)
{
    RegisterLayout Layout;

    if (pArch == nullptr || pArch->RegFile == nullptr) { return Layout; }

    for (Group CONST *pGroup : pArch->RegFile->Groups) {
        UINT32 GroupPos = 0;        // position of each member within its group, for the <Group><N> alias
        // Record the Phys base index BEFORE expanding this group's members, so we can
        // wire the memory-alias metadata if this group carries `aliases_memory`.
        UINT32 GroupPhysBase = (UINT32) Layout.Phys.size ();

        for (RegDecl CONST *pDecl : pGroup->Regs) {
            UINT32 Width = (pDecl->VType != nullptr) ? pDecl->VType->Width : 0;
            UINT32 Count = RepeatCount (pDecl);

            // A repeated declaration (`8 ** #f80 st?`) is also a register ARRAY -- the base name
            // (st) addresses its elements by a runtime index (an FPU stack, a RISC register file).
            // The array element numbering starts at `name?:Start` (r?:1 -> r1..rN) so a separate
            // hardwired r0 (declared before it) keeps slot 0 and the bank indexes line up: r[k]
            // resolves to BaseIndex + k - Start (computed in the translator's RegBankAddr).
            UINT32 Start = (UINT32) pDecl->RepeatStart;
            if (Count > 1) {
                RegArray Arr;
                Arr.Name      = pDecl->Name;
                Arr.BaseIndex = (UINT32) Layout.Phys.size ();
                Arr.Count     = Count;
                Arr.Width     = Width;
                Arr.Start     = Start;
                Arr.Float     = (pDecl->VType != nullptr && pDecl->VType->Kind == TypeFloat);
                Layout.Arrays.push_back (Arr);
            }

            for (UINT32 Copy = 0; Copy < Count; ++Copy) {
                RegPhys Phys;
                Phys.Name  = (Count > 1) ? (pDecl->Name + std::to_string (Start + Copy)) : pDecl->Name;
                Phys.Index = (UINT32) Layout.Phys.size ();
                Phys.Width = Width;
                Phys.Float = (pDecl->VType != nullptr && pDecl->VType->Kind == TypeFloat);

                if (pDecl->Binding != nullptr) {
                    Phys.IsPc  = (pDecl->Binding->Meta == "PC");
                    Phys.IsPsr = (pDecl->Binding->Meta == "PSR");
                    // A `<- 0` constant alias hardwires the register to zero (the m88k/RISC r0).
                    Expr *pAlias = pDecl->Binding->AliasExpr;
                    Phys.ZeroWired = (Count == 1 && pAlias != nullptr
                                      && pAlias->Kind == ExprInt && pAlias->Int == 0);
                }

                Layout.PhysIndex[Phys.Name] = Phys.Index;
                Layout.Phys.push_back (Phys);

                // Positional group alias: every member is also reachable as <GroupName><N> (group S's
                // pc/npc/ptbr as S0/S1/S2), the control-register-number view software addresses them
                // by. Modelled as a full-width sub-register so reads/writes hit the same storage.
                if (Width != 0 && !pGroup->Name.empty ()) {
                    std::string Alias = pGroup->Name + std::to_string (GroupPos);
                    if (Alias != Phys.Name) {
                        RegSub Gs;
                        Gs.Name   = Alias;
                        Gs.Parent = Phys.Index;
                        Gs.Lo     = 0;
                        Gs.Width  = Width;
                        Layout.Subs.push_back (Gs);
                    }
                }
                ++GroupPos;

                // Decompose a colon-list splitter into sub-registers / flags. Union
                // splitters (the FPU sw/cw words) are nested bit maps not needed by the
                // integer core -- the physical word is captured, decomposition deferred.
                if (pDecl->Binding != nullptr && pDecl->Binding->Split != nullptr
                    && !pDecl->Binding->Split->Union) {
                    SplitColonList (&Layout, Layout.Phys.back (), pDecl->Binding->Split);
                }
            }
        }

        // Register/memory aliasing: if this group was declared with `aliases_memory [base]`,
        // record the first unused alias (first encountered group wins). The aliased range is
        // [MemAliasWordBase, MemAliasWordBase + MemAliasCount), mapping word offset N to the
        // physical register at GroupPhysBase + N. Only the FIRST such group is wired; if a
        // second group also declares aliasing it is silently ignored (no ISA has two such groups).
        if (pGroup->MemAliasBase != ~(UINT32)0 && !Layout.HasMemAlias ()) {
            UINT32 Count = (UINT32) Layout.Phys.size () - GroupPhysBase;
            if (Count > 0) {
                Layout.MemAliasPhysBase = GroupPhysBase;
                Layout.MemAliasCount    = Count;
                Layout.MemAliasWordBase = pGroup->MemAliasBase;
            }
        }
    }

    // Synthesise the load-linked / store-conditional reservation state when the description uses it
    // (%LL / %SC). These are ordinary physical registers -- read/written through GetRegister /
    // PutRegister like any other -- so every backend supports the interlock with no new primitive.
    if (ArchUsesInterlock (pArch)) {
        UINT32 WordW = pArch->WordSize ? pArch->WordSize : 32;
        UINT32 AddrW = pArch->AddressSize ? pArch->AddressSize : WordW;

        RegPhys Bit;
        Bit.Name  = ReservationBitName ();        // the LLbit: nonzero while a reservation is held
        Bit.Index = (UINT32) Layout.Phys.size ();
        Bit.Width = WordW;
        Layout.PhysIndex[Bit.Name] = Bit.Index;
        Layout.Phys.push_back (Bit);

        RegPhys Adr;
        Adr.Name  = ReservationAddrName ();       // the reserved address (compared by %SC)
        Adr.Index = (UINT32) Layout.Phys.size ();
        Adr.Width = AddrW;
        Layout.PhysIndex[Adr.Name] = Adr.Index;
        Layout.Phys.push_back (Adr);
    }

    // Synthesise the MMU translation-result register when the arch describes an mmu { } -- the
    // table-walk assigns the physical address to %PA, which libcpu's TLB reads to install the entry.
    if (pArch->Mmu != nullptr) {
        UINT32 AddrW = pArch->AddressSize ? pArch->AddressSize : (pArch->WordSize ? pArch->WordSize : 32);
        RegPhys Pa;
        Pa.Name  = MmuResultName ();
        Pa.Index = (UINT32) Layout.Phys.size ();
        Pa.Width = AddrW;
        Layout.PhysIndex[Pa.Name] = Pa.Index;
        Layout.Phys.push_back (Pa);
    }

    return Layout;
}

} // namespace Upcl
} // namespace LibCPU
