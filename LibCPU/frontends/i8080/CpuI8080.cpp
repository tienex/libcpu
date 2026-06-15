/** @file
  Intel 8080 / 8085 frontend (see CpuI8080.h). Fixed-length opcodes, flat 16-bit memory, the
  8-bit register file A,B,C,D,E,H,L + 16-bit SP, and the S/Z/P/CY condition flags. Control flow
  (JMP/Jcond/CALL/Ccond/RET/Rcond/RST/PCHL) is wired through the driver via TagInstr +
  TranslateCond, the same model the V20 frontend uses; absolute targets instead of relative.
**/

#include "CpuI8080.h"
#include "LibCPU/PCom.h"
#include "LibCPU/CpuState.h"
#include <cstdio>

namespace LibCPU {

namespace {

static UINT16 Imm16At (UINT8 CONST *p, CPU_ADDR Pc) { return (UINT16) (p[Pc] | (p[Pc + 1] << 8)); }

// Opcode 3-bit register field order is B,C,D,E,H,L,M,A; map it to our register indices. 0xFF
// is the memory operand M = the byte at [HL].
enum { I8M = 0xFF };
static UINT8 RegOf (UINT8 Enc)
{
    static UINT8 CONST kEnc[8] = { RegI8080B, RegI8080C, RegI8080D, RegI8080E,
                                   RegI8080H, RegI8080L, I8M, RegI8080A };
    return kEnc[Enc & 7];
}

static CHAR8 CONST *RegName (UINT8 Enc)
{
    static CHAR8 CONST *kN[8] = { "b", "c", "d", "e", "h", "l", "m", "a" };
    return kN[Enc & 7];
}

class CpuI8080 final : public ComObject<ICpuArchitecture> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }

    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = "8080";
        pInfo->pFullName   = "Intel 8080/8085";
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = 8;
        pInfo->AddressSize = 16;
        pInfo->PsrSize     = 8;
        pInfo->IsBigEndian = FALSE;
        pInfo->GprCount    = 8;
        pInfo->GprBits     = 8;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase;                               // flat 16-bit address space
        m_CodeSize = Size;
        return S_OK;
    }

    // ---- instruction length + control-flow classification --------------------
    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        UINT8    Op  = m_pCode[Pc];
        UINT32   Len = InstrLen (Op);
        UINT32   Tag = TagContinue;
        CPU_ADDR NewPc = (CPU_ADDR) -1;
        UINT16   Abs = Imm16At (m_pCode, Pc + 1);          // absolute target for 3-byte flow ops

        switch (Op) {
            case 0xC3: case 0xCB:                          // JMP addr
                Tag = TagBranch; NewPc = Abs; break;
            case 0xC2: case 0xCA: case 0xD2: case 0xDA:    // Jcc addr
            case 0xE2: case 0xEA: case 0xF2: case 0xFA:
                Tag = TagConditional | TagBranch; NewPc = Abs; break;
            case 0xCD: case 0xDD: case 0xED: case 0xFD:    // CALL addr
                Tag = TagCall; NewPc = Abs; break;
            case 0xC4: case 0xCC: case 0xD4: case 0xDC:    // Ccc addr
            case 0xE4: case 0xEC: case 0xF4: case 0xFC:
                Tag = TagConditional | TagCall; NewPc = Abs; break;
            case 0xC9: case 0xD9:                          // RET
                Tag = TagReturn; break;
            case 0xC0: case 0xC8: case 0xD0: case 0xD8:    // Rcc
            case 0xE0: case 0xE8: case 0xF0: case 0xF8:
                Tag = TagConditional | TagReturn; break;
            case 0xC7: case 0xCF: case 0xD7: case 0xDF:    // RST n -> call n*8
            case 0xE7: case 0xEF: case 0xF7: case 0xFF:
                Tag = TagCall; NewPc = (CPU_ADDR) (Op & 0x38); break;
            case 0xE9:                                     // PCHL -> indirect
                Tag = TagReturn; break;
            case 0xDB: case 0xD3:                          // IN / OUT
            case 0x76:                                     // HLT
            case 0xFB: case 0xF3:                          // EI / DI
                Tag = TagTrap; break;
            default: break;
        }
        *pNextPc = Pc + Len;
        *pTag    = Tag;
        *pNewPc  = NewPc;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT8 Op = m_pCode[Pc];
        UINT8 D8 = m_pCode[Pc + 1];
        UINT16 A16 = Imm16At (m_pCode, Pc + 1);
        if (Op == 0x76) { std::snprintf (pLine, MaxLine, "hlt"); return S_OK; }
        if (Op >= 0x40 && Op <= 0x7F) {
            std::snprintf (pLine, MaxLine, "mov %s,%s", RegName ((Op >> 3) & 7), RegName (Op & 7)); return S_OK;
        }
        if ((Op & 0xC7) == 0x06) { std::snprintf (pLine, MaxLine, "mvi %s,0x%02x", RegName ((Op >> 3) & 7), D8); return S_OK; }
        if ((Op & 0xC7) == 0x04) { std::snprintf (pLine, MaxLine, "inr %s", RegName ((Op >> 3) & 7)); return S_OK; }
        if ((Op & 0xC7) == 0x05) { std::snprintf (pLine, MaxLine, "dcr %s", RegName ((Op >> 3) & 7)); return S_OK; }
        if (Op >= 0x80 && Op <= 0xBF) {
            static CHAR8 CONST *kAlu[8] = { "add", "adc", "sub", "sbb", "ana", "xra", "ora", "cmp" };
            std::snprintf (pLine, MaxLine, "%s %s", kAlu[(Op >> 3) & 7], RegName (Op & 7)); return S_OK;
        }
        switch (Op) {
            case 0x00: std::snprintf (pLine, MaxLine, "nop"); break;
            case 0x01: case 0x11: case 0x21: case 0x31:
                std::snprintf (pLine, MaxLine, "lxi %c,0x%04x", "bdhs"[(Op >> 4) & 3], A16); break;
            case 0xC6: std::snprintf (pLine, MaxLine, "adi 0x%02x", D8); break;
            case 0xCE: std::snprintf (pLine, MaxLine, "aci 0x%02x", D8); break;
            case 0xD6: std::snprintf (pLine, MaxLine, "sui 0x%02x", D8); break;
            case 0xDE: std::snprintf (pLine, MaxLine, "sbi 0x%02x", D8); break;
            case 0xE6: std::snprintf (pLine, MaxLine, "ani 0x%02x", D8); break;
            case 0xEE: std::snprintf (pLine, MaxLine, "xri 0x%02x", D8); break;
            case 0xF6: std::snprintf (pLine, MaxLine, "ori 0x%02x", D8); break;
            case 0xFE: std::snprintf (pLine, MaxLine, "cpi 0x%02x", D8); break;
            case 0x32: std::snprintf (pLine, MaxLine, "sta 0x%04x", A16); break;
            case 0x3A: std::snprintf (pLine, MaxLine, "lda 0x%04x", A16); break;
            case 0x22: std::snprintf (pLine, MaxLine, "shld 0x%04x", A16); break;
            case 0x2A: std::snprintf (pLine, MaxLine, "lhld 0x%04x", A16); break;
            case 0xC3: std::snprintf (pLine, MaxLine, "jmp 0x%04x", A16); break;
            case 0xCD: std::snprintf (pLine, MaxLine, "call 0x%04x", A16); break;
            case 0xC9: std::snprintf (pLine, MaxLine, "ret"); break;
            case 0xE9: std::snprintf (pLine, MaxLine, "pchl"); break;
            case 0xEB: std::snprintf (pLine, MaxLine, "xchg"); break;
            case 0xDB: std::snprintf (pLine, MaxLine, "in 0x%02x", D8); break;
            case 0xD3: std::snprintf (pLine, MaxLine, "out 0x%02x", D8); break;
            case 0xC5: case 0xD5: case 0xE5: case 0xF5:
                std::snprintf (pLine, MaxLine, "push %c", "bdhp"[(Op >> 4) & 3]); break;
            case 0xC1: case 0xD1: case 0xE1: case 0xF1:
                std::snprintf (pLine, MaxLine, "pop %c", "bdhp"[(Op >> 4) & 3]); break;
            default:
                if ((Op & 0xC7) == 0xC2) { std::snprintf (pLine, MaxLine, "jcc 0x%04x", A16); }
                else if ((Op & 0xC7) == 0xC4) { std::snprintf (pLine, MaxLine, "ccc 0x%04x", A16); }
                else if ((Op & 0xC7) == 0xC0) { std::snprintf (pLine, MaxLine, "rcc"); }
                else if ((Op & 0xC7) == 0xC7) { std::snprintf (pLine, MaxLine, "rst %u", (Op >> 3) & 7); }
                else { std::snprintf (pLine, MaxLine, "db 0x%02x", Op); }
                break;
        }
        return S_OK;
    }

    // ---- semantics ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT8 Op = m_pCode[Pc];

        if (Op == 0x00 || Op == 0x20 || Op == 0x30) { return S_OK; }        // NOP (and 8085 RIM/SIM stub)
        if (Op == 0x76) { return EmitSysTrap (pE, CPU_IO_HLT, Pc + 1); }    // HLT

        if (Op >= 0x40 && Op <= 0x7F) {                                     // MOV dst,src
            ComPtr<ICpuValue> S; EmitRegRead (pE, Op & 7, &S);
            EmitRegWrite (pE, (Op >> 3) & 7, S);
            return S_OK;
        }
        if ((Op & 0xC7) == 0x06) {                                         // MVI r,d8
            ComPtr<ICpuValue> V; pE->ConstInt (8, m_pCode[Pc + 1], &V);
            EmitRegWrite (pE, (Op >> 3) & 7, V);
            return S_OK;
        }
        if (Op >= 0x80 && Op <= 0xBF) {                                    // ALU A,r
            ComPtr<ICpuValue> Src; EmitRegRead (pE, Op & 7, &Src);
            EmitAlu (pE, (Op >> 3) & 7, Src);
            return S_OK;
        }
        if ((Op & 0xC7) == 0x04 || (Op & 0xC7) == 0x05) {                  // INR/DCR r
            ComPtr<ICpuValue> R; EmitRegRead (pE, (Op >> 3) & 7, &R);
            ComPtr<ICpuValue> One; pE->ConstInt (8, 1, &One);
            ComPtr<ICpuValue> Res;
            pE->BinaryOp ((Op & 1) ? BinSub : BinAdd, R, One, &Res);       // .04 INR, .05 DCR
            EmitRegWrite (pE, (Op >> 3) & 7, Res);
            EmitFlagsZSP (pE, Res);                                        // INR/DCR leave CY
            return S_OK;
        }

        switch (Op) {
            case 0xC6: case 0xCE: case 0xD6: case 0xDE:                    // ALU A,d8
            case 0xE6: case 0xEE: case 0xF6: case 0xFE: {
                ComPtr<ICpuValue> Imm; pE->ConstInt (8, m_pCode[Pc + 1], &Imm);
                EmitAlu (pE, (Op >> 3) & 7, Imm);
                break;
            }
            case 0x01: case 0x11: case 0x21: case 0x31: {                 // LXI rp,d16
                ComPtr<ICpuValue> V; pE->ConstInt (16, Imm16At (m_pCode, Pc + 1), &V);
                PutRp (pE, (Op >> 4) & 3, V);
                break;
            }
            case 0x03: case 0x13: case 0x23: case 0x33:                   // INX rp
            case 0x0B: case 0x1B: case 0x2B: case 0x3B: {                 // DCX rp
                ComPtr<ICpuValue> V; GetRp (pE, (Op >> 4) & 3, &V);
                ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
                ComPtr<ICpuValue> Res; pE->BinaryOp ((Op & 0x08) ? BinSub : BinAdd, V, One, &Res);
                PutRp (pE, (Op >> 4) & 3, Res);
                break;
            }
            case 0x09: case 0x19: case 0x29: case 0x39: {                 // DAD rp: HL += rp, set CY
                ComPtr<ICpuValue> Hl; GetRp (pE, 2, &Hl);
                ComPtr<ICpuValue> Rp; GetRp (pE, (Op >> 4) & 3, &Rp);
                ComPtr<ICpuValue> A32; pE->Cast (CastZExt, Hl, 32, &A32);
                ComPtr<ICpuValue> B32; pE->Cast (CastZExt, Rp, 32, &B32);
                ComPtr<ICpuValue> S32; pE->BinaryOp (BinAdd, A32, B32, &S32);
                ComPtr<ICpuValue> Res; pE->Cast (CastTrunc, S32, 16, &Res);
                PutRp (pE, 2, Res);
                ComPtr<ICpuValue> Sh; pE->ConstInt (32, 16, &Sh);
                ComPtr<ICpuValue> Hi; pE->BinaryOp (BinLShr, S32, Sh, &Hi);
                ComPtr<ICpuValue> CY; pE->Cast (CastTrunc, Hi, 1, &CY);
                pE->SetFlag (FlagCarry, CY);
                break;
            }
            case 0x32: { ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);          // STA addr
                         EmitStore8 (pE, Imm16At (m_pCode, Pc + 1), A); break; }
            case 0x3A: { ComPtr<ICpuValue> V; EmitLoad8 (pE, Imm16At (m_pCode, Pc + 1), &V);// LDA addr
                         pE->PutRegister (RegI8080A, V, 8, FALSE); break; }
            case 0x02: case 0x12: {                                       // STAX B/D
                ComPtr<ICpuValue> Addr; GetRp (pE, (Op >> 4) & 3, &Addr);
                ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);
                EmitStore8V (pE, Addr, A); break;
            }
            case 0x0A: case 0x1A: {                                       // LDAX B/D
                ComPtr<ICpuValue> Addr; GetRp (pE, (Op >> 4) & 3, &Addr);
                ComPtr<ICpuValue> V; EmitLoad8V (pE, Addr, &V);
                pE->PutRegister (RegI8080A, V, 8, FALSE); break;
            }
            case 0xEB: {                                                  // XCHG: HL <-> DE
                ComPtr<ICpuValue> H; pE->GetRegister (RegI8080H, 8, &H);
                ComPtr<ICpuValue> L; pE->GetRegister (RegI8080L, 8, &L);
                ComPtr<ICpuValue> D; pE->GetRegister (RegI8080D, 8, &D);
                ComPtr<ICpuValue> E; pE->GetRegister (RegI8080E, 8, &E);
                pE->PutRegister (RegI8080H, D, 8, FALSE); pE->PutRegister (RegI8080L, E, 8, FALSE);
                pE->PutRegister (RegI8080D, H, 8, FALSE); pE->PutRegister (RegI8080E, L, 8, FALSE);
                break;
            }
            case 0xF9: {                                                  // SPHL: SP = HL
                ComPtr<ICpuValue> Hl; GetRp (pE, 2, &Hl); pE->PutRegister (RegI8080SP, Hl, 16, FALSE); break;
            }
            case 0x07: case 0x0F: case 0x17: case 0x1F: EmitRotate (pE, Op); break;        // RLC/RRC/RAL/RAR
            case 0x2F: {                                                  // CMA: A = ~A
                ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);
                ComPtr<ICpuValue> R; pE->UnaryOp (UnCom, A, &R); pE->PutRegister (RegI8080A, R, 8, FALSE); break;
            }
            case 0x37: { ComPtr<ICpuValue> One; pE->ConstInt (1, 1, &One); pE->SetFlag (FlagCarry, One); break; }  // STC
            case 0x3F: { ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C);                  // CMC
                         ComPtr<ICpuValue> N; pE->UnaryOp (UnNot, C, &N); pE->SetFlag (FlagCarry, N); break; }
            case 0xC5: case 0xD5: case 0xE5: case 0xF5: EmitPush (pE, (Op >> 4) & 3); break; // PUSH rp/PSW
            case 0xC1: case 0xD1: case 0xE1: case 0xF1: EmitPop  (pE, (Op >> 4) & 3); break; // POP rp/PSW
            case 0xCD: case 0xDD: case 0xED: case 0xFD:                                     // CALL addr
                EmitCallPush (pE, (CPU_ADDR) (Pc + 3)); break;
            case 0xC4: case 0xCC: case 0xD4: case 0xDC:                                     // Ccc addr
            case 0xE4: case 0xEC: case 0xF4: case 0xFC:
                EmitCallPush (pE, (CPU_ADDR) (Pc + 3)); break;
            case 0xC7: case 0xCF: case 0xD7: case 0xDF:                                     // RST n
            case 0xE7: case 0xEF: case 0xF7: case 0xFF:
                EmitCallPush (pE, (CPU_ADDR) (Pc + 1)); break;
            case 0xC9: case 0xD9: EmitRet (pE); break;                                      // RET
            case 0xC0: case 0xC8: case 0xD0: case 0xD8:                                     // Rcc
            case 0xE0: case 0xE8: case 0xF0: case 0xF8:
                EmitRet (pE); break;
            case 0xE9: {                                                                    // PCHL -> dispatch HL
                ComPtr<ICpuValue> Hl; GetRp (pE, 2, &Hl);
                ICpuSmcEmitter *pFlow = nullptr;
                if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                    pFlow->SetDispatchTarget (Hl); pFlow->Release ();
                }
                break;
            }
            case 0xDB: EmitPortIn (pE, m_pCode[Pc + 1], Pc + 2); break;                      // IN port
            case 0xD3: EmitPortOut (pE, m_pCode[Pc + 1], Pc + 2); break;                     // OUT port
            case 0xFB: EmitSysTrap (pE, CPU_IO_STI, Pc + 1); break;                          // EI
            case 0xF3: EmitSysTrap (pE, CPU_IO_CLI, Pc + 1); break;                          // DI
            default: break;                                  // JMP/Jcc and unhandled: no data effect
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        UINT8 Cc = (m_pCode[Pc] >> 3) & 7;          // 0 NZ,1 Z,2 NC,3 C,4 PO,5 PE,6 P,7 M
        CPU_FLAG Flag = (Cc < 2) ? FlagZero : (Cc < 4) ? FlagCarry : (Cc < 6) ? FlagParity : FlagNegative;
        if ((Cc & 1) != 0) { return pE->GetFlag (Flag, ppCond); }   // odd condition: taken when flag SET
        ComPtr<ICpuValue> F; pE->GetFlag (Flag, &F);                // even condition: taken when flag CLEAR
        return pE->UnaryOp (UnNot, F, ppCond);
    }

