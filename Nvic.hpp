#pragma once

#include "chip/Interrupt.hpp"
#include "core/CoreInterrupts.hpp"
#include "core_peripherals/NVIC.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Mpl/Utility.hpp"
#include "kvasir/Register/Register.hpp"
#include "kvasir/StartUp/Resources.hpp"

#include <algorithm>
#include <bit>
#include <type_traits>
#include <utility>

namespace Kvasir { namespace Nvic {
    namespace Detail {
        using namespace Register;
        using NvicRegs = Kvasir::Peripheral::NVIC::Registers<>;

        template<int Interrupt>
        constexpr void checkIndex() {
            static_assert(Interrupt >= InterruptOffsetTraits<void>::begin
                            && Interrupt < InterruptOffsetTraits<void>::end,
                          "interrupt index outside this chip's vector table (chip/Interrupt.hpp, "
                          "InterruptOffsetTraits)");
        }

        template<typename InputIt>
        constexpr bool interruptIndexValid(int     Interrupt,
                                           InputIt f,
                                           InputIt l) {
            constexpr auto bd = std::begin(InterruptOffsetTraits<void>::disabled);
            constexpr auto ed = std::end(InterruptOffsetTraits<void>::disabled);
            return Interrupt < InterruptOffsetTraits<void>::end
                && Interrupt >= InterruptOffsetTraits<void>::begin
                && std::find(bd, ed, Interrupt) == ed && std::find(f, l, Interrupt) == l;
        }

        // Highest level a PRI field holds; core.svd describes only the implemented bits.
        template<typename Field>
        constexpr unsigned maxPriority(Field const&) {
            return Field::Mask >> std::countr_zero(Field::Mask);
        }

        template<int Interrupt>
        constexpr auto get_enable_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::ISER<Interrupt / 32>;
            using Field = Reg::template SETENA<Interrupt % 32>;

            return write(Field::SETENAValC::enable_interrupt);
        }

        template<int Interrupt>
        constexpr auto get_read_enable_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::ISER<Interrupt / 32>;
            using Field = Reg::template SETENA<Interrupt % 32>;

            return read(Field::setena);
        }

        template<int Interrupt>
        constexpr auto get_disable_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::ICER<Interrupt / 32>;
            using Field = Reg::template CLRENA<Interrupt % 32>;

            return write(Field::CLRENAValC::disable_interrupt);
        }

        template<int Interrupt>
        constexpr auto get_set_pending_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::ISPR<Interrupt / 32>;
            using Field = Reg::template SETPEND<Interrupt % 32>;

            return write(Field::SETPENDValC::pend_interrupt);
        }

        template<int Interrupt>
        constexpr auto get_clear_pending_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::ICPR<Interrupt / 32>;
            using Field = Reg::template CLRPEND<Interrupt % 32>;

            return write(Field::CLRPENDValC::clear_pending_state_of_interrupt);
        }

        template<int Priority,
                 int Interrupt>
        constexpr auto get_set_priority_action() {
            checkIndex<Interrupt>();
            using Reg   = NvicRegs::IPR<Interrupt / 4>;
            using Field = Reg::template PRI<Interrupt % 4>;

            static_assert(Priority >= 0 && unsigned(Priority) <= maxPriority(Field::pri),
                          "interrupt priority out of range for this core: 0-3 on a Cortex-M0+, "
                          "0-15 on the RP2350's Cortex-M33");
            return write(Field::pri, Register::value<Priority>());
        }

    }   // namespace Detail

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Enable, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_enable_action<Interrupt>())) {
        static_assert(Detail::interruptIndexValid(Interrupt,
                                                  std::begin(InterruptOffsetTraits<void>::noEnable),
                                                  std::end(InterruptOffsetTraits<void>::noEnable)),
                      "Unable to enable this interrupt, index is out of range");
    };

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Read, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_read_enable_action<Interrupt>())) {
        static_assert(Detail::interruptIndexValid(Interrupt,
                                                  std::begin(InterruptOffsetTraits<void>::noEnable),
                                                  std::end(InterruptOffsetTraits<void>::noEnable)),
                      "Unable to read this interrupt, index is out of range");
    };

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Disable, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_disable_action<Interrupt>())) {
        static_assert(
          Detail::interruptIndexValid(Interrupt,
                                      std::begin(InterruptOffsetTraits<void>::noDisable),
                                      std::end(InterruptOffsetTraits<void>::noDisable)),
          "Unable to disable this interrupt, index is out of range");
    };

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::SetPending, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_set_pending_action<Interrupt>())) {
        static_assert(
          Detail::interruptIndexValid(Interrupt,
                                      std::begin(InterruptOffsetTraits<void>::noSetPending),
                                      std::end(InterruptOffsetTraits<void>::noSetPending)),
          "Unable to set pending on this interrupt, index is out of range");
    };

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::ClearPending, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_clear_pending_action<Interrupt>())) {
        static_assert(
          Detail::interruptIndexValid(Interrupt,
                                      std::begin(InterruptOffsetTraits<void>::noClearPending),
                                      std::end(InterruptOffsetTraits<void>::noClearPending)),
          "Unable to clear pending on this interrupt, index is out of range");
    };

    template<int Priority, int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::SetPriority<Priority>, Index<Interrupt>>
      : decltype(MPL::list(Detail::get_set_priority_action<Priority, Interrupt>())) {
        static_assert(
          Detail::interruptIndexValid(Interrupt,
                                      std::begin(InterruptOffsetTraits<void>::noSetPriority),
                                      std::end(InterruptOffsetTraits<void>::noSetPriority)),
          "Unable to set priority on this interrupt, index is out of range");
    };

    // A software trigger is a pend (ISPR); neither core.svd has STIR.
    template<int Interrupt>
    struct MakeAction<Action::TriggerInterrupt, Index<Interrupt>>
      : MakeAction<Action::SetPending, Index<Interrupt>> {};
}}   // namespace Kvasir::Nvic

