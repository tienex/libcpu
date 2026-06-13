/** @file
  MOS 6502 frontend implementation (initial slice). See Cpu6502.h.

  Supported opcodes in this slice:
    A9  LDA #imm     A5  LDA $zp     85  STA $zp     8D  STA $abs
    E6  INC $zp      C6  DEC $zp     69  ADC #imm    18  CLC
    D0  BNE rel      4C  JMP $abs    20  JSR $abs    60  RTS
  Decimal mode is not modeled (binary ADC). JSR/RTS use the page-1 stack and
  drive the in-artifact PC dispatcher exactly as the V20 CALL/RET do, so the
  whole-program AOT / tiering / PGO machinery is frontend-agnostic.
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

class Cpu6502 final : public ComObject<ICpuArchitecture> {
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
        UINT8    Op    = m_pCode[Pc];
        UINT32   Len   = 2;                       // most of the slice is two bytes
        UINT32   Tag   = TagContinue;
        CPU_ADDR NewPc = (CPU_ADDR) -1;
        UINT16   Abs   = (UINT16) (m_pCode[Pc + 1] | (m_pCode[Pc + 2] << 8));   // abs operand
        switch (Op) {
        case 0x8D:                                // STA $abs
            Len = 3;
            break;
        case 0x4C:                                // JMP $abs
            Len = 3; Tag = TagBranch; NewPc = (CPU_ADDR) Abs;
            break;
        case 0x20:                                // JSR $abs (call: target known, ret = Pc+3)
            Len = 3; Tag = TagCall; NewPc = (CPU_ADDR) Abs;
            break;
        case 0x60:                                // RTS (indirect: target popped at run time)
            Len = 1; Tag = TagReturn;
            break;
        case 0x18:                                // CLC
            Len = 1;
            break;
        case 0xD0:                                // BNE rel
            Len = 2; Tag = TagConditional | TagBranch;
            NewPc = (CPU_ADDR) (Pc + 2 + (INT8) m_pCode[Pc + 1]);   // signed displacement
            break;
        default:                                  // LDA/STA/INC/DEC/ADC: 2-byte, linear
            break;
        }
        *pNextPc = Pc + Len;
        *pTag    = Tag;
        *pNewPc  = NewPc;
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
        case 0x8D: std::snprintf (pLine, MaxLine, "sta $%04x", (unsigned) (Op1 | (m_pCode[Pc + 2] << 8))); return S_OK;
        case 0xE6: std::snprintf (pLine, MaxLine, "inc $%02x", Op1);  return S_OK;
        case 0xC6: std::snprintf (pLine, MaxLine, "dec $%02x", Op1);  return S_OK;
        case 0x69: std::snprintf (pLine, MaxLine, "adc #$%02x", Op1); return S_OK;
        case 0x18: std::snprintf (pLine, MaxLine, "clc");             return S_OK;
        case 0xD0: std::snprintf (pLine, MaxLine, "bne $%04x", (unsigned) (Pc + 2 + (INT8) Op1)); return S_OK;
        case 0x4C: std::snprintf (pLine, MaxLine, "jmp $%04x", (unsigned) (Op1 | (m_pCode[Pc + 2] << 8))); return S_OK;
        case 0x20: std::snprintf (pLine, MaxLine, "jsr $%04x", (unsigned) (Op1 | (m_pCode[Pc + 2] << 8))); return S_OK;
        case 0x60: std::snprintf (pLine, MaxLine, "rts");            return S_OK;
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
        case 0x8D: {   // STA $abs (3-byte: opcode, lo, hi)
            ComPtr<ICpuValue> Acc;  pE->GetRegister (Reg6502A, 8, &Acc);
            ComPtr<ICpuValue> Addr; pE->ConstInt (16, Op1 | (m_pCode[Pc + 2] << 8), &Addr);
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
        case 0xC6: {   // DEC $zp
            ComPtr<ICpuValue> Addr; pE->ConstInt (16, Op1, &Addr);
            ComPtr<ICpuValue> Cur;  pE->Load (Addr, 8, &Cur);
            ComPtr<ICpuValue> One;  pE->ConstInt (8, 1, &One);
            ComPtr<ICpuValue> Res;  pE->BinaryOp (BinSub, Cur, One, &Res);
            pE->Store (Res, Addr, 8);
            EmitSetNZ (pE, Res);
            break;
        }
        case 0x18: {   // CLC
            ComPtr<ICpuValue> Zero; pE->ConstInt (1, 0, &Zero);
            pE->SetFlag (FlagCarry, Zero);
            break;
        }
        case 0x4C:     // JMP $abs -- no data effect; the branch is wired from TagInstr.
            break;
        case 0x20: {   // JSR $abs: push (Pc+2) hi, lo to the page-1 stack; S -= 2
            // The 6502 pushes the address of the JSR's last byte (return - 1); RTS
            // adds 1 back. The driver branches to the callee (TagCall).
            UINT16 Ret1 = (UINT16) (Pc + 2);
            ComPtr<ICpuValue> Base; pE->ConstInt (16, 0x100, &Base);
            ComPtr<ICpuValue> One;  pE->ConstInt (8, 1, &One);
            // push high byte at $0100 + S
            ComPtr<ICpuValue> S;    pE->GetRegister (Reg6502S, 8, &S);
            ComPtr<ICpuValue> S16;  pE->Cast (CastZExt, S, 16, &S16);
            ComPtr<ICpuValue> AddrH; pE->BinaryOp (BinAdd, Base, S16, &AddrH);
            ComPtr<ICpuValue> Hi;   pE->ConstInt (8, (Ret1 >> 8) & 0xFF, &Hi);
            pE->Store (Hi, AddrH, 8);
            // push low byte at $0100 + (S - 1)
            ComPtr<ICpuValue> S1;   pE->BinaryOp (BinSub, S, One, &S1);
            ComPtr<ICpuValue> S1_16; pE->Cast (CastZExt, S1, 16, &S1_16);
            ComPtr<ICpuValue> AddrL; pE->BinaryOp (BinAdd, Base, S1_16, &AddrL);
            ComPtr<ICpuValue> Lo;   pE->ConstInt (8, Ret1 & 0xFF, &Lo);
            pE->Store (Lo, AddrL, 8);
            // S -= 2
            ComPtr<ICpuValue> S2;   pE->BinaryOp (BinSub, S1, One, &S2);
            pE->PutRegister (Reg6502S, S2, 8, FALSE);
            break;
        }
        case 0x60: {   // RTS: pull lo, hi from the page-1 stack; target = addr + 1
            ComPtr<ICpuValue> Base; pE->ConstInt (16, 0x100, &Base);
            ComPtr<ICpuValue> One;  pE->ConstInt (8, 1, &One);
            // S += 1; lo = [$0100 + S]
            ComPtr<ICpuValue> S;    pE->GetRegister (Reg6502S, 8, &S);
            ComPtr<ICpuValue> S1;   pE->BinaryOp (BinAdd, S, One, &S1);
            ComPtr<ICpuValue> S1_16; pE->Cast (CastZExt, S1, 16, &S1_16);
            ComPtr<ICpuValue> AddrL; pE->BinaryOp (BinAdd, Base, S1_16, &AddrL);
            ComPtr<ICpuValue> Lo;   pE->Load (AddrL, 8, &Lo);
            // S += 2; hi = [$0100 + S]
            ComPtr<ICpuValue> S2;   pE->BinaryOp (BinAdd, S1, One, &S2);
            ComPtr<ICpuValue> S2_16; pE->Cast (CastZExt, S2, 16, &S2_16);
            ComPtr<ICpuValue> AddrH; pE->BinaryOp (BinAdd, Base, S2_16, &AddrH);
            ComPtr<ICpuValue> Hi;   pE->Load (AddrH, 8, &Hi);
            pE->PutRegister (Reg6502S, S2, 8, FALSE);
            // target = (hi << 8 | lo) + 1
            ComPtr<ICpuValue> Lo16; pE->Cast (CastZExt, Lo, 16, &Lo16);
            ComPtr<ICpuValue> Hi16; pE->Cast (CastZExt, Hi, 16, &Hi16);
            ComPtr<ICpuValue> Eight; pE->ConstInt (16, 8, &Eight);
            ComPtr<ICpuValue> HiSh; pE->BinaryOp (BinShl, Hi16, Eight, &HiSh);
            ComPtr<ICpuValue> Word; pE->BinaryOp (BinOr, HiSh, Lo16, &Word);
            ComPtr<ICpuValue> OneW; pE->ConstInt (16, 1, &OneW);
            ComPtr<ICpuValue> Tgt;  pE->BinaryOp (BinAdd, Word, OneW, &Tgt);
            // Hand the run-time return target to the in-artifact dispatcher (optional
            // capability; same path the V20 RET uses).
            ICpuSmcEmitter *pFlow = nullptr;
            if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                pFlow->SetDispatchTarget (Tgt);
                pFlow->Release ();
            }
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
