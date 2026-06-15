/** @file
  NEC V20/V30 frontend implementation (initial slice). See CpuV20.h.

  Supported (16-bit, flat addressing, register-direct ModR/M only):
    B8+r iw   MOV reg16,imm16     40+r   INC reg16        48+r   DEC reg16
    01 /r     ADD r/m16,r16       29 /r  SUB r/m16,r16    39 /r  CMP r/m16,r16
    89 /r     MOV r/m16,r16       8B /r  MOV r16,r/m16
    05 iw     ADD AX,imm16        2D iw  SUB AX,imm16     3D iw  CMP AX,imm16
    A1 ow     MOV AX,[addr16]     A3 ow  MOV [addr16],AX
    EB cb     JMP rel8            E9 cw  JMP rel16
    70..7F cb Jcc rel8            E2 cb  LOOP rel8
  NEC-only (0F prefix), r/m16 + imm8 bit number, register-direct:
    0F 19     TEST1               0F 1B  CLR1   0F 1D SET1   0F 1F NOT1

  Flags use the generic CPU_FLAG mapping: ZF=FlagZero, CF=FlagCarry, SF=FlagNegative
  (sign), OF=FlagOverflow.
**/
#include "CpuV20.h"
#include "CpuI8080.h"
#include "LibCPU/CpuState.h"
#include <cstdio>

namespace LibCPU {
namespace {

static UINT16
Imm16At (UINT8 CONST *pCode, CPU_ADDR Pc)
{
    return (UINT16) (pCode[Pc] | (pCode[Pc + 1] << 8));
}

// Parity flag: even parity of the low 8 bits of the result.
static VOID
EmitParity (ICpuEmitter *pE, ICpuValue *pRes)
{
    ComPtr<ICpuValue> Low;  pE->Cast (CastTrunc, pRes, 8, &Low);
    ComPtr<ICpuValue> S4;   pE->ConstInt (8, 4, &S4);
    ComPtr<ICpuValue> R4;   pE->BinaryOp (BinLShr, Low, S4, &R4);
    ComPtr<ICpuValue> X1;   pE->BinaryOp (BinXor, Low, R4, &X1);
    ComPtr<ICpuValue> S2;   pE->ConstInt (8, 2, &S2);
    ComPtr<ICpuValue> R2;   pE->BinaryOp (BinLShr, X1, S2, &R2);
    ComPtr<ICpuValue> X2;   pE->BinaryOp (BinXor, X1, R2, &X2);
    ComPtr<ICpuValue> S1;   pE->ConstInt (8, 1, &S1);
    ComPtr<ICpuValue> R1;   pE->BinaryOp (BinLShr, X2, S1, &R1);
    ComPtr<ICpuValue> X3;   pE->BinaryOp (BinXor, X2, R1, &X3);
    ComPtr<ICpuValue> One;  pE->ConstInt (8, 1, &One);
    ComPtr<ICpuValue> Lo;   pE->BinaryOp (BinAnd, X3, One, &Lo);
    ComPtr<ICpuValue> Zero; pE->ConstInt (8, 0, &Zero);
    ComPtr<ICpuValue> PF;   pE->Compare (CmpEq, Lo, Zero, &PF);
    pE->SetFlag (FlagParity, PF);                                       // even -> set
}

// Z (FlagZero), S (FlagNegative = bit 15) and P (parity) for a 16-bit result.
static VOID
EmitZSF (ICpuEmitter *pE, ICpuValue *pRes)
{
    ComPtr<ICpuValue> Zero;  pE->ConstInt (16, 0, &Zero);
    ComPtr<ICpuValue> ZF;    pE->Compare (CmpEq, pRes, Zero, &ZF);
    pE->SetFlag (FlagZero, ZF);

    ComPtr<ICpuValue> Bit15; pE->ConstInt (16, 0x8000, &Bit15);
    ComPtr<ICpuValue> Masked;pE->BinaryOp (BinAnd, pRes, Bit15, &Masked);
    ComPtr<ICpuValue> Zero2; pE->ConstInt (16, 0, &Zero2);
    ComPtr<ICpuValue> SF;    pE->Compare (CmpNe, Masked, Zero2, &SF);
    pE->SetFlag (FlagNegative, SF);

    EmitParity (pE, pRes);
}

// Logical ops (AND/OR/XOR/TEST): CF and OF cleared, then Z/S/P from the result.
static VOID
EmitLogicFlags (ICpuEmitter *pE, ICpuValue *pRes)
{
    ComPtr<ICpuValue> Zero1; pE->ConstInt (1, 0, &Zero1);
    pE->SetFlag (FlagCarry, Zero1);
    ComPtr<ICpuValue> Zero1b; pE->ConstInt (1, 0, &Zero1b);
    pE->SetFlag (FlagOverflow, Zero1b);
    EmitZSF (pE, pRes);
}

// CF/OF/ZF/SF for Res = A + B (16-bit).
static VOID
EmitAddFlags (ICpuEmitter *pE, ICpuValue *pA, ICpuValue *pB, ICpuValue *pRes)
{
    ComPtr<ICpuValue> A32;  pE->Cast (CastZExt, pA, 32, &A32);
    ComPtr<ICpuValue> B32;  pE->Cast (CastZExt, pB, 32, &B32);
    ComPtr<ICpuValue> Sum;  pE->BinaryOp (BinAdd, A32, B32, &Sum);
    ComPtr<ICpuValue> Sh;   pE->ConstInt (32, 16, &Sh);
    ComPtr<ICpuValue> Hi;   pE->BinaryOp (BinLShr, Sum, Sh, &Hi);
    ComPtr<ICpuValue> CF;   pE->Cast (CastTrunc, Hi, 1, &CF);
    pE->SetFlag (FlagCarry, CF);

    ComPtr<ICpuValue> AxR;  pE->BinaryOp (BinXor, pA, pRes, &AxR);
    ComPtr<ICpuValue> BxR;  pE->BinaryOp (BinXor, pB, pRes, &BxR);
    ComPtr<ICpuValue> And1; pE->BinaryOp (BinAnd, AxR, BxR, &And1);
    ComPtr<ICpuValue> Bit15;pE->ConstInt (16, 0x8000, &Bit15);
    ComPtr<ICpuValue> And2; pE->BinaryOp (BinAnd, And1, Bit15, &And2);
    ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
    ComPtr<ICpuValue> OF;   pE->Compare (CmpNe, And2, Zero, &OF);
    pE->SetFlag (FlagOverflow, OF);

    EmitZSF (pE, pRes);
}

// CF(borrow)/OF/ZF/SF for Res = A - B (16-bit). Used by SUB and CMP.
static VOID
EmitSubFlags (ICpuEmitter *pE, ICpuValue *pA, ICpuValue *pB, ICpuValue *pRes)
{
    ComPtr<ICpuValue> CF;   pE->Compare (CmpULt, pA, pB, &CF);   // borrow: A < B unsigned
    pE->SetFlag (FlagCarry, CF);

    ComPtr<ICpuValue> AxB;  pE->BinaryOp (BinXor, pA, pB, &AxB);
    ComPtr<ICpuValue> AxR;  pE->BinaryOp (BinXor, pA, pRes, &AxR);
    ComPtr<ICpuValue> And1; pE->BinaryOp (BinAnd, AxB, AxR, &And1);
    ComPtr<ICpuValue> Bit15;pE->ConstInt (16, 0x8000, &Bit15);
    ComPtr<ICpuValue> And2; pE->BinaryOp (BinAnd, And1, Bit15, &And2);
    ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
    ComPtr<ICpuValue> OF;   pE->Compare (CmpNe, And2, Zero, &OF);
    pE->SetFlag (FlagOverflow, OF);

    EmitZSF (pE, pRes);
}

// 16-bit ALU: Res = Dst <op> Src, with full flags. op: 0 ADD,1 OR,2 ADC,3 SBB,4 AND,5 SUB,
// 6 XOR,7 CMP. The result is returned (owned) in *ppRes; the caller writes it back except for
// CMP (7), which only sets flags.
static VOID
EmitAlu16 (ICpuEmitter *pE, UINT32 AluOp, ICpuValue *pDst, ICpuValue *pSrc, ICpuValue **ppRes)
{
    if (AluOp == 1 || AluOp == 4 || AluOp == 6) {                  // OR / AND / XOR
        CPU_BINOP B = (AluOp == 4) ? BinAnd : (AluOp == 6) ? BinXor : BinOr;
        pE->BinaryOp (B, pDst, pSrc, ppRes);
        EmitLogicFlags (pE, *ppRes);
        return;
    }
    bool Sub = (AluOp == 3 || AluOp == 5 || AluOp == 7);           // SBB / SUB / CMP
    bool Cin = (AluOp == 2 || AluOp == 3);                         // ADC / SBB carry-in
    ComPtr<ICpuValue> D32; pE->Cast (CastZExt, pDst, 32, &D32);
    ComPtr<ICpuValue> S32; pE->Cast (CastZExt, pSrc, 32, &S32);
    ComPtr<ICpuValue> Acc; pE->BinaryOp (Sub ? BinSub : BinAdd, D32, S32, &Acc);
    ComPtr<ICpuValue> Carried;
    ICpuValue *Wide = Acc;
    if (Cin) {
        ComPtr<ICpuValue> C;   pE->GetFlag (FlagCarry, &C);
        ComPtr<ICpuValue> C32; pE->Cast (CastZExt, C, 32, &C32);
        pE->BinaryOp (Sub ? BinSub : BinAdd, Acc, C32, &Carried);
        Wide = Carried;
    }
    pE->Cast (CastTrunc, Wide, 16, ppRes);                         // *ppRes = 16-bit result (owned)
    ICpuValue *Res = *ppRes;                                       // borrow for flag computation
    ComPtr<ICpuValue> Sh; pE->ConstInt (32, 16, &Sh);
    ComPtr<ICpuValue> Hi; pE->BinaryOp (BinLShr, Wide, Sh, &Hi);
    ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Hi, 1, &CF);
    pE->SetFlag (FlagCarry, CF);                                   // carry-out / borrow
    ComPtr<ICpuValue> DxS; pE->BinaryOp (BinXor, pDst, pSrc, &DxS);
    ComPtr<ICpuValue> DxR; pE->BinaryOp (BinXor, pDst, Res, &DxR);
    ComPtr<ICpuValue> T1;
    if (Sub) {
        pE->BinaryOp (BinAnd, DxS, DxR, &T1);                      // sub OF: (D^S)&(D^Res)
    } else {
        ComPtr<ICpuValue> NotDxS; pE->UnaryOp (UnCom, DxS, &NotDxS);
        pE->BinaryOp (BinAnd, NotDxS, DxR, &T1);                   // add OF: ~(D^S)&(D^Res)
    }
    ComPtr<ICpuValue> B15;  pE->ConstInt (16, 0x8000, &B15);
    ComPtr<ICpuValue> T2;   pE->BinaryOp (BinAnd, T1, B15, &T2);
    ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
    ComPtr<ICpuValue> OF;   pE->Compare (CmpNe, T2, Zero, &OF);
    pE->SetFlag (FlagOverflow, OF);
    EmitZSF (pE, Res);
}

class CpuV20 final : public ComObject<ICpuArchitecture> {
public:
    explicit CpuV20 (UINT16 CodeSeg, bool IsV30 = false)
        : m_CodeSeg (CodeSeg), m_IsV30 (IsV30), m_pEmu (CreateI8080 ()) {}
    ~CpuV20 () override { if (m_pEmu != nullptr) { m_pEmu->Release (); } }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        // The V20 (uPD70108) and V30 (uPD70116) share the instruction set; they differ only in
        // external bus width (8-bit vs 16-bit), which is invisible at this translation layer.
        pInfo->pName       = m_IsV30 ? "v30" : "v20";
        pInfo->pFullName   = m_IsV30 ? "NEC V30 (uPD70116)" : "NEC V20 (uPD70108)";
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = 16;
        pInfo->AddressSize = 16;
        pInfo->PsrSize     = 16;
        pInfo->IsBigEndian = FALSE;
        pInfo->GprCount    = 8;
        pInfo->GprBits     = 16;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        // Instructions are fetched from CS * 16 + IP. The AOT driver works in IP
        // space, so we bias the decode pointer by the code segment base once here;
        // every decode (m_pCode[Pc]) then reads the right linear byte.
        m_pBase    = pBase;
        m_pCode    = pBase + ((UINT64) m_CodeSeg << 4);
        m_CodeSize = Size;
        if (m_pEmu != nullptr) { m_pEmu->SetCodeMemory (pBase, Size); }   // shared RAM for 8080 mode
        return S_OK;
    }

