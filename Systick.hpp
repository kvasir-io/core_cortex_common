#pragma once

#include "core/InterruptMask.hpp"
#include "core_peripherals/SCB.hpp"
#include "core_peripherals/SYSTICK.hpp"
#include "cortex_common/SystemControl.hpp"
#include "cortex_common/SystickReading.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Common/Tags.hpp"   // Port::LateT
#include "kvasir/Register/Register.hpp"
#include "kvasir/Register/Utility.hpp"
#include "kvasir/StartUp/Resources.hpp"
#include "kvasir/Util/Config.hpp"
#include "kvasir/Util/SteadyClock.hpp"
#include "kvasir/Util/attributes.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Kvasir {
namespace Systick {
    using SystickRegs                       = Kvasir::Peripheral::SYSTICK::Registers<>;
    static constexpr auto useProcessorClock = SystickRegs::CSR::CLKSOURCEValC::processor;
}   // namespace Systick

namespace Nvic {
    using SystickRegs = Kvasir::Peripheral::SYSTICK::Registers<>;

    // SysTick is a core exception: these need no chip, and no core rule refuses it an action (it is in none of
    // Core::InterruptRules' no* lists on any profile, InterruptRules.hpp). Partial specialisations on Index<I> with
    // I == -1, as the NVIC's are.
    template<int I>
        requires(I == CoreInterrupts::systick.value)
    struct MakeAction<Action::Enable, Index<I>>
      : decltype(write(SystickRegs::CSR::TICKINTValC::interrupt_enabled)){};

    template<int I>
        requires(I == CoreInterrupts::systick.value)
    struct MakeAction<Action::Disable, Index<I>>
      : decltype(write(SystickRegs::CSR::TICKINTValC::interrupt_disabled)){};

    template<int I>
        requires(I == CoreInterrupts::systick.value)
    struct MakeAction<Action::Read, Index<I>> : decltype(read(SystickRegs::CSR::tickint)){};
}   // namespace Nvic

// Startup: TICKINT written to one enables the SysTick exception.
namespace Startup {
    template<unsigned Value>
        requires(Value != 0)
    struct InterruptOfAction<
      Register::Action<std::remove_cvref_t<decltype(Nvic::SystickRegs::CSR::tickint)>,
                       Register::WriteLiteralAction<Value>>> {
        using type = brigand::list<std::integral_constant<int, CoreInterrupts::systick.value>>;
    };
}   // namespace Startup

namespace Systick {
    template<typename TConfig>
    struct SystickClockBase;

    namespace Detail {
        // The std::chrono shell of SystickClockBase (duration, rep, period, time_point, is_steady, time_point +/- any
        // duration): one tick of the clock is one count of the counter.
        template<typename TConfig>
        using SystickShell = Chrono::SteadyClock<
          SystickClockBase<TConfig>,
          std::chrono::duration<std::int64_t, std::ratio<1, TConfig::clockSpeed>>>;
    }   // namespace Detail

    template<typename TConfig>
    struct SystickClockBase : Detail::SystickShell<TConfig> {
    private:
        // Config: clockSpeed, clockBase, minOverrunTime, optionally synchronised.
        using Config                              = TConfig;
        static constexpr std::uint64_t clockSpeed = Config::clockSpeed;
        using Regs                                = Kvasir::Peripheral::SYSTICK::Registers<>;
        // SysTick's index, named late (Port::LateT on the config): the init lists below then ask for the
        // chip's interrupt rules only when a firmware instantiates the clock (same type, Index<-1>).
        using Irq = Port::LateT<Nvic::Index<CoreInterrupts::systick.value>, TConfig>;

    public:
        // On the processor clock this claims it at Config::clockSpeed.
        using Claims
          = std::conditional_t<std::is_same_v<std::remove_cvref_t<decltype(Config::clockBase)>,
                                              std::remove_cvref_t<decltype(useProcessorClock)>>,
                               brigand::list<Startup::ProcessorClock<Config::clockSpeed>>,
                               brigand::list<>>;

        using Shell = Detail::SystickShell<TConfig>;
        using typename Shell::duration;
        using typename Shell::rep;
        using typename Shell::time_point;

    private:
        static_assert(Config::clockSpeed
                        < std::numeric_limits<std::uint64_t>::max() / 100'000'000ULL,
                      "SystickClockBase: Config::clockSpeed is too high (at most 2^64 / 10^8 Hz)");

        // SYST_RVR.RELOAD is bits [23:0] (Armv6-M ARM DDI0419E B3.3.4 Table B3-15, Armv8-M ARM DDI0553B
        // D1.2.243): the counter always runs its full 24 bits, whatever the clock.
        static constexpr std::uint32_t calcReloadValue() { return (1U << 24U) - 1U; }

