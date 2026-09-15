#pragma once

#include "chip/Interrupt.hpp"
#include "core/CoreInterrupts.hpp"
#include "core_peripherals/SCB.hpp"
#include "cortex_common/Nvic.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Register/Utility.hpp"
#include "kvasir/StartUp/Resources.hpp"

#include <bit>
#include <cstdint>
#include <type_traits>

namespace Kvasir {
namespace SystemControl {
    // What SYSRESETREQ resets is the chip's choice: on the RP2040 and RP2350 only this core.
    // A whole-chip reboot there is Kvasir::reboot().
    using SystemReset = decltype(Kvasir::Peripheral::SCB::Registers<>::AIRCR::overrideDefaults(
      write(Kvasir::Peripheral::SCB::Registers<>::AIRCR::VECTKEYValC::request_reset),
      write(Kvasir::Peripheral::SCB::Registers<>::AIRCR::SYSRESETREQValC::request_reset)));
}   // namespace SystemControl

namespace Nvic {

    namespace Detail { using ScbRegs = Kvasir::Peripheral::SCB::Registers<>; }

    // ICSR bits are write-one: written over the defaults, never read-modify-write.

    template<>
    struct MakeAction<Action::SetPending, Index<Interrupt::systick.index()>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSTSETValC::set_pending))) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::systick.index(),
                                      std::begin(InterruptOffsetTraits<void>::noSetPending),
                                      std::end(InterruptOffsetTraits<void>::noSetPending)),
          "Unable to set pending on this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::ClearPending, Index<Interrupt::systick.index()>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSTCLRValC::clear))) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::systick.index(),
                                      std::begin(InterruptOffsetTraits<void>::noClearPending),
                                      std::end(InterruptOffsetTraits<void>::noClearPending)),
          "Unable to clear pending on this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::SetPending, Index<Interrupt::pendSV.index()>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSVSETValC::set_pending))) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::pendSV.index(),
                                      std::begin(InterruptOffsetTraits<void>::noSetPending),
                                      std::end(InterruptOffsetTraits<void>::noSetPending)),
          "Unable to set pending on this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::ClearPending, Index<Interrupt::pendSV.index()>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::PENDSVCLRValC::clear))) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::pendSV.index(),
                                      std::begin(InterruptOffsetTraits<void>::noClearPending),
                                      std::end(InterruptOffsetTraits<void>::noClearPending)),
          "Unable to clear pending on this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::SetPending, Index<Interrupt::nonMaskableInt.index()>>
      : decltype(Detail::ScbRegs::ICSR::overrideDefaults(
          write(Detail::ScbRegs::ICSR::NMIPENDSETValC::set_pending))) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::nonMaskableInt.index(),
                                      std::begin(InterruptOffsetTraits<void>::noSetPending),
                                      std::end(InterruptOffsetTraits<void>::noSetPending)),
          "Unable to set pending on this interrupt, index is out of range");
    };

    namespace Detail {
        template<int         Priority,
                 int         Interrupt,
                 auto const& Field>
        constexpr auto setHandlerPriority() {
            static_assert(Priority >= 0 && unsigned(Priority) <= Detail::maxPriority(Field),
                          "interrupt priority out of range for this core: 0-3 on a Cortex-M0+, "
                          "0-15 on the RP2350's Cortex-M33");
            static_assert(
              Detail::interruptIndexValid(Interrupt,
                                          std::begin(InterruptOffsetTraits<void>::noSetPriority),
                                          std::end(InterruptOffsetTraits<void>::noSetPriority)),
              "Unable to set priority on this interrupt, index is out of range");
            return write(Field, Register::value<Priority>());
        }
    }   // namespace Detail

    template<int Priority>
    struct MakeAction<Action::SetPriority<Priority>, Index<Interrupt::systick.index()>>
      : decltype(Detail::setHandlerPriority<Priority,
                                            Interrupt::systick.index(),
                                            Detail::ScbRegs::SHPR3::pri_15>()) {};

    template<int Priority>
    struct MakeAction<Action::SetPriority<Priority>, Index<Interrupt::pendSV.index()>>
      : decltype(Detail::setHandlerPriority<Priority,
                                            Interrupt::pendSV.index(),
                                            Detail::ScbRegs::SHPR3::pri_14>()) {};

    template<int Priority>
    struct MakeAction<Action::SetPriority<Priority>, Index<Interrupt::sVCall.index()>>
      : decltype(Detail::setHandlerPriority<Priority,
                                            Interrupt::sVCall.index(),
                                            Detail::ScbRegs::SHPR2::pri_11>()) {};

    // Priority level of the running exception: NMI -2, HardFault -1, thread mode 0.
    struct ActiveIsrPriority {
        static int operator()() {
            std::uint32_t ipsr{};
            asm volatile("mrs %0, ipsr" : "=r"(ipsr));
            if(ipsr == 0) { return 0; }
            if(ipsr < 4) { return static_cast<int>(ipsr) - 4; }
            // Offset from SHPR2: a Cortex-M0+ SVD has no SHPR1.
            std::uint32_t const address = ipsr >= 16
                                          ? Detail::NvicRegs::IPR<0>::Addr::value + (ipsr - 16)
                                          : Detail::ScbRegs::SHPR2::Addr::value - 4 + (ipsr - 4);
            std::uint8_t const  byte    = *reinterpret_cast<std::uint8_t const volatile*>(address);
            constexpr auto      mask    = Detail::NvicRegs::IPR<0>::template PRI<0>::pri.Mask;
            return static_cast<int>(byte >> std::countr_zero(mask));
        }
    };
}   // namespace Nvic

// Startup: SHPR1..3 writes, read lane by lane like the IPR bytes in Nvic.hpp.
namespace Startup {
    namespace Detail {
        inline constexpr unsigned shpr1Address = Nvic::Detail::ScbRegs::SHPR2::Addr::value - 4U;

        constexpr bool isShprAddress(unsigned address) {
            return address >= shpr1Address && address <= shpr1Address + 8U
                && (address - shpr1Address) % 4U == 0;
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
             unsigned Value>
        requires(Detail::isShprAddress(Addr))
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      Register::WriteLiteralAction<Value>>> {
        using type
          = decltype(Detail::lanePriorities<Detail::shprFirstIndex<Addr>, Mask, true, Value>(
            std::make_integer_sequence<int, 4>{}));
    };

    template<unsigned Addr,
             unsigned Z,
             unsigned O,
             typename RegType,
             typename Mode,
             unsigned Mask,
             typename Access,
             typename FieldType,
             typename TAction>
        requires(Detail::isShprAddress(Addr) && Detail::writesUnknownValue<TAction>)
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      TAction>> {
        using type = decltype(Detail::lanePriorities<Detail::shprFirstIndex<Addr>, Mask, false, 0U>(
          std::make_integer_sequence<int, 4>{}));
    };
}   // namespace Startup
}   // namespace Kvasir
