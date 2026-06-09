/** @file
  CHIP-8 frontend implementation (ALU/load/store slice). See Chip8.h.

  Opcodes in this slice (all two bytes, big-endian):
    6xkk LD Vx,kk     7xkk ADD Vx,kk    8xy0 LD Vx,Vy
    8xy1 OR           8xy2 AND          8xy3 XOR
    8xy4 ADD Vx,Vy (VF=carry)           8xy5 SUB Vx,Vy (VF=NOT borrow)
    Annn LD I,nnn     Fx55 store V0..Vx -> [I]    Fx65 load [I] -> V0..Vx
**/
#include "Chip8.h"
#include <cstdio>

namespace LibCPU {
namespace {

class Chip8 final : public LcComObject<ICpuArchitecture> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        return DefaultQuery (riid, IID_ICpuArchitecture, ppvObject);
    }
    HRESULT STDMETHODCALLTYPE GetInfo (CPU_ARCH_INFO *pInfo) override {
        pInfo->pName       = "chip8";
        pInfo->pFullName   = "CHIP-8";
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = 8;
        pInfo->AddressSize = 16;
        pInfo->PsrSize     = 8;
        pInfo->IsBigEndian = TRUE;
        pInfo->GprCount    = 16;
        pInfo->GprBits     = 8;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase; m_CodeSize = Size; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        *pTag = TagContinue; *pNewPc = (CPU_ADDR) -1; *pNextPc = Pc + 2; return S_OK;   // slice is linear
    }
    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT16 Op = (UINT16) ((m_pCode[Pc] << 8) | m_pCode[Pc + 1]);
        UINT32 X = (Op >> 8) & 0xF, Y = (Op >> 4) & 0xF, KK = Op & 0xFF, NNN = Op & 0xFFF;
        switch (Op & 0xF000) {
        case 0x6000: std::snprintf (pLine, MaxLine, "LD V%X,#$%02x", X, KK); return S_OK;
        case 0x7000: std::snprintf (pLine, MaxLine, "ADD V%X,#$%02x", X, KK); return S_OK;
        case 0xA000: std::snprintf (pLine, MaxLine, "LD I,$%03x", NNN); return S_OK;
        case 0x8000:
            switch (Op & 0xF) {
            case 0x0: std::snprintf (pLine, MaxLine, "LD V%X,V%X", X, Y); return S_OK;
            case 0x1: std::snprintf (pLine, MaxLine, "OR V%X,V%X", X, Y); return S_OK;
            case 0x2: std::snprintf (pLine, MaxLine, "AND V%X,V%X", X, Y); return S_OK;
            case 0x3: std::snprintf (pLine, MaxLine, "XOR V%X,V%X", X, Y); return S_OK;
            case 0x4: std::snprintf (pLine, MaxLine, "ADD V%X,V%X", X, Y); return S_OK;
            case 0x5: std::snprintf (pLine, MaxLine, "SUB V%X,V%X", X, Y); return S_OK;
            default:  break;
            }
            break;
        case 0xF000:
            if (KK == 0x55) { std::snprintf (pLine, MaxLine, "LD [I],V0..V%X", X); return S_OK; }
            if (KK == 0x65) { std::snprintf (pLine, MaxLine, "LD V0..V%X,[I]", X); return S_OK; }
            break;
        default: break;
        }
        std::snprintf (pLine, MaxLine, "??? %04x", Op);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT16 Op = (UINT16) ((m_pCode[Pc] << 8) | m_pCode[Pc + 1]);
        UINT32 X = (Op >> 8) & 0xF, Y = (Op >> 4) & 0xF, KK = Op & 0xFF, NNN = Op & 0xFFF;
        switch (Op & 0xF000) {
        case 0x6000: {   // LD Vx, kk
            ComPtr<ICpuValue> V; pE->ConstInt (8, KK, &V);
            pE->PutRegister (X, V, 8, FALSE);
            break;
        }
        case 0x7000: {   // ADD Vx, kk  (no flag)
            ComPtr<ICpuValue> Vx;  pE->GetRegister (X, 8, &Vx);
            ComPtr<ICpuValue> Imm; pE->ConstInt (8, KK, &Imm);
            ComPtr<ICpuValue> Sum; pE->BinaryOp (BinAdd, Vx, Imm, &Sum);
            pE->PutRegister (X, Sum, 8, FALSE);
            break;
        }
        case 0xA000: {   // LD I, nnn
            ComPtr<ICpuValue> V; pE->ConstInt (16, NNN, &V);
            pE->PutRegister (Chip8RegI, V, 16, FALSE);
            break;
        }
        case 0x8000:
            switch (Op & 0xF) {
            case 0x0: { ComPtr<ICpuValue> Vy; pE->GetRegister (Y, 8, &Vy); pE->PutRegister (X, Vy, 8, FALSE); break; }
            case 0x1: EmitLogic (pE, X, Y, BinOr);  break;
            case 0x2: EmitLogic (pE, X, Y, BinAnd); break;
            case 0x3: EmitLogic (pE, X, Y, BinXor); break;
            case 0x4: {   // ADD Vx,Vy ; VF = carry
                ComPtr<ICpuValue> Vx;  pE->GetRegister (X, 8, &Vx);
                ComPtr<ICpuValue> Vy;  pE->GetRegister (Y, 8, &Vy);
                ComPtr<ICpuValue> Ax;  pE->Cast (CastZExt, Vx, 16, &Ax);
                ComPtr<ICpuValue> Ay;  pE->Cast (CastZExt, Vy, 16, &Ay);
                ComPtr<ICpuValue> S16; pE->BinaryOp (BinAdd, Ax, Ay, &S16);
                ComPtr<ICpuValue> Res; pE->Cast (CastTrunc, S16, 8, &Res);
                // VF = (sum >> 8) & 1
                ComPtr<ICpuValue> Eight; pE->ConstInt (16, 8, &Eight);
                ComPtr<ICpuValue> Shr;   pE->BinaryOp (BinLShr, S16, Eight, &Shr);
                ComPtr<ICpuValue> Cf;    pE->Cast (CastTrunc, Shr, 8, &Cf);
                pE->PutRegister (X, Res, 8, FALSE);
                pE->PutRegister (Chip8RegVF, Cf, 8, FALSE);
                break;
            }
            case 0x5: {   // SUB Vx,Vy ; VF = (Vx >= Vy) ? 1 : 0
                ComPtr<ICpuValue> Vx;  pE->GetRegister (X, 8, &Vx);
                ComPtr<ICpuValue> Vy;  pE->GetRegister (Y, 8, &Vy);
                ComPtr<ICpuValue> Ge;  pE->Compare (CmpUGe, Vx, Vy, &Ge);
                ComPtr<ICpuValue> GeB; pE->Cast (CastZExt, Ge, 8, &GeB);
                ComPtr<ICpuValue> Sub; pE->BinaryOp (BinSub, Vx, Vy, &Sub);   // 8-bit wrap
                pE->PutRegister (X, Sub, 8, FALSE);
                pE->PutRegister (Chip8RegVF, GeB, 8, FALSE);
                break;
            }
            default: break;
            }
            break;
        case 0xF000:
            if (KK == 0x55) {   // store V0..Vx to [I]
                for (UINT32 k = 0; k <= X; k++) {
                    ComPtr<ICpuValue> I;    pE->GetRegister (Chip8RegI, 16, &I);
                    ComPtr<ICpuValue> Off;  pE->ConstInt (16, k, &Off);
                    ComPtr<ICpuValue> Addr; pE->BinaryOp (BinAdd, I, Off, &Addr);
                    ComPtr<ICpuValue> Vk;   pE->GetRegister (k, 8, &Vk);
                    pE->Store (Vk, Addr, 8);
                }
            } else if (KK == 0x65) {   // load [I] to V0..Vx
                for (UINT32 k = 0; k <= X; k++) {
                    ComPtr<ICpuValue> I;    pE->GetRegister (Chip8RegI, 16, &I);
                    ComPtr<ICpuValue> Off;  pE->ConstInt (16, k, &Off);
                    ComPtr<ICpuValue> Addr; pE->BinaryOp (BinAdd, I, Off, &Addr);
                    ComPtr<ICpuValue> Vk;   pE->Load (Addr, 8, &Vk);
                    pE->PutRegister (k, Vk, 8, FALSE);
                }
            }
            break;
        default: break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR, ICpuEmitter *, ICpuValue **ppCond) override {
        *ppCond = nullptr; return E_NOTIMPL;
    }

private:
    static VOID EmitLogic (ICpuEmitter *pE, UINT32 X, UINT32 Y, CPU_BINOP Op) {
        ComPtr<ICpuValue> Vx; pE->GetRegister (X, 8, &Vx);
        ComPtr<ICpuValue> Vy; pE->GetRegister (Y, 8, &Vy);
        ComPtr<ICpuValue> R;  pE->BinaryOp (Op, Vx, Vy, &R);
        pE->PutRegister (X, R, 8, FALSE);
    }

    UINT8 CONST *m_pCode    = nullptr;
    UINT64       m_CodeSize = 0;
};

} // anonymous namespace

ICpuArchitecture *
CreateChip8 (VOID)
{
    return new Chip8 ();
}

} // namespace LibCPU
