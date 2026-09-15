#pragma once

#include <cstdint>
#include <optional>

namespace Kvasir::Systick::Detail {

// Ticks since start from one reading: the overruns before and after reading the counter (CVR,
// counting down from `reload`) and ICSR.PENDSTSET just after it. Differing overruns mean retry.
// Pending with the counter in its upper half is a wrap the handler has not counted yet; in its
// lower half the wrap came after the read. Holds while the handler runs within half a reload.
[[nodiscard]] constexpr std::optional<std::uint64_t> pairReading(std::uint64_t before,
                                                                 std::uint32_t count,
                                                                 bool          pending,
                                                                 std::uint64_t after,
                                                                 std::uint32_t reload) {
    if(before != after) { return std::nullopt; }
    auto const wraps = after + ((pending && count > reload / 2U) ? 1U : 0U);
    return std::uint64_t{reload - count} + wraps * (std::uint64_t{reload} + 1U);
}

}   // namespace Kvasir::Systick::Detail
