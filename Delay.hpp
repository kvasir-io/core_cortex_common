#pragma once

// Busy waits counted in core cycles, for a wait no status bit reports and no timer can time yet: a clock switch before
// the TIMER ticks, a few cycles between two register writes, a chip-select dwell in code that runs with XIP off. Each
// waits AT LEAST the cycles it names, never exactly: interrupts, wait states and bus stalls only add to it.
//
// One loop for both forms, `1: subs; bne 1b`: two instructions a turn, each at least one cycle, so a turn is at least
// two cycles on every core. No nop: "the timing effects of including a NOP instruction in code are not guaranteed ...
// NOP instructions are therefore not suitable for timing loops" (Armv6-M ARM DDI0419E A6.7.47, md l.5252).
//
// Asm only and always inlined: safe with XIP off and in a RAM function (Kvasir_SDK tests/ram_funcs CASE 12 builds them
// with inlining off).

#include <cstdint>

namespace Kvasir::Core {
/// At least `cycles` core cycles, the count known at run time: cycles / 2 turns, rounded up.
[[gnu::always_inline]] inline void spinCycles(std::uint32_t cycles) {
    std::uint32_t turns = cycles / 2U + (cycles & 1U);
    if(turns == 0U) { return; }
    // gcc assembles Thumb-1 inline asm in divided syntax, where `subs` with three operands does not exist; clang is
    // always unified. The directive emits nothing. "l": a low register, as Thumb-1's subs needs.
    asm volatile(
      ".syntax unified\n"
      "1: subs %0, %0, #1\n"
      "   bne 1b\n"
      : "+l"(turns)
      :
      : "cc", "memory");
}

/// At least N core cycles, the count known at compile time.
template<std::uint32_t N>
[[gnu::always_inline]] inline void spinCycles() {
    spinCycles(N);
}
}   // namespace Kvasir::Core
