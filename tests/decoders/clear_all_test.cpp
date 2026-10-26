// Nvic::clearAllEnablesAndPending for a chip of KVASIR_TEST_IRQ_END interrupts, on the host register mock:
// one NVIC_ICER word per 32 interrupts (the last one partly used), each written 0xFFFFFFFF in ascending order, then
// the same NVIC_ICPR words (Armv6-M ARM DDI0419E B3.4, Table B3-18: ICER 0xE000E180, ICPR 0xE000E280; Armv8-M ARM
// DDI0553B.y D1.2.183/184, n = 0 - 15). And the word ranges the Startup decoders accept for that chip.
#include "kvasir_test.hpp"

// The core header first, with no chip seen yet (it names no chip fact until something is
// instantiated); the stand-in chip's InterruptOffsetTraits<Tag::User> after it.
#include "cortex_common/SystemControl.hpp"
// clang-format off
#include "chip/Interrupt.hpp"
// clang-format on

#include <vector>

namespace {
using Kvasir::Test::recorder;
using W = Kvasir::Test::Recorder::Write;

constexpr unsigned end      = KVASIR_TEST_IRQ_END;
constexpr unsigned words    = end / 32U + (end % 32U != 0U ? 1U : 0U);   // 32 interrupts a word
constexpr unsigned iprWords = end / 4U + (end % 4U != 0U ? 1U : 0U);     // 4 a word
constexpr unsigned iser0    = 0xE000'E100U;
constexpr unsigned ipr0     = 0xE000'E400U;

// ISER words of this chip's interrupts, and not the next one
static_assert(Kvasir::Startup::Detail::isIserAddress<iser0>());
static_assert(Kvasir::Startup::Detail::isIserAddress<iser0 + 4U * (words - 1U)>());
static_assert(!Kvasir::Startup::Detail::isIserAddress<iser0 + 4U * words>());
static_assert(!Kvasir::Startup::Detail::isIserAddress<iser0 + 2U>());
// IPR words likewise, and the run ActiveIsrPriority reads (SystemControl.hpp)
static_assert(Kvasir::Startup::Detail::isIprAddress<ipr0 + 4U * (iprWords - 1U)>());
static_assert(!Kvasir::Startup::Detail::isIprAddress<ipr0 + 4U * iprWords>());
static_assert(brigand::size<Kvasir::Nvic::Detail::IprWordsFor<>>::value == iprWords);

void clearsEveryWord() {
    Kvasir::Test::test("clearsEveryWord");
    Kvasir::Nvic::clearAllEnablesAndPending();
    std::vector<Kvasir::Test::Recorder::Action> expected;
    expected.reserve(std::size_t{2} * words);
    for(unsigned i = 0; i < words; ++i) {
        expected.emplace_back(W{0xE000'E180U + 4U * i, 0xFFFF'FFFFU});
    }
    for(unsigned i = 0; i < words; ++i) {
        expected.emplace_back(W{0xE000'E280U + 4U * i, 0xFFFF'FFFFU});
    }
    Kvasir::Test::checkActions(expected);
    CHECK_EQ(recorder.actions.size(), 2U * words);
}
}   // namespace

int main() {
    clearsEveryWord();
    return Kvasir::Test::report();
}
