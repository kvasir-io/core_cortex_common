#pragma once

#include "core/Barrier.hpp"
#include "core/CoreInterrupts.hpp"
#include "core/Special.hpp"
#include "core_peripherals/SCB.hpp"
#include "cortex_common/Nvic.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Port/Interrupts.hpp"
#include "kvasir/Register/Utility.hpp"
#include "kvasir/StartUp/Resources.hpp"

#include <bit>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Kvasir {
namespace SystemControl {
    // What SYSRESETREQ resets is the chip's choice: on the RP2040 and RP2350 only this core.
    // A whole-chip reboot there is Kvasir::Bootrom::reboot().
    using SystemReset = decltype(Kvasir::Peripheral::SCB::Registers<>::AIRCR::overrideDefaults(
      write(Kvasir::Peripheral::SCB::Registers<>::AIRCR::VECTKEYValC::request_reset),
      write(Kvasir::Peripheral::SCB::Registers<>::AIRCR::SYSRESETREQValC::request_reset)));

    // The system reset request and the wait for it: AIRCR = VECTKEY | SYSRESETREQ, then "DSB; Loop B Loop", since the
    // reset need not take place at once (DDI0419E B1.5.16, md l.7746-7754; DDI0403E B1.5.16, md l.19113-19131;
    // DDI0553B.y B1.1 RHFRS, md l.3142); the DSB before it lets the stores in flight finish first. Registers only and
    // always inlined: a start that runs before RAM is set up calls it too. Nothing announced (a chip's reboot() does
    // that first).
    [[noreturn]] [[gnu::always_inline]] inline void resetNow() {
        Kvasir::Core::dsb();
        apply(SystemReset{});
        Kvasir::Core::dsb();
        while(true) {}
    }
}   // namespace SystemControl

namespace Nvic {

    namespace Detail { using ScbRegs = Kvasir::Peripheral::SCB::Registers<>; }

    // ICSR bits are write-one: written over the defaults, never read-modify-write.
    // SysTick is a core exception and needs no chip at all; no core rule refuses it an action (it is in none of
    // Core::InterruptRules' no* lists on any profile, InterruptRules.hpp), so its actions check only the level.

