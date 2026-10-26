#pragma once

// The exception rules of each Arm profile, as Nvic indexes (exception number - 16): which core
// exceptions take which NVIC/SCB action, and which exception slots are reserved. These are facts of
// the architecture, not of a chip: a chip's InterruptOffsetTraits<Tag::User> takes them from its core's
// Kvasir::Core::InterruptRules (core/CoreInterrupts.hpp) and adds its own end and device gaps.
// Standard headers only, so the rules can be tested without the SDK.

#include <array>
#include <cstddef>

namespace Kvasir::Core {
enum class ArmProfile { v6m, v7m, v8mMain };

namespace Detail {
    inline constexpr int nmi       = -14;
    inline constexpr int hardFault = -13;
    inline constexpr int svCall    = -5;
    inline constexpr int pendSV    = -2;

    // The core exceptions' actions, the lists every chip package carried: no enable or disable for
    // NMI, SVCall and PendSV (always enabled); ICSR pends NMI, PendSV and SysTick only and clears
    // PendSV and SysTick (DDI0419E Table B3-6); NMI and HardFault have a fixed priority.
    struct CommonInterruptRules {
        static constexpr int        begin          = nmi;
        static constexpr std::array noEnable       = {nmi, svCall, pendSV};
        static constexpr std::array noDisable      = {nmi, svCall, pendSV};
        static constexpr std::array noSetPending   = {svCall, hardFault};
        static constexpr std::array noClearPending = {nmi, svCall, hardFault};
        static constexpr std::array noSetPriority  = {nmi, hardFault};
    };
}   // namespace Detail

template<ArmProfile P>
struct CoreInterruptRules;

// Armv6-M: exceptions 4-10 and 12-13 are reserved (DDI0419E Table B1-3).
template<>
struct CoreInterruptRules<ArmProfile::v6m> : Detail::CommonInterruptRules {
    static constexpr std::array disabled = {-12, -11, -10, -9, -8, -7, -6, -4, -3};
};

// Armv7-M: 7-10 and 13 are reserved (DDI0403E Table B1-4); 12 is DebugMonitor, which Kvasir does
// not serve (a policy, not an architecture fact).
template<>
struct CoreInterruptRules<ArmProfile::v7m> : Detail::CommonInterruptRules {
    static constexpr std::array disabled = {-9, -8, -7, -6, -4, -3};
};

// Armv8-M Mainline: 8-10 and 13 are reserved, 7 is SecureFault (DDI0553B.y B3.9, the Security
// Extension assumed: a core without it would need a profile of its own); 12 as on Armv7-M.
template<>
struct CoreInterruptRules<ArmProfile::v8mMain> : Detail::CommonInterruptRules {
    static constexpr std::array disabled = {-8, -7, -6, -4, -3};
};

// The core's reserved slots, then the chip's gaps, in that order.
template<std::size_t N,
         std::size_t... M>
consteval auto disabledWith(std::array<int,
                                       N> const& core,
                            std::array<int,
                                       M> const&... gaps) {
    std::array<int, (N + ... + M)> all{};
    std::size_t                    i = 0;
    for(int const v : core) { all[i++] = v; }
    (
      [&] {
          for(int const v : gaps) { all[i++] = v; }
      }(),
      ...);
    return all;
}
}   // namespace Kvasir::Core

namespace Kvasir::Core {
// A chip's Nvic::InterruptOffsetTraits<Tag::User>, written once: its core's rules (Rules: the core's
// Core::InterruptRules, core/CoreInterrupts.hpp), one past its last device line (End: svd_converter's
// Peripheral::interruptEnd) and its reserved slots - the core's, then each list of Gaps (std::array<int, N>:
// svd_converter's Peripheral::interruptGaps and whatever the chip adds). A chip with more (RP's perCore lines) derives
// and adds them.
template<typename Rules, int End, auto... Gaps>
struct ChipInterruptTraits : Rules {
    static constexpr int  end      = End;
    static constexpr auto disabled = disabledWith(Rules::disabled, Gaps...);
};
}   // namespace Kvasir::Core
