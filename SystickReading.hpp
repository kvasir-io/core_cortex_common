#pragma once

#include <atomic>
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

// A 64-bit count from its two halves, read high, low, high: a high half that moved in between
// means the low half may belong to either, so retry.
[[nodiscard]] constexpr std::optional<std::uint64_t> joinHalves(std::uint32_t hiBefore,
                                                                std::uint32_t lo,
                                                                std::uint32_t hiAfter) {
    if(hiBefore != hiAfter) { return std::nullopt; }
    return (std::uint64_t{hiAfter} << 32U) | lo;
}

// The wraps SysTick's handler counts. It is the only writer and runs on the reader's core, so a
// reader sees each increment whole or not at all. A 32-bit count is one plain atomic. A 64-bit
// one is two 32-bit halves: ARMv8-M has no 64-bit atomic load, and std::atomic<uint64_t> would go
// through __atomic_load_8 and mask interrupts on every read - twice per now().
template<typename T>
class OverrunCounter {
    std::atomic<T> n_{};

public:
    void increment() {
        n_.store(n_.load(std::memory_order_relaxed) + 1U, std::memory_order_relaxed);
    }

    [[nodiscard]] T load() const { return n_.load(std::memory_order_relaxed); }
};

template<>
class OverrunCounter<std::uint64_t> {
    std::atomic<std::uint32_t> lo_{};
    std::atomic<std::uint32_t> hi_{};

public:
    void increment() {
        std::uint32_t const old = lo_.load(std::memory_order_relaxed);
        if(old == UINT32_MAX) {
            hi_.store(hi_.load(std::memory_order_relaxed) + 1U, std::memory_order_relaxed);
            lo_.store(0U, std::memory_order_relaxed);
        } else {
            lo_.store(old + 1U, std::memory_order_relaxed);
        }
    }

    // Tests only: start near a wrap of the low half.
    void preset(std::uint64_t value) {
        lo_.store(static_cast<std::uint32_t>(value), std::memory_order_relaxed);
        hi_.store(static_cast<std::uint32_t>(value >> 32U), std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint64_t load() const {
        while(true) {
            // Compiler fences only: the writer is a handler on this core, not another core.
            std::uint32_t const hiBefore = hi_.load(std::memory_order_relaxed);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            std::uint32_t const lo = lo_.load(std::memory_order_relaxed);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            std::uint32_t const hiAfter = hi_.load(std::memory_order_relaxed);
            if(auto const value = joinHalves(hiBefore, lo, hiAfter)) { return *value; }
        }
    }
};

}   // namespace Kvasir::Systick::Detail
