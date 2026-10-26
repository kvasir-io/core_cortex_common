#pragma once

#include "core/Barrier.hpp"
#include "core/CoreInterrupts.hpp"
#include "core/InterruptMask.hpp"
#include "core_peripherals/NVIC.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Port/Interrupts.hpp"
#include "kvasir/Register/Register.hpp"
#include "kvasir/StartUp/Resources.hpp"
#include "kvasir/Util/Diagnostic.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>
#include <utility>

// No chip header here: the chip's InterruptOffsetTraits<Tag::User> specialisation (chip/Interrupt.hpp) is
// looked up when a firmware instantiates an action, through a defaulted `Traits` parameter of a
// function template whose call is dependent. chip.hpp includes the chip's Interrupt.hpp before anything
// instantiates one.
namespace Kvasir { namespace Nvic {
    namespace Detail {
        using namespace Register;
        using NvicRegs = Kvasir::Peripheral::NVIC::Registers<>;

        // A device line (index >= 0, the MakeActions below) has a slot when it is inside the chip's table and not
        // one of its gaps. Nothing else refuses one: the core's no* lists name core exceptions only (every entry of
        // Core::InterruptRules' lists is < 0, InterruptRules.hpp). A refused index gives word 0 back, so the
        // static_assert is the only error and not followed by one about a register the core does not have.
        template<int Interrupt,
                 typename Traits = InterruptOffsetTraits<Kvasir::Tag::User>>
        constexpr int checkedLine() {
            static_assert(Interrupt < Traits::end,
                          "interrupt index outside this chip's vector table (chip/Interrupt.hpp, "
                          "InterruptOffsetTraits::end)");
            constexpr bool gap
              = std::find(std::begin(Traits::disabled), std::end(Traits::disabled), Interrupt)
             != std::end(Traits::disabled);
            static_assert(!gap,
                          "interrupt index is a gap of this chip's vector table: no line there "
                          "(chip/Interrupt.hpp, InterruptOffsetTraits::disabled)");
            return Interrupt < Traits::end && !gap ? Interrupt : 0;
        }

        // Highest level a PRI field holds; core.svd describes only the implemented bits: 2 on Armv6-M (DDI0419E
        // B1.5.4, md l.7093), 3 to 8 on Armv7-M and Armv8-M Mainline, the implementation's choice (DDI0403E Table
        // B1-6, md l.17585; DDI0553B.y RGGCP, md l.4060).
        template<typename Field>
        constexpr unsigned maxPriority(Field const&) {
            return Field::mask >> std::countr_zero(Field::mask);
        }

        // "interrupt priority out of range for this core: 9 is not in 0..7 (...)"
        template<int Priority, unsigned Max>
        struct PriorityOutOfRange {
            consteval Kvasir::Diagnostic::Text operator()() const {
                Kvasir::Diagnostic::Text t;
                t << "interrupt priority out of range for this core: ";
                t.sdec(Priority) << " is not in 0..";
                t.dec(Max) << " (the levels its priority field holds, 0 the most urgent)";
                return t;
            }
        };

        // Priority itself, or 0 once refused: the static_assert is the only error, not followed by the register
        // layer's "does not fit" for the same value.
        template<int      Priority,
                 unsigned Max>
        constexpr unsigned checkedPriority() {
            constexpr bool fits = Priority >= 0 && static_cast<unsigned>(Priority) <= Max;
            KVASIR_STATIC_ASSERT(fits, (PriorityOutOfRange<Priority, Max>));
            return fits ? static_cast<unsigned>(Priority) : 0U;
        }

        template<int Interrupt>
        constexpr auto getEnableAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::ISER<line / 32>;
            using Field        = Reg::template SETENA<line % 32>;

            return write(Field::SETENAValC::enable_interrupt);
        }

        template<int Interrupt>
        constexpr auto getReadEnableAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::ISER<line / 32>;
            using Field        = Reg::template SETENA<line % 32>;

            return read(Field::setena);
        }

        template<int Interrupt>
        constexpr auto getDisableAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::ICER<line / 32>;
            using Field        = Reg::template CLRENA<line % 32>;

