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
#include <cstdio>

namespace LibCPU {
namespace {

static UINT16
Imm16At (UINT8 CONST *pCode, CPU_ADDR Pc)
{
    return (UINT16) (pCode[Pc] | (pCode[Pc + 1] << 8));
}

// Z (FlagZero) and S (FlagNegative = bit 15) for a 16-bit result.
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

class CpuV20 final : public LcComObject<ICpuArchitecture> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = "v20";
        pInfo->pFullName   = "NEC V20/V30 (uPD70108/uPD70116)";
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
        m_pCode    = pBase;
        m_CodeSize = Size;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        UINT8    Op   = m_pCode[Pc];
        UINT32   Len;
        UINT32   Tag  = TagContinue;
        CPU_ADDR NewPc = (CPU_ADDR) -1;

        if (Op >= 0xB8 && Op <= 0xBF) {                          // MOV reg16,imm16
            Len = 3;
        } else if (Op >= 0x40 && Op <= 0x4F) {                   // INC/DEC reg16
            Len = 1;
        } else if (Op == 0x01 || Op == 0x29 || Op == 0x39 || Op == 0x89 || Op == 0x8B) {
            Len = 2;                                             // <alu> r/m16,r16 (ModR/M)
        } else if (Op == 0x05 || Op == 0x2D || Op == 0x3D || Op == 0xA1 || Op == 0xA3) {
            Len = 3;                                             // acc,imm16 / MOV AX,[addr16]
        } else if (Op == 0xEB) {                                 // JMP rel8
            Len = 2; Tag = TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0xE9) {                                 // JMP rel16
            Len = 3; Tag = TagBranch; NewPc = (CPU_ADDR) (Pc + 3 + (INT16) Imm16At (m_pCode, Pc + 1));
        } else if (Op >= 0x70 && Op <= 0x7F) {                   // Jcc rel8
            Len = 2; Tag = TagConditional | TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0xE2) {                                 // LOOP rel8
            Len = 2; Tag = TagConditional | TagBranch; NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);
        } else if (Op == 0x0F) {                                 // NEC bit op: 0F xx modrm ib
            Len = 4;
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
        if (Op >= 0xB8 && Op <= 0xBF) {
            std::snprintf (pLine, MaxLine, "mov %s,0x%04x", RegName (Op - 0xB8), Imm16At (m_pCode, Pc + 1));
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

        if (Op >= 0xB8 && Op <= 0xBF) {            // MOV reg16, imm16
            ComPtr<ICpuValue> V; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &V);
            pE->PutRegister (Op - 0xB8, V, 16, FALSE);
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

        switch (Op) {
        case 0x01: case 0x29: case 0x39: case 0x89: case 0x8B: {   // <alu> r/m16,r16 (mod=11)
            UINT8 M   = m_pCode[Pc + 1];
            UINT32 Rm = M & 7, Reg = (M >> 3) & 7;
            if (Op == 0x8B) {                                      // MOV r16, r/m16
                ComPtr<ICpuValue> S; pE->GetRegister (Rm, 16, &S);
                pE->PutRegister (Reg, S, 16, FALSE);
                break;
            }
            if (Op == 0x89) {                                      // MOV r/m16, r16
                ComPtr<ICpuValue> S; pE->GetRegister (Reg, 16, &S);
                pE->PutRegister (Rm, S, 16, FALSE);
                break;
            }
            ComPtr<ICpuValue> D; pE->GetRegister (Rm, 16, &D);
            ComPtr<ICpuValue> S; pE->GetRegister (Reg, 16, &S);
            ComPtr<ICpuValue> Res;
            pE->BinaryOp (Op == 0x01 ? BinAdd : BinSub, D, S, &Res);
            if (Op != 0x39) {                                      // CMP discards the result
                pE->PutRegister (Rm, Res, 16, FALSE);
            }
            if (Op == 0x01) { EmitAddFlags (pE, D, S, Res); } else { EmitSubFlags (pE, D, S, Res); }
            break;
        }
        case 0x05: case 0x2D: case 0x3D: {                         // <alu> AX, imm16
            ComPtr<ICpuValue> A; pE->GetRegister (RegV20AX, 16, &A);
            ComPtr<ICpuValue> B; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &B);
            ComPtr<ICpuValue> Res;
            pE->BinaryOp (Op == 0x05 ? BinAdd : BinSub, A, B, &Res);
            if (Op != 0x3D) {
                pE->PutRegister (RegV20AX, Res, 16, FALSE);
            }
            if (Op == 0x05) { EmitAddFlags (pE, A, B, Res); } else { EmitSubFlags (pE, A, B, Res); }
            break;
        }
        case 0xA1: {                                               // MOV AX, [addr16]
            ComPtr<ICpuValue> Ad; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            ComPtr<ICpuValue> V;  pE->Load (Ad, 16, &V);
            pE->PutRegister (RegV20AX, V, 16, FALSE);
            break;
        }
        case 0xA3: {                                               // MOV [addr16], AX
            ComPtr<ICpuValue> V;  pE->GetRegister (RegV20AX, 16, &V);
            ComPtr<ICpuValue> Ad; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &Ad);
            pE->Store (V, Ad, 16);
            break;
        }
        case 0xE2: {                                               // LOOP: CX-- (no flags)
            ComPtr<ICpuValue> C;   pE->GetRegister (RegV20CX, 16, &C);
            ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinSub, C, One, &Res);
            pE->PutRegister (RegV20CX, Res, 16, FALSE);
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

    UINT8 CONST *m_pCode    = nullptr;
    UINT64       m_CodeSize = 0;
};

} // anonymous namespace

ICpuArchitecture *
CreateV20 (VOID)
{
    return new CpuV20 ();
}

} // namespace LibCPU
