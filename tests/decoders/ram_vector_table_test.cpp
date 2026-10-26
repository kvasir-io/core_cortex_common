// Kvasir_SDK StartUp/RamVectorTable.hpp over this core SVD's generated SCB: the table's size rule against a chip's
// vector count (16 core exceptions + the chip's IRQs), and VTOR at its architectural address (Armv6-M ARM DDI0419E
// B3.2.2 Table B3-4; Armv8-M ARM DDI0553B.y D1.2.272: 0xE000ED08). Compile time only.
#include "kvasir/StartUp/RamVectorTable.hpp"

namespace {
struct Chip52 {   // 16 core exceptions + 52 IRQs = 68 vectors
    static constexpr int end = 52;
};

struct Chip48 {
    static constexpr int end = 48;
};

static_assert(sizeof(Kvasir::Startup::RamVectorTable<128,
                                                     Chip52>)
              > 0);
static_assert(sizeof(Kvasir::Startup::RamVectorTable<64,
                                                     Chip48>)
                > 0,
              "64 vectors exactly fit");
static_assert(Kvasir::Startup::RamVectorTable<64,
                                              Chip48>::Vtor::Addr::value
              == 0xE000'ED08U);
}   // namespace

int main() { return 0; }
