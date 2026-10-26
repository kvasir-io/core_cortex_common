#pragma once

// PRIMASK: the one API that masks every interrupt of this core. Needs nothing of the register layer or the chip, so
// the runtime's atomics can include it before any board header. The guard for one interrupt line
// (Nvic::InterruptGuard<Index<N>>) and the line masks are in Nvic.hpp.
//
// Every form here is asm only and always inlined: safe with XIP off and in a RAM function (check_ram_funcs; the SDK's
// tests/ram_funcs CASE 11 builds them with inlining off).
//
// One restore: PRIMASK is written back as found (`msr primask`), then an `isb`, so an interrupt that pended while
// masked is taken at the restore, before the code after it. The lowering instruction and its isb are ONE asm statement:
// as two, the compiler schedules what it sees as no memory access between them (a stack spill, a branch, a call to
// __aeabi_ldivmod), and the interrupt waits for that code. Lowering the execution priority by MSR or CPS is only
// guaranteed to be visible after a context synchronization event on Armv6-M and Armv7-M (DDI0419E "Visibility of
// changes in execution priority resulting from executing an MSR instruction" md l.10143-10149; DDI0403E.e md
// l.24292-24298). On Armv8-M the barrier is needed after CPSIE only (DDI0553B.y B3.32 RQZWN md l.7132-7136); an MSR's
// change is visible to every instruction after it (B3.3 RXHHC md l.3580-3584), so there the isb is harmless, and kept
// for one form on every core. Raising it (cpsid, msr 1) needs no barrier (same paragraphs).

#include <cstdint>

namespace Kvasir::Core {
// Whether PRIMASK is set (every configurable-priority interrupt masked).
[[nodiscard]] [[gnu::always_inline]] inline bool irqsMasked() {
    std::uint32_t r{};
    asm volatile("mrs %0, primask" : "=r"(r)::"memory");
    return r != 0U;
}

// PRIMASK = 1. Serializes by itself.
[[gnu::always_inline]] inline void maskIrqs() { asm volatile("cpsid i" ::: "memory"); }

// PRIMASK = 0, pending interrupts taken here (the isb). Startup's global enable; code that does not know PRIMASK was
// clear before uses IrqState / IrqGuard instead.
[[gnu::always_inline]] inline void unmaskIrqs() { asm volatile("cpsie i\n isb" ::: "memory"); }

// The PRIMASK word as found, for a section whose start and end are in different functions (a lock's lock/unlock). A
// scope uses IrqGuard.
struct IrqState {
    std::uint32_t primask;

    // PRIMASK read and set in one asm.
    [[nodiscard]] [[gnu::always_inline]] static IrqState saveAndMask() {
        std::uint32_t saved{};
        asm volatile("mrs %0, primask\n cpsid i" : "=r"(saved)::"memory");
        return IrqState{saved};
    }

    // PRIMASK back as found; an interrupt that pended meanwhile is taken here.
    [[gnu::always_inline]] void restore() const {
        asm volatile("msr primask, %0\n isb" ::"r"(primask) : "memory");
    }

    [[nodiscard]] [[gnu::always_inline]] bool wasMasked() const { return primask != 0U; }
};

// IrqState for a scope: masked from the constructor, PRIMASK back as found at the end. Neither copied nor moved. In a
// constant evaluation (a constexpr object's construction) it does nothing.
class IrqGuard {
    IrqState state_{};

public:
    [[gnu::always_inline]] constexpr IrqGuard() {
        if !consteval { state_ = IrqState::saveAndMask(); }
    }

    [[gnu::always_inline]] constexpr ~IrqGuard() {
        if !consteval { state_.restore(); }
    }

    IrqGuard(IrqGuard const&)            = delete;
    IrqGuard& operator=(IrqGuard const&) = delete;

    // Whether PRIMASK was already set when the guard was made (a nested section).
    [[nodiscard]] [[gnu::always_inline]] constexpr bool wasMasked() const {
        return state_.primask != 0U;
    }
};
}   // namespace Kvasir::Core
