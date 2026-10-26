// Host test of FaultDecode.hpp: every row, the priorities and the corner cases, for an Armv8-M and an Armv7-M table.
//
// The reference tables use the ARM ARM bit positions, in CFSR coordinates (MMFSR = CFSR[7:0], BFSR = CFSR[15:8],
// UFSR = CFSR[31:16]); the cores build theirs from their SVD field masks and static_assert them against the same
// positions (core/Fault.hpp, KVASIR_SELFTEST).
//   Armv8-M, DDI0553B.y: UFSR D1.2.270 (DIVBYZERO 9, UNALIGNED 8, STKOF 4, NOCP 3, INVPC 2, INVSTATE 1, UNDEFINSTR 0),
//     BFSR D1.2.7 (BFARVALID 7, LSPERR 5, STKERR 4, UNSTKERR 3, IMPRECISERR 2, PRECISERR 1, IBUSERR 0),
//     MMFSR D1.2.167 (MMFARVALID 7 - Armv7-M's MMARVALID, the SVDs' name -, MLSPERR 5, MSTKERR 4, MUNSTKERR 3,
//     DACCVIOL 1, IACCVIOL 0),
//     HFSR D1.2.123 (DEBUGEVT 31, FORCED 30, VECTTBL 1).
//   Armv7-M, DDI0403E.e: CFSR B3.2.15 (the same positions without STKOF), HFSR B3.2.16 (the same three bits).
#include "FaultDecode.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {
using namespace Kvasir::Fault::Core;
using D = FaultDescription;

constexpr std::uint32_t mm(unsigned bit) { return 1U << bit; }   // MMFSR bit in CFSR

constexpr std::uint32_t bf(unsigned bit) { return 1U << (8 + bit); }   // BFSR bit in CFSR

constexpr std::uint32_t uf(unsigned bit) { return 1U << (16 + bit); }   // UFSR bit in CFSR

constexpr std::uint32_t debugevt = 1U << 31;
constexpr std::uint32_t forced   = 1U << 30;
constexpr std::uint32_t vecttbl  = 1U << 1;

constexpr std::array<FaultRow, 6> bus{
  {{bf(5), D::lazyStatePreservationError},
   {bf(4), D::exceptionStackingError},
   {bf(3), D::exceptionUnstackingError},
   {bf(2), D::impreciseDataAccessError},
   {bf(1), D::preciseDataAccessError},
   {bf(0), D::instructionBusError}}
};
constexpr std::array<FaultRow, 5> memManage{
  {{mm(5), D::lazyStatePreservationError},
   {mm(4), D::exceptionStackingError},
   {mm(3), D::exceptionUnstackingError},
   {mm(1), D::dataAccessViolation},
   {mm(0), D::instructionAccessViolation}}
};

constexpr FaultTable<7, 6, 5> v8{.usage        = {{{uf(4), D::stackOverflow},
                                                   {uf(9), D::divisionByZero},
                                                   {uf(8), D::unalignedAccess},
                                                   {uf(3), D::noCoprocessor},
                                                   {uf(2), D::invalidPCLoad},
                                                   {uf(1), D::invalidState},
                                                   {uf(0), D::undefinedInstruction}}},
                                 .bus          = bus,
                                 .memManage    = memManage,
                                 .ufsr         = 0xFFFF'0000U,
                                 .bfsr         = 0x0000'FF00U,
                                 .mmfsr        = 0x0000'00FFU,
                                 .bfarValid    = bf(7),
                                 .mmarValid    = mm(7),
                                 .hfsrForced   = forced,
                                 .hfsrVecttbl  = vecttbl,
                                 .hfsrDebugevt = debugevt};

constexpr FaultTable<6, 6, 5> v7{.usage        = {{{uf(9), D::divisionByZero},
                                                   {uf(8), D::unalignedAccess},
                                                   {uf(3), D::noCoprocessor},
                                                   {uf(2), D::invalidPCLoad},
                                                   {uf(1), D::invalidState},
                                                   {uf(0), D::undefinedInstruction}}},
                                 .bus          = bus,
                                 .memManage    = memManage,
                                 .ufsr         = 0xFFFF'0000U,
                                 .bfsr         = 0x0000'FF00U,
                                 .mmfsr        = 0x0000'00FFU,
                                 .bfarValid    = bf(7),
                                 .mmarValid    = mm(7),
                                 .hfsrForced   = forced,
                                 .hfsrVecttbl  = vecttbl,
                                 .hfsrDebugevt = debugevt};

constexpr Snapshot snap(std::uint32_t cfsr,
                        std::uint32_t hfsr  = 0,
                        std::uint32_t mmfar = 0,
                        std::uint32_t bfar  = 0) {
    Snapshot s{};
    s.cfsr  = cfsr;
    s.hfsr  = hfsr;
    s.mmfar = mmfar;
    s.bfar  = bfar;
    return s;
}

