#pragma once
// The fault registers of one fault, copied as they are: the SDK's full fault record (Kvasir::CrashRecord::Full,
// Runtime/FaultHandler.hpp) stores them, and the Armv7-M/Armv8-M COREFAULT line decodes them (FaultDecode.hpp). Each core's
// core/Fault.hpp fills it in `snapshot()`: register reads only, no analysis, no log, nothing that can fault. A core
// without a register leaves its words 0 and says so in the two flags.
#include <cstdint>

namespace Kvasir::Fault::Core {
struct Snapshot {
    std::uint32_t cfsr, hfsr, mmfar, bfar, shcsr, icsr, sfsr, sfar;
    bool faultRegs;   // cfsr .. shcsr are real (Armv6-M has no fault status registers: all 0)
    bool
      secureRegs;   // sfsr, sfar are real (RAZ when the code runs Non-secure, 0 without a Security Extension)
};
}   // namespace Kvasir::Fault::Core
