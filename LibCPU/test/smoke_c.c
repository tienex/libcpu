/** @file
  C-API header smoke test: verify LibCPU/LibCPU.h is valid C23. Compiled to an
  object only (the C API implementation is not built yet), so it references the
  types and prototypes without linking.
**/
#include "LibCPU/LibCPU.h"

/* A function that exercises the opaque types and prototypes at compile time. */
LC_EXEC_STATUS
SmokeUseCApi (
    IN LCBackendRef Backend,
    IN VOID        *pRAM,
    IN VOID        *pGRF
    )
{
    LCEmitterRef Emitter = LCBackendCreateEmitter (Backend, (LCArchitectureRef)0);
    LCValueRef   One     = LCEmitterConstInt (Emitter, 8, 1);
    LCValueRef   Addr    = LCEmitterConstInt (Emitter, 16, 0x200);
    LCValueRef   Cur     = LCEmitterLoad (Emitter, Addr, 8);
    LCValueRef   Sum     = LCEmitterBinaryOp (Emitter, LCBinAdd, Cur, One);

    LCEmitterStore (Emitter, Sum, Addr, 8);

    LCCodeRef Code = LCBackendCompile (Backend, Emitter);
    LC_EXEC_STATUS Status = LCCodeExecute (Code, pRAM, pGRF, (VOID *)0);

    LCRelease ((LCTypeRef)Emitter);
    LCRelease ((LCTypeRef)Code);
    return Status;
}
