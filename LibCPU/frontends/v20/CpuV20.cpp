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

// Z (zero), S (bit 7) and P (parity) for an 8-bit result.
static VOID
EmitZSF8 (ICpuEmitter *pE, ICpuValue *pRes)
{
    ComPtr<ICpuValue> Zero; pE->ConstInt (8, 0, &Zero);
    ComPtr<ICpuValue> ZF;   pE->Compare (CmpEq, pRes, Zero, &ZF); pE->SetFlag (FlagZero, ZF);
    ComPtr<ICpuValue> B7;   pE->ConstInt (8, 0x80, &B7);
    ComPtr<ICpuValue> M;    pE->BinaryOp (BinAnd, pRes, B7, &M);
    ComPtr<ICpuValue> SF;   pE->Compare (CmpNe, M, Zero, &SF); pE->SetFlag (FlagNegative, SF);
    ComPtr<ICpuValue> R16;  pE->Cast (CastZExt, pRes, 16, &R16);
    EmitParity (pE, R16);
}

// 8-bit ALU: Res = Dst <op> Src with full flags (op encoding as for EmitAlu16). Result owned in
// *ppRes; caller writes it back except for CMP (7).
static VOID
EmitAlu8 (ICpuEmitter *pE, UINT32 AluOp, ICpuValue *pDst, ICpuValue *pSrc, ICpuValue **ppRes)
{
    if (AluOp == 1 || AluOp == 4 || AluOp == 6) {                  // OR / AND / XOR
        CPU_BINOP B = (AluOp == 4) ? BinAnd : (AluOp == 6) ? BinXor : BinOr;
        pE->BinaryOp (B, pDst, pSrc, ppRes);
        ComPtr<ICpuValue> Z1; pE->ConstInt (1, 0, &Z1); pE->SetFlag (FlagCarry, Z1);
        ComPtr<ICpuValue> Z2; pE->ConstInt (1, 0, &Z2); pE->SetFlag (FlagOverflow, Z2);
        EmitZSF8 (pE, *ppRes);
        return;
    }
    bool Sub = (AluOp == 3 || AluOp == 5 || AluOp == 7);
    bool Cin = (AluOp == 2 || AluOp == 3);
    ComPtr<ICpuValue> D16; pE->Cast (CastZExt, pDst, 16, &D16);
    ComPtr<ICpuValue> S16; pE->Cast (CastZExt, pSrc, 16, &S16);
    ComPtr<ICpuValue> Acc; pE->BinaryOp (Sub ? BinSub : BinAdd, D16, S16, &Acc);
    ComPtr<ICpuValue> Carried;
    ICpuValue *Wide = Acc;
    if (Cin) {
        ComPtr<ICpuValue> C;   pE->GetFlag (FlagCarry, &C);
        ComPtr<ICpuValue> C16; pE->Cast (CastZExt, C, 16, &C16);
        pE->BinaryOp (Sub ? BinSub : BinAdd, Acc, C16, &Carried);
        Wide = Carried;
    }
    pE->Cast (CastTrunc, Wide, 8, ppRes);
    ICpuValue *Res = *ppRes;
    ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
    ComPtr<ICpuValue> Hi; pE->BinaryOp (BinLShr, Wide, Sh, &Hi);
    ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Hi, 1, &CF);
    pE->SetFlag (FlagCarry, CF);
    ComPtr<ICpuValue> DxS; pE->BinaryOp (BinXor, pDst, pSrc, &DxS);
    ComPtr<ICpuValue> DxR; pE->BinaryOp (BinXor, pDst, Res, &DxR);
    ComPtr<ICpuValue> T1;
    if (Sub) { pE->BinaryOp (BinAnd, DxS, DxR, &T1); }
    else { ComPtr<ICpuValue> N; pE->UnaryOp (UnCom, DxS, &N); pE->BinaryOp (BinAnd, N, DxR, &T1); }
    ComPtr<ICpuValue> B7;   pE->ConstInt (8, 0x80, &B7);
    ComPtr<ICpuValue> T2;   pE->BinaryOp (BinAnd, T1, B7, &T2);
    ComPtr<ICpuValue> Zero; pE->ConstInt (8, 0, &Zero);
    ComPtr<ICpuValue> OF;   pE->Compare (CmpNe, T2, Zero, &OF);
    pE->SetFlag (FlagOverflow, OF);
    EmitZSF8 (pE, Res);
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
        // A segment-override prefix is one byte; the length and classification come from the
        // instruction it prefixes.
        if (Op == 0x26 || Op == 0x2E || Op == 0x36 || Op == 0x3E) {
            return TagInstr (Pc + 1, pTag, pNewPc, pNextPc);
        }

        if (Op >= 0xB8 && Op <= 0xBF) {                          // MOV reg16,imm16
            Len = 3;
        } else if (Op >= 0xB0 && Op <= 0xB7) {                   // MOV reg8,imm8
            Len = 2;
        } else if (Op >= 0x40 && Op <= 0x4F) {                   // INC/DEC reg16
            Len = 1;
        } else if (Op >= 0x50 && Op <= 0x5F) {                   // PUSH/POP reg16
            Len = 1;
        } else if ((Op < 0x40 && (Op & 7) <= 3) ||              // ALU r/m,r (8- and 16-bit, both dirs)
                   Op == 0x84 || Op == 0x85 || Op == 0x86 || Op == 0x87 || Op == 0x88 || Op == 0x89 ||
                   Op == 0x8A || Op == 0x8B || Op == 0x8C || Op == 0x8D || Op == 0x8E ||
                   Op == 0xC4 || Op == 0xC5) {
            Len = 1 + RmLen (m_pCode[Pc + 1]);                   // ALU/TEST/MOV/XCHG/LEA/LDS/LES r/m (+disp)
        } else if (Op == 0xD0 || Op == 0xD1 || Op == 0xD2 || Op == 0xD3) {   // grp2 shift r/m,1 or r/m,CL
            Len = 1 + RmLen (m_pCode[Pc + 1]);
        } else if (Op == 0x80 || Op == 0x83 || Op == 0xC6 || Op == 0xC0 || Op == 0xC1) {
            Len = 1 + RmLen (m_pCode[Pc + 1]) + 1;           // grp1 r/m,imm8 / MOV r/m8,imm8 / grp2 r/m,imm8
        } else if (Op == 0x81 || Op == 0xC7) {                   // grp1 r/m16,imm16 / MOV r/m16,imm16
            Len = 1 + RmLen (m_pCode[Pc + 1]) + 2;
        } else if ((Op < 0x40 && (Op & 7) == 4) || Op == 0xA8) {
            Len = 2;                                             // ALU AL,imm8 / TEST AL,imm8
        } else if (Op == 0xF6) {                                 // grp3 byte; /0,/1 carry an imm8
            UINT8 M = m_pCode[Pc + 1]; Len = 1 + RmLen (M) + (((M >> 3) & 7) <= 1 ? 1 : 0);
        } else if (Op == 0xF7) {                                 // grp3 word; /0,/1 carry an imm16
            UINT8 M = m_pCode[Pc + 1]; Len = 1 + RmLen (M) + (((M >> 3) & 7) <= 1 ? 2 : 0);
        } else if (Op == 0xFE) {                                 // grp4 INC/DEC r/m8
            Len = 1 + RmLen (m_pCode[Pc + 1]);
        } else if (Op == 0xFF) {                                 // grp5 INC/DEC/CALL/JMP/PUSH r/m16
            UINT8 M = m_pCode[Pc + 1]; UINT8 Sub = (M >> 3) & 7;
            Len = 1 + RmLen (M);
            if (Sub == 2 || Sub == 4) { Tag = TagReturn; }       // near indirect CALL/JMP -> dispatcher
            else if (Sub == 3 || Sub == 5) { Tag = TagTrap; }    // far indirect CALL/JMP
        } else if ((Op == 0xF2 || Op == 0xF3) && IsStringOp (m_pCode[Pc + 1])) {
            Len = 2;                                             // REP/REPNE prefix + string op
        } else if ((Op < 0x40 && (Op & 7) == 5) || Op == 0xA0 || Op == 0xA1 ||
                   Op == 0xA2 || Op == 0xA3 || Op == 0xA9) {
            Len = 3;                                             // acc,imm16 / MOV AL|AX,[addr16] / MOV [addr16],AL|AX / TEST AX,imm16
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
            UINT8 Mrm = m_pCode[Pc + 2];
            if (Sub == 0xFF) {                                   // BRKEM imm8: enter 8080 emulation mode
                Len = 3; m_In8080 = true; m_BrkemPc = Pc;
            } else if (Sub == 0xED) {                            // CALLN imm8: native call from 8080 mode
                Len = 3;
            } else if (Sub == 0x10 || Sub == 0x12 || Sub == 0x14 || Sub == 0x16 ||   // bit r/m,CL
                       Sub == 0x28 || Sub == 0x2A || Sub == 0x31 || Sub == 0x33) {   // ROL4/ROR4, INS/EXT
                Len = 2 + RmLen (Mrm);
            } else if (Sub == 0x18 || Sub == 0x1A || Sub == 0x1C || Sub == 0x1E ||   // bit r/m,imm8
                       Sub == 0x39 || Sub == 0x3B) {                                  // INS/EXT reg,imm
                Len = 2 + RmLen (Mrm) + 1;
            } else if (Sub == 0x20 || Sub == 0x22 || Sub == 0x26) {                  // ADD4S/SUB4S/CMP4S
                Len = 2;
            } else {
                Len = 4;                                         // legacy bit op: 0F xx modrm ib
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
        if (Op == 0x26 || Op == 0x2E || Op == 0x36 || Op == 0x3E) {
            CHAR8 CONST *Seg = (Op == 0x26) ? "es" : (Op == 0x2E) ? "cs" : (Op == 0x36) ? "ss" : "ds";
            CHAR8 Inner[80]; Disassemble (Pc + 1, Inner, sizeof (Inner));
            std::snprintf (pLine, MaxLine, "%s: %s", Seg, Inner);
            return S_OK;
        }
        if ((Op == 0xF2 || Op == 0xF3) && IsStringOp (m_pCode[Pc + 1])) {
            CHAR8 Inner[40]; Disassemble (Pc + 1, Inner, sizeof (Inner));
            std::snprintf (pLine, MaxLine, "%s %s", (Op == 0xF3) ? "rep" : "repne", Inner);
            return S_OK;
        }
        if ((Op >= 0xA4 && Op <= 0xA7) || (Op >= 0xAA && Op <= 0xAF)) {
            static CHAR8 CONST *kStr[16] = { "","","","","movs","movs","cmps","cmps",
                                             "","","stos","stos","lods","lods","scas","scas" };
            std::snprintf (pLine, MaxLine, "%s%c", kStr[Op - 0xA0], (Op & 1) ? 'w' : 'b');
            return S_OK;
        }
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
        } else if (Op == 0xA0) {
            std::snprintf (pLine, MaxLine, "mov al,[0x%04x]", Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0xA1) {
            std::snprintf (pLine, MaxLine, "mov ax,[0x%04x]", Imm16At (m_pCode, Pc + 1));
        } else if (Op == 0xA2) {
            std::snprintf (pLine, MaxLine, "mov [0x%04x],al", Imm16At (m_pCode, Pc + 1));
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
        } else if ((Op < 0x40 && ((Op & 7) == 0 || (Op & 7) == 2)) || Op == 0x84 || Op == 0x88 || Op == 0x8A) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Mn = (Op == 0x84) ? "test" : (Op == 0x88 || Op == 0x8A) ? "mov" : kAluName ((Op >> 3) & 7);
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? Reg8Name (M & 7) : "[mem]";
            if (Op & 2) { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, Reg8Name ((M >> 3) & 7), Rm); }
            else        { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, Rm, Reg8Name ((M >> 3) & 7)); }
        } else if ((Op < 0x40 && ((Op & 7) == 1 || (Op & 7) == 3)) || Op == 0x85 || Op == 0x89 || Op == 0x8B) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Mn = (Op == 0x85) ? "test" : (Op == 0x89 || Op == 0x8B) ? "mov" : kAluName ((Op >> 3) & 7);
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? RegName (M & 7) : "[mem]";
            if (Op & 2) { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, RegName ((M >> 3) & 7), Rm); }
            else        { std::snprintf (pLine, MaxLine, "%s %s,%s", Mn, Rm, RegName ((M >> 3) & 7)); }
        } else if (Op < 0x40 && (Op & 7) == 4) {
            std::snprintf (pLine, MaxLine, "%s al,0x%02x", kAluName ((Op >> 3) & 7), m_pCode[Pc + 1]);
        } else if (Op == 0xA8) {
            std::snprintf (pLine, MaxLine, "test al,0x%02x", m_pCode[Pc + 1]);
        } else if (Op == 0x80) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? Reg8Name (M & 7) : "[mem]";
            std::snprintf (pLine, MaxLine, "%s %s,0x%02x", kAluName ((M >> 3) & 7), Rm, m_pCode[Pc + 1 + RmLen (M)]);
        } else if (Op == 0xC6) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? Reg8Name (M & 7) : "[mem]";
            std::snprintf (pLine, MaxLine, "mov %s,0x%02x", Rm, m_pCode[Pc + 1 + RmLen (M)]);
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
        } else if (Op == 0x9E) { std::snprintf (pLine, MaxLine, "sahf");
        } else if (Op == 0x9F) { std::snprintf (pLine, MaxLine, "lahf");
        } else if (Op == 0xD7) { std::snprintf (pLine, MaxLine, "xlat");
        } else if (Op == 0x8D) {
            std::snprintf (pLine, MaxLine, "lea %s,[mem]", RegName ((m_pCode[Pc + 1] >> 3) & 7));
        } else if (Op == 0x86 || Op == 0x87) {
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rg = (Op & 1) ? RegName ((M >> 3) & 7) : Reg8Name ((M >> 3) & 7);
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? ((Op & 1) ? RegName (M & 7) : Reg8Name (M & 7)) : "[mem]";
            std::snprintf (pLine, MaxLine, "xchg %s,%s", Rg, Rm);
        } else if (Op >= 0x91 && Op <= 0x97) {
            std::snprintf (pLine, MaxLine, "xchg ax,%s", RegName (Op - 0x90));
        } else if (Op == 0xC4 || Op == 0xC5) {
            std::snprintf (pLine, MaxLine, "%s %s,[mem]", Op == 0xC4 ? "les" : "lds", RegName ((m_pCode[Pc + 1] >> 3) & 7));
        } else if (Op == 0xF6 || Op == 0xF7) {
            static CHAR8 CONST *kG3[8] = { "test","test","not","neg","mul","imul","div","idiv" };
            UINT8 M = m_pCode[Pc + 1]; UINT8 Sub = (M >> 3) & 7;
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? ((Op & 1) ? RegName (M & 7) : Reg8Name (M & 7)) : "[mem]";
            if (Sub <= 1 && (Op & 1)) { std::snprintf (pLine, MaxLine, "test %s,0x%04x", Rm, Imm16At (m_pCode, Pc + 1 + RmLen (M))); }
            else if (Sub <= 1)        { std::snprintf (pLine, MaxLine, "test %s,0x%02x", Rm, m_pCode[Pc + 1 + RmLen (M)]); }
            else                      { std::snprintf (pLine, MaxLine, "%s %s", kG3[Sub], Rm); }
        } else if (Op == 0xFE || Op == 0xFF) {
            static CHAR8 CONST *kG5[8] = { "inc","dec","call","callf","jmp","jmpf","push","?" };
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? ((Op == 0xFF) ? RegName (M & 7) : Reg8Name (M & 7)) : "[mem]";
            std::snprintf (pLine, MaxLine, "%s %s", kG5[(M >> 3) & 7], Rm);
        } else if (Op == 0xD0 || Op == 0xD1 || Op == 0xD2 || Op == 0xD3 || Op == 0xC0 || Op == 0xC1) {
            static CHAR8 CONST *kSh[8] = { "rol","ror","rcl","rcr","shl","shr","sal","sar" };
            UINT8 M = m_pCode[Pc + 1];
            CHAR8 CONST *Rm = ((M >> 6) == 3) ? ((Op & 1) ? RegName (M & 7) : Reg8Name (M & 7)) : "[mem]";
            if (Op == 0xD0 || Op == 0xD1)      { std::snprintf (pLine, MaxLine, "%s %s,1", kSh[(M >> 3) & 7], Rm); }
            else if (Op == 0xD2 || Op == 0xD3) { std::snprintf (pLine, MaxLine, "%s %s,cl", kSh[(M >> 3) & 7], Rm); }
            else { std::snprintf (pLine, MaxLine, "%s %s,0x%02x", kSh[(M >> 3) & 7], Rm, m_pCode[Pc + 1 + RmLen (M)]); }
        } else if (Op == 0x0F) {
            UINT8 Sub = m_pCode[Pc + 1];
            CHAR8 CONST *pMnem = nullptr;
            switch (Sub) {
            case 0x10: case 0x18: case 0x19: pMnem = "test1"; break;
            case 0x12: case 0x1A: case 0x1B: pMnem = "clr1";  break;
            case 0x14: case 0x1C: case 0x1D: pMnem = "set1";  break;
            case 0x16: case 0x1E: case 0x1F: pMnem = "not1";  break;
            case 0x20: pMnem = "add4s"; break;
            case 0x22: pMnem = "sub4s"; break;
            case 0x26: pMnem = "cmp4s"; break;
            case 0x28: pMnem = "rol4";  break;
            case 0x2A: pMnem = "ror4";  break;
            case 0x31: case 0x39: pMnem = "ins"; break;
            case 0x33: case 0x3B: pMnem = "ext"; break;
            }
            if (pMnem == nullptr) {
                std::snprintf (pLine, MaxLine, "db 0x0f,0x%02x", Sub);
            } else if (Sub == 0x20 || Sub == 0x22 || Sub == 0x26) {
                std::snprintf (pLine, MaxLine, "%s", pMnem);     // packed-BCD string op (implied SI/DI)
            } else {
                CHAR8 CONST *Rm = ((m_pCode[Pc + 2] >> 6) == 3) ? RegName (m_pCode[Pc + 2] & 7) : "[mem]";
                std::snprintf (pLine, MaxLine, "%s %s", pMnem, Rm);
            }
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
        // Segment-override prefix (ES:/CS:/SS:/DS:): translate the following instruction with the
        // override in effect, then clear it.
        if (Op == 0x26 || Op == 0x2E || Op == 0x36 || Op == 0x3E) {
            m_SegOv = (Op == 0x26) ? RegV20ES : (Op == 0x2E) ? RegV20CS : (Op == 0x36) ? RegV20SS : RegV20DS;
            HRESULT Hr = TranslateInstr (Pc + 1, pE);
            m_SegOv = -1;
            return Hr;
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

        // ---- 8-bit operand forms -------------------------------------------------------------
        // ALU r/m8 (alu*8 + 0 = r/m8,r8 ; +2 = r8,r/m8), MOV r/m8 (0x88/0x8A), TEST r/m8 (0x84).
        if ((Op < 0x40 && ((Op & 7) == 0 || (Op & 7) == 2)) || Op == 0x88 || Op == 0x8A || Op == 0x84) {
            UINT8  M       = m_pCode[Pc + 1];
            UINT32 Reg     = (M >> 3) & 7;
            bool   RegDest = (Op & 2) != 0;
            ComPtr<ICpuValue> RegV; EmitReg8Read (pE, (UINT8) Reg, &RegV);
            ComPtr<ICpuValue> RmV;  EmitRmRead8 (pE, M, Pc + 1, &RmV);
            if (Op == 0x88) { EmitRmWrite8 (pE, M, Pc + 1, RegV); return S_OK; }   // MOV r/m8, r8
            if (Op == 0x8A) { EmitReg8Write (pE, (UINT8) Reg, RmV); return S_OK; } // MOV r8, r/m8
            UINT32 Alu = (Op == 0x84) ? 4 /*AND for TEST*/ : (Op >> 3) & 7;
            ICpuValue *pDst = RegDest ? (ICpuValue *) RegV : (ICpuValue *) RmV;
            ICpuValue *pSrc = RegDest ? (ICpuValue *) RmV : (ICpuValue *) RegV;
            ComPtr<ICpuValue> Res; EmitAlu8 (pE, Alu, pDst, pSrc, &Res);
            if (Op != 0x84 && Alu != 7) {                                         // TEST/CMP: no writeback
                if (RegDest) { EmitReg8Write (pE, (UINT8) Reg, Res); }
                else         { EmitRmWrite8 (pE, M, Pc + 1, Res); }
            }
            return S_OK;
        }
        // ALU AL, imm8 (alu*8 + 4) and TEST AL, imm8 (0xA8).
        if ((Op < 0x40 && (Op & 7) == 4) || Op == 0xA8) {
            ComPtr<ICpuValue> Al;  EmitReg8Read (pE, 0, &Al);
            ComPtr<ICpuValue> Imm; pE->ConstInt (8, m_pCode[Pc + 1], &Imm);
            UINT32 Alu = (Op == 0xA8) ? 4 : (Op >> 3) & 7;
            ComPtr<ICpuValue> Res; EmitAlu8 (pE, Alu, Al, Imm, &Res);
            if (Op != 0xA8 && Alu != 7) { EmitReg8Write (pE, 0, Res); }
            return S_OK;
        }
        // grp1 8-bit: 0x80 <alu> r/m8, imm8.
        if (Op == 0x80) {
            UINT8  M   = m_pCode[Pc + 1];
            UINT32 Alu = (M >> 3) & 7;
            ComPtr<ICpuValue> Rm;  EmitRmRead8 (pE, M, Pc + 1, &Rm);
            ComPtr<ICpuValue> Imm; pE->ConstInt (8, m_pCode[Pc + 1 + RmLen (M)], &Imm);
            ComPtr<ICpuValue> Res; EmitAlu8 (pE, Alu, Rm, Imm, &Res);
            if (Alu != 7) { EmitRmWrite8 (pE, M, Pc + 1, Res); }
            return S_OK;
        }
        // MOV r/m8, imm8 (0xC6).
        if (Op == 0xC6) {
            UINT8 M = m_pCode[Pc + 1];
            ComPtr<ICpuValue> V; pE->ConstInt (8, m_pCode[Pc + 1 + RmLen (M)], &V);
            EmitRmWrite8 (pE, M, Pc + 1, V);
            return S_OK;
        }
        // grp2 shifts/rotates: D0/D1 (r/m,1), D2/D3 (r/m,CL), C0/C1 (r/m,imm8). Odd = 16-bit.
        if (Op == 0xD0 || Op == 0xD1 || Op == 0xD2 || Op == 0xD3 || Op == 0xC0 || Op == 0xC1) {
            UINT8  M    = m_pCode[Pc + 1];
            UINT8  Sub  = (M >> 3) & 7;
            bool   W16  = (Op & 1) != 0;
            UINT32 W    = W16 ? 16 : 8;
            ComPtr<ICpuValue> Val;   if (W16) { EmitRmRead (pE, M, Pc + 1, &Val); } else { EmitRmRead8 (pE, M, Pc + 1, &Val); }
            ComPtr<ICpuValue> Count;
            if (Op == 0xD0 || Op == 0xD1) {
                pE->ConstInt (W, 1, &Count);
            } else if (Op == 0xD2 || Op == 0xD3) {
                ComPtr<ICpuValue> Cl; EmitReg8Read (pE, 1, &Cl); pE->Cast (CastZExt, Cl, W, &Count);   // CL
            } else {
                pE->ConstInt (W, m_pCode[Pc + 1 + RmLen (M)], &Count);                                 // imm8
            }
            ComPtr<ICpuValue> Res; EmitShift (pE, Sub, W, Val, Count, &Res);
            if (W16) { EmitRmWrite (pE, M, Pc + 1, Res); } else { EmitRmWrite8 (pE, M, Pc + 1, Res); }
            return S_OK;
        }
        // grp3: F6 (byte) / F7 (word). /0,/1 TEST imm; /2 NOT; /3 NEG; /4 MUL; /5 IMUL; /6 DIV; /7 IDIV.
        if (Op == 0xF6 || Op == 0xF7) {
            UINT8  M    = m_pCode[Pc + 1];
            UINT8  Sub  = (M >> 3) & 7;
            bool   W16  = (Op & 1) != 0;
            UINT32 W    = W16 ? 16 : 8;
            ComPtr<ICpuValue> Rm; if (W16) { EmitRmRead (pE, M, Pc + 1, &Rm); } else { EmitRmRead8 (pE, M, Pc + 1, &Rm); }
            if (Sub == 0 || Sub == 1) {                            // TEST r/m, imm (flags only)
                CPU_ADDR IOff = Pc + 1 + RmLen (M);
                ComPtr<ICpuValue> Imm; if (W16) { pE->ConstInt (16, Imm16At (m_pCode, IOff), &Imm); } else { pE->ConstInt (8, m_pCode[IOff], &Imm); }
                ComPtr<ICpuValue> Res; if (W16) { EmitAlu16 (pE, 4, Rm, Imm, &Res); } else { EmitAlu8 (pE, 4, Rm, Imm, &Res); }
            } else if (Sub == 2) {                                 // NOT (no flags)
                ComPtr<ICpuValue> R; pE->UnaryOp (UnCom, Rm, &R);
                if (W16) { EmitRmWrite (pE, M, Pc + 1, R); } else { EmitRmWrite8 (pE, M, Pc + 1, R); }
            } else if (Sub == 3) {                                 // NEG: r/m = 0 - r/m
                ComPtr<ICpuValue> Zero; pE->ConstInt (W, 0, &Zero);
                ComPtr<ICpuValue> Res;  if (W16) { EmitAlu16 (pE, 5, Zero, Rm, &Res); } else { EmitAlu8 (pE, 5, Zero, Rm, &Res); }
                if (W16) { EmitRmWrite (pE, M, Pc + 1, Res); } else { EmitRmWrite8 (pE, M, Pc + 1, Res); }
            } else if (Sub == 4 || Sub == 5) {                     // MUL / IMUL
                CPU_CAST Ext = (Sub == 5) ? CastSExt : CastZExt;
                if (!W16) {                                        // AX = AL * r/m8
                    ComPtr<ICpuValue> Al; EmitReg8Read (pE, 0, &Al);
                    ComPtr<ICpuValue> A16; pE->Cast (Ext, Al, 16, &A16);
                    ComPtr<ICpuValue> R16; pE->Cast (Ext, Rm, 16, &R16);
                    ComPtr<ICpuValue> P;   pE->BinaryOp (BinMul, A16, R16, &P);
                    pE->PutRegister (RegV20AX, P, 16, FALSE);
                } else {                                           // DX:AX = AX * r/m16
                    ComPtr<ICpuValue> Ax;  pE->GetRegister (RegV20AX, 16, &Ax);
                    ComPtr<ICpuValue> A32; pE->Cast (Ext, Ax, 32, &A32);
                    ComPtr<ICpuValue> R32; pE->Cast (Ext, Rm, 32, &R32);
                    ComPtr<ICpuValue> P;   pE->BinaryOp (BinMul, A32, R32, &P);
                    ComPtr<ICpuValue> Lo;  pE->Cast (CastTrunc, P, 16, &Lo); pE->PutRegister (RegV20AX, Lo, 16, FALSE);
                    ComPtr<ICpuValue> Sh;  pE->ConstInt (32, 16, &Sh);
                    ComPtr<ICpuValue> Hi;  pE->BinaryOp (BinLShr, P, Sh, &Hi);
                    ComPtr<ICpuValue> Dx;  pE->Cast (CastTrunc, Hi, 16, &Dx); pE->PutRegister (RegV20DX, Dx, 16, FALSE);
                }
            } else {                                               // DIV (6) / IDIV (7)
                bool Signed = (Sub == 7);
                CPU_BINOP DivOp = Signed ? BinSDiv : BinUDiv;
                CPU_BINOP RemOp = Signed ? BinSRem : BinURem;
                CPU_CAST  Ext   = Signed ? CastSExt : CastZExt;
                if (!W16) {                                        // AX / r/m8 -> AL=quo, AH=rem
                    ComPtr<ICpuValue> Ax;  pE->GetRegister (RegV20AX, 16, &Ax);
                    ComPtr<ICpuValue> R16; pE->Cast (Ext, Rm, 16, &R16);
                    ComPtr<ICpuValue> Q;   pE->BinaryOp (DivOp, Ax, R16, &Q);
                    ComPtr<ICpuValue> Rr;  pE->BinaryOp (RemOp, Ax, R16, &Rr);
                    ComPtr<ICpuValue> Q8;  pE->Cast (CastTrunc, Q, 8, &Q8);  EmitReg8Write (pE, 0, Q8);
                    ComPtr<ICpuValue> R8;  pE->Cast (CastTrunc, Rr, 8, &R8); EmitReg8Write (pE, 4, R8);
                } else {                                           // DX:AX / r/m16 -> AX=quo, DX=rem
                    ComPtr<ICpuValue> Dx;  pE->GetRegister (RegV20DX, 16, &Dx);
                    ComPtr<ICpuValue> Ax;  pE->GetRegister (RegV20AX, 16, &Ax);
                    ComPtr<ICpuValue> Dx32;pE->Cast (CastZExt, Dx, 32, &Dx32);
                    ComPtr<ICpuValue> Ax32;pE->Cast (CastZExt, Ax, 32, &Ax32);
                    ComPtr<ICpuValue> Sh;  pE->ConstInt (32, 16, &Sh);
                    ComPtr<ICpuValue> HiS; pE->BinaryOp (BinShl, Dx32, Sh, &HiS);
                    ComPtr<ICpuValue> Num; pE->BinaryOp (BinOr, HiS, Ax32, &Num);
                    ComPtr<ICpuValue> R32; pE->Cast (Ext, Rm, 32, &R32);
                    ComPtr<ICpuValue> Q;   pE->BinaryOp (DivOp, Num, R32, &Q);
                    ComPtr<ICpuValue> Rr;  pE->BinaryOp (RemOp, Num, R32, &Rr);
                    ComPtr<ICpuValue> Q16; pE->Cast (CastTrunc, Q, 16, &Q16);  pE->PutRegister (RegV20AX, Q16, 16, FALSE);
                    ComPtr<ICpuValue> R16; pE->Cast (CastTrunc, Rr, 16, &R16); pE->PutRegister (RegV20DX, R16, 16, FALSE);
                }
            }
            return S_OK;
        }
        // grp4 (FE) / grp5 (FF): INC/DEC/CALL/JMP/PUSH r/m.
        if (Op == 0xFE || Op == 0xFF) {
            UINT8  M   = m_pCode[Pc + 1];
            UINT8  Sub = (M >> 3) & 7;
            bool   W16 = (Op == 0xFF);
            UINT32 Len = 1 + RmLen (M);
            if (Sub == 0 || Sub == 1) {                            // INC / DEC r/m
                ComPtr<ICpuValue> Rm;  if (W16) { EmitRmRead (pE, M, Pc + 1, &Rm); } else { EmitRmRead8 (pE, M, Pc + 1, &Rm); }
                ComPtr<ICpuValue> One; pE->ConstInt (W16 ? 16 : 8, 1, &One);
                ComPtr<ICpuValue> Res; pE->BinaryOp (Sub == 0 ? BinAdd : BinSub, Rm, One, &Res);
                if (W16) { EmitRmWrite (pE, M, Pc + 1, Res); EmitIncDecOverflow (pE, Rm, Sub == 0 ? 0x7FFF : 0x8000); EmitZSF (pE, Res); }
                else     { EmitRmWrite8 (pE, M, Pc + 1, Res); EmitZSF8 (pE, Res); }
                return S_OK;
            }
            if (Sub == 6) {                                        // PUSH r/m16
                ComPtr<ICpuValue> Rm; EmitRmRead (pE, M, Pc + 1, &Rm);
                ComPtr<ICpuValue> SP; pE->GetRegister (RegV20SP, 16, &SP);
                ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
                ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
                pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
                ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20SS, NewSP, &Lin);
                pE->Store (Rm, Lin, 16);
                return S_OK;
            }
            if (Sub == 2 || Sub == 4) {                            // CALL / JMP r/m16 (near indirect)
                ComPtr<ICpuValue> Target; EmitRmRead (pE, M, Pc + 1, &Target);
                if (Sub == 2) {                                    // push return address
                    ComPtr<ICpuValue> SP; pE->GetRegister (RegV20SP, 16, &SP);
                    ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
                    ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
                    pE->PutRegister (RegV20SP, NewSP, 16, FALSE);
                    ComPtr<ICpuValue> Ret; pE->ConstInt (16, (UINT16) (Pc + Len), &Ret);
                    ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20SS, NewSP, &Lin);
                    pE->Store (Ret, Lin, 16);
                }
                ICpuSmcEmitter *pFlow = nullptr;                   // dispatch to the runtime target
                if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                    pFlow->SetDispatchTarget (Target); pFlow->Release ();
                }
                return S_OK;
            }
            return S_OK;                                           // /3,/5 far indirect: handled as a trap
        }
        // REP/REPE/REPNE (F3/F2) on a string op: a loop { if CX==0 break; one element; CX-- }.
        // For CMPS/SCAS the prefix also tests ZF (REPE while ZF==1, REPNE while ZF==0).
        if ((Op == 0xF2 || Op == 0xF3) && IsStringOp (m_pCode[Pc + 1])) {
            UINT8 SOp = m_pCode[Pc + 1];
            bool  Repe  = (Op == 0xF3);
            bool  IsCmp = (SOp == 0xA6 || SOp == 0xA7 || SOp == 0xAE || SOp == 0xAF);
            ComPtr<ICpuBlock> Header, Body, Done;
            pE->CreateBlock ("rep.head", &Header);
            pE->CreateBlock ("rep.body", &Body);
            pE->CreateBlock ("rep.done", &Done);
            pE->Branch (Header);
            pE->SetInsertBlock (Header);
            ComPtr<ICpuValue> Cx;   pE->GetRegister (RegV20CX, 16, &Cx);
            ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
            ComPtr<ICpuValue> Ne;   pE->Compare (CmpNe, Cx, Zero, &Ne);
            pE->CondBranch (Ne, Body, Done);
            pE->SetInsertBlock (Body);
            EmitStringStep (pE, SOp);
            ComPtr<ICpuValue> Cx2; pE->GetRegister (RegV20CX, 16, &Cx2);
            ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
            ComPtr<ICpuValue> Dec; pE->BinaryOp (BinSub, Cx2, One, &Dec);
            pE->PutRegister (RegV20CX, Dec, 16, FALSE);
            if (IsCmp) {
                ComPtr<ICpuValue> ZF; pE->GetFlag (FlagZero, &ZF);
                if (Repe) { pE->CondBranch (ZF, Header, Done); }   // REPE: continue while ZF==1
                else { ComPtr<ICpuValue> NZ; pE->UnaryOp (UnNot, ZF, &NZ); pE->CondBranch (NZ, Header, Done); }
            } else {
                pE->Branch (Header);
            }
            pE->SetInsertBlock (Done);
            return S_OK;
        }
        // A single string element (no REP prefix).
        if (IsStringOp (Op)) { EmitStringStep (pE, Op); return S_OK; }

        // LEA r16, m: load the effective address (the offset), not the memory contents.
        if (Op == 0x8D) {
            UINT8 M = m_pCode[Pc + 1];
            if ((M >> 6) != 3) {
                ComPtr<ICpuValue> EA; EmitEA (pE, M, Pc + 2, &EA);
                pE->PutRegister ((M >> 3) & 7, EA, 16, FALSE);
            }
            return S_OK;
        }
        // XCHG r/m, r (0x86 byte / 0x87 word).
        if (Op == 0x86 || Op == 0x87) {
            UINT8 M = m_pCode[Pc + 1]; UINT32 Reg = (M >> 3) & 7;
            if (Op & 1) {
                ComPtr<ICpuValue> RegV; pE->GetRegister (Reg, 16, &RegV);
                ComPtr<ICpuValue> RmV;  EmitRmRead (pE, M, Pc + 1, &RmV);
                EmitRmWrite (pE, M, Pc + 1, RegV); pE->PutRegister (Reg, RmV, 16, FALSE);
            } else {
                ComPtr<ICpuValue> RegV; EmitReg8Read (pE, (UINT8) Reg, &RegV);
                ComPtr<ICpuValue> RmV;  EmitRmRead8 (pE, M, Pc + 1, &RmV);
                EmitRmWrite8 (pE, M, Pc + 1, RegV); EmitReg8Write (pE, (UINT8) Reg, RmV);
            }
            return S_OK;
        }
        // XCHG AX, reg (0x91..0x97; 0x90 = XCHG AX,AX = NOP).
        if (Op >= 0x91 && Op <= 0x97) {
            UINT32 Reg = Op - 0x90;
            ComPtr<ICpuValue> Ax; pE->GetRegister (RegV20AX, 16, &Ax);
            ComPtr<ICpuValue> Rg; pE->GetRegister (Reg, 16, &Rg);
            pE->PutRegister (RegV20AX, Rg, 16, FALSE); pE->PutRegister (Reg, Ax, 16, FALSE);
            return S_OK;
        }
        // LES/LDS r16, m: reg = [m], ES/DS = [m+2].
        if (Op == 0xC4 || Op == 0xC5) {
            UINT8 M = m_pCode[Pc + 1]; UINT32 Reg = (M >> 3) & 7;
            UINT32 Seg = (Op == 0xC4) ? RegV20ES : RegV20DS;
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, Pc + 2, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, SegForRm (M), EA, &Lin);
            ComPtr<ICpuValue> Off; pE->Load (Lin, 16, &Off); pE->PutRegister (Reg, Off, 16, FALSE);
            ComPtr<ICpuValue> Two; pE->ConstInt (32, 2, &Two);
            ComPtr<ICpuValue> Lin2; pE->BinaryOp (BinAdd, Lin, Two, &Lin2);
            ComPtr<ICpuValue> Sg;  pE->Load (Lin2, 16, &Sg); pE->PutRegister (Seg, Sg, 16, FALSE);
            return S_OK;
        }
        // LAHF: AH = flags byte (SF ZF 0 AF 0 PF 1 CF).
        if (Op == 0x9F) {
            ComPtr<ICpuValue> Base; pE->ConstInt (8, 0x02, &Base);
            ICpuValue *Acc = Base;
            ComPtr<ICpuValue> H[4];
            CPU_FLAG  Fl[4] = { FlagCarry, FlagParity, FlagZero, FlagNegative };
            UINT8     Bt[4] = { 0, 2, 6, 7 };
            for (int I = 0; I < 4; I++) {
                ComPtr<ICpuValue> F; pE->GetFlag (Fl[I], &F);
                ComPtr<ICpuValue> F8; pE->Cast (CastZExt, F, 8, &F8);
                ComPtr<ICpuValue> Sh; pE->ConstInt (8, Bt[I], &Sh);
                ComPtr<ICpuValue> Bv; pE->BinaryOp (BinShl, F8, Sh, &Bv);
                pE->BinaryOp (BinOr, Acc, Bv, &H[I]);
                Acc = H[I];
            }
            EmitReg8Write (pE, 4, Acc);                            // AH
            return S_OK;
        }
        // SAHF: CF/PF/ZF/SF from AH.
        if (Op == 0x9E) {
            ComPtr<ICpuValue> Ah; EmitReg8Read (pE, 4, &Ah);
            CPU_FLAG Fl[4] = { FlagCarry, FlagParity, FlagZero, FlagNegative };
            UINT8    Bt[4] = { 0, 2, 6, 7 };
            for (int I = 0; I < 4; I++) {
                ComPtr<ICpuValue> Sh;  pE->ConstInt (8, Bt[I], &Sh);
                ComPtr<ICpuValue> Shf; pE->BinaryOp (BinLShr, Ah, Sh, &Shf);
                ComPtr<ICpuValue> One; pE->ConstInt (8, 1, &One);
                ComPtr<ICpuValue> Bit; pE->BinaryOp (BinAnd, Shf, One, &Bit);
                ComPtr<ICpuValue> B1;  pE->Cast (CastTrunc, Bit, 1, &B1);
                pE->SetFlag (Fl[I], B1);
            }
            return S_OK;
        }
        // XLAT: AL = [DS:BX + AL].
        if (Op == 0xD7) {
            UINT32 Seg = (m_SegOv >= 0) ? (UINT32) m_SegOv : (UINT32) RegV20DS;
            ComPtr<ICpuValue> Bx;  pE->GetRegister (RegV20BX, 16, &Bx);
            ComPtr<ICpuValue> Al;  EmitReg8Read (pE, 0, &Al);
            ComPtr<ICpuValue> Al16;pE->Cast (CastZExt, Al, 16, &Al16);
            ComPtr<ICpuValue> Off; pE->BinaryOp (BinAdd, Bx, Al16, &Off);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, Seg, Off, &Lin);
            ComPtr<ICpuValue> V;   pE->Load (Lin, 8, &V);
            EmitReg8Write (pE, 0, V);
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
        case 0xFC: { ComPtr<ICpuValue> Z; pE->ConstInt (1, 0, &Z); pE->SetFlag (FlagDirection, Z); break; }   // CLD
        case 0xFD: { ComPtr<ICpuValue> O; pE->ConstInt (1, 1, &O); pE->SetFlag (FlagDirection, O); break; }   // STD
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
        case 0xA0: {                                               // MOV AL, [addr16] (DS-relative)
            ComPtr<ICpuValue> Ad;  pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, Ad, &Lin);
            ComPtr<ICpuValue> V;   pE->Load (Lin, 8, &V);
            EmitReg8Write (pE, 0, V);                              // AL
            break;
        }
        case 0xA2: {                                               // MOV [addr16], AL (DS-relative)
            ComPtr<ICpuValue> V;   EmitReg8Read (pE, 0, &V);       // AL
            ComPtr<ICpuValue> Ad;  pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, RegV20DS, Ad, &Lin);
            pE->Store (V, Lin, 8);
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
        case 0x0F: {                                               // NEC V20/V30-specific instructions
            UINT8 Op2 = m_pCode[Pc + 1];

            // Canonical bit instructions: TEST1/CLR1/SET1/NOT1 on r/m16, bit position from CL
            // (0F 10/12/14/16) or an imm8 (0F 18/1A/1C/1E). modrm is at Pc+2.
            if ((Op2 >= 0x10 && Op2 <= 0x16 && (Op2 & 1) == 0) ||
                (Op2 >= 0x18 && Op2 <= 0x1E && (Op2 & 1) == 0)) {
                UINT8 M = m_pCode[Pc + 2];
                ComPtr<ICpuValue> Rm; EmitRmRead (pE, M, Pc + 2, &Rm);
                ComPtr<ICpuValue> Mask;
                if (Op2 & 0x08) {                                  // imm8 form
                    UINT8 Bit = m_pCode[Pc + 2 + RmLen (M)] & 15;
                    pE->ConstInt (16, (UINT16) (1u << Bit), &Mask);
                } else {                                           // CL form
                    ComPtr<ICpuValue> Cl; pE->GetRegister (RegV20CX, 16, &Cl);
                    ComPtr<ICpuValue> F;  pE->ConstInt (16, 15, &F);
                    ComPtr<ICpuValue> Sh; pE->BinaryOp (BinAnd, Cl, F, &Sh);
                    ComPtr<ICpuValue> One;pE->ConstInt (16, 1, &One);
                    pE->BinaryOp (BinShl, One, Sh, &Mask);
                }
                UINT8 Kind = Op2 & 0x06;                           // 0=TEST1,2=CLR1,4=SET1,6=NOT1
                if (Kind == 0x04) {                                // SET1
                    ComPtr<ICpuValue> R; pE->BinaryOp (BinOr, Rm, Mask, &R); EmitRmWrite (pE, M, Pc + 2, R);
                } else if (Kind == 0x02) {                         // CLR1
                    ComPtr<ICpuValue> NM; pE->UnaryOp (UnCom, Mask, &NM);
                    ComPtr<ICpuValue> R;  pE->BinaryOp (BinAnd, Rm, NM, &R); EmitRmWrite (pE, M, Pc + 2, R);
                } else if (Kind == 0x06) {                         // NOT1
                    ComPtr<ICpuValue> R; pE->BinaryOp (BinXor, Rm, Mask, &R); EmitRmWrite (pE, M, Pc + 2, R);
                } else {                                           // TEST1 -> ZF = (bit == 0)
                    ComPtr<ICpuValue> T;    pE->BinaryOp (BinAnd, Rm, Mask, &T);
                    ComPtr<ICpuValue> Zero; pE->ConstInt (16, 0, &Zero);
                    ComPtr<ICpuValue> ZF;   pE->Compare (CmpEq, T, Zero, &ZF);
                    pE->SetFlag (FlagZero, ZF);
                }
                break;
            }

            // Legacy register-direct bit ops (0F 19/1B/1D/1F r,imm8) kept for back-compat.
            if (Op2 == 0x19 || Op2 == 0x1B || Op2 == 0x1D || Op2 == 0x1F) {
                UINT32 Rm  = m_pCode[Pc + 2] & 7;
                UINT32 Bit = m_pCode[Pc + 3] & 15;
                ComPtr<ICpuValue> R;    pE->GetRegister (Rm, 16, &R);
                ComPtr<ICpuValue> Mask; pE->ConstInt (16, (UINT16) (1u << Bit), &Mask);
                ComPtr<ICpuValue> Res;
                if (Op2 == 0x1D)      { pE->BinaryOp (BinOr,  R, Mask, &Res); pE->PutRegister (Rm, Res, 16, FALSE); }
                else if (Op2 == 0x1F) { pE->BinaryOp (BinXor, R, Mask, &Res); pE->PutRegister (Rm, Res, 16, FALSE); }
                else if (Op2 == 0x1B) { ComPtr<ICpuValue> NM; pE->ConstInt (16, (UINT16) (~(1u << Bit)), &NM);
                                        pE->BinaryOp (BinAnd, R, NM, &Res); pE->PutRegister (Rm, Res, 16, FALSE); }
                else { ComPtr<ICpuValue> T; pE->BinaryOp (BinAnd, R, Mask, &T);
                       ComPtr<ICpuValue> Z; pE->ConstInt (16, 0, &Z);
                       ComPtr<ICpuValue> ZF; pE->Compare (CmpEq, T, Z, &ZF); pE->SetFlag (FlagZero, ZF); }
                break;
            }

            // ROL4/ROR4 (0F 28/2A, nibble rotate of a byte), ADD4S/SUB4S/CMP4S (0F 20/22/26,
            // packed-BCD string ops on SI/DI), INS/EXT (0F 31/33/39/3B, bit-field insert/
            // extract): recognized NEC instructions; their full semantics need the 8-bit r/m
            // and string-loop machinery still to be added, so here they decode/disassemble but
            // execute as no-ops.
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

    // Add a (signed) step to a 16-bit register: SI/DI advance for the string ops.
    VOID AdvanceReg (ICpuEmitter *pE, UINT32 Reg, ICpuValue *pStep) {
        ComPtr<ICpuValue> R; pE->GetRegister (Reg, 16, &R);
        ComPtr<ICpuValue> N; pE->BinaryOp (BinAdd, R, pStep, &N);
        pE->PutRegister (Reg, N, 16, FALSE);
    }

    static bool IsStringOp (UINT8 Op) {
        return (Op >= 0xA4 && Op <= 0xA7) || (Op >= 0xAA && Op <= 0xAF);
    }

    // One string element: MOVS/CMPS/STOS/LODS/SCAS. SI is read through DS (or the active
    // override), DI through ES; SI/DI advance by +/-width per the Direction flag.
    VOID EmitStringStep (ICpuEmitter *pE, UINT8 Op) {
        bool   W16    = (Op & 1) != 0;
        UINT32 W      = W16 ? 16 : 8;
        UINT32 SrcSeg = (m_SegOv >= 0) ? (UINT32) m_SegOv : (UINT32) RegV20DS;
        ComPtr<ICpuValue> DF;   pE->GetFlag (FlagDirection, &DF);
        ComPtr<ICpuValue> DFs;  pE->Cast (CastSExt, DF, 16, &DFs);
        ComPtr<ICpuValue> WidV; pE->ConstInt (16, W16 ? 2 : 1, &WidV);
        ComPtr<ICpuValue> Xv;   pE->BinaryOp (BinXor, WidV, DFs, &Xv);
        ComPtr<ICpuValue> DFz;  pE->Cast (CastZExt, DF, 16, &DFz);
        ComPtr<ICpuValue> Step; pE->BinaryOp (BinAdd, Xv, DFz, &Step);

        if (Op == 0xA4 || Op == 0xA5) {                            // MOVS: [ES:DI] = [SrcSeg:SI]
            ComPtr<ICpuValue> Si; pE->GetRegister (RegV20SI, 16, &Si);
            ComPtr<ICpuValue> SL; EmitSegLinear (pE, SrcSeg, Si, &SL);
            ComPtr<ICpuValue> V;  pE->Load (SL, W, &V);
            ComPtr<ICpuValue> Di; pE->GetRegister (RegV20DI, 16, &Di);
            ComPtr<ICpuValue> DL; EmitSegLinear (pE, RegV20ES, Di, &DL);
            pE->Store (V, DL, W);
            AdvanceReg (pE, RegV20SI, Step); AdvanceReg (pE, RegV20DI, Step);
        } else if (Op == 0xAA || Op == 0xAB) {                     // STOS: [ES:DI] = AL/AX
            ComPtr<ICpuValue> V;  if (W16) { pE->GetRegister (RegV20AX, 16, &V); } else { EmitReg8Read (pE, 0, &V); }
            ComPtr<ICpuValue> Di; pE->GetRegister (RegV20DI, 16, &Di);
            ComPtr<ICpuValue> DL; EmitSegLinear (pE, RegV20ES, Di, &DL);
            pE->Store (V, DL, W);
            AdvanceReg (pE, RegV20DI, Step);
        } else if (Op == 0xAC || Op == 0xAD) {                     // LODS: AL/AX = [SrcSeg:SI]
            ComPtr<ICpuValue> Si; pE->GetRegister (RegV20SI, 16, &Si);
            ComPtr<ICpuValue> SL; EmitSegLinear (pE, SrcSeg, Si, &SL);
            ComPtr<ICpuValue> V;  pE->Load (SL, W, &V);
            if (W16) { pE->PutRegister (RegV20AX, V, 16, FALSE); } else { EmitReg8Write (pE, 0, V); }
            AdvanceReg (pE, RegV20SI, Step);
        } else if (Op == 0xAE || Op == 0xAF) {                     // SCAS: cmp AL/AX, [ES:DI]
            ComPtr<ICpuValue> A;  if (W16) { pE->GetRegister (RegV20AX, 16, &A); } else { EmitReg8Read (pE, 0, &A); }
            ComPtr<ICpuValue> Di; pE->GetRegister (RegV20DI, 16, &Di);
            ComPtr<ICpuValue> DL; EmitSegLinear (pE, RegV20ES, Di, &DL);
            ComPtr<ICpuValue> M;  pE->Load (DL, W, &M);
            ComPtr<ICpuValue> R;  if (W16) { EmitAlu16 (pE, 7, A, M, &R); } else { EmitAlu8 (pE, 7, A, M, &R); }
            AdvanceReg (pE, RegV20DI, Step);
        } else {                                                   // CMPS (A6/A7): cmp [SrcSeg:SI], [ES:DI]
            ComPtr<ICpuValue> Si; pE->GetRegister (RegV20SI, 16, &Si);
            ComPtr<ICpuValue> SL; EmitSegLinear (pE, SrcSeg, Si, &SL);
            ComPtr<ICpuValue> Sv; pE->Load (SL, W, &Sv);
            ComPtr<ICpuValue> Di; pE->GetRegister (RegV20DI, 16, &Di);
            ComPtr<ICpuValue> DL; EmitSegLinear (pE, RegV20ES, Di, &DL);
            ComPtr<ICpuValue> Dv; pE->Load (DL, W, &Dv);
            ComPtr<ICpuValue> R;  if (W16) { EmitAlu16 (pE, 7, Sv, Dv, &R); } else { EmitAlu8 (pE, 7, Sv, Dv, &R); }
            AdvanceReg (pE, RegV20SI, Step); AdvanceReg (pE, RegV20DI, Step);
        }
    }

    // The segment a memory operand uses: an active override prefix, else SS for BP-based
    // addressing (the 8086 default), else DS.
    UINT32 SegForRm (UINT8 M) {
        if (m_SegOv >= 0) { return (UINT32) m_SegOv; }
        UINT8 Mod = (UINT8) (M >> 6), Rm = (UINT8) (M & 7);
        if (Mod != 3 && (Rm == 2 || Rm == 3 || (Rm == 6 && Mod != 0))) { return RegV20SS; }
        return RegV20DS;
    }

    // Read / write the r/m operand: a register (mod=11) or segment-relative memory at the EA.
    VOID EmitRmRead (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue **ppValue) {
        if ((M >> 6) == 3) {
            pE->GetRegister ((UINT32) (M & 7), 16, ppValue);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, SegForRm (M), EA, &Lin);
            pE->Load (Lin, 16, ppValue);
        }
    }
    VOID EmitRmWrite (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue *pValue) {
        if ((M >> 6) == 3) {
            pE->PutRegister ((UINT32) (M & 7), pValue, 16, FALSE);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, SegForRm (M), EA, &Lin);
            pE->Store (pValue, Lin, 16);
        }
    }

    // 8-bit register access. The byte encoding is AL,CL,DL,BL,AH,CH,DH,BH: index & 3 selects the
    // GPR (AX..BX), index >= 4 selects its high byte. The register file holds 16-bit GPRs.
    VOID EmitReg8Read (ICpuEmitter *pE, UINT8 Reg, ICpuValue **ppVal) {
        ComPtr<ICpuValue> Full; pE->GetRegister ((UINT32) (Reg & 3), 16, &Full);
        if (Reg >= 4) {
            ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
            ComPtr<ICpuValue> Hi; pE->BinaryOp (BinLShr, Full, Sh, &Hi);
            pE->Cast (CastTrunc, Hi, 8, ppVal);
        } else {
            pE->Cast (CastTrunc, Full, 8, ppVal);
        }
    }
    VOID EmitReg8Write (ICpuEmitter *pE, UINT8 Reg, ICpuValue *pVal) {
        UINT32 Gpr  = Reg & 3;
        bool   High = Reg >= 4;
        ComPtr<ICpuValue> Cur;  pE->GetRegister (Gpr, 16, &Cur);
        ComPtr<ICpuValue> Mask; pE->ConstInt (16, High ? 0x00FF : 0xFF00, &Mask);
        ComPtr<ICpuValue> Kept; pE->BinaryOp (BinAnd, Cur, Mask, &Kept);
        ComPtr<ICpuValue> V16;     pE->Cast (CastZExt, pVal, 16, &V16);
        ComPtr<ICpuValue> Shifted;
        ICpuValue *Placed = V16;                                   // low byte: the value as-is
        if (High) {
            ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
            pE->BinaryOp (BinShl, V16, Sh, &Shifted);
            Placed = Shifted;                                      // high byte: shifted into place
        }
        ComPtr<ICpuValue> Res; pE->BinaryOp (BinOr, Kept, Placed, &Res);
        pE->PutRegister (Gpr, Res, 16, FALSE);
    }

    // 8-bit r/m: a byte register (mod=11) or segment-relative memory byte at the EA.
    VOID EmitRmRead8 (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue **ppVal) {
        if ((M >> 6) == 3) {
            EmitReg8Read (pE, (UINT8) (M & 7), ppVal);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, SegForRm (M), EA, &Lin);
            pE->Load (Lin, 8, ppVal);
        }
    }
    VOID EmitRmWrite8 (ICpuEmitter *pE, UINT8 M, CPU_ADDR ModRMPc, ICpuValue *pVal) {
        if ((M >> 6) == 3) {
            EmitReg8Write (pE, (UINT8) (M & 7), pVal);
        } else {
            ComPtr<ICpuValue> EA;  EmitEA (pE, M, ModRMPc + 1, &EA);
            ComPtr<ICpuValue> Lin; EmitSegLinear (pE, SegForRm (M), EA, &Lin);
            pE->Store (pVal, Lin, 8);
        }
    }

    // grp2 shift/rotate. Sub: 0 ROL,1 ROR,2 RCL,3 RCR,4/6 SHL,5 SHR,7 SAR. Width is 8 or 16.
    // pCount is a Width-bit count, masked to 5 bits (V20/V30/186 behaviour); correct for counts
    // 1..Width. CF is the last bit moved out; SHL/SHR/SAR also set Z/S/P. OF (only defined for a
    // 1-bit count on real hardware) is left cleared.
    VOID EmitShift (ICpuEmitter *pE, UINT8 Sub, UINT32 Width, ICpuValue *pVal, ICpuValue *pCountRaw, ICpuValue **ppRes) {
        ComPtr<ICpuValue> M5;    pE->ConstInt (Width, 0x1F, &M5);
        ComPtr<ICpuValue> Count; pE->BinaryOp (BinAnd, pCountRaw, M5, &Count);
        ComPtr<ICpuValue> WidthV; pE->ConstInt (Width, Width, &WidthV);
        ComPtr<ICpuValue> One;    pE->ConstInt (Width, 1, &One);
        ComPtr<ICpuValue> WmC;    pE->BinaryOp (BinSub, WidthV, Count, &WmC);   // Width - Count
        ComPtr<ICpuValue> Cm1;    pE->BinaryOp (BinSub, Count, One, &Cm1);      // Count - 1
        bool Shift = (Sub >= 4);
        if (Sub == 4 || Sub == 6) {                                            // SHL
            pE->BinaryOp (BinShl, pVal, Count, ppRes);
            ComPtr<ICpuValue> Sh; pE->BinaryOp (BinLShr, pVal, WmC, &Sh);
            ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Sh, 1, &CF); pE->SetFlag (FlagCarry, CF);
        } else if (Sub == 5 || Sub == 7) {                                     // SHR / SAR
            pE->BinaryOp (Sub == 5 ? BinLShr : BinAShr, pVal, Count, ppRes);
            ComPtr<ICpuValue> Sh; pE->BinaryOp (BinLShr, pVal, Cm1, &Sh);
            ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Sh, 1, &CF); pE->SetFlag (FlagCarry, CF);
        } else if (Sub == 0) {                                                 // ROL
            ComPtr<ICpuValue> L; pE->BinaryOp (BinShl, pVal, Count, &L);
            ComPtr<ICpuValue> R; pE->BinaryOp (BinLShr, pVal, WmC, &R);
            pE->BinaryOp (BinOr, L, R, ppRes);
            ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, *ppRes, 1, &CF); pE->SetFlag (FlagCarry, CF);
        } else if (Sub == 1) {                                                 // ROR
            ComPtr<ICpuValue> R; pE->BinaryOp (BinLShr, pVal, Count, &R);
            ComPtr<ICpuValue> L; pE->BinaryOp (BinShl, pVal, WmC, &L);
            pE->BinaryOp (BinOr, R, L, ppRes);
            ComPtr<ICpuValue> ShM; pE->ConstInt (Width, Width - 1, &ShM);
            ComPtr<ICpuValue> Hi;  pE->BinaryOp (BinLShr, *ppRes, ShM, &Hi);
            ComPtr<ICpuValue> CF;  pE->Cast (CastTrunc, Hi, 1, &CF); pE->SetFlag (FlagCarry, CF);
        } else {                                                               // RCL (2) / RCR (3) through carry
            ComPtr<ICpuValue> CFin;  pE->GetFlag (FlagCarry, &CFin);
            ComPtr<ICpuValue> CFinW; pE->Cast (CastZExt, CFin, Width, &CFinW);
            ComPtr<ICpuValue> Wp1;   pE->ConstInt (Width, Width + 1, &Wp1);
            ComPtr<ICpuValue> Wp1mC; pE->BinaryOp (BinSub, Wp1, Count, &Wp1mC);
            if (Sub == 2) {                                                    // RCL: {CF,val} rotate left
                ComPtr<ICpuValue> L; pE->BinaryOp (BinShl, pVal, Count, &L);
                ComPtr<ICpuValue> Cb; pE->BinaryOp (BinShl, CFinW, Cm1, &Cb);
                ComPtr<ICpuValue> R; pE->BinaryOp (BinLShr, pVal, Wp1mC, &R);
                ComPtr<ICpuValue> T; pE->BinaryOp (BinOr, L, Cb, &T);
                pE->BinaryOp (BinOr, T, R, ppRes);
                ComPtr<ICpuValue> Sh; pE->BinaryOp (BinLShr, pVal, WmC, &Sh);
                ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Sh, 1, &CF); pE->SetFlag (FlagCarry, CF);
            } else {                                                           // RCR
                ComPtr<ICpuValue> R; pE->BinaryOp (BinLShr, pVal, Count, &R);
                ComPtr<ICpuValue> Cb; pE->BinaryOp (BinShl, CFinW, WmC, &Cb);
                ComPtr<ICpuValue> L; pE->BinaryOp (BinShl, pVal, Wp1mC, &L);
                ComPtr<ICpuValue> T; pE->BinaryOp (BinOr, R, Cb, &T);
                pE->BinaryOp (BinOr, T, L, ppRes);
                ComPtr<ICpuValue> Sh; pE->BinaryOp (BinLShr, pVal, Cm1, &Sh);
                ComPtr<ICpuValue> CF; pE->Cast (CastTrunc, Sh, 1, &CF); pE->SetFlag (FlagCarry, CF);
            }
        }
        ComPtr<ICpuValue> Zero1; pE->ConstInt (1, 0, &Zero1); pE->SetFlag (FlagOverflow, Zero1);
        if (Shift) {
            if (Width == 16) { EmitZSF (pE, *ppRes); } else { EmitZSF8 (pE, *ppRes); }
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
    int               m_SegOv   = -1;              // active segment-override prefix (a Reg index), or -1
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