            return write(Field::CLRENAValC::disable_interrupt);
        }

        template<int Interrupt>
        constexpr auto getSetPendingAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::ISPR<line / 32>;
            using Field        = Reg::template SETPEND<line % 32>;

            return write(Field::SETPENDValC::pend_interrupt);
        }

        template<int Interrupt>
        constexpr auto getClearPendingAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::ICPR<line / 32>;
            using Field        = Reg::template CLRPEND<line % 32>;

            return write(Field::CLRPENDValC::clear_pending_state_of_interrupt);
        }

        template<int Priority,
                 int Interrupt>
        constexpr auto getSetPriorityAction() {
            constexpr int line = checkedLine<Interrupt>();
            using Reg          = NvicRegs::IPR<line / 4>;
            using Field        = Reg::template PRI<line % 4>;

            constexpr unsigned level = checkedPriority<Priority, maxPriority(Field::pri)>();
            return write(Field::pri, Register::value<level>());
        }

        template<std::size_t... I>
        [[gnu::always_inline]] inline void clearEnablesAndPending(std::index_sequence<I...>) {
            (apply(write(NvicRegs::ICER<I>::FULLREGISTER, 0xFFFF'FFFFU)), ...);
            (apply(write(NvicRegs::ICPR<I>::FULLREGISTER, 0xFFFF'FFFFU)), ...);
        }

    }   // namespace Detail

    // Every enable and pending bit of the chip's interrupts cleared: NVIC_ICERn / NVIC_ICPRn, one word per 32
    // interrupts up to the chip's last (InterruptOffsetTraits). A word past them holds no interrupt (Armv6-M ARM
    // DDI0419E B3.4: one NVIC_ICER / NVIC_ICPR; Armv8-M ARM DDI0553B.y D1.2.183/184: n = 0 - 15, "Bits
    // corresponding to unimplemented interrupts are RES0"). Startup calls it before it enables what its list claims.
    // A template, so the chip's traits are read where it is called; a member of Nvic itself, so qualified lookup
    // finds it before the port's fallback (kvasir/Port/Core.hpp).
    template<typename Traits = InterruptOffsetTraits<Kvasir::Tag::User>>
    [[gnu::always_inline]] inline void clearAllEnablesAndPending() {
        constexpr auto end = Traits::end;
        static_assert(end > 0, "a chip with no interrupt");
        Detail::clearEnablesAndPending(
          std::make_index_sequence<(static_cast<std::size_t>(end) + 31) / 32>{});
        // the barrier on the target only: the host register mock (tests/decoders) has none
#if defined(__arm__) && !defined(KVASIR_REGISTER_MOCK)
        Kvasir::Core::dsb();
        Kvasir::Core::isb();
#endif
    }

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Enable, Index<Interrupt>>
      : decltype(MPL::list(Detail::getEnableAction<Interrupt>())){};

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Read, Index<Interrupt>>
      : decltype(MPL::list(Detail::getReadEnableAction<Interrupt>())){};

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::Disable, Index<Interrupt>>
      : decltype(MPL::list(Detail::getDisableAction<Interrupt>())){};

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::SetPending, Index<Interrupt>>
      : decltype(MPL::list(Detail::getSetPendingAction<Interrupt>())){};

    template<int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::ClearPending, Index<Interrupt>>
      : decltype(MPL::list(Detail::getClearPendingAction<Interrupt>())){};

    template<int Priority, int Interrupt>
        requires(Interrupt >= 0)
    struct MakeAction<Action::SetPriority<Priority>, Index<Interrupt>>
      : decltype(MPL::list(Detail::getSetPriorityAction<Priority, Interrupt>())){};

    // An interrupt line (or a list of them) masked around thread code that shares state with its ISR. The ICER/ISER
    // writes alone are volatile stores that order nothing else: the section's plain loads could move above the mask
    // and its stores below the unmask, into reach of the interrupt. dsb; isb after the mask so one already on its way
    // is not taken after it either (Armv6-M DDI0419E B2.5, Armv8-M DDI0553B B7.2.16: SCS write; DSB; ISB).
    template<typename Indexes>
    struct LineMask {
        [[gnu::always_inline]] static void mask() {
            apply(makeDisable(Indexes{}));
            Kvasir::Core::dsb();
            Kvasir::Core::isb();
        }

        [[gnu::always_inline]] static void unmask() {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            apply(makeEnable(Indexes{}));
        }
    };

    // Nested masked sections (a callback run inside one may submit(), and its unmask must not open the outer
    // section to the interrupt): only the outermost unmask enables the line; ICER is written at every level. The
    // depth goes up first: an interrupt taken before the barrier sees a section open and does not unmask. Depth is
    // the owner's counter (a static member of the driver), so it stays where the driver keeps it.
    template<typename Indexes, auto& Depth>
    struct CountedLineMask {
        [[gnu::always_inline]] static void mask() {
            ++Depth;
            std::atomic_signal_fence(std::memory_order_seq_cst);
            LineMask<Indexes>::mask();
        }

        [[gnu::always_inline]] static void unmask() {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            if(--Depth == 0) { apply(makeEnable(Indexes{})); }
        }

        [[nodiscard]] static auto depth() { return Depth; }
    };

    // The guard that masks one interrupt line (every interrupt of the core: Core::IrqGuard, InterruptMask.hpp).
    template<typename T>
    struct InterruptGuard {
    private:
        bool oldState;

    public:
        constexpr InterruptGuard() {
            if(!std::is_constant_evaluated()) {
                oldState = static_cast<bool>(get<0>(apply(Nvic::makeRead(T{}))));
                apply(Nvic::makeDisable(T{}));
                // The ICER write is a volatile store and nothing more: without this the
                // guarded body's plain loads and stores may move above it (and the masked
                // interrupt may still be taken after the write, Armv8-M B3.13.7: dsb; isb).
                Kvasir::Core::dsb();
                Kvasir::Core::isb();
            } else {
                oldState = false;
            }
        }

        InterruptGuard(InterruptGuard const&)            = delete;
        InterruptGuard& operator=(InterruptGuard const&) = delete;

        constexpr ~InterruptGuard() {
            if(oldState) {
                // keep the guarded body's stores before the ISER write (the clobber below is
                // after it, too late for that)
                std::atomic_signal_fence(std::memory_order_seq_cst);
                apply(Nvic::makeEnable(T{}));
                // A line that pended while masked is taken here, before the code after the guard:
                // the ISER write's side effect is visible once the write completed (DSB) and a
                // Context synchronization event followed (ISB): DDI0553B.y B7.2.16 RSJQJ, RNPDJ,
                // RHMNM (md l.10750-10758); DDI0403E.e A3.7.3 "Synchronization requirements for
                // System Control Space updates" (md l.3692-3700); DAI0321A 4.5 "Architectural
                // requirements" (md l.1010-1020). Replaces two NOPs that counted on the pipeline.
                Kvasir::Core::dsb();
                Kvasir::Core::isb();
            }
        }
    };
}}   // namespace Kvasir::Nvic

// Startup: a literal write of SETENA bits in ISER<n> enables index 32n + b per set bit b.
namespace Kvasir { namespace Startup {
    namespace Detail {
        using NvicIser0 = Kvasir::Peripheral::NVIC::Registers<>::ISER<0>;

        // A run of `words` word registers from `first` (ISER, IPR, SHPR): the address tests of the decoders.
        constexpr bool inWordRun(unsigned address,
                                 unsigned first,
                                 unsigned words) {
            return address >= first && address < first + 4U * words && (address - first) % 4U == 0;
        }

        // The address is a template parameter, so the call in a `requires` is dependent and the
        // chip's traits are read when Startup asks, not here.
        template<unsigned Address,
                 typename Traits = Nvic::InterruptOffsetTraits<Kvasir::Tag::User>>
        constexpr bool isIserAddress() {
            return inWordRun(Address,
                             NvicIser0::Addr::value,
                             (static_cast<unsigned>(Traits::end) + 31U) / 32U);
        }

        // The positions of the set bits of `Value` (bit b of a word) or of its non-zero bytes (lane l), lowest first:
        // the decoders' index lists are built from these, one pack expansion, no list per bit to flatten.
        template<unsigned Value>
        consteval auto setBits() {
            std::array<int, static_cast<std::size_t>(std::popcount(Value))> bits{};
            std::size_t                                                     n = 0;
            for(int b = 0; b != 32; ++b) {
                if(((Value >> static_cast<unsigned>(b)) & 1U) != 0) { bits[n++] = b; }
            }
            return bits;
        }

        template<unsigned Mask>
        consteval unsigned touchedLanes() {
            unsigned lanes = 0;
            for(unsigned l = 0; l != 4; ++l) {
                if(((Mask >> (8U * l)) & 0xFFU) != 0) { lanes |= 1U << l; }
            }
            return lanes;
        }

        template<int      Base,
                 unsigned Value,
                 std::size_t... I>
        constexpr auto enabledIndexes(std::index_sequence<I...>) {
            return brigand::list<std::integral_constant<int, Base + setBits<Value>()[I]>...>{};
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
        requires(Detail::isIserAddress<Addr>() && (Value & Mask) != 0)
    struct InterruptOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      Register::WriteLiteralAction<Value>>> {
        using type
          = decltype(Detail::enabledIndexes<
                     32 * static_cast<int>((Addr - Detail::NvicIser0::Addr::value) / 4U),
                     (Value & Mask)>(std::make_index_sequence<std::popcount(Value& Mask)>{}));
    };

    // Startup: a write to IPR<n> sets the priority of 4n + lane for every lane its mask touches;
    // anything but a literal over a lane's implemented bits gives unknownIsrLevel.
    namespace Detail {
        using NvicIpr0 = Kvasir::Peripheral::NVIC::Registers<>::IPR<0>;

        template<unsigned Address,
                 typename Traits = Nvic::InterruptOffsetTraits<Kvasir::Tag::User>>
        constexpr bool isIprAddress() {
            return inWordRun(Address,
                             NvicIpr0::Addr::value,
                             (static_cast<unsigned>(Traits::end) + 3U) / 4U);
        }

        // One lane's implemented priority bits, the core.svd's PRI field: 0xC0 for 2 bits, 0xE0 for 3, 0xF0 for 4.
        inline constexpr unsigned priorityLaneMask = NvicIpr0::template PRI<0>::pri.mask;

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

        // The priority of First + lane for every lane `Mask` touches (IPR and SHPR alike).
        template<int      First,
                 unsigned Mask,
                 bool     Literal,
                 unsigned Value,
                 std::size_t... I>
        constexpr auto lanePriorities(std::index_sequence<I...>) {
            return brigand::list<IsrPriority<
              First + setBits<touchedLanes<Mask>()>()[I],
              laneLevel(Mask, Literal, Value, setBits<touchedLanes<Mask>()>()[I])>...>{};
        }

        template<unsigned Mask, int First, typename TAction>
        struct LanePriorities;

        template<unsigned Addr>
        inline constexpr int iprFirstIndex
          = 4 * static_cast<int>((Addr - NvicIpr0::Addr::value) / 4U);

        // The register actions that leave a field at a value the compiler does not know.
        template<typename TAction>
        inline constexpr bool writesUnknownValue = false;

        template<>
        inline constexpr bool writesUnknownValue<Register::WriteAction> = true;

        template<unsigned V, unsigned RuntimeMask>
        inline constexpr bool
          writesUnknownValue<Register::WriteRuntimeAndLiteralAction<V, RuntimeMask>> = true;

        template<unsigned V>
        inline constexpr bool writesUnknownValue<Register::XorLiteralAction<V>> = true;

        // What a priority register write sets: a literal its lanes' levels, anything in writesUnknownValue
        // unknownIsrLevel, other actions (reads) nothing - no PriorityOfAction then.
        template<unsigned Mask, int First, unsigned Value>
        struct LanePriorities<Mask, First, Register::WriteLiteralAction<Value>> {
            using type = decltype(lanePriorities<First, Mask, true, Value>(
              std::make_index_sequence<std::popcount(touchedLanes<Mask>())>{}));
        };

        template<unsigned Mask, int First, typename TAction>
            requires writesUnknownValue<TAction>
        struct LanePriorities<Mask, First, TAction> {
            using type = decltype(lanePriorities<First, Mask, false, 0U>(
              std::make_index_sequence<std::popcount(touchedLanes<Mask>())>{}));
        };

        template<typename TAction>
        inline constexpr bool setsPriority = writesUnknownValue<TAction>;

        template<unsigned Value>
        inline constexpr bool setsPriority<Register::WriteLiteralAction<Value>> = true;
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
        requires(Detail::isIprAddress<Addr>() && Detail::setsPriority<TAction>)
    struct PriorityOfAction<Register::Action<
      Register::
        FieldLocation<Register::Address<Addr, Z, O, RegType, Mode>, Mask, Access, FieldType>,
      TAction>> : Detail::LanePriorities<Mask, Detail::iprFirstIndex<Addr>, TAction> {};
}}   // namespace Kvasir::Startup
