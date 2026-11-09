#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
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

// Ticks from `countStart` to `countNow` on the counter alone (counting down from `reload` to 0, then `reload` again):
// right for an interval shorter than one reload (reload + 1 ticks); a longer one reads short by whole reloads.
[[nodiscard,
  gnu::always_inline]] constexpr std::uint32_t
countsBetween(std::uint32_t countStart,
              std::uint32_t countNow,
              std::uint32_t reload) {
    return countStart >= countNow ? countStart - countNow : countStart + (reload + 1U - countNow);
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

// SysTick wraps within `overrunTime` (rounded up): what the overrun counter must count without wrapping itself.
// `ticksPerWrap` is RELOAD + 1, `clockSpeed` in Hz; integer nanoseconds per wrap, as the clock has always computed it.
[[nodiscard]] constexpr std::uint64_t calcOverRunValue(std::uint64_t            clockSpeed,
                                                       std::uint64_t            ticksPerWrap,
                                                       std::chrono::nanoseconds overrunTime) {
    std::uint64_t const nanoSecPerOverrun = (ticksPerWrap * 1'000'000'000ULL) / clockSpeed;
    return (static_cast<std::uint64_t>(overrunTime.count()) + nanoSecPerOverrun - 1ULL)
         / nanoSecPerOverrun;
}

// The overrun counter's word: 32 bits while `Wraps` (calcOverRunValue of the config) fits - one plain atomic per
// read - else 64 (OverrunCounter<std::uint64_t>'s two halves). A 32-bit counter of a 24-bit SysTick wraps after
// 2^32 * 2^24 ticks: ~15.2 years at 150 MHz, ~18.3 at 125 MHz; `minOverrunTime = years(15)` stays 32-bit up to
// ~152 MHz.
template<std::uint64_t Wraps>
struct OverrunWord {
    using type = std::uint64_t;
};

template<std::uint64_t Wraps>
    requires(Wraps <= std::numeric_limits<std::uint32_t>::max())
struct OverrunWord<Wraps> {
    using type = std::uint32_t;
};

template<std::uint64_t Wraps>
using OverrunWordT = typename OverrunWord<Wraps>::type;

// One turn of a SysTick busy wait of `ticksToWait` (< one wrap) started at `countStart`: true once the counter (now
// `countNow`, counting down from `reload`) has gone `ticksToWait` counts, or the overrun counter says it must have -
// two wraps, or one wrap with the counter already below its start. Across a reload the counter passed 0 and `reload`:
// reload + 1 counts.
template<typename Overruns>
[[nodiscard,
  gnu::always_inline]] constexpr bool
delayReached(std::uint32_t countStart,
             std::uint32_t countNow,
             Overruns      overrunsElapsed,
             std::uint32_t ticksToWait,
             std::uint32_t reload) {
    auto const          countsRaw     = static_cast<std::int32_t>(countStart - countNow);
    std::uint32_t const countsElapsed = countsRaw >= 0 ? static_cast<std::uint32_t>(countsRaw)
                                                       : countStart + (reload + 1U - countNow);
    return countsElapsed >= ticksToWait || overrunsElapsed >= 2
        || (countNow < countStart && overrunsElapsed == 1);
}

// A wait of `ticks` in waits of at most `chunk` ticks each (`Waiter::wait(n)`, n < one wrap), the rest last.
template<typename Waiter>
[[gnu::always_inline]] inline void waitInChunks(std::uint64_t ticks,
                                                std::uint32_t chunk) {
    while(ticks >= chunk) {
        Waiter::wait(chunk);
        ticks -= chunk;
    }
    Waiter::wait(static_cast<std::uint32_t>(ticks));
}

}   // namespace Kvasir::Systick::Detail
