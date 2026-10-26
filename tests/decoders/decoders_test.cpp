// Nvic.hpp and SystemControl.hpp on the host register mock, against the core_peripherals headers generated from
// one core SVD (CMakeLists.txt): the NVIC action table executed, the active exception's priority
// (Nvic::Detail::activePriority) and the Startup decoders InterruptOfAction / PriorityOfAction. The chip is a
// stand-in with KVASIR_TEST_IRQ_END interrupts (chip/Interrupt.hpp). Addresses and bits from the architecture:
// Armv6-M ARM DDI0419E Table B3-18 (NVIC: ISER 0xE000E100, ICER 0xE000E180, ISPR 0xE000E200, ICPR 0xE000E280,
// IPR 0xE000E400), Table B3-4 (SHPR2 0xE000ED1C, SHPR3 0xE000ED20), B3.2.4 ICSR (PENDSTSET bit 26, PENDSTCLR
// bit 25); Armv7-M ARM DDI0403E Table B3-4 (SHPR1 0xE000ED18, which an Armv6-M core does not have).
// The decoder checks are static_asserts: building is the test.
#include "kvasir_test.hpp"

// The core headers first, with no chip seen yet: they name no chip fact until something is
// instantiated. The stand-in chip's InterruptOffsetTraits<Tag::User> comes after them.
#include "cortex_common/SystemControl.hpp"
#include "cortex_common/Systick.hpp"
// clang-format off
#include "chip/Interrupt.hpp"
// clang-format on

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <type_traits>

#ifndef KVASIR_TEST_PRIO_MASK
    #error "KVASIR_TEST_PRIO_MASK: the implemented bits of one priority byte in this core's SVD"
#endif