private:
    UINT32 InstrLen (UINT8 Op) {
        // 3-byte: LXI / SHLD / LHLD / STA / LDA, JMP/Jcc, CALL/Ccc.
        if (Op == 0x01 || Op == 0x11 || Op == 0x21 || Op == 0x31 ||
            Op == 0x22 || Op == 0x2A || Op == 0x32 || Op == 0x3A ||
            Op == 0xC3 || Op == 0xCB || (Op & 0xC7) == 0xC2 ||
            Op == 0xCD || Op == 0xDD || Op == 0xED || Op == 0xFD || (Op & 0xC7) == 0xC4) {
            return 3;
        }
        // 2-byte: MVI, ALU-immediate, IN/OUT.
        if ((Op & 0xC7) == 0x06 || Op == 0xC6 || Op == 0xCE || Op == 0xD6 || Op == 0xDE ||
            Op == 0xE6 || Op == 0xEE || Op == 0xF6 || Op == 0xFE || Op == 0xDB || Op == 0xD3) {
            return 2;
        }
        return 1;
    }

    // Read/write an opcode register field (B,C,D,E,H,L,M,A); M is the byte at [HL].
    VOID EmitRegRead (ICpuEmitter *pE, UINT8 Enc, ICpuValue **ppVal) {
        UINT8 R = RegOf (Enc);
        if (R == I8M) { ComPtr<ICpuValue> Hl; GetRp (pE, 2, &Hl); EmitLoad8V (pE, Hl, ppVal); }
        else          { pE->GetRegister (R, 8, ppVal); }
    }
    VOID EmitRegWrite (ICpuEmitter *pE, UINT8 Enc, ICpuValue *pVal) {
        UINT8 R = RegOf (Enc);
        if (R == I8M) { ComPtr<ICpuValue> Hl; GetRp (pE, 2, &Hl); EmitStore8V (pE, Hl, pVal); }
        else          { pE->PutRegister (R, pVal, 8, FALSE); }
    }

    // 16-bit register pair: 0=BC,1=DE,2=HL,3=SP. (push/pop treat 3 as PSW; handled separately.)
    VOID GetRp (ICpuEmitter *pE, UINT8 Rp, ICpuValue **ppVal) {
        if (Rp == 3) { pE->GetRegister (RegI8080SP, 16, ppVal); return; }
        UINT8 Hi = (Rp == 0) ? RegI8080B : (Rp == 1) ? RegI8080D : RegI8080H;
        UINT8 Lo = (UINT8) (Hi + 1);
        ComPtr<ICpuValue> H; pE->GetRegister (Hi, 8, &H);
        ComPtr<ICpuValue> L; pE->GetRegister (Lo, 8, &L);
        ComPtr<ICpuValue> H16; pE->Cast (CastZExt, H, 16, &H16);
        ComPtr<ICpuValue> L16; pE->Cast (CastZExt, L, 16, &L16);
        ComPtr<ICpuValue> Sh;  pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiSh;pE->BinaryOp (BinShl, H16, Sh, &HiSh);
        pE->BinaryOp (BinOr, HiSh, L16, ppVal);
    }
    VOID PutRp (ICpuEmitter *pE, UINT8 Rp, ICpuValue *pVal) {
        if (Rp == 3) { pE->PutRegister (RegI8080SP, pVal, 16, FALSE); return; }
        UINT8 Hi = (Rp == 0) ? RegI8080B : (Rp == 1) ? RegI8080D : RegI8080H;
        UINT8 Lo = (UINT8) (Hi + 1);
        ComPtr<ICpuValue> Sh;  pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiSh;pE->BinaryOp (BinLShr, pVal, Sh, &HiSh);
        ComPtr<ICpuValue> H;   pE->Cast (CastTrunc, HiSh, 8, &H);
        ComPtr<ICpuValue> L;   pE->Cast (CastTrunc, pVal, 8, &L);
        pE->PutRegister (Hi, H, 8, FALSE);
        pE->PutRegister (Lo, L, 8, FALSE);
    }

    // 8-bit memory access at a constant / value address (flat, zero-extended to a linear addr).
    VOID EmitLoad8 (ICpuEmitter *pE, UINT16 Addr, ICpuValue **ppVal) {
        ComPtr<ICpuValue> A; pE->ConstInt (32, Addr, &A); pE->Load (A, 8, ppVal);
    }
    VOID EmitStore8 (ICpuEmitter *pE, UINT16 Addr, ICpuValue *pVal) {
        ComPtr<ICpuValue> A; pE->ConstInt (32, Addr, &A); pE->Store (pVal, A, 8);
    }
    VOID EmitLoad8V (ICpuEmitter *pE, ICpuValue *pAddr16, ICpuValue **ppVal) {
        ComPtr<ICpuValue> A; pE->Cast (CastZExt, pAddr16, 32, &A); pE->Load (A, 8, ppVal);
    }
    VOID EmitStore8V (ICpuEmitter *pE, ICpuValue *pAddr16, ICpuValue *pVal) {
        ComPtr<ICpuValue> A; pE->Cast (CastZExt, pAddr16, 32, &A); pE->Store (pVal, A, 8);
    }

    // S (bit7), Z (== 0), P (even parity of all 8 bits).
    VOID EmitFlagsZSP (ICpuEmitter *pE, ICpuValue *pRes) {
        ComPtr<ICpuValue> Zero; pE->ConstInt (8, 0, &Zero);
        ComPtr<ICpuValue> ZF;   pE->Compare (CmpEq, pRes, Zero, &ZF); pE->SetFlag (FlagZero, ZF);
        ComPtr<ICpuValue> B7;   pE->ConstInt (8, 0x80, &B7);
        ComPtr<ICpuValue> M;    pE->BinaryOp (BinAnd, pRes, B7, &M);
        ComPtr<ICpuValue> SF;   pE->Compare (CmpNe, M, Zero, &SF); pE->SetFlag (FlagNegative, SF);
        // parity: fold the byte down to one bit, PF = (count even).
        ComPtr<ICpuValue> S4; pE->ConstInt (8, 4, &S4);
        ComPtr<ICpuValue> R4; pE->BinaryOp (BinLShr, pRes, S4, &R4);
        ComPtr<ICpuValue> X1; pE->BinaryOp (BinXor, pRes, R4, &X1);
        ComPtr<ICpuValue> S2; pE->ConstInt (8, 2, &S2);
        ComPtr<ICpuValue> R2; pE->BinaryOp (BinLShr, X1, S2, &R2);
        ComPtr<ICpuValue> X2; pE->BinaryOp (BinXor, X1, R2, &X2);
        ComPtr<ICpuValue> S1; pE->ConstInt (8, 1, &S1);
        ComPtr<ICpuValue> R1; pE->BinaryOp (BinLShr, X2, S1, &R1);
        ComPtr<ICpuValue> X3; pE->BinaryOp (BinXor, X2, R1, &X3);
        ComPtr<ICpuValue> One;pE->ConstInt (8, 1, &One);
        ComPtr<ICpuValue> Lo; pE->BinaryOp (BinAnd, X3, One, &Lo);
        ComPtr<ICpuValue> PF; pE->Compare (CmpEq, Lo, Zero, &PF); pE->SetFlag (FlagParity, PF);   // even -> set
    }

    // A <op> operand, op = 0 ADD,1 ADC,2 SUB,3 SBB,4 ANA,5 XRA,6 ORA,7 CMP.
    VOID EmitAlu (ICpuEmitter *pE, UINT8 AluOp, ICpuValue *pB) {
        ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);
        if (AluOp >= 4 && AluOp <= 6) {                              // ANA / XRA / ORA
            ComPtr<ICpuValue> Res;
            CPU_BINOP B2 = (AluOp == 4) ? BinAnd : (AluOp == 5) ? BinXor : BinOr;
            pE->BinaryOp (B2, A, pB, &Res);
            pE->PutRegister (RegI8080A, Res, 8, FALSE);
            ComPtr<ICpuValue> Zero; pE->ConstInt (1, 0, &Zero); pE->SetFlag (FlagCarry, Zero);
            EmitFlagsZSP (pE, Res);
            return;
        }
        bool Sub = (AluOp == 2 || AluOp == 3 || AluOp == 7);         // SUB/SBB/CMP
        bool UseCarry = (AluOp == 1 || AluOp == 3);                  // ADC/SBB
        ComPtr<ICpuValue> A16; pE->Cast (CastZExt, A, 16, &A16);
        ComPtr<ICpuValue> B16; pE->Cast (CastZExt, pB, 16, &B16);
        ComPtr<ICpuValue> Base; pE->BinaryOp (Sub ? BinSub : BinAdd, A16, B16, &Base);
        ComPtr<ICpuValue> Carried;                                   // (only used when UseCarry)
        ICpuValue *Acc = Base;                                       // alias the chosen accumulator
        if (UseCarry) {
            ComPtr<ICpuValue> C;   pE->GetFlag (FlagCarry, &C);
            ComPtr<ICpuValue> C16; pE->Cast (CastZExt, C, 16, &C16);
            pE->BinaryOp (Sub ? BinSub : BinAdd, Base, C16, &Carried);
            Acc = Carried;
        }
        ComPtr<ICpuValue> Res; pE->Cast (CastTrunc, Acc, 8, &Res);
        ComPtr<ICpuValue> Sh;  pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> Hi;  pE->BinaryOp (BinLShr, Acc, Sh, &Hi);
        ComPtr<ICpuValue> CY;  pE->Cast (CastTrunc, Hi, 1, &CY);
        pE->SetFlag (FlagCarry, CY);
        if (AluOp != 7) { pE->PutRegister (RegI8080A, Res, 8, FALSE); }   // CMP discards the result
        EmitFlagsZSP (pE, Res);
    }

    VOID EmitRotate (ICpuEmitter *pE, UINT8 Op) {
        ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);
        ComPtr<ICpuValue> S7; pE->ConstInt (8, 7, &S7);
        ComPtr<ICpuValue> S1; pE->ConstInt (8, 1, &S1);
        if (Op == 0x07 || Op == 0x17) {                       // RLC / RAL (left)
            ComPtr<ICpuValue> Hi; pE->BinaryOp (BinLShr, A, S7, &Hi);          // old bit7 (in bit0)
            ComPtr<ICpuValue> Shl; pE->BinaryOp (BinShl, A, S1, &Shl);
            ComPtr<ICpuValue> InBit;
            if (Op == 0x07) { pE->BinaryOp (BinAnd, Hi, S1, &InBit); }         // RLC: wrap old bit7 in
            else { ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C); pE->Cast (CastZExt, C, 8, &InBit); }  // RAL: old carry in
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinOr, Shl, InBit, &Res);
            pE->PutRegister (RegI8080A, Res, 8, FALSE);
            ComPtr<ICpuValue> C1; pE->Cast (CastTrunc, Hi, 1, &C1); pE->SetFlag (FlagCarry, C1);
        } else {                                              // RRC / RAR (right)
            ComPtr<ICpuValue> Lo; pE->BinaryOp (BinAnd, A, S1, &Lo);           // old bit0
            ComPtr<ICpuValue> Shr; pE->BinaryOp (BinLShr, A, S1, &Shr);
            ComPtr<ICpuValue> InBit;
            if (Op == 0x0F) { pE->BinaryOp (BinShl, Lo, S7, &InBit); }         // RRC: wrap old bit0 in
            else { ComPtr<ICpuValue> C; pE->GetFlag (FlagCarry, &C); ComPtr<ICpuValue> C8; pE->Cast (CastZExt, C, 8, &C8); pE->BinaryOp (BinShl, C8, S7, &InBit); }
            ComPtr<ICpuValue> Res; pE->BinaryOp (BinOr, Shr, InBit, &Res);
            pE->PutRegister (RegI8080A, Res, 8, FALSE);
            ComPtr<ICpuValue> C1; pE->Cast (CastTrunc, Lo, 1, &C1); pE->SetFlag (FlagCarry, C1);
        }
    }

    // PUSH/POP: rp 0=BC,1=DE,2=HL,3=PSW. SP grows down; high byte at the higher address.
    VOID EmitPush (ICpuEmitter *pE, UINT8 Rp) {
        ComPtr<ICpuValue> Val;
        if (Rp == 3) { EmitPackPsw (pE, &Val); } else { GetRp (pE, Rp, &Val); }
        ComPtr<ICpuValue> SP; pE->GetRegister (RegI8080SP, 16, &SP);
        ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
        ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
        pE->PutRegister (RegI8080SP, NewSP, 16, FALSE);
        ComPtr<ICpuValue> Lo; pE->Cast (CastTrunc, Val, 8, &Lo);
        ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiV;pE->BinaryOp (BinLShr, Val, Sh, &HiV);
        ComPtr<ICpuValue> Hi; pE->Cast (CastTrunc, HiV, 8, &Hi);
        EmitStore8V (pE, NewSP, Lo);
        ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
        ComPtr<ICpuValue> Sp1; pE->BinaryOp (BinAdd, NewSP, One, &Sp1);
        EmitStore8V (pE, Sp1, Hi);
    }
    VOID EmitPop (ICpuEmitter *pE, UINT8 Rp) {
        ComPtr<ICpuValue> SP; pE->GetRegister (RegI8080SP, 16, &SP);
        ComPtr<ICpuValue> Lo; EmitLoad8V (pE, SP, &Lo);
        ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
        ComPtr<ICpuValue> Sp1; pE->BinaryOp (BinAdd, SP, One, &Sp1);
        ComPtr<ICpuValue> Hi; EmitLoad8V (pE, Sp1, &Hi);
        ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
        ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinAdd, SP, Two, &NewSP);
        pE->PutRegister (RegI8080SP, NewSP, 16, FALSE);
        ComPtr<ICpuValue> Lo16; pE->Cast (CastZExt, Lo, 16, &Lo16);
        ComPtr<ICpuValue> Hi16; pE->Cast (CastZExt, Hi, 16, &Hi16);
        ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiSh; pE->BinaryOp (BinShl, Hi16, Sh, &HiSh);
        ComPtr<ICpuValue> Val; pE->BinaryOp (BinOr, HiSh, Lo16, &Val);
        if (Rp == 3) { EmitUnpackPsw (pE, Lo, Hi); } else { PutRp (pE, Rp, Val); }
    }
    // One flag bit, positioned: (flag << Bit) as an 8-bit value.
    VOID EmitFlagBit (ICpuEmitter *pE, CPU_FLAG Flag, UINT8 Bit, ICpuValue **ppOut) {
        ComPtr<ICpuValue> F;  pE->GetFlag (Flag, &F);
        ComPtr<ICpuValue> F8; pE->Cast (CastZExt, F, 8, &F8);
        ComPtr<ICpuValue> Sh; pE->ConstInt (8, Bit, &Sh);
        pE->BinaryOp (BinShl, F8, Sh, ppOut);
    }
    // PSW = A (high) + flags byte (low): bit7 S, bit6 Z, bit2 P, bit0 CY (bit1 reads as 1).
    VOID EmitPackPsw (ICpuEmitter *pE, ICpuValue **ppVal) {
        ComPtr<ICpuValue> Base; pE->ConstInt (8, 0x02, &Base);
        ComPtr<ICpuValue> Sb; EmitFlagBit (pE, FlagNegative, 7, &Sb);
        ComPtr<ICpuValue> O1; pE->BinaryOp (BinOr, Base, Sb, &O1);
        ComPtr<ICpuValue> Zb; EmitFlagBit (pE, FlagZero, 6, &Zb);
        ComPtr<ICpuValue> O2; pE->BinaryOp (BinOr, O1, Zb, &O2);
        ComPtr<ICpuValue> Pb; EmitFlagBit (pE, FlagParity, 2, &Pb);
        ComPtr<ICpuValue> O3; pE->BinaryOp (BinOr, O2, Pb, &O3);
        ComPtr<ICpuValue> Cb; EmitFlagBit (pE, FlagCarry, 0, &Cb);
        ComPtr<ICpuValue> Flags; pE->BinaryOp (BinOr, O3, Cb, &Flags);
        ComPtr<ICpuValue> A;   pE->GetRegister (RegI8080A, 8, &A);
        ComPtr<ICpuValue> A16; pE->Cast (CastZExt, A, 16, &A16);
        ComPtr<ICpuValue> F16; pE->Cast (CastZExt, Flags, 16, &F16);
        ComPtr<ICpuValue> Sh;  pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> ASh; pE->BinaryOp (BinShl, A16, Sh, &ASh);
        pE->BinaryOp (BinOr, ASh, F16, ppVal);
    }
    VOID EmitUnpackPsw (ICpuEmitter *pE, ICpuValue *pFlags, ICpuValue *pA) {
        pE->PutRegister (RegI8080A, pA, 8, FALSE);
        SetFlagBit (pE, pFlags, FlagNegative, 7);
        SetFlagBit (pE, pFlags, FlagZero, 6);
        SetFlagBit (pE, pFlags, FlagParity, 2);
        SetFlagBit (pE, pFlags, FlagCarry, 0);
    }
    VOID SetFlagBit (ICpuEmitter *pE, ICpuValue *pFlags, CPU_FLAG Flag, UINT8 Bit) {
        ComPtr<ICpuValue> Sh; pE->ConstInt (8, Bit, &Sh);
        ComPtr<ICpuValue> Shf; pE->BinaryOp (BinLShr, pFlags, Sh, &Shf);
        ComPtr<ICpuValue> One; pE->ConstInt (8, 1, &One);
        ComPtr<ICpuValue> Bitv; pE->BinaryOp (BinAnd, Shf, One, &Bitv);
        ComPtr<ICpuValue> B1; pE->Cast (CastTrunc, Bitv, 1, &B1);
        pE->SetFlag (Flag, B1);
    }

    VOID EmitCallPush (ICpuEmitter *pE, CPU_ADDR RetAddr) {
        ComPtr<ICpuValue> SP; pE->GetRegister (RegI8080SP, 16, &SP);
        ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
        ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinSub, SP, Two, &NewSP);
        pE->PutRegister (RegI8080SP, NewSP, 16, FALSE);
        ComPtr<ICpuValue> Ret; pE->ConstInt (16, (UINT16) RetAddr, &Ret);
        ComPtr<ICpuValue> Lo; pE->Cast (CastTrunc, Ret, 8, &Lo);
        ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiV; pE->BinaryOp (BinLShr, Ret, Sh, &HiV);
        ComPtr<ICpuValue> Hi; pE->Cast (CastTrunc, HiV, 8, &Hi);
        EmitStore8V (pE, NewSP, Lo);
        ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
        ComPtr<ICpuValue> Sp1; pE->BinaryOp (BinAdd, NewSP, One, &Sp1);
        EmitStore8V (pE, Sp1, Hi);                          // driver branches to the callee (TagCall)
    }
    VOID EmitRet (ICpuEmitter *pE) {
        ComPtr<ICpuValue> SP; pE->GetRegister (RegI8080SP, 16, &SP);
        ComPtr<ICpuValue> Lo; EmitLoad8V (pE, SP, &Lo);
        ComPtr<ICpuValue> One; pE->ConstInt (16, 1, &One);
        ComPtr<ICpuValue> Sp1; pE->BinaryOp (BinAdd, SP, One, &Sp1);
        ComPtr<ICpuValue> Hi; EmitLoad8V (pE, Sp1, &Hi);
        ComPtr<ICpuValue> Two; pE->ConstInt (16, 2, &Two);
        ComPtr<ICpuValue> NewSP; pE->BinaryOp (BinAdd, SP, Two, &NewSP);
        pE->PutRegister (RegI8080SP, NewSP, 16, FALSE);
        ComPtr<ICpuValue> Lo16; pE->Cast (CastZExt, Lo, 16, &Lo16);
        ComPtr<ICpuValue> Hi16; pE->Cast (CastZExt, Hi, 16, &Hi16);
        ComPtr<ICpuValue> Sh; pE->ConstInt (16, 8, &Sh);
        ComPtr<ICpuValue> HiSh; pE->BinaryOp (BinShl, Hi16, Sh, &HiSh);
        ComPtr<ICpuValue> T; pE->BinaryOp (BinOr, HiSh, Lo16, &T);
        ICpuSmcEmitter *pFlow = nullptr;
        if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
            pFlow->SetDispatchTarget (T); pFlow->Release ();
        }
    }

    HRESULT EmitSysTrap (ICpuEmitter *pE, UINT32 Reason, CPU_ADDR Ret) {
        ICpuSystemEmitter *pSys = nullptr;
        if (SUCCEEDED (pE->QueryInterface (IID_ICpuSystemEmitter, (VOID **) &pSys)) && pSys != nullptr) {
            ComPtr<ICpuValue> R; pE->ConstInt (16, (UINT16) Ret, &R);
            pSys->EmitSystemTrap (Reason, R);
            pSys->Release ();
        }
        return S_OK;
    }
    VOID EmitPortOut (ICpuEmitter *pE, UINT8 Port, CPU_ADDR Ret) {
        ICpuSystemEmitter *pSys = nullptr;
        if (SUCCEEDED (pE->QueryInterface (IID_ICpuSystemEmitter, (VOID **) &pSys)) && pSys != nullptr) {
            ComPtr<ICpuValue> P; pE->ConstInt (16, Port, &P);
            ComPtr<ICpuValue> A; pE->GetRegister (RegI8080A, 8, &A);
            ComPtr<ICpuValue> R; pE->ConstInt (16, (UINT16) Ret, &R);
            pSys->EmitPortOut (P, A, 8, R);
            pSys->Release ();
        }
    }
    VOID EmitPortIn (ICpuEmitter *pE, UINT8 Port, CPU_ADDR Ret) {
        ICpuSystemEmitter *pSys = nullptr;
        if (SUCCEEDED (pE->QueryInterface (IID_ICpuSystemEmitter, (VOID **) &pSys)) && pSys != nullptr) {
            ComPtr<ICpuValue> P; pE->ConstInt (16, Port, &P);
            ComPtr<ICpuValue> R; pE->ConstInt (16, (UINT16) Ret, &R);
            pSys->EmitPortIn (P, 8, R);
            pSys->Release ();
        }
    }

    UINT8 CONST *m_pCode    = nullptr;
    UINT64       m_CodeSize = 0;
};

} // anonymous namespace

ICpuArchitecture *
CreateI8080 (VOID)
{
    return new CpuI8080 ();
}

} // namespace LibCPU
