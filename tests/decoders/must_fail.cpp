// One compile error per MUST_FAIL value; CMakeLists.txt checks the compiler's message. Without MUST_FAIL the file
// compiles (and is not built).
#include "kvasir_test.hpp"

// The core header first, with no chip seen yet (it names no chip fact until something is
// instantiated); the stand-in chip's InterruptOffsetTraits<Tag::User> after it.
#include "cortex_common/SystemControl.hpp"
// clang-format off
#include "chip/Interrupt.hpp"
// clang-format on

namespace {
using Kvasir::Nvic::Index;

[[maybe_unused]] void f() {
#if MUST_FAIL == 1
    // a level above the core's priority bits
    apply(
      Kvasir::Nvic::makeSetPriority<
        KVASIR_TEST_PRIO_MASK / (KVASIR_TEST_PRIO_MASK & -KVASIR_TEST_PRIO_MASK) + 1>(Index<0>{}));
#elif MUST_FAIL == 2
    // an interrupt past the chip's vector table
    apply(Kvasir::Nvic::makeEnable(Index<KVASIR_TEST_IRQ_END>{}));
#elif MUST_FAIL == 3
    // SysTick's level in SHPR3, above the core's priority bits
    apply(Kvasir::Nvic::makeSetPriority<
          KVASIR_TEST_PRIO_MASK / (KVASIR_TEST_PRIO_MASK & -KVASIR_TEST_PRIO_MASK) + 1>(
      Kvasir::Interrupt::systick));
#elif MUST_FAIL == 4
    // a gap of the chip's vector table (KVASIR_TEST_IRQ_GAP)
    apply(Kvasir::Nvic::makeEnable(Index<KVASIR_TEST_IRQ_GAP>{}));
#endif
}
}   // namespace

int main() { return 0; }
