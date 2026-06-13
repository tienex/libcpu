/** @file  System-level emulation: the machine run loop. See System.h. */

#include "System.h"
#include "../aot/AotGenerator.h"
#include "LibCPU/PCom.h"
#include <cstring>

namespace LibCPU {

// 8086 register-file indices (must match the V20 frontend's RegV20* layout).
enum { S_AX = 0, S_DX = 2, S_SP = 4, S_CS = 9, S_SS = 10 };

System::System (ICpuArchitecture *pArch, ICpuBackend *pBackend, UINT8 *pRAM, UINT64 RamSize)
    : m_pArch (pArch), m_pBackend (pBackend), m_pRAM (pRAM), m_RamSize (RamSize),
      m_If (false), m_Halted (false), m_Shutdown (false), m_NextPc (0)
{
    std::memset (&m_State, 0, sizeof (m_State));
    m_State.RamSize = RamSize;
}

void
System::AddDevice (Device *pDevice)
{
    m_Devices.push_back (pDevice);
}

void
System::SetIvt (UINT32 Vector, UINT16 Seg, UINT16 Off)
{
    UINT32 Slot = Vector * 4;                          // real-mode IVT at physical 0
    if (Slot + 4 > m_RamSize) {
        return;
    }
    m_pRAM[Slot + 0] = (UINT8) (Off & 0xFF);
    m_pRAM[Slot + 1] = (UINT8) (Off >> 8);
    m_pRAM[Slot + 2] = (UINT8) (Seg & 0xFF);
    m_pRAM[Slot + 3] = (UINT8) (Seg >> 8);
}

Device *
System::FindPort (UINT16 Port) CONST
{
    for (Device *p : m_Devices) {
        if (p->HandlesPort (Port)) {
            return p;
        }
    }
    return nullptr;
}

int
System::PollDevices ()
{
    for (Device *p : m_Devices) {
        int Irq = p->Poll ();
        if (Irq >= 0) {
            return Irq;
        }
    }
    return -1;
}

// 8086 interrupt entry: push FLAGS, CS, IP; load CS:IP from the IVT; mask interrupts.
void
System::InjectInterrupt (UINT32 Vector, CPU_ADDR ReturnPc)
{
    UINT16 Sp = (UINT16) m_State.Reg[S_SP];
    UINT16 Ss = (UINT16) m_State.Reg[S_SS];
    auto Push = [&] (UINT16 V) {
        Sp -= 2;
        UINT32 A = ((UINT32) Ss << 4) + Sp;
        if (A + 1 < m_RamSize) { m_pRAM[A] = (UINT8) (V & 0xFF); m_pRAM[A + 1] = (UINT8) (V >> 8); }
    };
    UINT16 Flags = (UINT16) (m_If ? 0x0200 : 0x0000);  // synthesize FLAGS (only IF matters here)
    Push (Flags);
    Push ((UINT16) m_State.Reg[S_CS]);
    Push ((UINT16) ReturnPc);
    m_State.Reg[S_SP] = Sp;

    UINT32 Slot = Vector * 4;
    UINT16 Off = (UINT16) (m_pRAM[Slot] | (m_pRAM[Slot + 1] << 8));
    UINT16 Seg = (UINT16) (m_pRAM[Slot + 2] | (m_pRAM[Slot + 3] << 8));
    m_State.Reg[S_CS] = Seg;
    m_NextPc = (CPU_ADDR) Off;                         // CS=0 flat machine: linear == offset
    m_If = false;                                      // interrupts masked inside the ISR
    m_Halted = false;                                  // an interrupt wakes a halted CPU
}

LC_SYS_RESULT
System::Run (CPU_ADDR CodeEntry, CPU_ADDR CodeEnd, UINT64 MaxSteps)
{
    LC_SYS_RESULT R;
    R.Reason = LC_SYS_RESULT::StepBudget;
    R.Steps = R.Interrupts = R.PortWrites = R.PortReads = 0;

    CPU_ADDR Pc = CodeEntry;
    for (UINT64 Step = 0; Step < MaxSteps; Step++) {
        // Halted (HLT): the CPU idles until a device raises an interrupt. Advance the
        // device bus looking for one; if interrupts are disabled or no device will
        // ever raise another, the machine is idle and stops.
        if (m_Halted) {
            int Irq = -1;
            for (UINT64 Spin = 0; Spin < MaxSteps && Irq < 0; Spin++) {
                Irq = PollDevices ();
            }
            if (Irq < 0 || !m_If) {
                R.Reason = LC_SYS_RESULT::HaltedIdle;
                break;
            }
            InjectInterrupt ((UINT32) (8 + Irq), Pc);    // IRQ n -> INT 8+n
            Pc = m_NextPc;
            R.Interrupts++;
            continue;
        }
        // Running with interrupts enabled: deliver a pending IRQ at this boundary.
        if (m_If) {
            int Irq = PollDevices ();
            if (Irq >= 0) {
                InjectInterrupt ((UINT32) (8 + Irq), Pc);
                Pc = m_NextPc;
                R.Interrupts++;
                continue;
            }
        }

        // Translate a window from Pc and run it until the next trap.
        ComPtr<ICpuCode> Code;
        if (FAILED (GenerateAotCfg (m_pArch, m_pBackend, Pc, CodeEnd, &Code, nullptr)) || Code == nullptr) {
            R.Reason = LC_SYS_RESULT::Fault;
            break;
        }
        m_State.TrapPc = CPU_SMC_NO_TRAP;
        m_State.IoCtrl = CPU_IO_NONE;
        m_State.SyscallVector = CPU_NO_SYSCALL;
        Code->Execute (m_pRAM, &m_State, nullptr);
        R.Steps++;

        if (m_State.TrapPc == CPU_SMC_NO_TRAP) {
            R.Reason = LC_SYS_RESULT::HaltedIdle;       // ran off the code window
            break;
        }
        if (m_State.IoCtrl == CPU_IO_NONE) {
            Pc = (CPU_ADDR) m_State.TrapPc;             // a non-system trap (far jump)
            continue;
        }

        // A system trap: drive the machine.
        UINT32 Reason = CPU_IO_REASON (m_State.IoCtrl);
        UINT32 Width  = CPU_IO_WIDTH (m_State.IoCtrl);
        CPU_ADDR Return = (CPU_ADDR) m_State.TrapPc;
        switch (Reason) {
        case CPU_IO_OUT: {
            UINT16 Port = (UINT16) m_State.IoPort;
            UINT16 Data = (UINT16) (m_State.IoData & (Width == 8 ? 0xFF : 0xFFFF));
            if (Device *p = FindPort (Port)) { p->WritePort (Port, Width, Data); }
            R.PortWrites++;
            Pc = Return;
            break;
        }
        case CPU_IO_IN: {
            UINT16 Port = (UINT16) m_State.IoPort;
            UINT16 Val  = 0;
            if (Device *p = FindPort (Port)) { Val = p->ReadPort (Port, Width); }
            if (Width == 8) {
                m_State.Reg[S_AX] = (m_State.Reg[S_AX] & 0xFF00) | (Val & 0xFF);
            } else {
                m_State.Reg[S_AX] = Val;
            }
            R.PortReads++;
            Pc = Return;
            break;
        }
        case CPU_IO_STI: m_If = true;  Pc = Return; break;
        case CPU_IO_CLI: m_If = false; Pc = Return; break;
        case CPU_IO_HLT: m_Halted = true; Pc = Return; break;
        case CPU_IO_IRET: {
            UINT16 Sp = (UINT16) m_State.Reg[S_SP];
            UINT16 Ss = (UINT16) m_State.Reg[S_SS];
            auto Pop = [&] () -> UINT16 {
                UINT32 A = ((UINT32) Ss << 4) + Sp;
                UINT16 V = (A + 1 < m_RamSize) ? (UINT16) (m_pRAM[A] | (m_pRAM[A + 1] << 8)) : 0;
                Sp += 2;
                return V;
            };
            UINT16 Ip = Pop ();
            UINT16 Cs = Pop ();
            UINT16 Fl = Pop ();
            m_State.Reg[S_SP] = Sp;
            m_State.Reg[S_CS] = Cs;
            m_If = ((Fl >> 9) & 1) != 0;                // restore IF from FLAGS bit 9
            Pc = (CPU_ADDR) Ip;
            break;
        }
        default:
            Pc = Return;
            break;
        }

        if (m_Shutdown) {
            R.Reason = LC_SYS_RESULT::Shutdown;
            break;
        }
    }
    return R;
}

} // namespace LibCPU
