#include "Pdp1Io.h"
#include <cstdio>

// Read / write an 18-bit word cell (word index -> pRam[idx * CPU_WORD_CELL_BYTES], little-endian).
static UINT32
Pdp1ReadWord (UINT8 CONST *pRam, UINT64 RamSize, UINT32 WordIdx)
{
    UINT64 Off = (UINT64) WordIdx * CPU_WORD_CELL_BYTES;
    if (Off + CPU_WORD_CELL_BYTES > RamSize) { return 0; }
    UINT64 V = 0;
    for (UINT32 B = 0; B < CPU_WORD_CELL_BYTES; B++) { V |= (UINT64) pRam[Off + B] << (8 * B); }
    return (UINT32) (V & 0777777);
}

//
// FIO-DEC -> ASCII is not modelled here beyond raw octal; the typewriter prints the low 8 bits of
// IO for now (full FIO-DEC translation is a paper-tape/console concern, out of the Spacewar path).
//
bool
Pdp1IoTrap (CPU_STATE *pState, UINT8 *pRam, UINT64 RamSize)
{
    // The trap resumes at TrapPc; the IOT word is the instruction just executed (word index - 1).
    UINT32 WordIdx = (UINT32) (pState->TrapPc - 1);
    UINT32 Insn    = Pdp1ReadWord (pRam, RamSize, WordIdx);
    UINT32 Dev     = Insn & 077;

    switch (Dev) {
    case 003:                                          // TYO -- typewriter out (low 8 bits of IO)
        std::putchar ((int) (pState->Reg[PDP1_IO] & 0377));
        return true;
    default:                                           // unknown / unimplemented device: no-op
        return true;
    }
}
