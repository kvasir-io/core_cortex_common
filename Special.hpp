#pragma once

// The core's special registers as the layers above read them: IPSR. Asm only, every function always inlined (a RAM
// function may use them: check_ram_funcs). PRIMASK (Core::IrqGuard, IrqState, irqsMasked, maskIrqs, unmaskIrqs) is
// InterruptMask.hpp, included here.

#include "InterruptMask.hpp"

#include <cstdint>

namespace Kvasir::Core {
// IPSR as read: 0 in thread mode, else the exception number (IPSR<5:0> on Armv6-M, IPSR<8:0> on Armv7-M: DDI0419E and
// DDI0403E B1.4.2, the TakeReset pseudocode).
[[gnu::always_inline]] inline std::uint32_t ipsr() {
    std::uint32_t r{};
    asm volatile("mrs %0, ipsr" : "=r"(r));
    return r;
}

// The exception number alone (the 9 bits of the widest IPSR).
[[gnu::always_inline]] inline std::uint32_t exceptionNumber() { return ipsr() & 0x1FFU; }

// Whether a debugger has halting debug enabled (DHCSR.C_DEBUGEN), as far as this core can tell:
// cortex_common/Debug.hpp defines `haltingDebug()` for every core; `unknown` where software cannot read DHCSR or the
// architecture leaves it IMPLEMENTATION DEFINED (core/Features.hpp KVASIR_CORE_HAS_DEBUGEN_READ).
enum class HaltingDebug : std::uint8_t { off, on, unknown };
}   // namespace Kvasir::Core