    template<int I>
        requires(I == CoreInterrupts::systick.value)
    struct MakeAction<Action::SetPending, Index<I>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSTSETValC::set_pending))){};

    template<int I>
        requires(I == CoreInterrupts::systick.value)
    struct MakeAction<Action::ClearPending, Index<I>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSTCLRValC::clear))){};

    namespace Detail {
        // A core exception's level in its SHPR field (SysTick's: SHPR3 PRI_15)
        template<int         Priority,
                 auto const& Field>
        constexpr auto setHandlerPriority() {
            constexpr unsigned level
              = Detail::checkedPriority<Priority, Detail::maxPriority(Field)>();
            return write(Field, Register::value<level>());
        }

        // Word `index` of a register run, read through the register layer: a word access whatever
        // the caller wants from it, and the host mock sees it. No match (index past the run) reads 0.
        template<typename... Regs>
        std::uint32_t readWord(unsigned index,
                               brigand::list<Regs...>) {
            std::uint32_t word{};
            unsigned      i{};
            (void)(((i++ == index)
                    && (word = Register::RegisterTraits<typename Regs::Addr>::read(), true))
                   || ...);
            return word;
        }

        template<std::size_t... I>
        constexpr auto iprList(std::index_sequence<I...>) {
            return brigand::list<NvicRegs::IPR<I>...>{};
        }

        // The IPR words the chip's interrupts reach.
        template<typename Traits = InterruptOffsetTraits<Kvasir::Tag::User>>
        using IprWordsFor = decltype(iprList(std::make_index_sequence<(Traits::end + 3) / 4>{}));

        // SHPR words from the one holding exception 4 * first; SHPR1 (exceptions 4..7) is not on a
        // Cortex-M0+, which has none of those exceptions.
        template<typename S>
        struct ShprWords {
            static constexpr unsigned first = 2;
            using type                      = brigand::list<typename S::SHPR2, typename S::SHPR3>;
        };

        template<typename S>
            requires requires { typename S::SHPR1; }
        struct ShprWords<S> {
            static constexpr unsigned first = 1;
            using type = brigand::list<typename S::SHPR1, typename S::SHPR2, typename S::SHPR3>;
        };
    }   // namespace Detail

    template<int Priority>
    struct MakeAction<Action::SetPriority<Priority>, Index<CoreInterrupts::systick.value>>
      : decltype(Detail::setHandlerPriority<Priority, Detail::ScbRegs::SHPR3::pri_15>()) {};

    namespace Detail {
        // The level of exception `ipsr` (IPSR's exception number) from the IPR/SHPR words:
        // thread mode 0, NMI -2, HardFault -1. Kept apart from ActiveIsrPriority's IPSR read, so
        // the host register mock can drive it (tests/decoders).
        template<typename Traits = InterruptOffsetTraits<Kvasir::Tag::User>>
        [[gnu::always_inline]] inline int activePriority(std::uint32_t ipsr) {
            if(ipsr == 0) { return 0; }
            if(ipsr < 4) { return static_cast<int>(ipsr) - 4; }
            // The word and its byte lane, not a byte load: on the Cortex-M0+ "all NVIC registers are
            // only accessible using word transfers. Any attempt to read or write a halfword or byte
            // individually is unpredictable" (Cortex-M0+ TRM DDI0484C 5.1 "About the NVIC", md
            // l.1235), and the same for "all system control registers" (4.2, Table 4-1 note, md
            // l.1164), SHPR2/3 among them. A byte load there read
            // a wrong level, and a per-level ISR policy then dropped every interrupt's log record as
            // from a silent level.
            // IPR<n> holds IRQs 4n..4n+3 (exceptions 16+4n..), SHPRk exceptions 4k..4k+3: the lane is
            // ipsr % 4 in both. An exception before the first SHPR word wraps the index and reads 0.
            using Shpr = Detail::ShprWords<Detail::ScbRegs>;
            std::uint32_t const word
              = ipsr >= 16 ? Detail::readWord(ipsr / 4 - 4, Detail::IprWordsFor<Traits>{})
                           : Detail::readWord(ipsr / 4 - Shpr::first, typename Shpr::type{});
            auto const     byte = static_cast<std::uint8_t>(word >> (8U * (ipsr % 4)));
            constexpr auto mask = Detail::NvicRegs::IPR<0>::template PRI<0>::pri.mask;
            return static_cast<int>(byte >> std::countr_zero(mask));
        }
    }   // namespace Detail

    // Priority level of the running exception: NMI -2, HardFault -1, thread mode 0.
    // A template so the chip's traits are read where it is called (uc_log's PerLevel policy calls
    // `ActivePriorityFunction{}()` inside a template).
    struct ActiveIsrPriority {
        template<typename Traits = InterruptOffsetTraits<Kvasir::Tag::User>>
        static int operator()() {
            return Detail::activePriority<Traits>(Kvasir::Core::ipsr());
        }
    };
}   // namespace Nvic

// Startup: SHPR1..3 writes, read lane by lane like the IPR bytes in Nvic.hpp.
namespace Startup {
    namespace Detail {
        inline constexpr unsigned shpr1Address = Nvic::Detail::ScbRegs::SHPR2::Addr::value - 4U;

        constexpr bool isShprAddress(unsigned address) {
            return inWordRun(address, shpr1Address, 3U);
        }

        template<unsigned Addr>
        inline constexpr int shprFirstIndex = 4 + static_cast<int>(Addr - shpr1Address) - 16;
    }   // namespace Detail

    template<unsigned Addr,
             unsigned Z,
             unsigned O,
             typename RegType,
             typename Mode,
             unsigned Mask,
             typename Access,
             typename FieldType,
             typename TAction>
        requires(Detail::isShprAddress(Addr) && Detail::setsPriority<TAction>)
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      TAction>> : Detail::LanePriorities<Mask, Detail::shprFirstIndex<Addr>, TAction> {};
}   // namespace Startup
}   // namespace Kvasir
