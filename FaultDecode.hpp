#pragma once
// The Armv7-M / Armv8-M mainline fault decode behind the COREFAULT line: a pure function of a Snapshot and the
// core's decode table - no register access, so it runs on the host (tests/fault_decode_test.cpp) and in a
// static_assert. Each core's core/Fault.hpp builds its Table from its SVD's field masks (UFSR/BFSR/MMFSR fields in
// CFSR coordinates, HFSR fields); this header holds no bit number. Armv6-M has no fault status registers and keeps
// its own line (core_cortex_m0plus core/Fault.hpp).
//
// The rules:
//  - HFSR.VECTTBL wins: VectorTable, status 1, `forced` false.
//  - otherwise `forced` = HFSR.FORCED, and the first non-zero sub-register of CFSR in the order UFSR, BFSR, MMFSR
//    names the type; `status_bits` is that sub-register shifted down (the COREFAULT `flags`); the first row of its
//    table whose bit is set names the description (Unknown when none is); BFSR/MMFSR give BFAR/MMFAR as the
//    address when their VALID bit is set.
//  - CFSR 0 and FORCED: UnknownEscalation, status 0. Neither: Unknown, status HFSR.DEBUGEVT.
//
// kvasir_bench.py (Kvasir_SDK/tools) has its own host-side CFSR bit table for `crash`; a standalone tool keeps its copy.
#include "FaultSnapshot.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace Kvasir::Fault::Core {
// The enumerator names are what the COREFAULT line prints (`type({}Fault)`, `info({})`).
enum class FaultType : std::uint8_t { hard, usage, bus, memManage };

enum class FaultDescription : std::uint8_t {
    vectorTable,
    unknownEscalation,
    unknown,
    divisionByZero,
    unalignedAccess,
    noCoprocessor,
    invalidPCLoad,
    invalidState,
    undefinedInstruction,
    lazyStatePreservationError,
    exceptionStackingError,
    exceptionUnstackingError,
    impreciseDataAccessError,
    preciseDataAccessError,
    instructionBusError,
    dataAccessViolation,
    instructionAccessViolation,
    stackOverflow
};

struct FaultInfo {
    FaultType                    type;
    FaultDescription             description;
    std::optional<std::uint32_t> faultAddress;
    std::uint32_t                statusBits;
    bool                         forced;

    constexpr bool operator==(FaultInfo const&) const = default;
};

// One status bit: its mask in the snapshot word (CFSR or HFSR) and what it means.
struct FaultRow {
    std::uint32_t    mask;
    FaultDescription what;
};

// A core's decode table: the rows of each CFSR sub-register in priority order (first set bit wins), the
// sub-registers' masks in CFSR, the two VALID bits (CFSR) and the three HFSR bits.
template<std::size_t Usage, std::size_t Bus, std::size_t MemManage>
struct FaultTable {
    std::array<FaultRow, Usage>     usage;
    std::array<FaultRow, Bus>       bus;
    std::array<FaultRow, MemManage> memManage;
    std::uint32_t                   ufsr, bfsr, mmfsr;
    std::uint32_t                   bfarValid, mmarValid;
    std::uint32_t                   hfsrForced, hfsrVecttbl, hfsrDebugevt;
};

namespace Detail {
    constexpr std::uint32_t subRegister(std::uint32_t cfsr,
                                        std::uint32_t mask) {
        return (cfsr & mask) >> std::countr_zero(mask);
    }

    template<std::size_t N>
    constexpr FaultDescription firstRow(std::array<FaultRow,
                                                   N> const& rows,
                                        std::uint32_t        word) {
        for(auto const& r : rows) {
            if((word & r.mask) != 0) { return r.what; }
        }
        return FaultDescription::unknown;
    }
}   // namespace Detail

template<std::size_t U,
         std::size_t B,
         std::size_t M>
constexpr FaultInfo decode(Snapshot const&      s,
                           FaultTable<U,
                                      B,
                                      M> const& t) {
    FaultInfo info{FaultType::hard, FaultDescription::unknown, std::nullopt, 0, false};
    if((s.hfsr & t.hfsrVecttbl) != 0) {
        info.description = FaultDescription::vectorTable;
        info.statusBits  = 1;
        return info;
    }
    info.forced = (s.hfsr & t.hfsrForced) != 0;

    if(auto const ufsr = Detail::subRegister(s.cfsr, t.ufsr); ufsr != 0) {
        info.type        = FaultType::usage;
        info.statusBits  = ufsr;
        info.description = Detail::firstRow(t.usage, s.cfsr);
        return info;
    }
    if(auto const bfsr = Detail::subRegister(s.cfsr, t.bfsr); bfsr != 0) {
        info.type       = FaultType::bus;
        info.statusBits = bfsr;
        if((s.cfsr & t.bfarValid) != 0) { info.faultAddress = s.bfar; }
        info.description = Detail::firstRow(t.bus, s.cfsr);
        return info;
    }
    if(auto const mmfsr = Detail::subRegister(s.cfsr, t.mmfsr); mmfsr != 0) {
        info.type       = FaultType::memManage;
        info.statusBits = mmfsr;
        if((s.cfsr & t.mmarValid) != 0) { info.faultAddress = s.mmfar; }
        info.description = Detail::firstRow(t.memManage, s.cfsr);
        return info;
    }
    if(info.forced) {
        info.description = FaultDescription::unknownEscalation;
        return info;
    }
    info.statusBits = (s.hfsr & t.hfsrDebugevt) != 0 ? 1U : 0U;
    return info;
}
}   // namespace Kvasir::Fault::Core
