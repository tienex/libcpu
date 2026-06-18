/** @file
  Standalone AsmJit-backend codegen test, built for Windows (x86 + x86-64) and run
  under Wine. The full device-emulated machine is macOS-bundle-based, so this does
  not exercise it; instead it drives the emitter directly to JIT one function over
  every codegen path (ALU incl. division/remainder, compare+select, shifts, guest
  memory load/store, conditional branches, register load/store), then EXECUTES the
  result. The point is the Windows ABI: ICpuCode::Execute calls the JITted function
  via a plain C pointer using the platform convention (Microsoft x64 on Win64,
  cdecl on Win32), so a green run proves AsmJit's FuncFrame prologue/epilogue and
  argument assignment match the host ABI -- the part that could not be verified on
  the arm64/Rosetta path.
**/
#include "AsmjitBackend.h"
#include "LibCPU/ICpu.h"
#include "LibCPU/CpuState.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace LibCPU;

int
main (void)
{
    ICpuBackend *Be = CreateAsmjitBackend ();
    ICpuEmitter *Em = nullptr;
    Be->CreateEmitter (nullptr, &Em);

    auto CI = [&] (UINT64 V, UINT32 Bits) -> ICpuValue * {
        ICpuValue *O = nullptr; Em->ConstInt (Bits, V, &O); return O;
    };
    auto GR = [&] (UINT32 I) -> ICpuValue * {
        ICpuValue *O = nullptr; Em->GetRegister (I, 64, &O); return O;
    };
    auto BIN = [&] (CPU_BINOP Op, ICpuValue *A, ICpuValue *B) -> ICpuValue * {
        ICpuValue *O = nullptr; Em->BinaryOp (Op, A, B, &O); return O;
    };

    ICpuBlock *Entry = nullptr, *Then = nullptr, *Else = nullptr, *Exit = nullptr;
    Em->CreateBlock ("entry", &Entry);
    Em->CreateBlock ("then",  &Then);
    Em->CreateBlock ("else",  &Else);
    Em->CreateBlock ("exit",  &Exit);          // left uninserted: Build binds it as the ExecOk return

    // ---- entry: arithmetic, division, select, shift, guest memory, then a branch ----
    Em->SetInsertBlock (Entry);
    ICpuValue *R0 = GR (0);
    ICpuValue *R1 = GR (1);
    Em->PutRegister (2, BIN (BinAdd,  R0, R1),          64, FALSE);   // Reg2 = R0 + R1
    Em->PutRegister (3, BIN (BinUDiv, R0, CI (10, 64)), 64, FALSE);   // Reg3 = R0 / 10
    Em->PutRegister (4, BIN (BinURem, R0, CI (10, 64)), 64, FALSE);   // Reg4 = R0 % 10
    Em->PutRegister (5, BIN (BinMul,  R0, R1),          64, FALSE);   // Reg5 = R0 * R1
    ICpuValue *Lt = nullptr; Em->Compare (CmpULt, R0, R1, &Lt);
    ICpuValue *Mn = nullptr; Em->Select (Lt, R0, R1, &Mn);
    Em->PutRegister (6, Mn,                             64, FALSE);   // Reg6 = min(R0, R1)
    Em->PutRegister (7, BIN (BinShl,  R0, CI (4, 64)),  64, FALSE);   // Reg7 = R0 << 4
    Em->Store (CI (0xCAFE, 64), CI (16, 64), 16);                     // RAM[16] = 0xCAFE (16-bit)
    ICpuValue *Ld = nullptr; Em->Load (CI (16, 64), 16, &Ld);
    Em->PutRegister (9, Ld,                             64, FALSE);   // Reg9 = RAM[16]
    ICpuValue *Nz = nullptr; Em->Compare (CmpNe, R0, CI (0, 64), &Nz);
    Em->CondBranch (Nz, Then, Else);

    Em->SetInsertBlock (Then);
    Em->PutRegister (8, CI (111, 64), 64, FALSE);                     // Reg8 = 111 if R0 != 0
    Em->Branch (Exit);

    Em->SetInsertBlock (Else);
    Em->PutRegister (8, CI (222, 64), 64, FALSE);                     // Reg8 = 222 if R0 == 0
    Em->Branch (Exit);

    ICpuCode *Code = nullptr;
    if (FAILED (Be->Compile (Em, &Code)) || Code == nullptr) {
        std::printf ("FAIL: Compile returned no code\n");
        return 1;
    }

    // Known inputs; run the JITted function with the platform calling convention.
    UINT64 State[128];
    std::memset (State, 0, sizeof (State));
    UINT8  Ram[256];
    std::memset (Ram, 0, sizeof (Ram));
    State[0] = 1234;   // Reg0
    State[1] = 56;     // Reg1
    Code->Execute (Ram, State, nullptr);

    struct { CHAR8 CONST *Name; UINT32 Idx; UINT64 Want; } Checks[] = {
        { "add  R0+R1",  2, 1234 + 56 },
        { "udiv R0/10",  3, 1234 / 10 },
        { "urem R0%10",  4, 1234 % 10 },
        { "mul  R0*R1",  5, (UINT64) 1234 * 56 },
        { "min  R0,R1",  6, 56 },
        { "shl  R0<<4",  7, 1234ull << 4 },
        { "cbr  R0!=0",  8, 111 },
        { "mem  RAM[16]",9, 0xCAFE },
    };
    int Failed = 0;
    for (auto CONST &C : Checks) {
        UINT64 Got = State[C.Idx];
        bool Ok = (Got == C.Want);
        std::printf ("  [%s] Reg%u = %llu (want %llu) %s\n",
                     C.Name, C.Idx, (unsigned long long) Got, (unsigned long long) C.Want,
                     Ok ? "ok" : "<-- FAIL");
        if (!Ok) { Failed++; }
    }
    std::printf ("%s\n", Failed == 0 ? "ALL PASS" : "FAIL");
    return Failed == 0 ? 0 : 1;
}
