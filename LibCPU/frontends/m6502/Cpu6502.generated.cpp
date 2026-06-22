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
        pInfo->pName       = "6502";
        pInfo->pFullName   = "MOS 6502";
        pInfo->ByteSize    = 8;
        pInfo->WordSize    = 8;
        pInfo->AddressSize = 16;
        pInfo->PsrSize     = 8;
        pInfo->IsBigEndian = FALSE;
        pInfo->GprCount    = 6;
        pInfo->GprBits     = 8;
        pInfo->AddrSegShift = 0;
        pInfo->AddrOffBits  = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetCodeMemory (UINT8 CONST *pBase, UINT64 Size) override {
        m_pCode = pBase; m_CodeSize = Size; return S_OK;
    }

    int Decode (CPU_ADDR Pc, UINT32 *pLen) CONST {
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0xa9))) { *pLen = 2; return 0; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0xa5))) { *pLen = 2; return 1; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x85))) { *pLen = 2; return 2; }
        if (Pc + 3 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x8d))) { *pLen = 3; return 3; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0xe6))) { *pLen = 2; return 4; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0xc6))) { *pLen = 2; return 5; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x69))) { *pLen = 2; return 6; }
        if (Pc + 1 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x18))) { *pLen = 1; return 7; }
        if (Pc + 2 <= m_CodeSize && ((m_pCode[Pc + 0] == 0xd0))) { *pLen = 2; return 8; }
        if (Pc + 3 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x4c))) { *pLen = 3; return 9; }
        if (Pc + 3 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x20))) { *pLen = 3; return 10; }
        if (Pc + 1 <= m_CodeSize && ((m_pCode[Pc + 0] == 0x60))) { *pLen = 1; return 11; }
        *pLen = 1; return -1;
    }

    HRESULT STDMETHODCALLTYPE TagInstr (CPU_ADDR Pc, UINT32 *pTag, CPU_ADDR *pNewPc, CPU_ADDR *pNextPc) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        *pNextPc = Pc + Len;
        *pNewPc  = (CPU_ADDR) -1;
        *pTag    = TagContinue;
        switch (Id) {
        case 8:   // bne
            *pTag = TagConditional | TagBranch;
            *pNewPc = (CPU_ADDR)((CPU_ADDR)((INT64)(Pc + 2) + (INT8)(m_pCode[Pc + 1])));
            break;
        case 9:   // jmp
            *pTag = TagBranch;
            *pNewPc = (CPU_ADDR)(((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)));
            break;
        case 10:   // jsr
            *pTag = TagCall;
            *pNewPc = (CPU_ADDR)(((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)));
            break;
        case 11:   // rts
            *pTag = TagReturn;
            break;
        default: break;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Disassemble (CPU_ADDR Pc, CHAR8 *pLine, UINT32 MaxLine) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        switch (Id) {
        case 0: std::snprintf (pLine, MaxLine, "lda $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 1: std::snprintf (pLine, MaxLine, "lda $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 2: std::snprintf (pLine, MaxLine, "sta $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 3: std::snprintf (pLine, MaxLine, "sta $%x", (unsigned)(((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)))); return S_OK;
        case 4: std::snprintf (pLine, MaxLine, "inc $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 5: std::snprintf (pLine, MaxLine, "dec $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 6: std::snprintf (pLine, MaxLine, "adc $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 7: std::snprintf (pLine, MaxLine, "clc"); return S_OK;
        case 8: std::snprintf (pLine, MaxLine, "bne $%x", (unsigned)(m_pCode[Pc + 1])); return S_OK;
        case 9: std::snprintf (pLine, MaxLine, "jmp $%x", (unsigned)(((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)))); return S_OK;
        case 10: std::snprintf (pLine, MaxLine, "jsr $%x", (unsigned)(((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)))); return S_OK;
        case 11: std::snprintf (pLine, MaxLine, "rts"); return S_OK;
        default: std::snprintf (pLine, MaxLine, "db 0x%02x", m_pCode[Pc]); return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE TranslateInstr (CPU_ADDR Pc, ICpuEmitter *pE) override {
        UINT32 Len = 1;
        int Id = Decode (Pc, &Len);
        (void) Len;
        switch (Id) {
        case 0: {   // lda_imm
            ComPtr<ICpuValue> t0; pE->ConstInt (8, m_pCode[Pc + 1], &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (8, 0x0, &t1);
            ComPtr<ICpuValue> t2; pE->Compare (CmpEq, t0, t1, &t2);
            pE->SetFlag (FlagZero, t2);
            ComPtr<ICpuValue> t3; pE->ConstInt (8, 0x7, &t3);
            ComPtr<ICpuValue> t4; pE->BinaryOp (BinLShr, t0, t3, &t4);
            ComPtr<ICpuValue> t5; pE->Cast (CastTrunc, t4, 1, &t5);
            pE->SetFlag (FlagNegative, t5);
            ComPtr<ICpuValue> t6; pE->ConstInt (8, 0x4, &t6);
            ComPtr<ICpuValue> t7; pE->BinaryOp (BinLShr, t0, t6, &t7);
            ComPtr<ICpuValue> t8; pE->BinaryOp (BinXor, t0, t7, &t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (8, 0x2, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinLShr, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->BinaryOp (BinXor, t8, t10, &t11);
            ComPtr<ICpuValue> t12; pE->ConstInt (8, 0x1, &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinLShr, t11, t12, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinXor, t11, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (8, 0x1, &t15);
            ComPtr<ICpuValue> t16; pE->BinaryOp (BinAnd, t14, t15, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastTrunc, t16, 1, &t17);
            ComPtr<ICpuValue> t18; pE->UnaryOp (UnNot, t17, &t18);
            pE->PutRegister (0, t0, 8, FALSE);
            break;
        }
        case 1: {   // lda_zp
            ComPtr<ICpuValue> t0; pE->ConstInt (8, m_pCode[Pc + 1], &t0);
            ComPtr<ICpuValue> t1; pE->Cast (CastZExt, t0, 16, &t1);
            ComPtr<ICpuValue> t2; pE->Load (t1, 8, &t2);
            ComPtr<ICpuValue> t3; pE->ConstInt (8, 0x0, &t3);
            ComPtr<ICpuValue> t4; pE->Compare (CmpEq, t2, t3, &t4);
            pE->SetFlag (FlagZero, t4);
            ComPtr<ICpuValue> t5; pE->ConstInt (8, 0x7, &t5);
            ComPtr<ICpuValue> t6; pE->BinaryOp (BinLShr, t2, t5, &t6);
            ComPtr<ICpuValue> t7; pE->Cast (CastTrunc, t6, 1, &t7);
            pE->SetFlag (FlagNegative, t7);
            ComPtr<ICpuValue> t8; pE->ConstInt (8, 0x4, &t8);
            ComPtr<ICpuValue> t9; pE->BinaryOp (BinLShr, t2, t8, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinXor, t2, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (8, 0x2, &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinLShr, t10, t11, &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinXor, t10, t12, &t13);
            ComPtr<ICpuValue> t14; pE->ConstInt (8, 0x1, &t14);
            ComPtr<ICpuValue> t15; pE->BinaryOp (BinLShr, t13, t14, &t15);
            ComPtr<ICpuValue> t16; pE->BinaryOp (BinXor, t13, t15, &t16);
            ComPtr<ICpuValue> t17; pE->ConstInt (8, 0x1, &t17);
            ComPtr<ICpuValue> t18; pE->BinaryOp (BinAnd, t16, t17, &t18);
            ComPtr<ICpuValue> t19; pE->Cast (CastTrunc, t18, 1, &t19);
            ComPtr<ICpuValue> t20; pE->UnaryOp (UnNot, t19, &t20);
            pE->PutRegister (0, t2, 8, FALSE);
            break;
        }
        case 2: {   // sta_zp
            ComPtr<ICpuValue> t0; pE->GetRegister (0, 8, &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (8, m_pCode[Pc + 1], &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t1, 16, &t2);
            pE->Store (t0, t2, 8);
            break;
        }
        case 3: {   // sta_abs
            ComPtr<ICpuValue> t0; pE->GetRegister (0, 8, &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (16, ((m_pCode[Pc + 1]) | (m_pCode[Pc + 2] << 8)), &t1);
            pE->Store (t0, t1, 8);
            break;
        }
        case 4: {   // inc_zp
            ComPtr<ICpuValue> t0; pE->ConstInt (8, m_pCode[Pc + 1], &t0);
            ComPtr<ICpuValue> t1; pE->Cast (CastZExt, t0, 16, &t1);
            ComPtr<ICpuValue> t2; pE->Load (t1, 8, &t2);
            ComPtr<ICpuValue> t3; pE->ConstInt (8, 0x1, &t3);
            ComPtr<ICpuValue> t4; pE->BinaryOp (BinAdd, t2, t3, &t4);
            ComPtr<ICpuValue> t5; pE->ConstInt (8, m_pCode[Pc + 1], &t5);
            ComPtr<ICpuValue> t6; pE->Cast (CastZExt, t5, 16, &t6);
            pE->Store (t4, t6, 8);
            ComPtr<ICpuValue> t7; pE->ConstInt (8, 0x0, &t7);
            ComPtr<ICpuValue> t8; pE->Compare (CmpEq, t4, t7, &t8);
            pE->SetFlag (FlagZero, t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (8, 0x7, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinLShr, t4, t9, &t10);
            ComPtr<ICpuValue> t11; pE->Cast (CastTrunc, t10, 1, &t11);
            pE->SetFlag (FlagNegative, t11);
            ComPtr<ICpuValue> t12; pE->ConstInt (8, 0x4, &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinLShr, t4, t12, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinXor, t4, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (8, 0x2, &t15);
            ComPtr<ICpuValue> t16; pE->BinaryOp (BinLShr, t14, t15, &t16);
            ComPtr<ICpuValue> t17; pE->BinaryOp (BinXor, t14, t16, &t17);
            ComPtr<ICpuValue> t18; pE->ConstInt (8, 0x1, &t18);
            ComPtr<ICpuValue> t19; pE->BinaryOp (BinLShr, t17, t18, &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinXor, t17, t19, &t20);
            ComPtr<ICpuValue> t21; pE->ConstInt (8, 0x1, &t21);
            ComPtr<ICpuValue> t22; pE->BinaryOp (BinAnd, t20, t21, &t22);
            ComPtr<ICpuValue> t23; pE->Cast (CastTrunc, t22, 1, &t23);
            ComPtr<ICpuValue> t24; pE->UnaryOp (UnNot, t23, &t24);
            break;
        }
        case 5: {   // dec_zp
            ComPtr<ICpuValue> t0; pE->ConstInt (8, m_pCode[Pc + 1], &t0);
            ComPtr<ICpuValue> t1; pE->Cast (CastZExt, t0, 16, &t1);
            ComPtr<ICpuValue> t2; pE->Load (t1, 8, &t2);
            ComPtr<ICpuValue> t3; pE->ConstInt (8, 0x1, &t3);
            ComPtr<ICpuValue> t4; pE->BinaryOp (BinSub, t2, t3, &t4);
            ComPtr<ICpuValue> t5; pE->ConstInt (8, m_pCode[Pc + 1], &t5);
            ComPtr<ICpuValue> t6; pE->Cast (CastZExt, t5, 16, &t6);
            pE->Store (t4, t6, 8);
            ComPtr<ICpuValue> t7; pE->ConstInt (8, 0x0, &t7);
            ComPtr<ICpuValue> t8; pE->Compare (CmpEq, t4, t7, &t8);
            pE->SetFlag (FlagZero, t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (8, 0x7, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinLShr, t4, t9, &t10);
            ComPtr<ICpuValue> t11; pE->Cast (CastTrunc, t10, 1, &t11);
            pE->SetFlag (FlagNegative, t11);
            ComPtr<ICpuValue> t12; pE->ConstInt (8, 0x4, &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinLShr, t4, t12, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinXor, t4, t13, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (8, 0x2, &t15);
            ComPtr<ICpuValue> t16; pE->BinaryOp (BinLShr, t14, t15, &t16);
            ComPtr<ICpuValue> t17; pE->BinaryOp (BinXor, t14, t16, &t17);
            ComPtr<ICpuValue> t18; pE->ConstInt (8, 0x1, &t18);
            ComPtr<ICpuValue> t19; pE->BinaryOp (BinLShr, t17, t18, &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinXor, t17, t19, &t20);
            ComPtr<ICpuValue> t21; pE->ConstInt (8, 0x1, &t21);
            ComPtr<ICpuValue> t22; pE->BinaryOp (BinAnd, t20, t21, &t22);
            ComPtr<ICpuValue> t23; pE->Cast (CastTrunc, t22, 1, &t23);
            ComPtr<ICpuValue> t24; pE->UnaryOp (UnNot, t23, &t24);
            break;
        }
        case 6: {   // adc_imm
            ComPtr<ICpuValue> t0; pE->GetRegister (0, 8, &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (8, m_pCode[Pc + 1], &t1);
            ComPtr<ICpuValue> t2; pE->GetFlag (FlagCarry, &t2);
            ComPtr<ICpuValue> t3; pE->Cast (CastZExt, t2, 8, &t3);
            ComPtr<ICpuValue> t4; pE->BinaryOp (BinAdd, t0, t1, &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinAdd, t4, t3, &t5);
            ComPtr<ICpuValue> t6; pE->Cast (CastZExt, t0, 9, &t6);
            ComPtr<ICpuValue> t7; pE->Cast (CastZExt, t1, 9, &t7);
            ComPtr<ICpuValue> t8; pE->BinaryOp (BinAdd, t6, t7, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t3, 9, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            ComPtr<ICpuValue> t11; pE->ConstInt (9, 0x8, &t11);
            ComPtr<ICpuValue> t12; pE->BinaryOp (BinLShr, t10, t11, &t12);
            ComPtr<ICpuValue> t13; pE->Cast (CastTrunc, t12, 1, &t13);
            ComPtr<ICpuValue> t14; pE->ConstInt (8, 0x0, &t14);
            ComPtr<ICpuValue> t15; pE->Compare (CmpEq, t5, t14, &t15);
            pE->SetFlag (FlagZero, t15);
            ComPtr<ICpuValue> t16; pE->ConstInt (8, 0x7, &t16);
            ComPtr<ICpuValue> t17; pE->BinaryOp (BinLShr, t5, t16, &t17);
            ComPtr<ICpuValue> t18; pE->Cast (CastTrunc, t17, 1, &t18);
            pE->SetFlag (FlagNegative, t18);
            ComPtr<ICpuValue> t19; pE->ConstInt (8, 0x4, &t19);
            ComPtr<ICpuValue> t20; pE->BinaryOp (BinLShr, t5, t19, &t20);
            ComPtr<ICpuValue> t21; pE->BinaryOp (BinXor, t5, t20, &t21);
            ComPtr<ICpuValue> t22; pE->ConstInt (8, 0x2, &t22);
            ComPtr<ICpuValue> t23; pE->BinaryOp (BinLShr, t21, t22, &t23);
            ComPtr<ICpuValue> t24; pE->BinaryOp (BinXor, t21, t23, &t24);
            ComPtr<ICpuValue> t25; pE->ConstInt (8, 0x1, &t25);
            ComPtr<ICpuValue> t26; pE->BinaryOp (BinLShr, t24, t25, &t26);
            ComPtr<ICpuValue> t27; pE->BinaryOp (BinXor, t24, t26, &t27);
            ComPtr<ICpuValue> t28; pE->ConstInt (8, 0x1, &t28);
            ComPtr<ICpuValue> t29; pE->BinaryOp (BinAnd, t27, t28, &t29);
            ComPtr<ICpuValue> t30; pE->Cast (CastTrunc, t29, 1, &t30);
            ComPtr<ICpuValue> t31; pE->UnaryOp (UnNot, t30, &t31);
            pE->SetFlag (FlagCarry, t13);
            ComPtr<ICpuValue> t32; pE->BinaryOp (BinXor, t0, t5, &t32);
            ComPtr<ICpuValue> t33; pE->BinaryOp (BinXor, t1, t5, &t33);
            ComPtr<ICpuValue> t34; pE->BinaryOp (BinAnd, t32, t33, &t34);
            ComPtr<ICpuValue> t35; pE->ConstInt (8, 0x7, &t35);
            ComPtr<ICpuValue> t36; pE->BinaryOp (BinLShr, t34, t35, &t36);
            ComPtr<ICpuValue> t37; pE->Cast (CastTrunc, t36, 1, &t37);
            pE->SetFlag (FlagOverflow, t37);
            ComPtr<ICpuValue> t38; pE->BinaryOp (BinXor, t0, t1, &t38);
            ComPtr<ICpuValue> t39; pE->BinaryOp (BinXor, t38, t5, &t39);
            ComPtr<ICpuValue> t40; pE->ConstInt (8, 0x4, &t40);
            ComPtr<ICpuValue> t41; pE->BinaryOp (BinLShr, t39, t40, &t41);
            ComPtr<ICpuValue> t42; pE->Cast (CastTrunc, t41, 1, &t42);
            pE->PutRegister (0, t5, 8, FALSE);
            break;
        }
        case 7: {   // clc
            ComPtr<ICpuValue> t0; pE->ConstInt (8, 0x0, &t0);
            ComPtr<ICpuValue> t1; pE->Cast (CastTrunc, t0, 1, &t1);
            pE->SetFlag (FlagCarry, t1);
            break;
        }
        case 8: {   // bne
            break;
        }
        case 9: {   // jmp
            break;
        }
        case 10: {   // jsr
            ComPtr<ICpuValue> t0; pE->ConstInt (16, (CPU_ADDR)(Pc + 3), &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (8, 0x1, &t1);
            ComPtr<ICpuValue> t2; pE->Cast (CastZExt, t1, 16, &t2);
            ComPtr<ICpuValue> t3; pE->BinaryOp (BinSub, t0, t2, &t3);
            ComPtr<ICpuValue> t4; pE->ConstInt (16, 0x8, &t4);
            ComPtr<ICpuValue> t5; pE->BinaryOp (BinLShr, t3, t4, &t5);
            ComPtr<ICpuValue> t6; pE->Cast (CastTrunc, t5, 8, &t6);
            ComPtr<ICpuValue> t7; pE->ConstInt (16, 0x100, &t7);
            ComPtr<ICpuValue> t8; pE->GetRegister (3, 8, &t8);
            ComPtr<ICpuValue> t9; pE->Cast (CastZExt, t8, 16, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t7, t9, &t10);
            pE->Store (t6, t10, 8);
            ComPtr<ICpuValue> t11; pE->GetRegister (3, 8, &t11);
            ComPtr<ICpuValue> t12; pE->ConstInt (8, 0x1, &t12);
            ComPtr<ICpuValue> t13; pE->BinaryOp (BinSub, t11, t12, &t13);
            pE->PutRegister (3, t13, 8, FALSE);
            ComPtr<ICpuValue> t14; pE->Cast (CastTrunc, t3, 8, &t14);
            ComPtr<ICpuValue> t15; pE->ConstInt (16, 0x100, &t15);
            ComPtr<ICpuValue> t16; pE->GetRegister (3, 8, &t16);
            ComPtr<ICpuValue> t17; pE->Cast (CastZExt, t16, 16, &t17);
            ComPtr<ICpuValue> t18; pE->BinaryOp (BinAdd, t15, t17, &t18);
            pE->Store (t14, t18, 8);
            ComPtr<ICpuValue> t19; pE->GetRegister (3, 8, &t19);
            ComPtr<ICpuValue> t20; pE->ConstInt (8, 0x1, &t20);
            ComPtr<ICpuValue> t21; pE->BinaryOp (BinSub, t19, t20, &t21);
            pE->PutRegister (3, t21, 8, FALSE);
            break;
        }
        case 11: {   // rts
            ComPtr<ICpuValue> t0; pE->GetRegister (3, 8, &t0);
            ComPtr<ICpuValue> t1; pE->ConstInt (8, 0x1, &t1);
            ComPtr<ICpuValue> t2; pE->BinaryOp (BinAdd, t0, t1, &t2);
            pE->PutRegister (3, t2, 8, FALSE);
            ComPtr<ICpuValue> t3; pE->ConstInt (16, 0x100, &t3);
            ComPtr<ICpuValue> t4; pE->GetRegister (3, 8, &t4);
            ComPtr<ICpuValue> t5; pE->Cast (CastZExt, t4, 16, &t5);
            ComPtr<ICpuValue> t6; pE->BinaryOp (BinAdd, t3, t5, &t6);
            ComPtr<ICpuValue> t7; pE->Load (t6, 8, &t7);
            ComPtr<ICpuValue> t8; pE->GetRegister (3, 8, &t8);
            ComPtr<ICpuValue> t9; pE->ConstInt (8, 0x1, &t9);
            ComPtr<ICpuValue> t10; pE->BinaryOp (BinAdd, t8, t9, &t10);
            pE->PutRegister (3, t10, 8, FALSE);
            ComPtr<ICpuValue> t11; pE->ConstInt (16, 0x100, &t11);
            ComPtr<ICpuValue> t12; pE->GetRegister (3, 8, &t12);
            ComPtr<ICpuValue> t13; pE->Cast (CastZExt, t12, 16, &t13);
            ComPtr<ICpuValue> t14; pE->BinaryOp (BinAdd, t11, t13, &t14);
            ComPtr<ICpuValue> t15; pE->Load (t14, 8, &t15);
            ComPtr<ICpuValue> t16; pE->Cast (CastZExt, t15, 16, &t16);
            ComPtr<ICpuValue> t17; pE->ConstInt (8, 0x8, &t17);
            ComPtr<ICpuValue> t18; pE->Cast (CastZExt, t17, 16, &t18);
            ComPtr<ICpuValue> t19; pE->BinaryOp (BinShl, t16, t18, &t19);
            ComPtr<ICpuValue> t20; pE->Cast (CastZExt, t7, 16, &t20);
            ComPtr<ICpuValue> t21; pE->BinaryOp (BinOr, t19, t20, &t21);
            ComPtr<ICpuValue> t22; pE->ConstInt (8, 0x1, &t22);
            ComPtr<ICpuValue> t23; pE->Cast (CastZExt, t22, 16, &t23);
            ComPtr<ICpuValue> t24; pE->BinaryOp (BinAdd, t21, t23, &t24);
            {
                ICpuSmcEmitter *pFlow = nullptr;
                if (SUCCEEDED (pE->QueryInterface (IID_ICpuSmcEmitter, (VOID **) &pFlow)) && pFlow != nullptr) {
                    pFlow->SetDispatchTarget (t24);
                    pFlow->Release ();
                }
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
        case 8: {   // bne
            ComPtr<ICpuValue> t0; pE->GetFlag (FlagZero, &t0);
            ComPtr<ICpuValue> t1; pE->UnaryOp (UnNot, t0, &t1);
            t1->AddRef ();
            *ppCond = t1;
            return S_OK;
        }
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
Create6502 (VOID)
{
    return new GenFrontend ();
}

} // namespace LibCPU
