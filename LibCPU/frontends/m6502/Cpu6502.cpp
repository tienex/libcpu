/** @file
  MOS 6502 frontend implementation (initial slice). See Cpu6502.h.

  Supported opcodes in this slice:
    A9  LDA #imm     A5  LDA $zp     85  STA $zp
    E6  INC $zp      69  ADC #imm
  All are two bytes. Decimal mode is not modeled (binary ADC).
**/
#include "Cpu6502.h"
#include <cstdio>

namespace LibCPU {
namespace {

//
// Emit the standard 6502 N/Z flag update for an 8-bit result.
//
static VOID
EmitSetNZ (ICpuEmitter *pE, ICpuValue *pResult)
{
    // Z = (Result == 0)
    ComPtr<ICpuValue> Zero;   pE->ConstInt (8, 0, &Zero);
    ComPtr<ICpuValue> ZFlag;  pE->Compare (CmpEq, pResult, Zero, &ZFlag);
    pE->SetFlag (FlagZero, ZFlag);

    // N = (Result & 0x80) != 0
    ComPtr<ICpuValue> Bit7;   pE->ConstInt (8, 0x80, &Bit7);
    ComPtr<ICpuValue> Masked; pE->BinaryOp (BinAnd, pResult, Bit7, &Masked);
    ComPtr<ICpuValue> Zero2;  pE->ConstInt (8, 0, &Zero2);
    ComPtr<ICpuValue> NFlag;  pE->Compare (CmpNe, Masked, Zero2, &NFlag);
    pE->SetFlag (FlagNegative, NFlag);
}

class Cpu6502 final : public LcComObject<ICpuArchitecture> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = "6502";
        pInfo->pFullName   = "MOS 6502";
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = 8;
        pInfo->AddressSize = 16;
        pInfo->PsrSize     = 8;
        pInfo->IsBigEndian = FALSE;
        pInfo->GprCount    = 4;
        pInfo->GprBits     = 8;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode    = pBase;
        m_CodeSize = Size;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        // Every opcode in this slice is two bytes. BNE is a relative conditional
        // branch; everything else continues linearly.
        *pNextPc = Pc + 2;
        if (m_pCode[Pc] == 0xD0) {   // BNE rel
            *pTag   = TagConditional | TagBranch;
            *pNewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);   // signed displacement
        } else {
            *pTag   = TagContinue;
            *pNewPc = (CPU_ADDR) -1;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT8 Opcode = m_pCode[Pc];
        UINT8 Op1    = m_pCode[Pc + 1];
        CHAR8 CONST *pMnem;
        switch (Opcode) {
        case 0xA9: std::snprintf (pLine, MaxLine, "lda #$%02x", Op1); return S_OK;
        case 0xA5: std::snprintf (pLine, MaxLine, "lda $%02x", Op1);  return S_OK;
        case 0x85: std::snprintf (pLine, MaxLine, "sta $%02x", Op1);  return S_OK;
        case 0xE6: std::snprintf (pLine, MaxLine, "inc $%02x", Op1);  return S_OK;
        case 0x69: std::snprintf (pLine, MaxLine, "adc #$%02x", Op1); return S_OK;
        case 0xD0: std::snprintf (pLine, MaxLine, "bne $%04x", (unsigned) (Pc + 2 + (INT8) Op1)); return S_OK;
        default:   pMnem = "???"; std::snprintf (pLine, MaxLine, "%s ($%02x)", pMnem, Opcode); return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT8 Opcode = m_pCode[Pc];
        UINT8 Op1    = m_pCode[Pc + 1];

        switch (Opcode) {
        case 0xA9: {   // LDA #imm
            ComPtr<ICpuValue> Value; pE->ConstInt (8, Op1, &Value);
            pE->PutRegister (Reg6502A, Value, 8, FALSE);
            EmitSetNZ (pE, Value);
            break;
        }
        case 0xA5: {   // LDA $zp
            ComPtr<ICpuValue> Addr;  pE->ConstInt (16, Op1, &Addr);
            ComPtr<ICpuValue> Value; pE->Load (Addr, 8, &Value);
            pE->PutRegister (Reg6502A, Value, 8, FALSE);
            EmitSetNZ (pE, Value);
            break;
        }
        case 0x85: {   // STA $zp
            ComPtr<ICpuValue> Acc;  pE->GetRegister (Reg6502A, 8, &Acc);
            ComPtr<ICpuValue> Addr; pE->ConstInt (16, Op1, &Addr);
            pE->Store (Acc, Addr, 8);
            break;
        }
        case 0xE6: {   // INC $zp
            ComPtr<ICpuValue> Addr; pE->ConstInt (16, Op1, &Addr);
            ComPtr<ICpuValue> Cur;  pE->Load (Addr, 8, &Cur);
            ComPtr<ICpuValue> One;  pE->ConstInt (8, 1, &One);
            ComPtr<ICpuValue> Res;  pE->BinaryOp (BinAdd, Cur, One, &Res);
            pE->Store (Res, Addr, 8);
            EmitSetNZ (pE, Res);
            break;
        }
        case 0x69: {   // ADC #imm (binary mode)
            ComPtr<ICpuValue> Acc;   pE->GetRegister (Reg6502A, 8, &Acc);
            ComPtr<ICpuValue> Acc16; pE->Cast (CastZExt, Acc, 16, &Acc16);
            ComPtr<ICpuValue> Imm16; pE->ConstInt (16, Op1, &Imm16);
            ComPtr<ICpuValue> Carry; pE->GetFlag (FlagCarry, &Carry);
            ComPtr<ICpuValue> Car16; pE->Cast (CastZExt, Carry, 16, &Car16);
            ComPtr<ICpuValue> Sum1;  pE->BinaryOp (BinAdd, Acc16, Imm16, &Sum1);
            ComPtr<ICpuValue> Sum;   pE->BinaryOp (BinAdd, Sum1, Car16, &Sum);   // 16-bit sum
            ComPtr<ICpuValue> Res;   pE->Cast (CastTrunc, Sum, 8, &Res);          // 8-bit result
            pE->PutRegister (Reg6502A, Res, 8, FALSE);

            // Carry out = bit 8 of the 16-bit sum.
            ComPtr<ICpuValue> Eight; pE->ConstInt (16, 8, &Eight);
            ComPtr<ICpuValue> Shr;   pE->BinaryOp (BinLShr, Sum, Eight, &Shr);
            ComPtr<ICpuValue> CFlag; pE->Cast (CastTrunc, Shr, 1, &CFlag);
            pE->SetFlag (FlagCarry, CFlag);

            // Overflow = ((A ^ R) & (Imm ^ R) & 0x80) != 0.
            ComPtr<ICpuValue> Imm8;  pE->ConstInt (8, Op1, &Imm8);
            ComPtr<ICpuValue> AxR;   pE->BinaryOp (BinXor, Acc, Res, &AxR);
            ComPtr<ICpuValue> IxR;   pE->BinaryOp (BinXor, Imm8, Res, &IxR);
            ComPtr<ICpuValue> And1;  pE->BinaryOp (BinAnd, AxR, IxR, &And1);
            ComPtr<ICpuValue> Bit7;  pE->ConstInt (8, 0x80, &Bit7);
            ComPtr<ICpuValue> And2;  pE->BinaryOp (BinAnd, And1, Bit7, &And2);
            ComPtr<ICpuValue> Zero;  pE->ConstInt (8, 0, &Zero);
            ComPtr<ICpuValue> VFlag; pE->Compare (CmpNe, And2, Zero, &VFlag);
            pE->SetFlag (FlagOverflow, VFlag);

            EmitSetNZ (pE, Res);
            break;
        }
        default:
            // Unknown opcode in this slice: emit nothing.
            break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        if (m_pCode[Pc] != 0xD0) {   // only BNE is a branch in this slice
            return E_NOTIMPL;
        }
        // BNE is taken when the Zero flag is clear, i.e. the condition is (Z == 0).
        ComPtr<ICpuValue> Z;
        pE->GetFlag (FlagZero, &Z);
        return pE->UnaryOp (UnNot, Z, ppCond);   // ownership transferred to caller
    }

private:
    UINT8 CONST *m_pCode    = nullptr;
    UINT64       m_CodeSize = 0;
};

} // anonymous namespace

ICpuArchitecture *
Create6502 (VOID)
{
    return new Cpu6502 ();
}

} // namespace LibCPU