// decode stays constexpr
static_assert(decode(snap(uf(9)),
                     v8)
                .description
              == D::divisionByZero);
static_assert(sizeof(Snapshot)
              == 8 * 4 + 4);   // the layout the full record copies (8 words, 2 flags, padding)

int failures = 0;

void check(bool          ok,
           char const*   what,
           std::uint32_t a = 0,
           std::uint32_t b = 0) {
    if(!ok) {
        ++failures;
        std::printf("FAIL: %s (%#010x, %#010x)\n", what, a, b);
    }
}

constexpr std::uint32_t bfarValue  = 0xF000'0000U;
constexpr std::uint32_t mmfarValue = 0x2000'1234U;

FaultInfo run(auto const&   t,
              std::uint32_t cfsr,
              std::uint32_t hfsr = 0) {
    return decode(snap(cfsr, hfsr, mmfarValue, bfarValue), t);
}

void rows(auto const& t) {
    for(auto const r : t.usage) {
        auto const i = run(t, r.mask);
        check(i.type == FaultType::usage && i.description == r.what
                && i.statusBits == (r.mask >> 16) && !i.faultAddress && !i.forced,
              "usage row alone",
              r.mask);
    }
    for(auto const r : t.bus) {
        auto const i = run(t, r.mask);
        check(i.type == FaultType::bus && i.description == r.what && i.statusBits == (r.mask >> 8)
                && !i.faultAddress,
              "bus row alone",
              r.mask);
        auto const v = run(t, r.mask | bf(7));
        check(v.description == r.what && v.faultAddress == bfarValue,
              "bus row with BFARVALID",
              r.mask);
    }
    for(auto const r : t.memManage) {
        auto const i = run(t, r.mask);
        check(i.type == FaultType::memManage && i.description == r.what && i.statusBits == r.mask
                && !i.faultAddress,
              "memmanage row alone",
              r.mask);
        auto const v = run(t, r.mask | mm(7));
        check(v.description == r.what && v.faultAddress == mmfarValue,
              "memmanage row with MMARVALID",
              r.mask);
    }
}

void corners() {
    // VECTTBL over everything, forced stays false
    auto const vt = run(v8, uf(9) | bf(1) | mm(0), forced | vecttbl);
    check(vt.type == FaultType::hard && vt.description == D::vectorTable && vt.statusBits == 1
            && !vt.forced,
          "VECTTBL wins");
    // usage over bus over memmanage
    check(run(v8, uf(0) | bf(0) | mm(0)).type == FaultType::usage, "usage first");
    check(run(v8, bf(0) | mm(0)).type == FaultType::bus, "bus before memmanage");
    // STKOF over DIVBYZERO (v8); on v7 bit 20 is no row
    check(run(v8, uf(4) | uf(9)).description == D::stackOverflow, "STKOF first");
    check(run(v7, uf(4) | uf(9)).description == D::divisionByZero, "v7 has no STKOF row");
    auto const v7stk = run(v7, uf(4));
    check(v7stk.type == FaultType::usage && v7stk.description == D::unknown
            && v7stk.statusBits == 0x10,
          "v7 bit 20 alone: Usage, Unknown");
    // only the VALID bit: that type, Unknown, with the address
    auto const bv = run(v8, bf(7));
    check(bv.type == FaultType::bus && bv.description == D::unknown && bv.faultAddress == bfarValue
            && bv.statusBits == 0x80,
          "BFARVALID alone");
    auto const mv = run(v8, mm(7));
    check(mv.type == FaultType::memManage && mv.description == D::unknown
            && mv.faultAddress == mmfarValue,
          "MMARVALID alone");
    // FORCED with CFSR 0
    auto const fe = run(v8, 0, forced);
    check(fe.type == FaultType::hard && fe.description == D::unknownEscalation && fe.statusBits == 0
            && fe.forced,
          "FORCED alone");
    // forced is kept next to a CFSR fault
    check(run(v8, uf(9), forced).forced, "FORCED with a usage fault");
    // DEBUGEVT alone, and nothing
    auto const de = run(v8, 0, debugevt);
    check(de.description == D::unknown && de.statusBits == 1 && !de.forced, "DEBUGEVT alone");
    auto const none = run(v8, 0, 0);
    check(none.type == FaultType::hard && none.description == D::unknown && none.statusBits == 0,
          "nothing");
}

}   // namespace

int main() {
    rows(v8);
    rows(v7);
    corners();
    if(failures != 0) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("fault_decode_test: ok\n");
    return 0;
}