namespace {
using namespace Kvasir;
using Kvasir::Test::checkActions;
using Kvasir::Test::recorder;
using Kvasir::Test::test;
using R        = Kvasir::Test::Recorder::Read;
using W        = Kvasir::Test::Recorder::Write;
using NvicRegs = Peripheral::NVIC::Registers<>;
using ScbRegs  = Peripheral::SCB::Registers<>;
using Nvic::Index;
constexpr int end = KVASIR_TEST_IRQ_END;

// ---- the generated layout --------------------------------------------------------------------------------------
static_assert(NvicRegs::ISER<0>::Addr::value == 0xE000'E100U);
static_assert(NvicRegs::ICER<0>::Addr::value == 0xE000'E180U);
static_assert(NvicRegs::ISPR<0>::Addr::value == 0xE000'E200U);
static_assert(NvicRegs::ICPR<0>::Addr::value == 0xE000'E280U);
static_assert(NvicRegs::IPR<0>::Addr::value == 0xE000'E400U);
static_assert(ScbRegs::SHPR2::Addr::value == 0xE000'ED1CU);
static_assert(ScbRegs::SHPR3::Addr::value == 0xE000'ED20U);
template<typename S>
constexpr bool hasShpr1Of = requires { typename S::SHPR1; };
constexpr bool hasShpr1   = hasShpr1Of<ScbRegs>;
#ifdef KVASIR_TEST_HAS_SHPR1
static_assert(hasShpr1);
static_assert(ScbRegs::SHPR1::Addr::value == 0xE000'ED18U);
#else
static_assert(!hasShpr1);
#endif

// one priority byte: the implemented bits are the top ones
constexpr unsigned laneMask = NvicRegs::IPR<0>::PRI<0>::pri.mask;
static_assert(laneMask == KVASIR_TEST_PRIO_MASK);
static_assert(Startup::Detail::priorityLaneMask == laneMask);
constexpr unsigned shift    = std::countr_zero(laneMask);
constexpr int      maxLevel = static_cast<int>(laneMask >> shift);
static_assert(Nvic::Detail::maxPriority(NvicRegs::IPR<0>::PRI<0>::pri)
              == static_cast<unsigned>(maxLevel));
static_assert(Nvic::Detail::maxPriority(ScbRegs::SHPR3::pri_15) == static_cast<unsigned>(maxLevel));

constexpr unsigned iprAddress(int irq) {
    return 0xE000'E400U + 4U * static_cast<unsigned>(irq / 4);
}

// a priority byte holding `level`, with the unimplemented low bits set (they read as zero on the chip; the
// decoder must not depend on that)
constexpr unsigned byteOf(int level) {
    return (static_cast<unsigned>(level) << shift) | (~laneMask & 0xFFU);
}

// four different levels, one per lane
constexpr std::array<int, 4> lanes{1 % (maxLevel + 1),
                                   2 % (maxLevel + 1),
                                   3 % (maxLevel + 1),
                                   maxLevel};
constexpr unsigned           laneWord = byteOf(lanes[0]) | (byteOf(lanes[1]) << 8U)
                                      | (byteOf(lanes[2]) << 16U) | (byteOf(lanes[3]) << 24U);

// ---- Detail::activePriority ------------------------------------------------------------------------------------
int activeLevel(unsigned ipsr) {
    recorder.reset();
    return Nvic::Detail::activePriority(ipsr);
}

void threadModeAndFixedLevels() {
    test("threadModeAndFixedLevels");
    CHECK_EQ(activeLevel(0), 0);    // thread mode
    CHECK_EQ(activeLevel(2), -2);   // NMI
    CHECK_EQ(activeLevel(3), -1);   // HardFault
    checkActions({});               // none of them reads a register
}

// exception `ipsr` reads lane ipsr % 4 of the one word `address`, by a word read
void readsLane(unsigned             ipsr,
               unsigned             address,
               std::source_location loc = std::source_location::current()) {
    recorder.reset();
    recorder.setReadValue(address, laneWord);
    int const level = Nvic::Detail::activePriority(ipsr);
    if(level != lanes[ipsr % 4]) {
        Kvasir::Test::failf(loc, "ipsr {}: level {}, wanted {}", ipsr, level, lanes[ipsr % 4]);
    }
    checkActions(
      {
        R{address, laneWord}
    },
      loc);
}

void systemHandlers() {
    test("systemHandlers");
    readsLane(11, ScbRegs::SHPR2::Addr::value);   // SVCall
    readsLane(14, ScbRegs::SHPR3::Addr::value);   // PendSV
    readsLane(15, ScbRegs::SHPR3::Addr::value);   // SysTick
    if constexpr(hasShpr1) {
        for(unsigned ipsr = 4; ipsr < 8; ++ipsr) { readsLane(ipsr, 0xE000'ED18U); }
    } else {
        // no SHPR1 on an Armv6-M core: the exceptions it would hold do not exist, and the index wraps past the
        // SHPR run - level 0, no access
        for(unsigned ipsr = 4; ipsr < 8; ++ipsr) {
            CHECK_EQ(activeLevel(ipsr), 0);
            checkActions({});
        }
    }
}

void deviceInterrupts() {
    test("deviceInterrupts");
    for(int irq = 0; irq < end; ++irq) {
        readsLane(16U + static_cast<unsigned>(irq), iprAddress(irq));
    }
    // past the last IPR word the chip's interrupts reach: no access, level 0
    unsigned const pastLast = 16U + 4U * ((static_cast<unsigned>(end) + 3U) / 4U);
    CHECK_EQ(activeLevel(pastLast), 0);
    checkActions({});
}

// ---- the action table, executed --------------------------------------------------------------------------------
// ISER/ICER/ISPR/ICPR bits act on a written one and ignore a zero (DDI0419E B3.4.3-B3.4.6, DDI0403E B3.4.4-B3.4.7):
// one write of the one bit, no read. A read-modify-write would write back every other enabled or pending interrupt
// of the word - a disable would disable all of them.
template<int N>
void bitWrite(unsigned             base,
              auto                 action,
              std::source_location loc = std::source_location::current()) {
    constexpr unsigned word = 4U * (N / 32);
    constexpr unsigned bit  = 1U << (N % 32);
    constexpr unsigned others
      = 0x0F00'0F00U & ~bit;   // other interrupts of the word enabled or pending
    recorder.reset();
    recorder.setReadValue(base + word, others);
    apply(action);
    checkActions(
      {
        W{base + word, bit}
    },
      loc);
}

template<int N>
void enableBits() {
    bitWrite<N>(0xE000'E100U, Nvic::makeEnable(Index<N>{}));
    bitWrite<N>(0xE000'E180U, Nvic::makeDisable(Index<N>{}));
    bitWrite<N>(0xE000'E200U, Nvic::makeSetPending(Index<N>{}));
    bitWrite<N>(0xE000'E280U, Nvic::makeClearPending(Index<N>{}));
}

// IPR is a word register of four lanes: a read-modify-write that changes lane N % 4 only. The read value holds
// implemented bits only, as the chip returns it.
template<int N,
         int P>
void setPriority() {
    constexpr unsigned lane = 8U * (N % 4);
    constexpr unsigned read = laneWord & (laneMask * 0x0101'0101U);
    recorder.reset();
    recorder.setReadValue(iprAddress(N), read);
    apply(Nvic::makeSetPriority<P>(Index<N>{}));
    checkActions({
      R{iprAddress(N),                                    read                      },
      W{iprAddress(N),
        (read & ~(laneMask << lane)) | ((static_cast<unsigned>(P) << shift) << lane)}
    });
}

void actionTable() {
    test("actionTable");
    enableBits<0>();
    enableBits<31>();
    if constexpr(end > 32) { enableBits<32>(); }
    enableBits<end - 1>();
    setPriority<0, maxLevel>();
    setPriority<1, 1>();
    setPriority<end - 1, maxLevel>();
    if constexpr(end > 32) { setPriority<33, 0>(); }
}

void systickActions() {
    test("systickActions");
    // ICSR is written over its defaults, never read
    apply(Nvic::makeSetPending(Interrupt::systick));
    checkActions({
      W{ScbRegs::ICSR::Addr::value, 1U << 26U}
    });
    recorder.reset();
    apply(Nvic::makeClearPending(Interrupt::systick));
    checkActions({
      W{ScbRegs::ICSR::Addr::value, 1U << 25U}
    });
}

// ---- the Startup decoders --------------------------------------------------------------------------------------
template<typename List>
using First = brigand::front<List>;

template<typename A>
using EnabledBy = typename Startup::InterruptOfAction<A>::type;

template<typename A>
using PrioritiesOf = typename Startup::PriorityOfAction<A>::type;

template<int I>
using IC = std::integral_constant<int, I>;

template<int I, int L>
using P = Startup::IsrPriority<I, L>;

// an ISER literal: the indexes of its set bits, 32 per block
static_assert(std::is_same_v<EnabledBy<First<Nvic::MakeActionT<Nvic::Action::Enable,
                                                               Index<5>>>>,
                             brigand::list<IC<5>>>);
static_assert(std::is_same_v<EnabledBy<decltype(Register::write(NvicRegs::ISER<0>::FULLREGISTER,
                                                                Register::value<std::uint32_t,
                                                                                0x8000'0005U>()))>,
                             brigand::list<IC<0>,
                                           IC<2>,
                                           IC<31>>>);
#if KVASIR_TEST_IRQ_END > 32
static_assert(std::is_same_v<EnabledBy<First<Nvic::MakeActionT<Nvic::Action::Enable,
                                                               Index<33>>>>,
                             brigand::list<IC<33>>>);
static_assert(std::is_same_v<EnabledBy<decltype(Register::write(NvicRegs::ISER<1>::FULLREGISTER,
                                                                Register::value<std::uint32_t,
                                                                                0x11U>()))>,
                             brigand::list<IC<32>,
                                           IC<36>>>);
#endif
// a disable (ICER), a write of zero bits: nothing enabled
static_assert(std::is_same_v<EnabledBy<First<Nvic::MakeActionT<Nvic::Action::Disable,
                                                               Index<5>>>>,
                             brigand::list<>>);

// IPR: a literal over a lane's implemented bits gives that lane's level
template<unsigned Word, unsigned Mask, typename TAction>
using IprAction = Register::Action<Register::FieldLocation<typename NvicRegs::IPR<Word>::Addr,
                                                           Mask,
                                                           Register::ReadWriteAccess,
                                                           std::uint32_t>,
                                   TAction>;

static_assert(
  std::is_same_v<PrioritiesOf<First<Nvic::MakeActionT<Nvic::Action::SetPriority<maxLevel>,
                                                      Index<6>>>>,
                 brigand::list<P<6,
                                 maxLevel>>>);
// two and four lanes at once
static_assert(
  std::is_same_v<
    PrioritiesOf<IprAction<1,
                           laneMask | (laneMask << 16U),
                           Register::WriteLiteralAction<
                             (1U << shift) | (static_cast<unsigned>(maxLevel) << shift << 16U)>>>,
    brigand::list<P<4,
                    1>,
                  P<6,
                    maxLevel>>>);
static_assert(std::is_same_v<PrioritiesOf<IprAction<2,
                                                    laneMask * 0x0101'0101U,
                                                    Register::WriteLiteralAction<laneWord>>>,
                             brigand::list<P<8,
                                             lanes[0]>,
                                           P<9,
                                             lanes[1]>,
                                           P<10,
                                             lanes[2]>,
                                           P<11,
                                             lanes[3]>>>);
// not known at compile time: a run-time write, a toggle, a mask over part of a lane's bits or past them
static_assert(std::is_same_v<PrioritiesOf<IprAction<0,
                                                    laneMask << 8U,
                                                    Register::WriteAction>>,
                             brigand::list<P<1,
                                             Startup::unknownIsrLevel>>>);
static_assert(std::is_same_v<PrioritiesOf<IprAction<0,
                                                    laneMask,
                                                    Register::XorLiteralAction<laneMask>>>,
                             brigand::list<P<0,
                                             Startup::unknownIsrLevel>>>);
static_assert(std::is_same_v<PrioritiesOf<IprAction<0,
                                                    0x80U,
                                                    Register::WriteLiteralAction<0x80U>>>,
                             brigand::list<P<0,
                                             Startup::unknownIsrLevel>>>);
static_assert(std::is_same_v<PrioritiesOf<IprAction<0,
                                                    0xFFU,
                                                    Register::WriteLiteralAction<0xFFU>>>,
                             brigand::list<P<0,
                                             Startup::unknownIsrLevel>>>);

// SHPR2/SHPR3: SVCall -5, PendSV -2, SysTick -1 (SysTick's entry is the action itself, not a list of one)
static_assert(std::is_same_v<PrioritiesOf<Nvic::MakeActionT<Nvic::Action::SetPriority<1>,
                                                            Index<Interrupt::systick.value>>>,
                             brigand::list<P<-1,
                                             1>>>);
static_assert(
  std::is_same_v<
    PrioritiesOf<decltype(Register::write(ScbRegs::SHPR3::pri_14,
                                          Register::value<std::uint32_t,
                                                          static_cast<unsigned>(maxLevel)>()))>,
    brigand::list<P<-2,
                    maxLevel>>>);
static_assert(std::is_same_v<PrioritiesOf<decltype(Register::write(ScbRegs::SHPR2::pri_11,
                                                                   Register::value<std::uint32_t,
                                                                                   1U>()))>,
                             brigand::list<P<-5,
                                             1>>>);
// any other register: no priority
static_assert(std::is_same_v<PrioritiesOf<First<Nvic::MakeActionT<Nvic::Action::Enable,
                                                                  Index<5>>>>,
                             brigand::list<>>);

// ---- SysTick's clock, defined above the stand-in chip --------------------------------------------------------
// Its init lists are built when the clock is instantiated (here, after the chip): SysTick's enable is TICKINT,
// its interrupt config SHPR3's level and ICSR's PENDSTCLR, and its ISR entry is index -1.
struct ClockConfig {
    static constexpr auto clockBase      = Systick::useProcessorClock;
    static constexpr auto clockSpeed     = 125'000'000;
    static constexpr auto minOverrunTime = std::chrono::years(15);
};

using Clock = Systick::SystickClockBase<ClockConfig>;
static_assert(
  std::is_same_v<
    std::remove_cvref_t<decltype(Clock::initStepPeripheryEnable)>,
    decltype(MPL::list(Register::write(Systick::SystickRegs::CSR::ENABLEValC::counter_is_operating),
                       Nvic::makeEnable(Interrupt::systick)))>);
static_assert(std::is_same_v<Clock::Isr::IType,
                             Index<-1>>);
// the std::chrono shell (Kvasir::Chrono::SteadyClock): a steady clock of one counter tick, and time_point + any
// duration in the clock's own duration (8 ns = one tick at 125 MHz)
static_assert(std::chrono::is_clock_v<Clock> && Clock::is_steady);
static_assert(std::is_same_v<Clock::duration,
                             std::chrono::duration<std::int64_t,
                                                   std::ratio<1,
                                                              125'000'000>>>);
static_assert(std::is_same_v<Clock::time_point,
                             std::chrono::time_point<Clock,
                                                     Clock::duration>>);
static_assert((Clock::time_point{} + std::chrono::nanoseconds{8}).time_since_epoch().count() == 1);
static_assert(
  (Clock::time_point{Clock::duration{5}} - std::chrono::microseconds{0}).time_since_epoch().count()
  == 5);
static_assert(std::is_same_v<decltype(Nvic::ActiveIsrPriority{}()),
                             int>);
}   // namespace

int main() {
    threadModeAndFixedLevels();
    systemHandlers();
    deviceInterrupts();
    actionTable();
    systickActions();
    return Kvasir::Test::report();
}