// Startup: a literal write of SETENA bits in ISER<n> enables index 32n + b per set bit b.
namespace Kvasir { namespace Startup {
    namespace Detail {
        using NvicIser0 = Kvasir::Peripheral::NVIC::Registers<>::ISER<0>;

        constexpr bool isIserAddress(unsigned address) {
            constexpr unsigned first = NvicIser0::Addr::value;
            constexpr unsigned blocks
              = (static_cast<unsigned>(Nvic::InterruptOffsetTraits<void>::end) + 31U) / 32U;
            return address >= first && address < first + 4U * blocks && (address - first) % 4U == 0;
        }

        template<unsigned Address,
                 unsigned Value,
                 int... Bits>
        constexpr auto enabledIndexes(std::integer_sequence<int,
                                                            Bits...>) {
            constexpr int block = static_cast<int>((Address - NvicIser0::Addr::value) / 4U);
            return brigand::flatten<brigand::list<
              std::conditional_t<((Value >> Bits) & 1U) != 0,
                                 brigand::list<std::integral_constant<int, 32 * block + Bits>>,
                                 brigand::list<>>...>>{};
        }
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
        requires(Detail::isIserAddress(Addr) && (Value & Mask) != 0)
    struct InterruptOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      Register::WriteLiteralAction<Value>>> {
        using type = decltype(Detail::enabledIndexes<Addr, (Value & Mask)>(
          std::make_integer_sequence<int, 32>{}));
    };

    // Startup: a write to IPR<n> sets the priority of 4n + lane for every lane its mask touches;
    // anything but a literal over a lane's implemented bits gives unknownIsrLevel.
    namespace Detail {
        using NvicIpr0 = Kvasir::Peripheral::NVIC::Registers<>::IPR<0>;

        constexpr bool isIprAddress(unsigned address) {
            constexpr unsigned first = NvicIpr0::Addr::value;
            constexpr unsigned blocks
              = (static_cast<unsigned>(Nvic::InterruptOffsetTraits<void>::end) + 3U) / 4U;
            return address >= first && address < first + 4U * blocks && (address - first) % 4U == 0;
        }

        // One lane's implemented priority bits: 0xC0 on a Cortex-M0+, 0xF0 on the M33.
        inline constexpr unsigned priorityLaneMask = NvicIpr0::template PRI<0>::pri.Mask;

        constexpr int laneLevel(unsigned mask,
                                bool     literal,
                                unsigned value,
                                int      lane) {
            unsigned const shift = 8U * static_cast<unsigned>(lane);
            if(!literal || ((mask >> shift) & 0xFFU) != priorityLaneMask) {
                return unknownIsrLevel;
            }
            return static_cast<int>(((value >> shift) & priorityLaneMask)
                                    >> std::countr_zero(priorityLaneMask));
        }

        template<int      First,
                 unsigned Mask,
                 bool     Literal,
                 unsigned Value,
                 int... Lanes>
        constexpr auto lanePriorities(std::integer_sequence<int,
                                                            Lanes...>) {
            return brigand::flatten<brigand::list<std::conditional_t<
              ((Mask >> (8U * static_cast<unsigned>(Lanes))) & 0xFFU) != 0,
              brigand::list<IsrPriority<First + Lanes, laneLevel(Mask, Literal, Value, Lanes)>>,
              brigand::list<>>...>>{};
        }

        template<unsigned Addr>
        inline constexpr int iprFirstIndex
          = 4 * static_cast<int>((Addr - NvicIpr0::Addr::value) / 4U);

        // The register actions that leave a field at a value the compiler does not know.
        template<typename TAction>
        inline constexpr bool writesUnknownValue = false;

        template<>
        inline constexpr bool writesUnknownValue<Register::WriteAction> = true;

        template<unsigned V>
        inline constexpr bool writesUnknownValue<Register::WriteRuntimeAndLiteralAction<V>> = true;

        template<>
        inline constexpr bool writesUnknownValue<Register::XorAction> = true;

        template<unsigned V>
        inline constexpr bool writesUnknownValue<Register::XorLiteralAction<V>> = true;
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
        requires(Detail::isIprAddress(Addr))
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      Register::WriteLiteralAction<Value>>> {
        using type
          = decltype(Detail::lanePriorities<Detail::iprFirstIndex<Addr>, Mask, true, Value>(
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
        requires(Detail::isIprAddress(Addr) && Detail::writesUnknownValue<TAction>)
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      TAction>> {
        using type = decltype(Detail::lanePriorities<Detail::iprFirstIndex<Addr>, Mask, false, 0U>(
          std::make_integer_sequence<int, 4>{}));
    };
}}   // namespace Kvasir::Startup
