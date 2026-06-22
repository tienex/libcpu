/** @file
  GENERATED from a UPCL description by Upcl::GenerateCpp -- DO NOT EDIT.

  A specialised ICpuArchitecture: the decode is a generated dispatch and each
  instruction's translation is the exact emitter-call sequence the UPCL semantics
  interpreter would issue. Compiled into a frontend, it replaces the hand-written one.
**/

#include "LibCPU/ICpu.h"
#include "LibCPU/PCom.h"
#include "LibCPU/CpuState.h"
#include <cstdio>

namespace LibCPU {
namespace {

class GenFrontend final : public ComObject<ICpuArchitecture> {
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
        pInfo->GprCount    = 18;
        pInfo->GprBits     = 8;
        pInfo->AddrSegShift = 0;
        pInfo->AddrOffBits  = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase; m_CodeSize = Size; return S_OK;
    }

    int Decode (CPU_ADDR Pc, UINT32 *pLen) CONST {
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x6))) { *pLen = 2; return 0; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x7))) { *pLen = 2; return 1; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0xa))) { *pLen = 2; return 2; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x0))) { *pLen = 2; return 3; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x1))) { *pLen = 2; return 4; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x2))) { *pLen = 2; return 5; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x3))) { *pLen = 2; return 6; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x4))) { *pLen = 2; return 7; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0x8) && (((m_pCode[Pc + 1] >> 0) & 0xf) == 0x5))) { *pLen = 2; return 8; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0xf) && (m_pCode[Pc + 1] == 0x55))) { *pLen = 2; return 9; }
        if (Pc + 2 <= m_CodeSize && ((((m_pCode[Pc + 0] >> 4) & 0xf) == 0xf) && (m_pCode[Pc + 1] == 0x65))) { *pLen = 2; return 10; }
        *pLen = 1; return -1;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        *pNextPc = Pc + Len;
        *pNewPc  = (CPU_ADDR) -1;
        *pTag    = TagContinue;
        switch (Id) {
        default: break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        switch (Id) {
        case 0: std::snprintf (pLine, MaxLine, "ld $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 1: std::snprintf (pLine, MaxLine, "add $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 2: std::snprintf (pLine, MaxLine, "ld $%x", (unsigned)((((m_pCode[Pc + 0] << 8) | (m_pCode[Pc + 1])) & 0xfff))); return S_OK;
        case 3: std::snprintf (pLine, MaxLine, "ld $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 4: std::snprintf (pLine, MaxLine, "or $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 5: std::snprintf (pLine, MaxLine, "and $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 6: std::snprintf (pLine, MaxLine, "xor $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 7: std::snprintf (pLine, MaxLine, "add $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 8: std::snprintf (pLine, MaxLine, "sub $%x $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf)), (unsigned)(((m_pCode[Pc + 1] >> 4) & 0xf))); return S_OK;
        case 9: std::snprintf (pLine, MaxLine, "ld $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf))); return S_OK;
        case 10: std::snprintf (pLine, MaxLine, "ld $%x", (unsigned)(((m_pCode[Pc + 0] >> 0) & 0xf))); return S_OK;
        default: std::snprintf (pLine, MaxLine, "db 0x%02x", m_pCode[Pc]); return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        (void) Len;
        switch (Id) {
        case 0: {   // ld_vx_kk
            ComPtr<ICpuValue> t0; pE->ConstInt (8, m_pCode[Pc + 1], &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t1);
            ComPtr<ICpuValue> t2; pE->ConstInt (64, 0x0, &t2);
            ComPtr<ICpuValue> t3; pE->Cast (CastZExt, t1, 64, &t3);
            ComPtr<ICpuValue> t4; pE->BinaryOp (BinAdd, t2, t3, &t4);
            ComPtr<ICpuValue> t5; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t5);
            ComPtr<ICpuValue> t6; pE->BinaryOp (BinOr, t5, t4, &t6);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t0, 8, FALSE);
            break;
        }
        case 1: {   // add_vx_kk
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (8, m_pCode[Pc + 1], &t7);
            ComPtr<ICpuValue> t8; pE->BinaryOp (BinAdd, t6, t7, &t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t9);
            ComPtr<ICpuValue> t10; pE->ConstInt (64, 0x0, &t10);
            ComPtr<ICpuValue> t11; pE->Cast (CastZExt, t9, 64, &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinAdd, t10, t11, &t12);
            ComPtr<ICpuValue> t13; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinOr, t13, t12, &t14);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t8, 8, FALSE);
            break;
        }
        case 2: {   // ld_i_nnn
            ComPtr<ICpuValue> t0; pE->ConstInt (12, (((m_pCode[Pc + 0] << 8) | (m_pCode[Pc + 1])) & 0xfff), &t0);
            ComPtr<ICpuValue> t1; pE->Cast (CastZExt, t0, 16, &t1);
            pE->PutRegister (16, t1, 16, FALSE);
            break;
        }
        case 3: {   // ld_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (64, 0x0, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t7, 64, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinOr, t11, t10, &t12);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t6, 8, FALSE);
            break;
        }
        case 4: {   // or_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (64, 0x0, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t7, 64, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinOr, t11, t10, &t12);
            ComPtr<ICpuValue> t13; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinOr, t6, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t15);
            ComPtr<ICpuValue> t16; pE->ConstInt (64, 0x0, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastZExt, t15, 64, &t17);
            ComPtr<ICpuValue> t18; pE->BinaryOp (BinAdd, t16, t17, &t18);
            ComPtr<ICpuValue> t19; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinOr, t19, t18, &t20);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t14, 8, FALSE);
            break;
        }
        case 5: {   // and_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (64, 0x0, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t7, 64, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinOr, t11, t10, &t12);
            ComPtr<ICpuValue> t13; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinAnd, t6, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t15);
            ComPtr<ICpuValue> t16; pE->ConstInt (64, 0x0, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastZExt, t15, 64, &t17);
            ComPtr<ICpuValue> t18; pE->BinaryOp (BinAdd, t16, t17, &t18);
            ComPtr<ICpuValue> t19; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinOr, t19, t18, &t20);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t14, 8, FALSE);
            break;
        }
        case 6: {   // xor_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (64, 0x0, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t7, 64, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinOr, t11, t10, &t12);
            ComPtr<ICpuValue> t13; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinXor, t6, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t15);
            ComPtr<ICpuValue> t16; pE->ConstInt (64, 0x0, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastZExt, t15, 64, &t17);
            ComPtr<ICpuValue> t18; pE->BinaryOp (BinAdd, t16, t17, &t18);
            ComPtr<ICpuValue> t19; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinOr, t19, t18, &t20);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t14, 8, FALSE);
            break;
        }
        case 7: {   // add_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->Cast (CastZExt, t6, 16, &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (64, 0x0, &t9);
            ComPtr<ICpuValue> t10; pE->Cast (CastZExt, t8, 64, &t10);
            ComPtr<ICpuValue> t11; pE->BinaryOp (BinAdd, t9, t10, &t11);
            ComPtr<ICpuValue> t12; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinOr, t12, t11, &t13);
            ComPtr<ICpuValue> t14; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t14);
            ComPtr<ICpuValue> t15; pE->Cast (CastZExt, t14, 16, &t15);
            ComPtr<ICpuValue> t16; pE->BinaryOp (BinAdd, t7, t15, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastTrunc, t16, 8, &t17);
            ComPtr<ICpuValue> t18; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t18);
            ComPtr<ICpuValue> t19; pE->ConstInt (64, 0x0, &t19);
            ComPtr<ICpuValue> t20; pE->Cast (CastZExt, t18, 64, &t20);
            ComPtr<ICpuValue> t21; pE->BinaryOp (BinAdd, t19, t20, &t21);
            ComPtr<ICpuValue> t22; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t22);
            ComPtr<ICpuValue> t23; pE->BinaryOp (BinOr, t22, t21, &t23);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t17, 8, FALSE);
            ComPtr<ICpuValue> t24; pE->ConstInt (16, 0x8, &t24);
            ComPtr<ICpuValue> t25; pE->BinaryOp (BinLShr, t16, t24, &t25);
            ComPtr<ICpuValue> t26; pE->Cast (CastTrunc, t25, 1, &t26);
            ComPtr<ICpuValue> t27; pE->ConstInt (8, 0xf, &t27);
            ComPtr<ICpuValue> t28; pE->ConstInt (64, 0x0, &t28);
            ComPtr<ICpuValue> t29; pE->Cast (CastZExt, t27, 64, &t29);
            ComPtr<ICpuValue> t30; pE->BinaryOp (BinAdd, t28, t29, &t30);
            ComPtr<ICpuValue> t31; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t31);
            ComPtr<ICpuValue> t32; pE->BinaryOp (BinOr, t31, t30, &t32);
            ComPtr<ICpuValue> t33; pE->Cast (CastZExt, t26, 8, &t33);
            pE->PutRegister ((0x0 + 0xf), t33, 8, FALSE);
            break;
        }
        case 8: {   // sub_vx_vy
            ComPtr<ICpuValue> t0; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (64, 0x0, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t7, 64, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinOr, t11, t10, &t12);
            ComPtr<ICpuValue> t13; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t13);
            ComPtr<ICpuValue> t14; pE->Compare (CmpUGe, t6, t13, &t14);
            ComPtr<ICpuValue> t15; pE->Cast (CastZExt, t14, 8, &t15);
            ComPtr<ICpuValue> t16; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t16);
            ComPtr<ICpuValue> t17; pE->ConstInt (64, 0x0, &t17);
            ComPtr<ICpuValue> t18; pE->Cast (CastZExt, t16, 64, &t18);
            ComPtr<ICpuValue> t19; pE->BinaryOp (BinAdd, t17, t18, &t19);
            ComPtr<ICpuValue> t20; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t20);
            ComPtr<ICpuValue> t21; pE->BinaryOp (BinOr, t20, t19, &t21);
            ComPtr<ICpuValue> t22; pE->GetRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), 8, &t22);
            ComPtr<ICpuValue> t23; pE->ConstInt (4, ((m_pCode[Pc + 1] >> 4) & 0xf), &t23);
            ComPtr<ICpuValue> t24; pE->ConstInt (64, 0x0, &t24);
            ComPtr<ICpuValue> t25; pE->Cast (CastZExt, t23, 64, &t25);
            ComPtr<ICpuValue> t26; pE->BinaryOp (BinAdd, t24, t25, &t26);
            ComPtr<ICpuValue> t27; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t27);
            ComPtr<ICpuValue> t28; pE->BinaryOp (BinOr, t27, t26, &t28);
            ComPtr<ICpuValue> t29; pE->GetRegister ((0x0 + ((m_pCode[Pc + 1] >> 4) & 0xf)), 8, &t29);
            ComPtr<ICpuValue> t30; pE->BinaryOp (BinSub, t22, t29, &t30);
            ComPtr<ICpuValue> t31; pE->ConstInt (4, ((m_pCode[Pc + 0] >> 0) & 0xf), &t31);
            ComPtr<ICpuValue> t32; pE->ConstInt (64, 0x0, &t32);
            ComPtr<ICpuValue> t33; pE->Cast (CastZExt, t31, 64, &t33);
            ComPtr<ICpuValue> t34; pE->BinaryOp (BinAdd, t32, t33, &t34);
            ComPtr<ICpuValue> t35; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t35);
            ComPtr<ICpuValue> t36; pE->BinaryOp (BinOr, t35, t34, &t36);
            pE->PutRegister ((0x0 + ((m_pCode[Pc + 0] >> 0) & 0xf)), t30, 8, FALSE);
            ComPtr<ICpuValue> t37; pE->ConstInt (8, 0xf, &t37);
            ComPtr<ICpuValue> t38; pE->ConstInt (64, 0x0, &t38);
            ComPtr<ICpuValue> t39; pE->Cast (CastZExt, t37, 64, &t39);
            ComPtr<ICpuValue> t40; pE->BinaryOp (BinAdd, t38, t39, &t40);
            ComPtr<ICpuValue> t41; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t41);
            ComPtr<ICpuValue> t42; pE->BinaryOp (BinOr, t41, t40, &t42);
            pE->PutRegister ((0x0 + 0xf), t15, 8, FALSE);
            break;
        }
        case 9: {   // st_vx_i
            for (UINT32 j = 0; (j <= (((m_pCode[Pc + 0] >> 0) & 0xf))); j = (j + 1)) {
                ComPtr<ICpuValue> t0; pE->ConstInt (16, j, &t0);
                ComPtr<ICpuValue> t1; pE->ConstInt (64, 0x0, &t1);
                ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t0, 64, &t2);
                ComPtr<ICpuValue> t3; pE->BinaryOp (BinAdd, t1, t2, &t3);
                ComPtr<ICpuValue> t4; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t4);
                ComPtr<ICpuValue> t5; pE->BinaryOp (BinOr, t4, t3, &t5);
                ComPtr<ICpuValue> t6; pE->GetRegister ((0x0 + j), 8, &t6);
                ComPtr<ICpuValue> t7; pE->GetRegister (16, 16, &t7);
                ComPtr<ICpuValue> t8; pE->ConstInt (16, j, &t8);
                ComPtr<ICpuValue> t9; pE->BinaryOp (BinAdd, t7, t8, &t9);
                pE->Store (t6, t9, 8);
            }
            break;
        }
        case 10: {   // ld_vx_i
            for (UINT32 j = 0; (j <= (((m_pCode[Pc + 0] >> 0) & 0xf))); j = (j + 1)) {
                ComPtr<ICpuValue> t0; pE->GetRegister (16, 16, &t0);
                ComPtr<ICpuValue> t1; pE->ConstInt (16, j, &t1);
                ComPtr<ICpuValue> t2; pE->BinaryOp (BinAdd, t0, t1, &t2);
                ComPtr<ICpuValue> t3; pE->Load (t2, 8, &t3);
                ComPtr<ICpuValue> t4; pE->ConstInt (16, j, &t4);
                ComPtr<ICpuValue> t5; pE->ConstInt (64, 0x0, &t5);
                ComPtr<ICpuValue> t6; pE->Cast (CastZExt, t4, 64, &t6);
                ComPtr<ICpuValue> t7; pE->BinaryOp (BinAdd, t5, t6, &t7);
                ComPtr<ICpuValue> t8; pE->ConstInt (64, UINT64_C (0xffff000000000000), &t8);
                ComPtr<ICpuValue> t9; pE->BinaryOp (BinOr, t8, t7, &t9);
                pE->PutRegister ((0x0 + j), t3, 8, FALSE);
            }
            break;
        }
        default: break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TranslateCond (CPU_ADDR Pc, ICpuEmitter *pE, ICpuValue **ppCond) override {
        *ppCond = nullptr;
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        (void) Len;
        switch (Id) {
        default: break;
        }
        return E_NOTIMPL;
    }

private:
    UINT8 CONST *m_pCode    = nullptr;
    UINT64       m_CodeSize = 0;
};

} // anonymous namespace

ICpuArchitecture *
CreateChip8 (VOID)
{
    return new GenFrontend ();
}

} // namespace LibCPU
