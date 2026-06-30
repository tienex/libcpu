#include "Pdp1Io.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

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
// Type 30 CRT point buffer. Each plot pushes one Pdp1Point; Pdp1DisplayPointCount() exposes
// the count for tests. The buffer is never cleared -- Spacewar fills it frame by frame.
//
struct Pdp1Point { int X; int Y; int Intensity; };
static std::vector<Pdp1Point> g_Points;

unsigned
Pdp1DisplayPointCount ()
{
    return (unsigned) g_Points.size ();
}

//
// Convert a raw 10-bit one's-complement PDP-1 display coordinate to a signed host integer.
// The Type 30 uses a center-origin grid: bit 01000 set means the value is negative (range
// -511..+511, with -0 = 01777 treated as -511 by the complement formula below).
//
static int
Pdp1Coord (UINT32 v)
{
    int c = (int) (v & 01777);
    if (c & 01000) { c = -(int) ((~c) & 0777); }    // negative: one's-complement magnitude
    return c;
}

//
// Return the 18-bit Spacewar control word from the current key state.
//
// Layout (SIMH pdp1_sys.c / original Spacewar source):
//   Bit 17 (0400000) = Player-1 rotate-left
//   Bit 16 (0200000) = Player-1 rotate-right
//   Bit 15 (0100000) = Player-1 thrust
//   Bit 14 (0040000) = Player-1 fire
//   Bit  3 (0000010) = Player-2 rotate-left
//   Bit  2 (0000004) = Player-2 rotate-right
//   Bit  1 (0000002) = Player-2 thrust
//   Bit  0 (0000001) = Player-2 fire
//
// Rotate-left AND rotate-right pressed together = hyperspace (for each player).
//
// Default host key map (for the future interactive path via IKeyboardSink):
//   Player 1: A=rotate-left, D=rotate-right, W=thrust, S=fire
//   Player 2: left=rotate-left, right=rotate-right, up=thrust, down=fire
//
// For the headless test no keys are pressed; returns 0.
//
static UINT32
Pdp1ControlWord ()
{
    return 0;
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
    case 007: {                                        // DPY -- Type 30 point plot
        int X  = Pdp1Coord ((UINT32) (pState->Reg[PDP1_AC] >> 8));
        int Y  = Pdp1Coord ((UINT32) (pState->Reg[PDP1_IO] >> 8));
        int In = (int) ((Insn >> 6) & 077);
        g_Points.push_back (Pdp1Point { X, Y, In });
        if (std::getenv ("PDP1_DPY_DUMP") != nullptr) { std::printf ("DPY %d %d %d\n", X, Y, In); }
        return true;
    }
    case 011:                                          // spacewar controls -> IO
        pState->Reg[PDP1_IO] = Pdp1ControlWord ();
        return true;
    default:                                           // unknown / unimplemented device: no-op
        return true;
    }
}
