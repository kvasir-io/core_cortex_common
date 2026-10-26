// InterruptRules.hpp against the Arm exception tables (Nvic index = exception number - 16): Armv6-M ARM DDI0419E
// Table B1-3 (4-10 and 12-13 reserved), Armv7-M ARM DDI0403E Table B1-4 (7-10 and 13 reserved, 12 DebugMonitor),
// Armv8-M ARM DDI0553B.y B3.9 (8-10 and 13 reserved, 7 SecureFault, 12 DebugMonitor); 12 is disabled as Kvasir does
// not serve DebugMonitor. Standard headers only: building is most of the test.
#include "InterruptRules.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
using Kvasir::Core::ArmProfile;
using Kvasir::Core::CoreInterruptRules;
using Kvasir::Core::disabledWith;

template<std::size_t N,
         std::size_t M>
constexpr bool same(std::array<int,
                               N> const& a,
                    std::array<int,
                               M> const& b) {
    return N == M && std::equal(a.begin(), a.end(), b.begin());
}

template<std::size_t N>
constexpr bool allCoreExceptions(std::array<int,
                                            N> const& a) {
    return std::all_of(a.begin(), a.end(), [](int i) { return i >= -14 && i < 0; });
}

constexpr int exc(int number) { return number - 16; }

using V6 = CoreInterruptRules<ArmProfile::v6m>;
using V7 = CoreInterruptRules<ArmProfile::v7m>;
using V8 = CoreInterruptRules<ArmProfile::v8mMain>;

// reserved slots per profile, element by element
static_assert(same(V6::disabled,
                   std::array{exc(4),
                              exc(5),
                              exc(6),
                              exc(7),
                              exc(8),
                              exc(9),
                              exc(10),
                              exc(12),
                              exc(13)}));
static_assert(same(V7::disabled,
                   std::array{exc(7),
                              exc(8),
                              exc(9),
                              exc(10),
                              exc(12),
                              exc(13)}));
static_assert(same(V8::disabled,
                   std::array{exc(8),
                              exc(9),
                              exc(10),
                              exc(12),
                              exc(13)}));

// the actions every profile shares: NMI 2, HardFault 3, SVCall 11, PendSV 14
static_assert(V6::begin == exc(2) && V7::begin == exc(2) && V8::begin == exc(2));
static_assert(same(V6::noEnable,
                   std::array{exc(2),
                              exc(11),
                              exc(14)}));
static_assert(same(V6::noDisable,
                   std::array{exc(2),
                              exc(11),
                              exc(14)}));
static_assert(same(V6::noSetPending,
                   std::array{exc(11),
                              exc(3)}));
static_assert(same(V6::noClearPending,
                   std::array{exc(2),
                              exc(11),
                              exc(3)}));
static_assert(same(V6::noSetPriority,
                   std::array{exc(2),
                              exc(3)}));
static_assert(same(V7::noEnable,
                   V6::noEnable)
              && same(V8::noSetPriority,
                      V6::noSetPriority));

// only core exceptions in every list
static_assert(allCoreExceptions(V6::disabled) && allCoreExceptions(V7::disabled)
              && allCoreExceptions(V8::disabled));
static_assert(allCoreExceptions(V6::noEnable) && allCoreExceptions(V6::noDisable)
              && allCoreExceptions(V6::noSetPending) && allCoreExceptions(V6::noClearPending)
              && allCoreExceptions(V6::noSetPriority));

// SysTick (15, index -1) takes every action on every profile
constexpr bool notIn(int         i,
                     auto const& a) {
    return std::find(a.begin(), a.end(), i) == a.end();
}

static_assert(notIn(-1,
                    V6::disabled)
              && notIn(-1,
                       V6::noEnable)
              && notIn(-1,
                       V6::noSetPending)
              && notIn(-1,
                       V6::noClearPending)
              && notIn(-1,
                       V6::noSetPriority));

// disabledWith: the core's slots first, then each chip's gaps in order; empty gaps allowed
static_assert(same(disabledWith(V6::disabled),
                   V6::disabled));
static_assert(same(disabledWith(V6::disabled,
                                std::array<int,
                                           0>{}),
                   V6::disabled));
static_assert(same(disabledWith(V8::disabled,
                                std::array{21,
                                           22}),
                   std::array{exc(8),
                              exc(9),
                              exc(10),
                              exc(12),
                              exc(13),
                              21,
                              22}));
static_assert(same(disabledWith(V7::disabled,
                                std::array{42},
                                std::array{79}),
                   std::array{exc(7),
                              exc(8),
                              exc(9),
                              exc(10),
                              exc(12),
                              exc(13),
                              42,
                              79}));
}   // namespace

int main() {
    std::puts("ALL OK");
    return 0;
}
