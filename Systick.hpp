#pragma once

#include "core_peripherals/SCB.hpp"
#include "core_peripherals/SYSTICK.hpp"
#include "cortex_common/SystemControl.hpp"
#include "cortex_common/SystickReading.hpp"
#include "kvasir/Atomic/Atomic.hpp"
#include "kvasir/Common/Interrupt.hpp"
#include "kvasir/Register/Register.hpp"
#include "kvasir/Register/Utility.hpp"
#include "kvasir/StartUp/Resources.hpp"
#include "kvasir/Util/attributes.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Kvasir {
namespace Systick {
    using SystickRegs                       = Kvasir::Peripheral::SYSTICK::Registers<>;
    static constexpr auto useExternalClock  = SystickRegs::CSR::CLKSOURCEValC::external;
    static constexpr auto useProcessorClock = SystickRegs::CSR::CLKSOURCEValC::processor;
}   // namespace Systick

namespace Nvic {
    using SystickRegs = Kvasir::Peripheral::SYSTICK::Registers<>;

    template<>
    struct MakeAction<Action::Enable, Index<Kvasir::Interrupt::systick.index()>>
      : decltype(write(SystickRegs::CSR::TICKINTValC::interrupt_enabled)) {
        static_assert(Detail::interruptIndexValid(Interrupt::systick.index(),
                                                  std::begin(InterruptOffsetTraits<void>::noEnable),
                                                  std::end(InterruptOffsetTraits<void>::noEnable)),
                      "Unable to enable this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::Disable, Index<Kvasir::Interrupt::systick.index()>>
      : decltype(write(SystickRegs::CSR::TICKINTValC::interrupt_disabled)) {
        static_assert(
          Detail::interruptIndexValid(Interrupt::systick.index(),
                                      std::begin(InterruptOffsetTraits<void>::noDisable),
                                      std::end(InterruptOffsetTraits<void>::noDisable)),
          "Unable to disable this interrupt, index is out of range");
    };

    template<>
    struct MakeAction<Action::Read, Index<Kvasir::Interrupt::systick.index()>>
      : decltype(read(SystickRegs::CSR::tickint)) {
        static_assert(Detail::interruptIndexValid(Interrupt::systick.index(),
                                                  std::begin(InterruptOffsetTraits<void>::noEnable),
                                                  std::end(InterruptOffsetTraits<void>::noEnable)),
                      "Unable to read this interrupt, index is out of range");
    };
}   // namespace Nvic

// Startup: TICKINT written to one enables the SysTick exception.
namespace Startup {
    template<unsigned Value>
        requires(Value != 0)
    struct InterruptOfAction<
      Register::Action<std::remove_cvref_t<decltype(Nvic::SystickRegs::CSR::tickint)>,
                       Register::WriteLiteralAction<Value>>> {
        using type = brigand::list<std::integral_constant<int, Kvasir::Interrupt::systick.index()>>;
    };
}   // namespace Startup

namespace Systick {
    template<typename TConfig>
    struct SystickClockBase {
    private:
        // Config: clockSpeed, clockBase, minOverrunTime, optionally synchronised.
        using Config                              = TConfig;
        static constexpr std::uint64_t ClockSpeed = Config::clockSpeed;
        using Regs                                = Kvasir::Peripheral::SYSTICK::Registers<>;

    public:
        // On the processor clock this claims it at Config::clockSpeed.
        using Claims
          = std::conditional_t<std::is_same_v<std::remove_cvref_t<decltype(Config::clockBase)>,
                                              std::remove_cvref_t<decltype(useProcessorClock)>>,
                               brigand::list<Startup::ProcessorClock<Config::clockSpeed>>,
                               brigand::list<>>;

        using duration
          = std::chrono::duration<std::int64_t,
                                  std::ratio<1, Config::clockSpeed>>;   // std::chrono::nanoseconds;
        using rep        = typename duration::rep;
        using period     = typename duration::period;
        using time_point = std::chrono::time_point<SystickClockBase, duration>;

        static constexpr bool is_steady = true;

        template<typename Rep,
                 typename Period>
        friend constexpr std::enable_if_t<!std::is_same_v<std::chrono::duration<Rep,
                                                                                Period>,
                                                          duration>,
                                          time_point>
        operator+(time_point                    t,
                  std::chrono::duration<Rep,
                                        Period> d) {
            return t + std::chrono::duration_cast<duration>(d);
        }

        template<typename Rep,
                 typename Period>
        friend constexpr std::enable_if_t<!std::is_same_v<std::chrono::duration<Rep,
                                                                                Period>,
                                                          duration>,
                                          time_point>
        operator-(time_point                    t,
                  std::chrono::duration<Rep,
                                        Period> d) {
            return t - std::chrono::duration_cast<duration>(d);
        }

    private:
        static_assert(Config::clockSpeed
                        < std::numeric_limits<std::uint64_t>::max() / 100'000'000ULL,
                      "ClockSpeed to high");

        template<std::uint64_t OverRunValue, typename = void>
        struct GetOverrunType {
            using type = std::uint64_t;
        };

        template<std::uint64_t OverRunValue>
        struct GetOverrunType<
          OverRunValue,
          std::enable_if_t<(OverRunValue <= std::numeric_limits<std::uint32_t>::max())>> {
            using type = std::uint32_t;
        };

        template<std::uint64_t OverRunValue>
        using GetOverrunTypeT = typename GetOverrunType<OverRunValue, void>::type;

        static constexpr std::uint32_t calcReloadValue(std::uint64_t clockSpeed) {
            (void)clockSpeed;
            return (1U << 24U) - 1U;
        }

        static constexpr std::uint64_t calcOverRunValue(std::uint64_t            clockSpeed,
                                                        std::chrono::nanoseconds overrunTime) {
            std::uint64_t NanoSecPerOverrun
              = ((std::uint64_t(calcReloadValue(clockSpeed)) + 1ULL) * 1'000'000'000ULL)
              / clockSpeed;
            return (std::uint64_t(overrunTime.count()) + NanoSecPerOverrun - 1ULL)
                 / NanoSecPerOverrun;
        }

        using overrunT = GetOverrunTypeT<calcOverRunValue(ClockSpeed, Config::minOverrunTime)>;
        static inline std::atomic<overrunT> overruns{};

        // A synchronised clock adds an epoch so it reads the same as a clock on another core.
        static constexpr bool Synchronised = [] {
            if constexpr(requires { Config::synchronised; }) {
                return static_cast<bool>(Config::synchronised);
            } else {
                return false;
            }
        }();

        static inline rep epoch_{};

        // The overruns read before and after the counter must agree, else retry (pairReading).
        // Not COUNTFLAG: reading CSR clears it for every caller, pairing a count a reload off.
        [[KVASIR_NO_SANITIZE_UNSIGNED_OVERFLOW]] static duration rawNow() {
            static constexpr auto reloadValue = calcReloadValue(ClockSpeed);
            using Icsr                        = Kvasir::Peripheral::SCB::Registers<>::ICSR;

            while(true) {
                overrunT const      before  = overruns.load(std::memory_order_relaxed);
                std::uint32_t const count   = apply(read(Regs::CVR::current));
                bool const          pending = fieldEquals(Icsr::PENDSTSETValC::set_pending);
                overrunT const      after   = overruns.load(std::memory_order_relaxed);
                if(auto const ticks
                   = Detail::pairReading(before, count, pending, after, reloadValue))
                {
                    return duration{static_cast<rep>(*ticks)};
                }
            }
        }

        static void onIsr() {
            overrunT old = overruns.load(std::memory_order_relaxed);
            ++old;
            overruns.store(old, std::memory_order_relaxed);
        }

        static void delay_ticks(std::uint32_t ticksToWait) {
            std::uint32_t const countStart    = apply(read(Regs::CVR::current));
            overrunT const      overrunsStart = overruns.load(std::memory_order_relaxed);
            while(true) {
                std::uint32_t const countNow    = apply(read(Regs::CVR::current));
                overrunT const      overrunsNow = overruns.load(std::memory_order_relaxed);
                auto const          countsRaw   = std::int32_t(countStart - countNow);
                // Across a reload the counter passed 0 and reloadValue: reloadValue + 1 counts.
                std::uint32_t const countsElapsed
                  = countsRaw >= 0 ? std::uint32_t(countsRaw)
                                   : countStart + (calcReloadValue(ClockSpeed) + 1U - countNow);
                if(countsElapsed >= ticksToWait || overrunsNow - overrunsStart >= 2
                   || (countNow < countStart && overrunsNow - overrunsStart == 1))
                {
                    break;
                }
            }
        }

    public:
        [[KVASIR_NO_SANITIZE_UNSIGNED_OVERFLOW]] static time_point now() {
            if constexpr(Synchronised) {
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
            static_assert(Synchronised, "syncTo() needs Config::synchronised = true");
            bool const enabled = Nvic::disable_all_and_get_old_state();
            epoch_             = referenceAt.count() - rawAt.count();
            if(enabled) { Nvic::enable_all(); }
        }

        // Make now() read `referenceNow` from this instant on.
        static void syncTo(duration referenceNow) { syncTo(referenceNow, rawNow()); }

        /// Blocking delay of a compile-time duration: `delay<std::chrono::milliseconds, 5>()`.
        template<typename Duration,
                 typename duration::rep value>
        static void delay() {
            delay(Duration{value});
        }

        /// Blocking delay; zero or negative returns at once.
        template<typename Rep,
                 typename Period>
        static void delay(std::chrono::duration<Rep,
                                                Period> d) {
            static constexpr auto reloadValue = calcReloadValue(ClockSpeed);
            auto const            ticks       = std::chrono::duration_cast<duration>(d).count();
            if(ticks <= 0) { return; }
            auto ticksToWait = static_cast<std::uint64_t>(ticks);
            while(ticksToWait >= reloadValue) {
                delay_ticks(reloadValue);
                ticksToWait -= reloadValue;
            }
            delay_ticks(static_cast<std::uint32_t>(ticksToWait));
        }

        /// Block until this clock reads `t`; returns at once if `t` is already past.
        static void delayUntil(time_point t) {
            auto const n = now();
            if(t > n) { delay(t - n); }
        }

        static constexpr auto initStepPeripheryConfig
          = list(write(Config::clockBase),
                 write(Regs::RVR::reload, Register::value<calcReloadValue(ClockSpeed)>()),
                 write(Regs::CSR::ENABLEValC::counter_is_disabled),
                 makeDisable(Interrupt::systick),
                 write(Regs::CVR::current, Register::value<0>()));

        static constexpr auto initStepInterruptConfig
          = list(action(Nvic::Action::setPriority0, Interrupt::systick),
                 action(Nvic::Action::clearPending, Interrupt::systick));

        static constexpr auto initStepPeripheryEnable
          = list(write(Regs::CSR::ENABLEValC::counter_is_operating),
                 makeEnable(Interrupt::systick));

        static constexpr Nvic::Isr<std::addressof(onIsr),
                                   std::decay_t<decltype(Interrupt::systick)>>
          isr{};
    };
}   // namespace Systick
}   // namespace Kvasir
