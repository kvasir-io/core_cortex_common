#pragma once
// Startup::Core::cleanCore() and its pieces (every core's core/StartUp.hpp includes it): what a warm reset would have
// done and the SDK's naked reset entry does not (kvasir/StartUp/StartUp.hpp, KVASIR_RESET_ENTRY_ASM), for an image not entered from
// a reset. Before .data and .bss: the register layer only, no static, each write its own apply() (stores in the order
// written), always inlined. Citations: Armv6-M ARM DDI0419E, Armv7-M ARM DDI0403E, Armv8-M ARM DDI0553B.y.
#include "core_peripherals/SCB.hpp"
#include "core_peripherals/SYSTICK.hpp"
#include "kvasir/Port/Capabilities.hpp"
#include "kvasir/Port/LinkerSymbols.hpp"
#include "kvasir/Register/Register.hpp"
#ifdef __ARM_FP
    #include "core_peripherals/FP.hpp"
#endif

#include <cstdint>

namespace Kvasir::Startup::Core::Clean {

// SYST_CSR (v6-M B3.3.3, v7-M B3.3.3, v8-M D1.2.241): ENABLE and TICKINT off; CLKSOURCE kept, its reset value is
// IMPLEMENTATION DEFINED (v6-M Table B3-13 "0x00000000 or 0x00000004") and SystickClock writes it anyway
[[gnu::always_inline]] inline void stopSysTick() {
    using SystickR = Kvasir::Peripheral::SYSTICK::Registers<>;
    using Kvasir::Register::apply;
    using Kvasir::Register::clear;
    apply(clear(SystickR::CSR::enable), clear(SystickR::CSR::tickint));
}

// ICSR (v6-M B3.2.4 Table B3-6, v7-M B3.2.4, v8-M D1.2.126): PENDSVCLR and PENDSTCLR written over the register's
// defaults, one store and no read; the SVDs leave the RO status fields read-write (and v8-M's STTNS is read-write), so
// a plain set() would read first. v8-M STTNS gets its reset value 0.
[[gnu::always_inline]] inline void clearPendSvSysTick() {
    using ScbR = Kvasir::Peripheral::SCB::Registers<>;
    using Kvasir::Register::apply;
    using Kvasir::Register::set;
    apply(ScbR::ICSR::overrideDefaults(set(ScbR::ICSR::pendsvclr), set(ScbR::ICSR::pendstclr)));
}

// VTOR (v6-M B3.2.5: RAZ/WI where not implemented; v7-M B3.2.5; v8-M D1.2.272: TBLOFF bits [31:7]): this image's
// table. The only VTOR write of a start: a chip whose boot ROM leaves VTOR elsewhere (a copy it loaded from) relies on
// this one. The table has to be naturally aligned (v8-M B3.30 RVDPD, IPLSB: the entry for the highest exception inside
// the alignment; at least 128 bytes); the SDK's vector table and the linker scripts place it so.
[[gnu::always_inline]] inline void vtorToThisImage() {
    using ScbR = Kvasir::Peripheral::SCB::Registers<>;
    using Kvasir::Register::apply;
    using Kvasir::Register::write;
    apply(
      write(ScbR::VTOR::FULLREGISTER,
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&_LINKER_vectors_start_))));
}

#if KVASIR_CORE_HAS_FAULT_STATUS
// FAULTMASK = 0, a special register; PRIMASK stays set (the entry stub). Armv6-M has no FAULTMASK.
[[gnu::always_inline]] inline void clearFaultMask() { asm volatile("cpsie f" ::: "memory"); }

// CFSR, HFSR (v7-M B3.2.15, B3.2.16; v8-M D1.2.11, D1.2.123): write one to clear, every bit an image before left
[[gnu::always_inline]] inline void clearFaultStatus() {
    using ScbR = Kvasir::Peripheral::SCB::Registers<>;
    using Kvasir::Register::apply;
    using Kvasir::Register::write;
    apply(write(ScbR::CFSR::FULLREGISTER, 0xFFFF'FFFFU));
    apply(write(ScbR::HFSR::FULLREGISTER, 0xFFFF'FFFFU));
}
#endif

#ifdef __ARM_FP
// FPCCR (v7-M B3.2.21, v8-M D1.2.100): no lazy FP context pending; CONTROL.FPCA is 0 already (the entry stub)
[[gnu::always_inline]] inline void noLazyFpPending() {
    using FpR = Kvasir::Peripheral::FP::Registers<>;
    using Kvasir::Register::apply;
    using Kvasir::Register::write;
    apply(write(FpR::FPCCR::LSPACTValC::lazy_state_preservation_is_not_active));
}
#endif

// dsb; isb - one asm statement, as the cleanCore()s had it (not Core::dsb(); Core::isb(): two statements would let
// the compiler put register-only instructions between them in the reset path)
[[gnu::always_inline]] inline void barrier() { asm volatile("dsb\n isb" ::: "memory"); }

}   // namespace Kvasir::Startup::Core::Clean

namespace Kvasir::Startup::Core {
// What a warm reset would have done and the SDK's naked reset entry does not, for an image not entered from a reset:
// SysTick stopped, PendSV/SysTick no longer pending, VTOR on this image's table; where the core has them
// (KVASIR_CORE_HAS_FAULT_STATUS: Armv7-M, Armv8-M Mainline) FAULTMASK clear and the fault status of an image before
// cleared; with an FPU (__ARM_FP) no lazy FP context pending. One body for every core: the pieces above are what
// differs, and each is there or not by the core's capability macros.
[[gnu::always_inline]] inline void cleanCore() {
    Clean::stopSysTick();
    Clean::clearPendSvSysTick();
    Clean::vtorToThisImage();
#if KVASIR_CORE_HAS_FAULT_STATUS
    Clean::clearFaultMask();
    Clean::clearFaultStatus();
#endif
#ifdef __ARM_FP
    Clean::noLazyFpPending();
#endif
    Clean::barrier();
}
}   // namespace Kvasir::Startup::Core
