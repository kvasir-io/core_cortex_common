#pragma once
// A stand-in chip for the decoder tests, written as the real chips write theirs (chip/Interrupt.hpp): the core's
// exceptions and rules (core/CoreInterrupts.hpp, the configuration's core) and KVASIR_TEST_IRQ_END device interrupts
// (0 .. end - 1), with no gaps unless KVASIR_TEST_IRQ_GAP names one (the must-fail cases).
#include "core/CoreInterrupts.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Port/Interrupts.hpp"

#include <array>

#ifndef KVASIR_TEST_IRQ_END
    #error "KVASIR_TEST_IRQ_END: the stand-in chip's interrupt count"
#endif

namespace Kvasir {
struct Interrupt : CoreInterrupts {};

namespace Nvic {
    template<>
    struct InterruptOffsetTraits<Kvasir::Tag::User>
#ifdef KVASIR_TEST_IRQ_GAP
      : Core::ChipInterruptTraits<Core::InterruptRules,
                                  KVASIR_TEST_IRQ_END,
                                  std::array{KVASIR_TEST_IRQ_GAP}> {
    };
#else
      : Core::ChipInterruptTraits<Core::InterruptRules, KVASIR_TEST_IRQ_END> {
    };
#endif
}   // namespace Nvic
}   // namespace Kvasir
