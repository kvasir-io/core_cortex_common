#pragma once
// What a secondary core has to do for itself before any compiled code runs on it, for every core (each core's
// core/StartUp.hpp includes it): the CPACR mask the SDK's entry trampoline writes in assembly
// (kvasir/StartUp/SecondaryCore.hpp), to which the chip layer ORs its own coprocessors.
//   Armv6-M          nothing: no coprocessors and no CPACR (DDI0419E: the SCB has none).
//   Armv7-M, Armv8-M CP10 and CP11 (the FPU) full access, CPACR bits [21:20] and [23:22] = 0b11 (DDI0403E B3.2.20,
//                    md l.21573;
//                    DDI0553B.y D1.2.14, md l.69515, l.69521). CPACR is per core and its CP10/CP11 fields reset to an
//                    UNKNOWN value on a warm reset (D1.2.14), so the core cannot count on the FPU, and a hard-float
//                    ABI uses it in the very first frame.
#include "kvasir/Port/Capabilities.hpp"

#include <cstdint>
#if !KVASIR_CORE_ARMV6M
    #include "core_peripherals/SCB.hpp"
    #include "kvasir/Register/Register.hpp"
#endif

namespace Kvasir::Startup::Core {
struct SecondaryCoreTraits {
#if KVASIR_CORE_ARMV6M
    static constexpr std::uint32_t cpacrEnable = 0;
#else
    // the generated fields that hold bits 20 and 22 - CP10 and CP11 (the m33 SVD names them cp10/cp11, the m4 SVD has
    // them as elements of its CP<m> dim): both bits of each, full access
    using CPACR                                = Kvasir::Peripheral::SCB::Registers<>::CPACR;
    static constexpr std::uint32_t cpacrEnable = ::Kvasir::Register::fieldAt<20>(CPACR{}).mask
                                               | ::Kvasir::Register::fieldAt<22>(CPACR{}).mask;
    static_assert(cpacrEnable == ((3U << 20U) | (3U << 22U)),
                  "CPACR.CP10 bits 21:20, CP11 bits 23:22");
#endif
};
}   // namespace Kvasir::Startup::Core
