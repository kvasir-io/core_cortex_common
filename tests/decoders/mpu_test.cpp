// core_cortex_m33's core/Mpu.hpp on the host register mock, against the MPU block of its core.svd (the m33 configuration
// only, CMakeLists.txt): the generated layout at the architecture's addresses (Armv8-M ARM DDI0553B.y D1.2.168-176:
// MPU_TYPE 0xE000ED90, MPU_CTRL 0xE000ED94, MPU_RNR 0xE000ED98, MPU_RBAR 0xE000ED9C, MPU_RLAR 0xE000EDA0, MPU_MAIR0
// 0xE000EDC0), a region's two words, and what program() writes: one store each to RBAR and RLAR of every region, the
// region number before them, the rest of the implemented regions disabled (RLAR.EN 0).
#include "core/Mpu.hpp"

#include "kvasir_test.hpp"

#include <array>
#include <cstdint>

namespace {
using Kvasir::Test::checkActions;
using Kvasir::Test::recorder;
using Kvasir::Test::test;
using R       = Kvasir::Test::Recorder::Read;
using W       = Kvasir::Test::Recorder::Write;
using Regs    = Kvasir::Core::Mpu::Regs;
namespace Mpu = Kvasir::Core::Mpu;

static_assert(Regs::TYPE::Addr::value == 0xE000'ED90U);
static_assert(Regs::CTRL::Addr::value == 0xE000'ED94U);
static_assert(Regs::RNR::Addr::value == 0xE000'ED98U);
static_assert(Regs::RBAR::Addr::value == 0xE000'ED9CU);
static_assert(Regs::RLAR::Addr::value == 0xE000'EDA0U);
static_assert(Regs::RBAR_A3::Addr::value == 0xE000'EDB4U);
static_assert(Regs::RLAR_A3::Addr::value == 0xE000'EDB8U);
static_assert(Regs::MAIR0::Addr::value == 0xE000'EDC0U);
static_assert(Regs::MAIR1::Addr::value == 0xE000'EDC4U);

// SRAM code of a RAM image: read-only, executable, the last block 0x2002'FFE0; attribute 0
static_assert(Mpu::encode({.begin    = 0x2000'1000U,
                           .end      = 0x2003'0000U,
                           .readOnly = true,
                           .execute  = true})
                .rbar
              == 0x2000'1004U);
static_assert(Mpu::encode({.begin    = 0x2000'1000U,
                           .end      = 0x2003'0000U,
                           .readOnly = true,
                           .execute  = true})
                .rlar
              == 0x2002'FFE1U);
// writable, never executed, attribute 2: AttrIndx bits 3:1
static_assert(Mpu::encode({.begin    = 0x2003'0000U,
                           .end      = 0x2008'2000U,
                           .readOnly = false,
                           .execute  = false,
                           .attr     = 2})
                .rbar
              == 0x2003'0001U);
static_assert(Mpu::encode({.begin    = 0x2003'0000U,
                           .end      = 0x2008'2000U,
                           .readOnly = false,
                           .execute  = false,
                           .attr     = 2})
                .rlar
              == 0x2008'1FE5U);
static_assert(!Mpu::valid({.begin    = 0x2000'1004U,
                           .end      = 0x2000'2000U,
                           .readOnly = true,
                           .execute  = true}));
static_assert(!Mpu::valid({.begin    = 0x2000'2000U,
                           .end      = 0x2000'1000U,
                           .readOnly = true,
                           .execute  = true}));
static_assert(!Mpu::valid({.begin    = 0,
                           .end      = 0x20U,
                           .readOnly = true,
                           .execute  = true,
                           .attr     = 8}));
static_assert(Mpu::valid({.begin    = 0x2000'2000U,
                          .end      = 0x2000'2000U,
                          .readOnly = true,
                          .execute  = true}));

void programs() {
    test("program");
    recorder.reset();
    constexpr std::array<Mpu::Region, 4> regions{
      {{.begin = 0x1000'0000U, .end = 0x1000'4000U, .readOnly = true, .execute = true},
       {.begin    = 0x2000'0000U,
        .end      = 0x2000'0000U,
        .readOnly = false,
        .execute  = false},   // empty: skipped
       {.begin    = 0x2000'0010U,
        .end      = 0x2000'0100U,
        .readOnly = false,
        .execute  = false},   // misaligned: refused
       {.begin = 0x2000'0000U, .end = 0x2008'0000U, .readOnly = false, .execute = false}}
    };
    auto const done = Mpu::program<4>(regions);
    if(done.regions != 2 || done.invalid != 1) {
        Kvasir::Test::failf(std::source_location::current(),
                            "regions {} invalid {}",
                            done.regions,
                            done.invalid);
    }
    constexpr unsigned rnr  = Regs::RNR::Addr::value;
    constexpr unsigned rbar = Regs::RBAR::Addr::value;
    constexpr unsigned rlar = Regs::RLAR::Addr::value;
    checkActions({
      W{ rnr,            0},
      W{rbar, 0x1000'0004U},
      W{rlar, 0x1000'3FE1U}, // region 0
      W{ rnr,            1},
      W{rbar, 0x2000'0001U},
      W{rlar, 0x2007'FFE1U}, // region 1
      W{ rnr,            2},
      R{rlar,            0},
      W{rlar,            0}, // 2 and 3: EN 0
      W{ rnr,            3},
      R{rlar,            0},
      W{rlar,            0},
    });
}

void attributes() {
    test("setAttributes");
    recorder.reset();
    Mpu::setAttributes(Mpu::normalWriteBack);
    checkActions({
      W{Regs::MAIR0::Addr::value, 0x0000'00FFU}
    });   // every field: one store, no read
}
}   // namespace

int main() {
    programs();
    attributes();
    return Kvasir::Test::report();
}
