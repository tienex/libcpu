/** @file
  System-level emulation demo: an emulated 8086 machine with a device bus and a
  timer interrupt. The guest reads a "dip switch" device (port IN), echoes it to a
  console device (port OUT), enables interrupts (STI) and idles (HLT). A timer device
  raises IRQ0 a few times; each delivery vectors through the IVT to a guest ISR that
  writes a '*' to the console and returns with IRET. When the timer is exhausted the
  halted guest has no interrupt source left and the machine stops.

  Exercises: port OUT and IN (device bus), STI / HLT / IRET, IVT-vectored hardware
  interrupts -- the whole system-emulation path, all on the interpreter backend.
**/
#ifndef LIBCPU_RUNSYSTEM_H
#define LIBCPU_RUNSYSTEM_H

#include "CpuV20.h"
#include "LibCPU/CpuState.h"
#include "../core/System.h"
#include "../core/X86System.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace LibCPU {

// A console: bytes written to its port land on host stdout and in a capture buffer.
class SysConsole final : public Device {
public:
    explicit SysConsole (UINT16 Port) : m_Port (Port) {}
    CHAR8 CONST *Name () CONST override { return "console"; }
    bool HandlesPort (UINT16 Port) CONST override { return Port == m_Port; }
    void WritePort (UINT16, UINT32, UINT16 Value) override {
        char c = (char) (Value & 0xFF);
        m_Buf.push_back (c);
        std::fputc ((int) (unsigned char) c, stdout);
        std::fflush (stdout);
    }
    std::string CONST &Buffer () CONST { return m_Buf; }
private:
    UINT16      m_Port;
    std::string m_Buf;
};

// A read-only "dip switch": IN returns a fixed value.
class SysDipSwitch final : public Device {
public:
    SysDipSwitch (UINT16 Port, UINT8 Value) : m_Port (Port), m_Value (Value) {}
    CHAR8 CONST *Name () CONST override { return "dipswitch"; }
    bool HandlesPort (UINT16 Port) CONST override { return Port == m_Port; }
    UINT16 ReadPort (UINT16, UINT32) override { return m_Value; }
private:
    UINT16 m_Port;
    UINT8  m_Value;
};

// A timer: raises IRQ0 once every Period polls, up to MaxFires times, then stops.
class SysTimer final : public Device {
public:
    SysTimer (UINT32 Period, UINT32 MaxFires) : m_Period (Period), m_Max (MaxFires) {}
    CHAR8 CONST *Name () CONST override { return "timer"; }
    int Poll () override {
        if (m_Fires >= m_Max) { return -1; }
        if (++m_Tick >= m_Period) { m_Tick = 0; m_Fires++; return 0; }   // IRQ0
        return -1;
    }
    UINT32 Fires () CONST { return m_Fires; }
private:
    UINT32 m_Period;
    UINT32 m_Max;
    UINT32 m_Tick  = 0;
    UINT32 m_Fires = 0;
};

static inline int
RunSystemDemo (ICpuBackend *pBackend)
{
    std::printf ("== System-level emulation on '%s': 8086 machine, device bus + timer IRQ\n", pBackend->GetName ());

    static UINT8 Ram[65536];
    std::memset (Ram, 0, sizeof (Ram));

    // ISR @0x40: MOV AX,0x2A (AL='*'); OUT 0xE9,AL (console '*'); IRET
    // main @0x46: IN AL,0x64 (dip switch); OUT 0xE9,AL; STI; loop: HLT; JMP loop
    UINT8 const Prog[] = {
        /*0x40*/ 0xB8, 0x2A, 0x00,  0xE6, 0xE9,  0xCF,
        /*0x46*/ 0xE4, 0x64,  0xE6, 0xE9,  0xFB,
        /*0x4B*/ 0xF4,  0xEB, 0xFD
    };
    std::memcpy (Ram + 0x40, Prog, sizeof (Prog));
    CPU_ADDR Entry = 0x46, End = 0x4E;

    ICpuArchitecture *pArch = CreateV20 ();
    pArch->SetCodeMemory (Ram, sizeof (Ram));

    System Machine (pArch, pBackend, Ram, sizeof (Ram));
    InstallX86System (Machine);                              // CPU personality: x86 privileged ops
    SysConsole   Console (0xE9);
    SysDipSwitch Dip (0x64, (UINT8) 'S');
    SysTimer     Timer (1, 3);
    Machine.AddDevice (&Console);
    Machine.AddDevice (&Dip);
    Machine.AddDevice (&Timer);

    // "BIOS": stack at 0:0x1000, and the timer vector (IRQ0 -> INT 8) -> our ISR.
    Machine.State ()->Reg[4]  = 0x1000;   // SP
    Machine.State ()->Reg[10] = 0x0000;   // SS
    X86SetIvt (Machine, 0x08, 0x0000, 0x0040);

    std::printf ("  devices: console@0xE9, dipswitch@0x64='S', timer(IRQ0 x3)\n");
    std::printf ("  --- guest console output ---\n  ");
    LC_SYS_RESULT R = Machine.Run (Entry, End, 100000);
    std::printf ("\n  --- machine stopped ---\n");

    CHAR8 CONST *pReason = R.Reason == LC_SYS_RESULT::Shutdown   ? "shutdown" :
                           R.Reason == LC_SYS_RESULT::HaltedIdle ? "halted-idle (timer exhausted)" :
                           R.Reason == LC_SYS_RESULT::StepBudget ? "step-budget" : "fault";
    std::printf ("  reason=%s  bursts=%llu  interrupts=%llu  port writes=%llu reads=%llu\n",
                 pReason, (unsigned long long) R.Steps, (unsigned long long) R.Interrupts,
                 (unsigned long long) R.PortWrites, (unsigned long long) R.PortReads);
    std::printf ("  console captured: \"%s\"  (exp \"S***\")\n", Console.Buffer ().c_str ());

    bool Ok = Console.Buffer () == "S***"
              && R.Interrupts == 3 && Timer.Fires () == 3 && R.PortReads == 1
              && R.Reason == LC_SYS_RESULT::HaltedIdle;
    std::printf ("RESULT: %s  (port IN/OUT to devices + 3 timer interrupts vectored through the IVT to the ISR)\n",
                 Ok ? "PASS" : "FAIL");

    pArch->Release ();
    return Ok ? 0 : 1;
}

} // namespace LibCPU

#endif // LIBCPU_RUNSYSTEM_H