    // Re-point the decode base at a new code segment (used by a far JMP/CALL: the host
    // reads the guest CS register after the trap and re-translates the new segment).
    VOID SetCodeSeg (UINT16 Cs) {
        m_CodeSeg = Cs;
        if (m_pBase != nullptr) {
            m_pCode = m_pBase + ((UINT64) Cs << 4);
        }
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        UINT8    Op   = m_pCode[Pc];
        UINT32   Len;
        UINT32   Tag  = TagContinue;
        CPU_ADDR NewPc = (CPU_ADDR) -1;

        // In 8080 emulation mode the instruction stream is 8080 code: RETEM (ED FD) returns to
        // native mode, everything else is decoded by the shared 8080 core.
        if (m_In8080 && Pc != m_BrkemPc) {
            if (Op == 0xED && m_pCode[Pc + 1] == 0xFD) {
                m_In8080 = false;
                *pNextPc = Pc + 2; *pTag = TagContinue; *pNewPc = (CPU_ADDR) -1;
                return S_OK;
            }
            return m_pEmu->TagInstr (Pc, pTag, pNewPc, pNextPc);
        }

        if (Op >= 0xB8 && Op <= 0xBF) {                          // MOV reg16,imm16
            Len = 3;
        } else if (Op >= 0xB0 && Op <= 0xB7) {                   // MOV reg8,imm8
            Len = 2;
        } else if (Op >= 0x40 && Op <= 0x4F) {                   // INC/DEC reg16
            Len = 1;
        } else if (Op >= 0x50 && Op <= 0x5F) {                   // PUSH/POP reg16
            Len = 1;
        } else if ((Op < 0x40 && ((Op & 7) == 1 || (Op & 7) == 3)) ||
                   Op == 0x85 || Op == 0x89 || Op == 0x8B || Op == 0x8E || Op == 0x8C) {
            Len = 1 + RmLen (m_pCode[Pc + 1]);                   // ALU/TEST/MOV r/m16,r16 (+disp)
        } else if (Op == 0x81) {                                 // grp1: <alu> r/m16, imm16
            Len = 1 + RmLen (m_pCode[Pc + 1]) + 2;
        } else if (Op == 0x83) {                                 // grp1: <alu> r/m16, imm8 (sign-extended)
            Len = 1 + RmLen (m_pCode[Pc + 1]) + 1;
        } else if (Op == 0xC7) {                                 // MOV r/m16, imm16
            Len = 1 + RmLen (m_pCode[Pc + 1]) + 2;
        } else if ((Op < 0x40 && (Op & 7) == 5) || Op == 0xA1 || Op == 0xA3 || Op == 0xA9) {
            Len = 3;                                             // acc,imm16 / MOV AX,[addr16] / TEST AX,imm16
        } else if (Op == 0xEB) {                                 // JMP rel8
            Len = 2; Tag = TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0xE9) {                                 // JMP rel16
            Len = 3; Tag = TagBranch; NewPc = (CPU_ADDR) (Pc + 3 + (INT16) Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0xE8) {                                 // CALL rel16
            Len = 3; Tag = TagCall; NewPc = (CPU_ADDR) (Pc + 3 + (INT16) Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0xC3) {                                 // RET (indirect: target popped at run time)
            Len = 1; Tag = TagReturn;
        } else if (Op == 0xEA) {                                 // JMP ptr16:16 (far: CS:IP reload -> host)
            Len = 5; Tag = TagTrap;
        } else if (Op == 0x9A) {                                 // CALL ptr16:16 (far: push CS:IP, reload -> host)
            Len = 5; Tag = TagTrap;
        } else if (Op == 0xCB) {                                 // RETF (far return: pop CS:IP at run time -> host)
            Len = 1; Tag = TagTrap;
        } else if (Op == 0xCD) {                                 // INT imm8 (software interrupt -> host syscall)
            Len = 2; Tag = TagTrap;
        } else if (Op == 0xE4 || Op == 0xE6) {                   // IN AL,imm8 / OUT imm8,AL (device bus)
            Len = 2; Tag = TagTrap;
        } else if (Op == 0xEC || Op == 0xEE) {                   // IN AL,DX / OUT DX,AL (device bus)
            Len = 1; Tag = TagTrap;
        } else if (Op == 0xCF || Op == 0xF4 || Op == 0xFA || Op == 0xFB) {   // IRET / HLT / CLI / STI
            Len = 1; Tag = TagTrap;
        } else if (Op >= 0x70 && Op <= 0x7F) {                   // Jcc rel8
            Len = 2; Tag = TagConditional | TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0xE2) {                                 // LOOP rel8
            Len = 2; Tag = TagConditional | TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0x0F) {                                 // NEC 0F-prefixed instructions
            UINT8 Sub = m_pCode[Pc + 1];
            if (Sub == 0xFF) {                                   // BRKEM imm8: enter 8080 emulation mode
                Len = 3; m_In8080 = true; m_BrkemPc = Pc;
            } else if (Sub == 0xED) {                            // CALLN imm8: native call from 8080 mode
                Len = 3;
            } else {
                Len = 4;                                         // bit op: 0F xx modrm ib
            }
        } else {
            Len = 1;                                             // unknown: treat as 1-byte, continue
        }
        *pNextPc = Pc + Len;
        *pTag    = Tag;
        *pNewPc  = NewPc;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT8 Op = m_pCode[Pc];
        if (m_In8080 && Pc != m_BrkemPc) {                        // 8080 emulation mode: defer to the core
            if (Op == 0xED && m_pCode[Pc + 1] == 0xFD) { std::snprintf (pLine, MaxLine, "retem"); return S_OK; }
            return m_pEmu->Disassemble (Pc, pLine, MaxLine);
        }
        if (Op == 0x0F && m_pCode[Pc + 1] == 0xFF) { std::snprintf (pLine, MaxLine, "brkem 0x%02x", m_pCode[Pc + 2]); return S_OK; }
        if (Op == 0x0F && m_pCode[Pc + 1] == 0xED) { std::snprintf (pLine, MaxLine, "calln 0x%02x", m_pCode[Pc + 2]); return S_OK; }
        if (Op >= 0xB8 && Op <= 0xBF) {
            std::snprintf (pLine, MaxLine, "mov %s,0x%04x", RegName (Op - 0xB8), Imm16At (m_pCode, Pc + 1));
        } else if (Op >= 0xB0 && Op <= 0xB7) {
            std::snprintf (pLine, MaxLine, "mov %s,0x%02x", Reg8Name (Op - 0xB0), m_pCode[Pc + 1]);
        } else if (Op >= 0x40 && Op <= 0x47) {
            std::snprintf (pLine, MaxLine, "inc %s", RegName (Op - 0x40));
        } else if (Op >= 0x48 && Op <= 0x4F) {
            std::snprintf (pLine, MaxLine, "dec %s", RegName (Op - 0x48));
        } else if (Op == 0x75) {
            std::snprintf (pLine, MaxLine, "jnz 0x%04x", (unsigned) (Pc + 2 + (INT8) m_pCode[Pc + 1]));
        } else if (Op == 0x74) {
            std::snprintf (pLine, MaxLine, "jz 0x%04x", (unsigned) (Pc + 2 + (INT8) m_pCode[Pc + 1]));
        } else if (Op == 0xE2) {
            std::snprintf (pLine, MaxLine, "loop 0x%04x", (unsigned) (Pc + 2 + (INT8) m_pCode[Pc + 1]));
        } else if (Op == 0x01) {
            UINT8 M = m_pCode[Pc + 1];
            std::snprintf (pLine, MaxLine, "add %s,%s", RegName (M & 7), RegName ((M >> 3) & 7));
        } else if (Op == 0xA3) {
            std::snprintf (pLine, MaxLine, "mov [0x%04x],ax", Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0x9A) {
            std::snprintf (pLine, MaxLine, "call 0x%04x:0x%04x", Imm16At (m_pCode, Pc + 3), Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0xCB) {
            std::snprintf (pLine, MaxLine, "retf");
        } else if (Op == 0xCD) {
            std::snprintf (pLine, MaxLine, "int 0x%02x", m_pCode[Pc + 1]);
        } else if (Op == 0xE4) {
            std::snprintf (pLine, MaxLine, "in al,0x%02x", m_pCode[Pc + 1]);
        } else if (Op == 0xE6) {
            std::snprintf (pLine, MaxLine, "out 0x%02x,al", m_pCode[Pc + 1]);
        } else if (Op == 0xEC) {
            std::snprintf (pLine, MaxLine, "in al,dx");
        } else if (Op == 0xEE) {
            std::snprintf (pLine, MaxLine, "out dx,al");
        } else if (Op == 0xCF) {
            std::snprintf (pLine, MaxLine, "iret");
        } else if (Op == 0xF4) {
            std::snprintf (pLine, MaxLine, "hlt");
        } else if (Op == 0xFA) {
            std::snprintf (pLine, MaxLine, "cli");
        } else if (Op == 0xFB) {
            std::snprintf (pLine, MaxLine, "sti");
        } else if (Op >= 0x70 && Op <= 0x7F) {
            static CHAR8 CONST *kJ[16] = { "jo","jno","jb","jnb","jz","jnz","jbe","ja",
                                           "js","jns","jp","jnp","jl","jge","jle","jg" };
            std::snprintf (pLine, MaxLine, "%s 0x%04x", kJ[Op - 0x70], (unsigned) (Pc + 2 + (INT8) m_pCode[Pc + 1]));
        } else if ((Op < 0x40 && ((Op & 7) == 1 || (Op & 7) == 3)) || Op == 0x85) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Mn = (Op == 0x85) ? "test" : kAluName ((Op >> 3) & 7);
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? RegName (M & 7) : "[mem]";
            if (Op & 2) { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, RegName ((M >> 3) & 7), Rm); }
            else        { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, Rm, RegName ((M >> 3) & 7)); }
        } else if (Op < 0x40 && (Op & 7) == 5) {
            std::snprintf (pLine, MaxLine, "%s ax,0x%04x", kAluName ((Op >> 3) & 7), Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0x81 || Op == 0x83) {
            UINT8 M = m_pCode[Pc + 1];
            UINT16 Imm = (Op == 0x81) ? Imm16At (m_pCode, Pc + 1 + RmLen (M))
                                      : (UINT16) (INT16) (INT8) m_pCode[Pc + 1 + RmLen (M)];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? RegName (M & 7) : "[mem]";
            std::snprintf (pLine, MaxLine, "%s %s,0x%04x", kAluName ((M >> 3) & 7), Rm, Imm);
        } else if (Op == 0xC7) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? RegName (M & 7) : "[mem]";
            std::snprintf (pLine, MaxLine, "mov %s,0x%04x", Rm, Imm16At (m_pCode, Pc + 1 + RmLen (M)));
        } else if (Op == 0xA9) {
            std::snprintf (pLine, MaxLine, "test ax,0x%04x", Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0x90) { std::snprintf (pLine, MaxLine, "nop");
        } else if (Op == 0xF5) { std::snprintf (pLine, MaxLine, "cmc");
        } else if (Op == 0xF8) { std::snprintf (pLine, MaxLine, "clc");
        } else if (Op == 0xF9) { std::snprintf (pLine, MaxLine, "stc");
        } else if (Op == 0xFC) { std::snprintf (pLine, MaxLine, "cld");
        } else if (Op == 0xFD) { std::snprintf (pLine, MaxLine, "std");
        } else if (Op == 0x98) { std::snprintf (pLine, MaxLine, "cbw");
        } else if (Op == 0x99) { std::snprintf (pLine, MaxLine, "cwd");
        } else if (Op == 0x0F) {
            CHAR8 CONST *pMnem = "?1";
            switch (m_pCode[Pc + 1]) {
            case 0x19: pMnem = "test1"; break;
            case 0x1B: pMnem = "clr1";  break;
            case 0x1D: pMnem = "set1";  break;
            case 0x1F: pMnem = "not1";  break;
            }
            std::snprintf (pLine, MaxLine, "%s %s,%u", pMnem, RegName (m_pCode[Pc + 2] & 7), m_pCode[Pc + 3] & 15);
        } else {
            std::snprintf (pLine, MaxLine, "db 0x%02x", Op);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT8 Op = m_pCode[Pc];

        // 8080 emulation mode: the boundary ops (BRKEM/RETEM) only flip mode (handled in
        // TagInstr); the body is translated by the shared 8080 core.
        if (m_In8080 && Pc != m_BrkemPc) {
            if (Op == 0xED && m_pCode[Pc + 1] == 0xFD) { return S_OK; }   // RETEM: mode change only
            return m_pEmu->TranslateInstr (Pc, pE);
        }
        if (Op == 0x0F && (m_pCode[Pc + 1] == 0xFF || m_pCode[Pc + 1] == 0xED)) {
            return S_OK;                            // BRKEM / CALLN: emulation-mode switch, no data effect
        }

        if (Op >= 0xB8 && Op <= 0xBF) {            // MOV reg16, imm16
            ComPtr<ICpuValue> V; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &V);
            pE->PutRegister (Op - 0xB8, V, 16, FALSE);
            return S_OK;
        }
        if (Op >= 0xB0 && Op <= 0xB7) {            // MOV reg8, imm8
            // The register file holds 16-bit GPRs (AL/AH are halves of AX), so write the
            // byte by read-modify-write: clear the target half and OR the immediate in.
            UINT32 Enc  = Op - 0xB0;               // 0..7 -> AL,CL,DL,BL,AH,CH,DH,BH
            UINT32 Gpr  = Enc & 3;                 // AX,CX,DX,BX
            bool   High = Enc >= 4;
            UINT16 Imm  = m_pCode[Pc + 1];
            ComPtr<ICpuValue> Cur;  pE->GetRegister (Gpr, 16, &Cur);
            ComPtr<ICpuValue> Mask; pE->ConstInt (16, High ? 0x00FF : 0xFF00, &Mask);
            ComPtr<ICpuValue> Kept; pE->BinaryOp (BinAnd, Cur, Mask, &Kept);
            ComPtr<ICpuValue> ImmV; pE->ConstInt (16, High ? (UINT16) (Imm << 8) : Imm, &ImmV);
            ComPtr<ICpuValue> Res;  pE->BinaryOp (BinOr, Kept, ImmV, &Res);
            pE->PutRegister (Gpr, Res, 16, FALSE);
            return S_OK;
        }
        if (Op >= 0x40 && Op <= 0x47) {            // INC reg16
            UINT32 Reg = Op - 0x40;
            ComPtr<ICpuValue> A;   pE->GetRegister (Reg, 16, &A);
            ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinAdd, A, One, &Res);
            pE->PutRegister (Reg, Res, 16, FALSE);
            EmitIncDecOverflow (pE, A, 0x7FFF);    // INC overflows out of 0x7FFF
            EmitZSF (pE, Res);
            return S_OK;
        }
        if (Op >= 0x48 && Op <= 0x4F) {            // DEC reg16
            UINT32 Reg = Op - 0x48;
            ComPtr<ICpuValue> A;   pE->GetRegister (Reg, 16, &A);
            ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinSub, A, One, &Res);
            pE->PutRegister (Reg, Res, 16, FALSE);
            EmitIncDecOverflow (pE, A, 0x8000);    // DEC overflows out of 0x8000
            EmitZSF (pE, Res);
            return S_OK;
        }

        // PUSH/POP reg16 (implicit [SP]).
        if (Op >= 0x50 && Op <= 0x57) {                            // PUSH reg16 (SS:SP)
            UINT32 Reg = Op - 0x50;
            ComPtr<ICpuValue> SP;  pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
            ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
            pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
            ComPtr<ICpuValue> V;   pE->GetRegister (Reg, 16, &V);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20SS, NewSP, &Lin);
            pE->Store (V, Lin, 16);
            return S_OK;
        }
        if (Op >= 0x58 && Op <= 0x5F) {                            // POP reg16 (SS:SP)
            UINT32 Reg = Op - 0x58;
            ComPtr<ICpuValue> SP;  pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20SS, SP, &Lin);
            ComPtr<ICpuValue> V;   pE->Load (Lin, 16, &V);
            pE->PutRegister (Reg, V, 16, FALSE);
            ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
            ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinAdd, SP, Two, &NewSP);
            pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
            return S_OK;
        }

        switch (Op) {
        // The eight ALU ops (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP) in their r/m16,r16 and r16,r/m16
        // forms, plus MOV (0x89/0x8B). The op is the reg field of the opcode: (Op >> 3) & 7.
        case 0x01: case 0x03: case 0x09: case 0x0B: case 0x11: case 0x13:
        case 0x19: case 0x1B: case 0x21: case 0x23: case 0x29: case 0x2B:
        case 0x31: case 0x33: case 0x39: case 0x3B: case 0x89: case 0x8B: {
            UINT8  M       = m_pCode[Pc + 1];
            UINT32 Reg     = (M >> 3) & 7;
            bool   RegDest = (Op & 2) != 0;                        // direction bit
            ComPtr<ICpuValue> RegV; pE->GetRegister (Reg, 16, &RegV);
            ComPtr<ICpuValue> RmV;  EmitRmRead (pE, M, Pc + 1, &RmV);

            if (Op == 0x89) { EmitRmWrite (pE, M, Pc + 1, RegV); break; }     // MOV r/m, r
            if (Op == 0x8B) { pE->PutRegister (Reg, RmV, 16, FALSE); break; } // MOV r, r/m

            ICpuValue *pDst = RegDest ? (ICpuValue *) RegV : (ICpuValue *) RmV;
            ICpuValue *pSrc = RegDest ? (ICpuValue *) RmV : (ICpuValue *) RegV;
            UINT32 Alu = (Op >> 3) & 7;
            ComPtr<ICpuValue> Res; EmitAlu16 (pE, Alu, pDst, pSrc, &Res);
            if (Alu != 7) {                                        // CMP writes no result
                if (RegDest) { pE->PutRegister (Reg, Res, 16, FALSE); }
                else         { EmitRmWrite (pE, M, Pc + 1, Res); }
            }
            break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D:                // <alu> AX, imm16
        case 0x25: case 0x2D: case 0x35: case 0x3D: {
            ComPtr<ICpuValue> A; pE->GetRegister (RegV20AX, 16, &A);
            ComPtr<ICpuValue> B; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &B);
            UINT32 Alu = (Op >> 3) & 7;
            ComPtr<ICpuValue> Res; EmitAlu16 (pE, Alu, A, B, &Res);
            if (Alu != 7) { pE->PutRegister (RegV20AX, Res, 16, FALSE); }
            break;
        }
        case 0x81: case 0x83: {                                    // grp1: <alu> r/m16, imm
            UINT8  M   = m_pCode[Pc + 1];
            UINT32 Alu = (M >> 3) & 7;
            ComPtr<ICpuValue> Rm; EmitRmRead (pE, M, Pc + 1, &Rm);
            UINT16 Imm = (Op == 0x81) ? Imm16At (m_pCode, Pc + 1 + RmLen (M))
                                      : (UINT16) (INT16) (INT8) m_pCode[Pc + 1 + RmLen (M)];   // 0x83: sign-extend imm8
            ComPtr<ICpuValue> B; pE->ConstInt (16, Imm, &B);
            ComPtr<ICpuValue> Res; EmitAlu16 (pE, Alu, Rm, B, &Res);
            if (Alu != 7) { EmitRmWrite (pE, M, Pc + 1, Res); }
            break;
        }
        case 0xC7: {                                               // MOV r/m16, imm16
            UINT8  M   = m_pCode[Pc + 1];
            ComPtr<ICpuValue> V; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1 + RmLen (M)), &V);
            EmitRmWrite (pE, M, Pc + 1, V);
            break;
        }
        case 0x85: {                                               // TEST r/m16, r16 (no writeback)
            UINT8 M = m_pCode[Pc + 1];
            ComPtr<ICpuValue> RegV; pE->GetRegister ((M >> 3) & 7, 16, &RegV);
            ComPtr<ICpuValue> RmV;  EmitRmRead (pE, M, Pc + 1, &RmV);
            ComPtr<ICpuValue> Res;  EmitAlu16 (pE, 4 /*AND*/, RmV, RegV, &Res);
            break;
        }
        case 0xA9: {                                               // TEST AX, imm16
            ComPtr<ICpuValue> A; pE->GetRegister (RegV20AX, 16, &A);
            ComPtr<ICpuValue> B; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &B);
            ComPtr<ICpuValue> Res; EmitAlu16 (pE, 4 /*AND*/, A, B, &Res);
            break;
        }
        case 0x90: break;                                          // NOP
        case 0xF8: { ComPtr<ICpuValue> Z; pE->ConstInt (1, 0, &Z); pE->SetFlag (FlagCarry, Z); break; }   // CLC
        case 0xF9: { ComPtr<ICpuValue> O; pE->ConstInt (1, 1, &O); pE->SetFlag (FlagCarry, O); break; }   // STC
        case 0xF5: { ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C);                                    // CMC
                     ComPtr<ICpuValue> N; pE->UnaryOp (UnNot, C, &N); pE->SetFlag (FlagCarry, N); break; }
        case 0xFC: case 0xFD: break;                               // CLD/STD: no direction flag modelled yet
        case 0x98: {                                               // CBW: AX = sign-extend AL
            ComPtr<ICpuValue> Ax; pE->GetRegister (RegV20AX, 16, &Ax);
            ComPtr<ICpuValue> Al; pE->Cast (CastTrunc, Ax, 8, &Al);
            ComPtr<ICpuValue> Sx; pE->Cast (CastSExt, Al, 16, &Sx);
            pE->PutRegister (RegV20AX, Sx, 16, FALSE);
            break;
        }
        case 0x99: {                                               // CWD: DX = sign of AX (0 or 0xFFFF)
            ComPtr<ICpuValue> Ax;  pE->GetRegister (RegV20AX, 16, &Ax);
            ComPtr<ICpuValue> Sx;  pE->Cast (CastSExt, Ax, 32, &Sx);
            ComPtr<ICpuValue> Sh;  pE->ConstInt (32, 16, &Sh);
            ComPtr<ICpuValue> HiV; pE->BinaryOp (BinLShr, Sx, Sh, &HiV);
            ComPtr<ICpuValue> Dx;  pE->Cast (CastTrunc, HiV, 16, &Dx);
            pE->PutRegister (RegV20DX, Dx, 16, FALSE);
            break;
        }
        case 0xA1: {                                               // MOV AX, [addr16] (DS-relative)
            ComPtr<ICpuValue> Ad;  pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, Ad, &Lin);
            ComPtr<ICpuValue> V;   pE->Load (Lin, 16, &V);
            pE->PutRegister (RegV20AX, V, 16, FALSE);
            break;
        }
        case 0xA3: {                                               // MOV [addr16], AX (DS-relative)
            ComPtr<ICpuValue> V;   pE->GetRegister (RegV20AX, 16, &V);
            ComPtr<ICpuValue> Ad;  pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, Ad, &Lin);
            pE->Store (V, Lin, 16);
            break;
        }
        case 0x8E: case 0x8C: {                                    // MOV sreg,r/m16 / MOV r/m16,sreg
            UINT8  M    = m_pCode[Pc + 1];
            UINT32 Sreg = RegV20ES + ((M >> 3) & 3);               // sreg field: ES,CS,SS,DS
            if (Op == 0x8E) {                                      // load segment register
                ComPtr<ICpuValue> S; EmitRmRead (pE, M, Pc + 1, &S);
                pE->PutRegister (Sreg, S, 16, FALSE);
            } else {                                               // store segment register
                ComPtr<ICpuValue> S; pE->GetRegister (Sreg, 16, &S);
                EmitRmWrite (pE, M, Pc + 1, S);
            }
            break;
        }
        case 0xE2: {                                               // LOOP: CX-- (no flags)
            ComPtr<ICpuValue> C;   pE->GetRegister (RegV20CX, 16, &C);
            ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinSub, C, One, &Res);
            pE->PutRegister (RegV20CX, Res, 16, FALSE);
            break;
        }
        case 0xE8: {                                               // CALL rel16: push return addr (SS:SP)
            ComPtr<ICpuValue> SP;    pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> Two;   pE->ConstInt (16, 2, &Two);
            ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
            pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
            ComPtr<ICpuValue> Ret;   pE->ConstInt (16, (UINT16) (Pc + 3), &Ret);   // address after CALL
            ComPtr<ICpuValue> Lin;   EmitSegLinear (pE, RegV20SS, NewSP, &Lin);
            pE->Store (Ret, Lin, 16);
            break;                                                 // driver branches to the callee (TagCall)
        }
        case 0xC3: {                                               // RET: pop target (SS:SP), indirect branch
            ComPtr<ICpuValue> SP;    pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> Lin;   EmitSegLinear (pE, RegV20SS, SP, &Lin);
            ComPtr<ICpuValue> T;     pE->Load (Lin, 16, &T);
            ComPtr<ICpuValue> Two;   pE->ConstInt (16, 2, &Two);
            ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinAdd, SP, Two, &NewSP);
            pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
            ICpuSmcEmitter *pFlow = nullptr;                       // indirect-dispatch capability (optional)
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                pFlow->SetDispatchTarget (T);   // driver routes the TagReturn block to the dispatcher
                pFlow->Release ();
            }
            break;
        }
        case 0xEA: {                                               // JMP ptr16:16 (far)
            // Load CS:IP and trap to the host, which re-translates the new segment.
            UINT16 NewIp = Imm16At (m_pCode, Pc + 1);
            UINT16 NewCs = Imm16At (m_pCode, Pc + 3);
            ComPtr<ICpuValue> Cs; pE->ConstInt (16, NewCs, &Cs);
            pE->PutRegister (RegV20CS, Cs, 16, FALSE);             // guest CS = NewCs
            ICpuSmcEmitter *pFlow = nullptr;
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                ComPtr<ICpuValue> Ip; pE->ConstInt (16, NewIp, &Ip);
                pFlow->IndirectBranch (Ip);   // TrapPc = NewIp; host resumes there (in NewCs)
                pFlow->Release ();
            }
            break;
        }
        case 0x9A: {                                               // CALL ptr16:16 (far)
            // Push the return CS:IP (8086 order: CS first, then IP), reload CS:IP, and
            // trap to the host, which re-translates the callee's segment. A matching
            // RETF pops CS:IP and traps back.
            UINT16 NewIp = Imm16At (m_pCode, Pc + 1);
            UINT16 NewCs = Imm16At (m_pCode, Pc + 3);
            ComPtr<ICpuValue> SP;    pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> Two;   pE->ConstInt (16, 2, &Two);
            // push CS: SP -= 2; [SS:SP] = CS
            ComPtr<ICpuValue> Sp1;   pE->BinaryOp (BinSub, SP, Two, &Sp1);
            ComPtr<ICpuValue> CurCs; pE->GetRegister (RegV20CS, 16, &CurCs);
            ComPtr<ICpuValue> LinCs; EmitSegLinear (pE, RegV20SS, Sp1, &LinCs);
            pE->Store (CurCs, LinCs, 16);
            // push IP: SP -= 2; [SS:SP] = return offset (after the 5-byte CALL)
            ComPtr<ICpuValue> Sp2;   pE->BinaryOp (BinSub, Sp1, Two, &Sp2);
            pE->PutRegister (RegV20SP, Sp2, 16, FALSE);
            ComPtr<ICpuValue> Ret;   pE->ConstInt (16, (UINT16) (Pc + 5), &Ret);
            ComPtr<ICpuValue> LinIp; EmitSegLinear (pE, RegV20SS, Sp2, &LinIp);
            pE->Store (Ret, LinIp, 16);
            // reload CS and trap to the host at NewIp (in NewCs)
            ComPtr<ICpuValue> Cs;    pE->ConstInt (16, NewCs, &Cs);
            pE->PutRegister (RegV20CS, Cs, 16, FALSE);
            ICpuSmcEmitter *pFlow = nullptr;
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                ComPtr<ICpuValue> Ip; pE->ConstInt (16, NewIp, &Ip);
                pFlow->IndirectBranch (Ip);
                pFlow->Release ();
            }
            break;
        }
        case 0xCB: {                                               // RETF (far return)
            // Pop IP then CS (8086 order), set guest CS = popped CS, and trap to the
            // host at the popped IP -- the return crosses segments, so the in-artifact
            // dispatcher (single CS) cannot route it; the host reconfigures CS.
            ComPtr<ICpuValue> SP;    pE->GetRegister (RegV20SP, 16, &SP);
            ComPtr<ICpuValue> LinIp; EmitSegLinear (pE, RegV20SS, SP, &LinIp);
            ComPtr<ICpuValue> Ip;    pE->Load (LinIp, 16, &Ip);    // IP = [SS:SP]
            ComPtr<ICpuValue> Two;   pE->ConstInt (16, 2, &Two);
            ComPtr<ICpuValue> Sp1;   pE->BinaryOp (BinAdd, SP, Two, &Sp1);
            ComPtr<ICpuValue> LinCs; EmitSegLinear (pE, RegV20SS, Sp1, &LinCs);
            ComPtr<ICpuValue> Cs;    pE->Load (LinCs, 16, &Cs);    // CS = [SS:SP+2]
            ComPtr<ICpuValue> Four;  pE->ConstInt (16, 4, &Four);
            ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinAdd, SP, Four, &NewSP);
            pE->PutRegister (RegV20SP, NewSP, 16, FALSE);          // SP += 4
            pE->PutRegister (RegV20CS, Cs, 16, FALSE);             // guest CS = popped CS
            ICpuSmcEmitter *pFlow = nullptr;
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                pFlow->IndirectBranch (Ip);   // TrapPc = popped IP; host resumes there (in popped CS)
                pFlow->Release ();
            }
            break;
        }
        case 0xCD: {                                               // INT imm8 (software interrupt)
            // Record the interrupt vector and trap to the instruction after the INT;
            // the host's knowledge-library dispatcher performs the native call and
            // resumes. Requires a syscall-capable backend (e.g. the interpreter); on
            // one without it the INT is inert (treated as a no-op trap target).
            UINT8 Vector = m_pCode[Pc + 1];
            ICpuSyscallEmitter *pSys = nullptr;
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSyscallEmitter, (VOID **) &pSys)) && pSys != nullptr) {
                ComPtr<ICpuValue> Ret; pE->ConstInt (16, (UINT16) (Pc + 2), &Ret);
                pSys->EmitSyscall (Vector, Ret);
                pSys->Release ();
            }
            break;
        }
        case 0xE4: case 0xE6: case 0xEC: case 0xEE:                // port I/O (device bus)
        case 0xCF: case 0xF4: case 0xFA: case 0xFB: {              // IRET / HLT / CLI / STI
            ICpuSystemEmitter *pSysm = nullptr;
            if (FAILED (pE->QueryInterface (IID_ICpuSystemEmitter, (VOID **) &pSysm)) || pSysm == nullptr) {
                break;                                             // backend is not system-capable
            }
            bool Imm8Port = (Op == 0xE4 || Op == 0xE6);            // port in imm8 vs DX
            UINT16 Len = (Imm8Port ? 2 : 1);
            ComPtr<ICpuValue> Ret; pE->ConstInt (16, (UINT16) (Pc + Len), &Ret);
            ComPtr<ICpuValue> Port;
            if (Imm8Port) { pE->ConstInt (16, m_pCode[Pc + 1], &Port); }
            else          { pE->GetRegister (RegV20DX, 16, &Port); }
            if (Op == 0xE6 || Op == 0xEE) {                        // OUT port, AL
                ComPtr<ICpuValue> Data; pE->GetRegister (RegV20AX, 16, &Data);
                pSysm->EmitPortOut (Port, Data, 8, Ret);
            } else if (Op == 0xE4 || Op == 0xEC) {                 // IN AL, port
                pSysm->EmitPortIn (Port, 8, Ret);
            } else {                                               // IRET/HLT/CLI/STI
                UINT32 Reason = (Op == 0xCF) ? (UINT32) CPU_IO_IRET :
                                (Op == 0xF4) ? (UINT32) CPU_IO_HLT  :
                                (Op == 0xFA) ? (UINT32) CPU_IO_CLI  : (UINT32) CPU_IO_STI;
                pSysm->EmitSystemTrap (Reason, Ret);
            }
            pSysm->Release ();
            break;
        }
        case 0x0F: {                                               // NEC SET1/CLR1/NOT1/TEST1 r/m16,imm8
            UINT8  Op2 = m_pCode[Pc + 1];
            UINT32 Rm  = m_pCode[Pc + 2] & 7;
            UINT32 Bit = m_pCode[Pc + 3] & 15;
            ComPtr<ICpuValue> R;    pE->GetRegister (Rm, 16, &R);
            ComPtr<ICpuValue> Mask; pE->ConstInt (16, (UINT16) (1u << Bit), &Mask);
            ComPtr<ICpuValue> Res;
            switch (Op2) {
            case 0x1D: pE->BinaryOp (BinOr,  R, Mask, &Res); pE->PutRegister (Rm, Res, 16, FALSE); break;  // SET1
            case 0x1F: pE->BinaryOp (BinXor, R, Mask, &Res); pE->PutRegister (Rm, Res, 16, FALSE); break;  // NOT1
            case 0x1B: {                                                                                   // CLR1
                ComPtr<ICpuValue> NMask; pE->ConstInt (16, (UINT16) (~(1u << Bit)), &NMask);
                pE->BinaryOp (BinAnd, R, NMask, &Res);
                pE->PutRegister (Rm, Res, 16, FALSE);
                break;
            }
            case 0x19: {                                                                                   // TEST1 -> ZF
                ComPtr<ICpuValue> T;    pE->BinaryOp (BinAnd, R, Mask, &T);
                ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
                ComPtr<ICpuValue> ZF;   pE->Compare (CmpEq, T, Zero, &ZF);
                pE->SetFlag (FlagZero, ZF);
                break;
            }
            }
            break;
        }
        default:
            // Jcc / JMP have no data effect; the branch is wired from TagInstr +
            // TranslateCond. Anything else in this slice is a no-op.
            break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        if (m_In8080 && Pc != m_BrkemPc) { return m_pEmu->TranslateCond (Pc, pE, ppCond); }
        UINT8 Op = m_pCode[Pc];
        switch (Op) {
        case 0x74:                                  // JZ/JE: taken if ZF == 1
            return pE->GetFlag (FlagZero, ppCond);
        case 0x75: {                                // JNZ/JNE: ZF == 0
            ComPtr<ICpuValue> Z; pE->GetFlag (FlagZero, &Z);
            return pE->UnaryOp (UnNot, Z, ppCond);
        }
        case 0x72:                                  // JB/JC: CF == 1
            return pE->GetFlag (FlagCarry, ppCond);
        case 0x73: {                                // JNB/JNC: CF == 0
            ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C);
            return pE->UnaryOp (UnNot, C, ppCond);
        }
        case 0x70:                                  // JO: OF == 1
            return pE->GetFlag (FlagOverflow, ppCond);
        case 0x71: {                                // JNO: OF == 0
            ComPtr<ICpuValue> O; pE->GetFlag (FlagOverflow, &O);
            return pE->UnaryOp (UnNot, O, ppCond);
        }
        case 0x78:                                  // JS: SF == 1
            return pE->GetFlag (FlagNegative, ppCond);
        case 0x79: {                                // JNS: SF == 0
            ComPtr<ICpuValue> S; pE->GetFlag (FlagNegative, &S);
            return pE->UnaryOp (UnNot, S, ppCond);
        }
        case 0x7A:                                  // JP/JPE: PF == 1
            return pE->GetFlag (FlagParity, ppCond);
        case 0x7B: {                                // JNP/JPO: PF == 0
            ComPtr<ICpuValue> P; pE->GetFlag (FlagParity, &P);
            return pE->UnaryOp (UnNot, P, ppCond);
        }
        case 0x76: {                                // JBE/JNA: CF || ZF
            ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C);
            ComPtr<ICpuValue> Z; pE->GetFlag (FlagZero, &Z);
            return pE->BinaryOp (BinOr, C, Z, ppCond);
        }
        case 0x77: {                                // JA/JNBE: !CF && !ZF
            ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C);
            ComPtr<ICpuValue> Z; pE->GetFlag (FlagZero, &Z);
            ComPtr<ICpuValue> Or; pE->BinaryOp (BinOr, C, Z, &Or);
            return pE->UnaryOp (UnNot, Or, ppCond);
        }
        case 0x7C: {                                // JL: SF != OF
            ComPtr<ICpuValue> S; pE->GetFlag (FlagNegative, &S);
            ComPtr<ICpuValue> O; pE->GetFlag (FlagOverflow, &O);
            return pE->Compare (CmpNe, S, O, ppCond);
        }
        case 0x7D: {                                // JGE: SF == OF
            ComPtr<ICpuValue> S; pE->GetFlag (FlagNegative, &S);
            ComPtr<ICpuValue> O; pE->GetFlag (FlagOverflow, &O);
            return pE->Compare (CmpEq, S, O, ppCond);
        }
        case 0x7E: {                                // JLE: ZF || (SF != OF)
            ComPtr<ICpuValue> Z; pE->GetFlag (FlagZero, &Z);
            ComPtr<ICpuValue> S; pE->GetFlag (FlagNegative, &S);
            ComPtr<ICpuValue> O; pE->GetFlag (FlagOverflow, &O);
            ComPtr<ICpuValue> Ne; pE->Compare (CmpNe, S, O, &Ne);
            return pE->BinaryOp (BinOr, Z, Ne, ppCond);
        }
        case 0x7F: {                                // JG: !ZF && (SF == OF)
            ComPtr<ICpuValue> Z;  pE->GetFlag (FlagZero, &Z);
            ComPtr<ICpuValue> NZ; pE->UnaryOp (UnNot, Z, &NZ);
            ComPtr<ICpuValue> S;  pE->GetFlag (FlagNegative, &S);
            ComPtr<ICpuValue> O;  pE->GetFlag (FlagOverflow, &O);
            ComPtr<ICpuValue> Eq; pE->Compare (CmpEq, S, O, &Eq);
            return pE->BinaryOp (BinAnd, NZ, Eq, ppCond);
        }
        case 0xE2: {                                // LOOP: taken if CX != 0 (CX already decremented)
            ComPtr<ICpuValue> C;    pE->GetRegister (RegV20CX, 16, &C);
            ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
            return pE->Compare (CmpNe, C, Zero, ppCond);
        }
        default:
            return E_NOTIMPL;                       // unimplemented Jcc: driver falls through
        }
    }

