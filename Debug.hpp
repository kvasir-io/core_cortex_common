#pragma once
// The core's debug view from software, one file for every core, chosen by the capability macros each core's
// core/Features.hpp asserts (kvasir/Port/Capabilities.hpp):
//   Core::haltingDebug()   off/on from DHCSR.C_DEBUGEN where software can read it (KVASIR_CORE_HAS_DEBUGEN_READ),
//                          else `unknown`
//   Debug::isDebuggerConnected, enableCycleCounter, cycleCount, keepCycleCounterRunning
//                          the DCB/DWT helpers, where the DWT has a cycle counter (KVASIR_CORE_HAS_CYCLE_COUNTER)
// Per architecture: Armv6-M (Cortex-M0+) - "Software cannot access the debug registers" (Cortex-M0+ TRM DDI0484C) and
// its DWT has no cycle counter (DDI0419E Table C1-18): haltingDebug() only. Armv7-M (Cortex-M4) - the cycle counter;
// DHCSR.C_DEBUGEN is at the same address (DDI0403E C1.6.2) but "access to the DHCSR from software running on the
// processor" is IMPLEMENTATION DEFINED, and the ST Cortex-M4 programming manual (PM0214) does not say it can be read:
// `unknown`, isDebuggerConnected() stays for a chip that measured it. Armv8-M Mainline (Cortex-M33) - both: DHCSR.C_DEBUGEN
// (DDI0553B.y D1.2.39), which the Cortex-M33 TRM (100230) lists for Secure and Non-secure software.
#include "cortex_common/Special.hpp"
#include "kvasir/Port/Capabilities.hpp"

#if KVASIR_CORE_HAS_CYCLE_COUNTER
    #include "core_peripherals/DCB.hpp"
    #include "core_peripherals/DWT.hpp"
    #include "kvasir/Register/Register.hpp"

    #include <cstdint>

namespace Kvasir::Core::Debug {

using DcbR = Kvasir::Peripheral::DCB::Registers<>;

// Returns true if a debugger has enabled halting debug (DHCSR.C_DEBUGEN).
// This bit is set by the debugger over SWD when it attaches; software can only read it.
[[nodiscard]] inline bool isDebuggerConnected() {
    using Kvasir::Register::apply;
    using Kvasir::Register::read;
    // both core SVDs name the values (a scoped enum read)
    return get<0>(apply(read(DcbR::DHCSR::c_debugen))) == DcbR::DHCSR::C_DEBUGENVal::enabled;
}

using DwtR = Kvasir::Peripheral::DWT::Registers<>;

// The DWT cycle counter: DWT_CYCCNT counts core cycles while DEMCR.TRCENA and DWT_CTRL.CYCCNTENA are set and wraps to
// zero; Armv7-M ARM DDI0403E C1.6.5 (DEMCR), C1.8.7 (DWT_CTRL), C1.8.8 (DWT_CYCCNT); Armv8-M ARM DDI0553B.y D1.2.37
// (DEMCR), D1.2.58 (DWT_CTRL), D1.2.59 (DWT_CYCCNT). TRCENA first: the DWT is not written while it is off - two
// apply()s, one apply() would order the stores by address. Each set() is a load, an OR and a store. CYCCNT itself is
// never written: its start value is UNKNOWN (DDI0403E Table C1-21; DDI0553B.y D1.2.59: "resets to an UNKNOWN value on
// a Cold reset") and every user measures by the difference of two reads, so a second call (another profiler, a
// firmware's own) leaves a running counter as it is and never breaks an interval in flight.
[[gnu::always_inline]] inline void enableCycleCounter() {   // inlined: a RAM function may call it
    using namespace Kvasir::Register;
    apply(set(DcbR::DEMCR::trcena));
    apply(set(DwtR::CTRL::cyccntena));
}

// The cycle count now: one load, always inlined (an ISR profiler's entry and exit).
[[gnu::always_inline]] inline std::uint32_t cycleCount() {
    using namespace Kvasir::Register;
    return get<0>(apply(read(DwtR::CYCCNT::cyccnt)));
}

// Whether the cycle counter has been running, and running again from here on: true if DEMCR.TRCENA is set; else it
// starts the counter again (enableCycleCounter) and returns false - every difference of two cycleCount()s across
// that moment is short, the counter stood still for a while. A debugger owns DEMCR and may clear TRCENA under a
// running firmware: CYCCNT is "disabled when DEMCR.TRCENA == 0" (DDI0553B.y B14.2.8 RXDVS; "When TRCENA is set to 0"
// the DWT is off, DDI0403E C1.6.5), and a J-Link session that ends after a reset writes DEMCR with TRCENA clear ~1 ms
// into the new image, the next session sets it ~70 ms later (measured on a Cortex-M33; DWT_CTRL.CYCCNTENA stays set,
// so only TRCENA is read here). One load and a test.
[[gnu::always_inline]] inline bool keepCycleCounterRunning() {
    using namespace Kvasir::Register;
    if(get<0>(apply(read(DcbR::DEMCR::trcena)))
       == DcbR::DEMCR::TRCENAVal::dwt_and_itm_units_enabled)
    {
        return true;
    }
    enableCycleCounter();
    return false;
}
}   // namespace Kvasir::Core::Debug
#endif

namespace Kvasir::Core {
#if KVASIR_CORE_HAS_DEBUGEN_READ
static_assert(KVASIR_CORE_HAS_CYCLE_COUNTER,
              "haltingDebug() reads DHCSR through Debug::isDebuggerConnected()");

[[nodiscard]] inline HaltingDebug haltingDebug() {
    return Debug::isDebuggerConnected() ? HaltingDebug::on : HaltingDebug::off;
}
#else
[[nodiscard]] constexpr HaltingDebug haltingDebug() { return HaltingDebug::unknown; }
#endif
}   // namespace Kvasir::Core