        // 32 bits while the wraps of Config::minOverrunTime fit (SystickReading.hpp OverrunWord).
        using OverrunT
          = Detail::OverrunWordT<Detail::calcOverRunValue(clockSpeed,
                                                          std::uint64_t{calcReloadValue()} + 1ULL,
                                                          Config::minOverrunTime)>;
        static inline Detail::OverrunCounter<OverrunT> overruns{};

        // A synchronised clock adds an epoch so it reads the same as a clock on another core.
        static constexpr bool synchronised
          = static_cast<bool>(KVASIR_CONFIG_OR(Config, synchronised, false));

        static inline rep epoch_{};

        // The overruns read before and after the counter must agree, else retry (pairReading).
        // Not COUNTFLAG: reading CSR clears it for every caller, pairing a count a reload off.
        [[KVASIR_NO_SANITIZE_UNSIGNED_OVERFLOW]] static duration rawNow() {
            static constexpr auto reloadValue = calcReloadValue();
            using Icsr                        = Kvasir::Peripheral::SCB::Registers<>::ICSR;

            while(true) {
                OverrunT const      before  = overruns.load();
                std::uint32_t const count   = apply(read(Regs::CVR::current));
                bool const          pending = fieldEquals(Icsr::PENDSTSETValC::set_pending);
                OverrunT const      after   = overruns.load();
                if(auto const ticks
                   = Detail::pairReading(before, count, pending, after, reloadValue))
                {
                    return duration{static_cast<rep>(*ticks)};
                }
            }
        }

        static void onIsr() { overruns.increment(); }

        static void delayTicks(std::uint32_t ticksToWait) {
            std::uint32_t const countStart    = apply(read(Regs::CVR::current));
            OverrunT const      overrunsStart = overruns.load();
            while(true) {
                std::uint32_t const countNow    = apply(read(Regs::CVR::current));
                OverrunT const      overrunsNow = overruns.load();
                if(Detail::delayReached(countStart,
                                        countNow,
                                        static_cast<OverrunT>(overrunsNow - overrunsStart),
                                        ticksToWait,
                                        calcReloadValue()))
                {
                    break;
                }
            }
        }

        // delay()'s waits, at most one reload each (Detail::waitInChunks).
        struct TickWait {
            static void wait(std::uint32_t ticks) { delayTicks(ticks); }
        };

    public:
        [[KVASIR_NO_SANITIZE_UNSIGNED_OVERFLOW]] static time_point now() {
            if constexpr(synchronised) {
                return time_point{rawNow() + duration{epoch_}};
            } else {
                return time_point{rawNow()};
            }
        }

        // The counter without the epoch. Owning core only.
        static duration raw() { return rawNow(); }

        // Make now() read `referenceAt` where raw() read `rawAt`. Owning core only; the 64-bit
        // epoch is written with interrupts masked.
        static void syncTo(duration referenceAt,
                           duration rawAt) {
            static_assert(synchronised, "syncTo() needs Config::synchronised = true");
            Core::IrqGuard const masked{};
            epoch_ = referenceAt.count() - rawAt.count();
        }

        // Make now() read `referenceNow` from this instant on.
        static void syncTo(duration referenceNow) { syncTo(referenceNow, rawNow()); }

        /// Blocking delay of a compile-time duration: `delay<std::chrono::milliseconds, 5>()`.
        template<typename Duration,
                 typename duration::rep Value>
        static void delay() {
            delay(Duration{Value});
        }

        /// Blocking delay; zero or negative returns at once.
        template<typename Rep,
                 typename Period>
        static void delay(std::chrono::duration<Rep,
                                                Period> d) {
            auto const ticks = std::chrono::duration_cast<duration>(d).count();
            if(ticks <= 0) { return; }
            Detail::waitInChunks<TickWait>(static_cast<std::uint64_t>(ticks), calcReloadValue());
        }

        /// Block until this clock reads `t`; returns at once if `t` is already past.
        static void delayUntil(time_point t) {
            auto const n = now();
            if(t > n) { delay(t - n); }
        }

        static constexpr auto initStepPeripheryConfig
          = list(write(Config::clockBase),
                 write(Regs::RVR::reload, Register::value<calcReloadValue()>()),
                 write(Regs::CSR::ENABLEValC::counter_is_disabled),
                 makeDisable(Irq{}),
                 write(Regs::CVR::current, Register::value<0>()));

        static constexpr auto initStepInterruptConfig
          = list(action(Nvic::Action::setPriority0, Irq{}),
                 action(Nvic::Action::clearPending, Irq{}));

        static constexpr auto initStepPeripheryEnable
          = list(write(Regs::CSR::ENABLEValC::counter_is_operating), makeEnable(Irq{}));

        using Isr = Nvic::Isr<std::addressof(onIsr), Irq>;
    };
}   // namespace Systick
}   // namespace Kvasir