private:
    // Bytes occupied by a ModR/M byte + its displacement.
    static UINT32 RmLen (UINT8 M) {
        UINT8 Mod = (UINT8) (M >> 6), Rm = (UINT8) (M & 7);
        if (Mod == 3) { return 1; }                  // register-direct: ModR/M only
        if (Mod == 0) { return (Rm == 6) ? 3 : 1; }  // [disp16] carries 2 bytes
        if (Mod == 1) { return 2; }                  // disp8
        return 3;                                    // mod==2: disp16
    }

    // Emit the 16-bit effective address for a memory ModR/M (mod != 3). DispPc points
    // at the displacement bytes (one past the ModR/M byte). Standard 8086 modes:
    //   rm 0..7 = [BX+SI] [BX+DI] [BP+SI] [BP+DI] [SI] [DI] [BP or disp16] [BX]
    VOID EmitEA (ICpuEmitter *pE, UINT8 M, CPU_ADDR DispPc, ICpuValue **ppEA) {
        UINT8 Mod = (UINT8) (M >> 6), Rm = (UINT8) (M & 7);
        if (Mod == 0 && Rm == 6) {
            pE->ConstInt (16, Imm16At (m_pCode, DispPc), ppEA);   // [disp16]
            return;
        }
        static CONST INT8 kBase[8][2] = {
            { RegV20BX, RegV20SI }, { RegV20BX, RegV20DI }, { RegV20BP, RegV20SI }, { RegV20BP, RegV20DI },
            { RegV20SI, -1 },       { RegV20DI, -1 },       { RegV20BP, -1 },       { RegV20BX, -1 }
        };
        ICpuValue *pAcc = nullptr;
        pE->GetRegister ((UINT32) kBase[Rm][0], 16, &pAcc);
        if (kBase[Rm][1] >= 0) {
            ComPtr<ICpuValue> Idx; pE->GetRegister ((UINT32) kBase[Rm][1], 16, &Idx);
            ICpuValue *pSum = nullptr; pE->BinaryOp (BinAdd, pAcc, Idx, &pSum);
            pAcc->Release (); pAcc = pSum;
        }
        if (Mod == 1 || Mod == 2) {
            UINT16 Disp = (Mod == 1) ? (UINT16) (INT16) (INT8) m_pCode[DispPc] : Imm16At (m_pCode, DispPc);
            ComPtr<ICpuValue> D; pE->ConstInt (16, Disp, &D);
            ICpuValue *pSum = nullptr; pE->BinaryOp (BinAdd, pAcc, D, &pSum);
            pAcc->Release (); pAcc = pSum;
        }
        *ppEA = pAcc;   // ownership to caller
    }

    // Read / write the r/m operand: a register (mod=11) or memory at the EA.
    // Form the 20-bit linear address SegReg * 16 + (offset & 0xFFFF), as a 32-bit
    // value. With the segment register 0 this is just the offset (unsegmented).
    VOID EmitSegLinear (ICpuEmitter *pE, UINT32 SegReg, ICpuValue *pOffset, ICpuValue **ppLinear) {
        ComPtr<ICpuValue> Seg;   pE->GetRegister (SegReg, 16, &Seg);
        ComPtr<ICpuValue> Seg32; pE->Cast (CastZExt, Seg, 32, &Seg32);
        ComPtr<ICpuValue> Four;  pE->ConstInt (32, 4, &Four);
        ComPtr<ICpuValue> Base;  pE->BinaryOp (BinShl, Seg32, Four, &Base);     // seg << 4
        ComPtr<ICpuValue> Off32; pE->Cast (CastZExt, pOffset, 32, &Off32);
        pE->BinaryOp (BinAdd, Base, Off32, ppLinear);                          // + offset
    }

    // Read / write the r/m operand: a register (mod=11) or DS-relative memory at the EA.
    VOID EmitRmRead (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue **ppValue) {
        if ((M >> 6) == 3) {
            pE->GetRegister ((UINT32) (M & 7), 16, ppValue);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, EA, &Lin);
            pE->Load (Lin, 16, ppValue);
        }
    }
    VOID EmitRmWrite (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue *pValue) {
        if ((M >> 6) == 3) {
            pE->PutRegister ((UINT32) (M & 7), pValue, 16, FALSE);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, EA, &Lin);
            pE->Store (pValue, Lin, 16);
        }
    }

    // INC/DEC overflow: OF is set when the operand was at the signed limit (INC out
    // of 0x7FFF, DEC out of 0x8000). CF is left untouched (per 8086 INC/DEC).
    static VOID EmitIncDecOverflow (ICpuEmitter *pE, ICpuValue *pA, UINT16 Limit) {
        ComPtr<ICpuValue> Lim; pE->ConstInt (16, Limit, &Lim);
        ComPtr<ICpuValue> OF;  pE->Compare (CmpEq, pA, Lim, &OF);
        pE->SetFlag (FlagOverflow, OF);
    }

    static CHAR8 CONST *RegName (UINT32 Index) {
        static CHAR8 CONST *kNames[8] = { "ax", "cx", "dx", "bx", "sp", "bp", "si", "di" };
        return kNames[Index & 7];
    }

    static CHAR8 CONST *kAluName (UINT32 Index) {
        static CHAR8 CONST *kNames[8] = { "add", "or", "adc", "sbb", "and", "sub", "xor", "cmp" };
        return kNames[Index & 7];
    }

    // 8-bit register encoding (B0+r, ModR/M reg field in byte ops): the four GPR low bytes
    // then the four high bytes -- AL,CL,DL,BL,AH,CH,DH,BH. Index 0..3 is the GPR (AX..BX);
    // index >= 4 selects its high byte.
    static CHAR8 CONST *Reg8Name (UINT32 Index) {
        static CHAR8 CONST *kNames[8] = { "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh" };
        return kNames[Index & 7];
    }

    UINT8 CONST *m_pBase    = nullptr;   // RAM base (for re-biasing on a CS change)
    UINT8 CONST *m_pCode    = nullptr;   // = m_pBase + m_CodeSeg * 16
    UINT64       m_CodeSize = 0;
    UINT16       m_CodeSeg  = 0;   // CS: code is fetched from m_CodeSeg * 16 + IP
    bool         m_IsV30    = false;

    // 8080 emulation mode (NEC V20/V30 BRKEM/RETEM): while active, decoding/translation is
    // delegated to a shared Intel 8080 core, so the 8080 instruction set is implemented once.
    ICpuArchitecture *m_pEmu    = nullptr;
    bool              m_In8080  = false;
    CPU_ADDR          m_BrkemPc = (CPU_ADDR) -1;   // address of the BRKEM that entered 8080 mode
};

} // anonymous namespace

ICpuArchitecture *
CreateV20 (UINT16 CodeSeg)
{
    return new CpuV20 (CodeSeg);
}

ICpuArchitecture *
CreateV20 (VOID)
{
    return new CpuV20 (0);
}

ICpuArchitecture *
CreateV30 (UINT16 CodeSeg)
{
    return new CpuV20 (CodeSeg, /*IsV30=*/ true);
}

ICpuArchitecture *
CreateV30 (VOID)
{
    return new CpuV20 (0, /*IsV30=*/ true);
}

VOID
SetV20CodeSegment (ICpuArchitecture *pArch, UINT16 Cs)
{
    static_cast<CpuV20 *> (pArch)->SetCodeSeg (Cs);
}

} // namespace LibCPU
