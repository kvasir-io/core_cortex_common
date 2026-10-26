#pragma once
// The fault module body every Armv7-M / Armv8-M mainline core shares (core_cortex_m4, core_cortex_m33): the decode
// table's rows that both architectures have, the snapshot of the fault registers and the CFSR/HFSR clear, each over
// the core's own generated SCB (`SCB_R`). A core's core/Fault.hpp keeps what is its own: the rows only it has (the
// m33's STKOF, first in the usage rows), the registers only it has (the m33's SAU words), the ARM ARM position
// asserts against its SVD, the fault enables (EarlyInitList: the SVDs name the values differently) and its Log.
// Armv6-M has no fault status registers and does not use this header (core_cortex_m0plus core/Fault.hpp).
#include "FaultDecode.hpp"
#include "FaultSnapshot.hpp"
#include "kvasir/Register/Register.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Kvasir::Fault::Core::V7m {
// The decode table of a v7-M/v8-M core from its SVD's field masks: the core's own usage rows first (`lead`, in
// priority order), then the six both architectures have. Rows in priority order inside each sub-register (the first
// set bit names the fault).
template<class ScbR,
         std::size_t Lead = 0>
constexpr FaultTable<Lead + 6,
                     6,
                     5>
faultTable(std::array<FaultRow,
                      Lead> const& lead = {}) {
    FaultTable<Lead + 6, 6, 5> t{
      .usage        = {},
      .bus          = {{
        {ScbR::BFSR::lsperr.mask, FaultDescription::lazyStatePreservationError},
        {ScbR::BFSR::stkerr.mask, FaultDescription::exceptionStackingError},
        {ScbR::BFSR::unstkerr.mask, FaultDescription::exceptionUnstackingError},
        {ScbR::BFSR::impreciserr.mask, FaultDescription::impreciseDataAccessError},
        {ScbR::BFSR::preciserr.mask, FaultDescription::preciseDataAccessError},
        {ScbR::BFSR::ibuserr.mask, FaultDescription::instructionBusError},
      }},
      .memManage    = {{
        {ScbR::MMFSR::mlsperr.mask, FaultDescription::lazyStatePreservationError},
        {ScbR::MMFSR::mstkerr.mask, FaultDescription::exceptionStackingError},
        {ScbR::MMFSR::munstkerr.mask, FaultDescription::exceptionUnstackingError},
        {ScbR::MMFSR::daccviol.mask, FaultDescription::dataAccessViolation},
        {ScbR::MMFSR::iaccviol.mask, FaultDescription::instructionAccessViolation},
      }},
      .ufsr         = ScbR::CFSR::ufsr.mask,
      .bfsr         = ScbR::CFSR::bfsr.mask,
      .mmfsr        = ScbR::CFSR::mmfsr.mask,
      .bfarValid    = ScbR::BFSR::bfarvalid.mask,
      .mmarValid    = ScbR::MMFSR::mmarvalid.mask,
      .hfsrForced   = ScbR::HFSR::forced.mask,
      .hfsrVecttbl  = ScbR::HFSR::vecttbl.mask,
      .hfsrDebugevt = ScbR::HFSR::debugevt.mask};
    std::array<FaultRow, 6> const common{
      {
       {ScbR::UFSR::divbyzero.mask, FaultDescription::divisionByZero},
       {ScbR::UFSR::unaligned.mask, FaultDescription::unalignedAccess},
       {ScbR::UFSR::nocp.mask, FaultDescription::noCoprocessor},
       {ScbR::UFSR::invpc.mask, FaultDescription::invalidPCLoad},
       {ScbR::UFSR::invstate.mask, FaultDescription::invalidState},
       {ScbR::UFSR::undefinstr.mask, FaultDescription::undefinedInstruction},
       }
    };
    for(std::size_t i = 0; i != Lead; ++i) { t.usage[i] = lead[i]; }
    for(std::size_t i = 0; i != common.size(); ++i) { t.usage[Lead + i] = common[i]; }
    return t;
}

// The words a core without a Security Extension reads as 0 (Snapshot::secureRegs false).
struct NoSecureRegs {
    static constexpr bool present = false;

    static std::uint32_t sfsr() { return 0; }

    static std::uint32_t sfar() { return 0; }
};

// The fault registers for the SDK's full fault record (Kvasir::CrashRecord::Full, FaultHandler.hpp) and the
// COREFAULT line: register reads only, in the order of the Snapshot's words (CFSR, HFSR, MMFAR, BFAR, SHCSR, ICSR,
// then `Secure`'s SFSR, SFAR), no analysis, no log, nothing that can fault.
template<class ScbR,
         class Secure = NoSecureRegs>
inline Snapshot snapshotOf() {
    using Kvasir::Register::readWhole;
    return Snapshot{.cfsr       = readWhole(ScbR::CFSR::FULLREGISTER),
                    .hfsr       = readWhole(ScbR::HFSR::FULLREGISTER),
                    .mmfar      = readWhole(ScbR::MMFAR::FULLREGISTER),
                    .bfar       = readWhole(ScbR::BFAR::FULLREGISTER),
                    .shcsr      = readWhole(ScbR::SHCSR::FULLREGISTER),
                    .icsr       = readWhole(ScbR::ICSR::FULLREGISTER),
                    .sfsr       = Secure::sfsr(),
                    .sfar       = Secure::sfar(),
                    .faultRegs  = true,
                    .secureRegs = Secure::present};
}

// CFSR and HFSR are write-one-to-clear (each core's Fault.hpp cites its ARM ARM): writing back what was read clears
// this fault's bits, MMFARVALID/BFARVALID with them, and keeps a bit a nested fault set meanwhile. Whole-register
// writes: a field write would read the register again first. For a fault handler that returns, and a start without a
// core reset, where the next fault would otherwise find the old bits OR'ed into its own.
template<class ScbR>
inline void clearFaultStatus(std::uint32_t cfsr,
                             std::uint32_t hfsr) {
    Kvasir::Register::apply(Kvasir::Register::write(ScbR::CFSR::FULLREGISTER, cfsr));
    Kvasir::Register::apply(Kvasir::Register::write(ScbR::HFSR::FULLREGISTER, hfsr));
}

// Reads CFSR, then HFSR (two statements: as call arguments gcc read HFSR first), then writes them back.
template<class ScbR>
inline void clearFaultStatus() {
    std::uint32_t const cfsr = Kvasir::Register::readWhole(ScbR::CFSR::FULLREGISTER);
    std::uint32_t const hfsr = Kvasir::Register::readWhole(ScbR::HFSR::FULLREGISTER);
    clearFaultStatus<ScbR>(cfsr, hfsr);
}
}   // namespace Kvasir::Fault::Core::V7m
