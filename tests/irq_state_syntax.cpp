// Special.hpp and InterruptMask.hpp under it (the one PRIMASK API) compiled for the target: every function called
// once, the guard's shape pinned. A ctest runs this with -fsyntax-only for Armv6-M and Armv8-M mainline (clang only:
// one compiler for both targets). The host twin (Kvasir_SDK Atomic/detail/host_atomic.hpp) has the same asserts in the
// SDK's tests/critical_section_tests.cpp.
#include "Special.hpp"

#include <cstdint>

static_assert(!__is_constructible(Kvasir::Core::IrqGuard,
                                  Kvasir::Core::IrqGuard const&),
              "IrqGuard is not copied");
static_assert(!__is_constructible(Kvasir::Core::IrqGuard,
                                  Kvasir::Core::IrqGuard&&),
              "IrqGuard is not moved");
static_assert(__is_trivially_copyable(Kvasir::Core::IrqState),
              "IrqState is a plain word");
static_assert(sizeof(Kvasir::Core::IrqState) == sizeof(std::uint32_t));
// constructed in a constant evaluation: no asm runs there
static_assert([] {
    Kvasir::Core::IrqGuard const g{};
    return !g.wasMasked();
}());

std::uint32_t touchEverything();

std::uint32_t touchEverything() {
    std::uint32_t r = Kvasir::Core::ipsr() + Kvasir::Core::exceptionNumber();
    {
        Kvasir::Core::IrqGuard const g{};
        r += static_cast<std::uint32_t>(g.wasMasked())
           + static_cast<std::uint32_t>(Kvasir::Core::irqsMasked());
    }
    auto const state = Kvasir::Core::IrqState::saveAndMask();
    r += static_cast<std::uint32_t>(state.wasMasked()) + state.primask;
    state.restore();
    Kvasir::Core::maskIrqs();
    Kvasir::Core::unmaskIrqs();
    return r + static_cast<std::uint32_t>(Kvasir::Core::HaltingDebug::unknown);
}
