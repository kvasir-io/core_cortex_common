// FaultV7m.hpp over one v7-M/v8-M core SVD's generated SCB, on the host register mock: the decode table against the
// ARM ARM bit positions (Armv7-M ARM DDI0403E B3.2.15/16, Armv8-M ARM DDI0553B.y D1.2.270 UFSR, D1.2.7 BFSR, D1.2.167
// MMFSR, D1.2.123 HFSR - the same positions in both), a core's own lead rows first, the snapshot's register reads in
// the Snapshot's order (SCB CFSR 0xE000ED28, HFSR ED2C, MMFAR ED34, BFAR ED38, SHCSR ED24, ICSR ED04; DDI0403E
// B3.2.2, DDI0553B.y SCB table) and the CFSR/HFSR clear as two whole-word writes, CFSR first.
#include "core_peripherals/SCB.hpp"
#include "cortex_common/FaultV7m.hpp"
#include "kvasir_test.hpp"

#include <array>
#include <cstdint>

namespace {
using Kvasir::Fault::Core::FaultDescription;
using Kvasir::Fault::Core::FaultRow;
using Kvasir::Test::recorder;
using R       = Kvasir::Test::Recorder::Read;
using W       = Kvasir::Test::Recorder::Write;
using ScbR    = Kvasir::Peripheral::SCB::Registers<>;
namespace V7m = Kvasir::Fault::Core::V7m;

constexpr bool sameRows(auto const& a,
                        auto const& b) {
    if(a.size() != b.size()) { return false; }
    for(std::size_t i = 0; i != a.size(); ++i) {
        if(a[i].mask != b[i].mask || a[i].what != b[i].what) { return false; }
    }
    return true;
}

constexpr std::array<FaultRow, 6> usage{
  {
   {1U << (16 + 9), FaultDescription::divisionByZero},
   {1U << (16 + 8), FaultDescription::unalignedAccess},
   {1U << (16 + 3), FaultDescription::noCoprocessor},
   {1U << (16 + 2), FaultDescription::invalidPCLoad},
   {1U << (16 + 1), FaultDescription::invalidState},
   {1U << 16, FaultDescription::undefinedInstruction},
   }
};
constexpr std::array<FaultRow, 6> bus{
  {
   {1U << (8 + 5), FaultDescription::lazyStatePreservationError},
   {1U << (8 + 4), FaultDescription::exceptionStackingError},
   {1U << (8 + 3), FaultDescription::exceptionUnstackingError},
   {1U << (8 + 2), FaultDescription::impreciseDataAccessError},
   {1U << (8 + 1), FaultDescription::preciseDataAccessError},
   {1U << 8, FaultDescription::instructionBusError},
   }
};
constexpr std::array<FaultRow, 5> memManage{
  {
   {1U << 5, FaultDescription::lazyStatePreservationError},
   {1U << 4, FaultDescription::exceptionStackingError},
   {1U << 3, FaultDescription::exceptionUnstackingError},
   {1U << 1, FaultDescription::dataAccessViolation},
   {1U << 0, FaultDescription::instructionAccessViolation},
   }
};

constexpr auto table = V7m::faultTable<ScbR>();
static_assert(sameRows(table.usage,
                       usage)
              && sameRows(table.bus,
                          bus)
              && sameRows(table.memManage,
                          memManage));
static_assert(table.ufsr == 0xFFFF'0000U && table.bfsr == 0x0000'FF00U
              && table.mmfsr == 0x0000'00FFU);
static_assert(table.bfarValid == 1U << 15 && table.mmarValid == 1U << 7);
static_assert(table.hfsrForced == 1U << 30 && table.hfsrVecttbl == 1U << 1
              && table.hfsrDebugevt == 1U << 31);

// A core's own rows come first, in the order given, then the six shared ones.
constexpr std::array<FaultRow, 2> lead{
  {
   {1U << (16 + 4), FaultDescription::stackOverflow},
   {1U << (16 + 5), FaultDescription::unknown},
   }
};
constexpr auto withLead = V7m::faultTable<ScbR>(lead);
static_assert(withLead.usage.size() == 8
              && sameRows(std::array{withLead.usage[0],
                                     withLead.usage[1]},
                          lead));
static_assert(withLead.usage[2].mask == usage[0].mask && withLead.usage[7].mask == usage[5].mask);
static_assert(sameRows(withLead.bus,
                       bus)
              && sameRows(withLead.memManage,
                          memManage));

struct FakeSecure {
    static constexpr bool present = true;

    static std::uint32_t sfsr() { return 0x51U; }

    static std::uint32_t sfar() { return 0x52U; }
};

void snapshotReadsInOrder() {
    Kvasir::Test::test("snapshotReadsInOrder");
    recorder.setReadValue(0xE000'ED28U, 0x0002'0000U);
    recorder.setReadValue(0xE000'ED2CU, 0x4000'0000U);
    recorder.setReadValue(0xE000'ED34U, 0x1111U);
    recorder.setReadValue(0xE000'ED38U, 0x2222U);
    recorder.setReadValue(0xE000'ED24U, 0x7'0000U);
    recorder.setReadValue(0xE000'ED04U, 0x803U);
    auto const s = V7m::snapshotOf<ScbR>();
    Kvasir::Test::checkActions({
      R{0xE000'ED28U, 0x0002'0000U},
      R{0xE000'ED2CU, 0x4000'0000U},
      R{0xE000'ED34U,      0x1111U},
      R{0xE000'ED38U,      0x2222U},
      R{0xE000'ED24U,    0x7'0000U},
      R{0xE000'ED04U,       0x803U}
    });
    CHECK_EQ(s.cfsr, 0x0002'0000U);
    CHECK_EQ(s.hfsr, 0x4000'0000U);
    CHECK_EQ(s.mmfar, 0x1111U);
    CHECK_EQ(s.bfar, 0x2222U);
    CHECK_EQ(s.shcsr, 0x7'0000U);
    CHECK_EQ(s.icsr, 0x803U);
    CHECK_EQ(s.sfsr, 0U);
    CHECK_EQ(s.sfar, 0U);
    CHECK(s.faultRegs);
    CHECK(!s.secureRegs);
}

void snapshotTakesTheSecureWords() {
    Kvasir::Test::test("snapshotTakesTheSecureWords");
    auto const s = V7m::snapshotOf<ScbR, FakeSecure>();
    CHECK_EQ(recorder.actions.size(), 6U);
    CHECK_EQ(s.sfsr, 0x51U);
    CHECK_EQ(s.sfar, 0x52U);
    CHECK(s.secureRegs);
}

void clearWritesBackCfsrThenHfsr() {
    Kvasir::Test::test("clearWritesBackCfsrThenHfsr");
    V7m::clearFaultStatus<ScbR>(0x0102'0304U, 0x4000'0002U);
    Kvasir::Test::checkActions({
      W{0xE000'ED28U, 0x0102'0304U},
      W{0xE000'ED2CU, 0x4000'0002U}
    });
}

void clearReadsFirst() {
    Kvasir::Test::test("clearReadsFirst");
    recorder.setReadValue(0xE000'ED28U, 0x82U);
    recorder.setReadValue(0xE000'ED2CU, 0x4000'0000U);
    V7m::clearFaultStatus<ScbR>();
    Kvasir::Test::checkActions({
      R{0xE000'ED28U,        0x82U},
      R{0xE000'ED2CU, 0x4000'0000U},
      W{0xE000'ED28U,        0x82U},
      W{0xE000'ED2CU, 0x4000'0000U}
    });
}
}   // namespace

int main() {
    snapshotReadsInOrder();
    snapshotTakesTheSecureWords();
    clearWritesBackCfsrThenHfsr();
    clearReadsFirst();
    return Kvasir::Test::report();
}
