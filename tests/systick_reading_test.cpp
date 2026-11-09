// Host test for pairReading against a simulated SysTick whose handler counts each wrap
// `latency` ticks late.

#include "SystickReading.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <vector>

namespace {
int failures = 0;

void check(bool        ok,
           char const* what,
           long long   a = 0,
           long long   b = 0) {
    if(!ok) {
        ++failures;
        std::printf("FAIL: %s (%lld, %lld)\n", what, a, b);
    }
}

using Kvasir::Systick::Detail::calcOverRunValue;
using Kvasir::Systick::Detail::countsBetween;
using Kvasir::Systick::Detail::delayReached;
using Kvasir::Systick::Detail::joinHalves;
using Kvasir::Systick::Detail::OverrunCounter;
using Kvasir::Systick::Detail::OverrunWordT;
using Kvasir::Systick::Detail::pairReading;
using Kvasir::Systick::Detail::waitInChunks;

// The overrun counter's width (SystickReading.hpp OverrunWord): a 24-bit SysTick (2^24 ticks a wrap) counting
// years(15) = 15 * 365.2425 days. Integer nanoseconds per wrap: 134'217'728 at 125 MHz, 111'848'106 at 150 MHz.
constexpr std::uint64_t ticksPerWrap = std::uint64_t{1} << 24U;
constexpr auto          fifteenYears = std::chrono::years(15);
static_assert(calcOverRunValue(125'000'000,
                               ticksPerWrap,
                               fifteenYears)
              == 3'526'764'215ULL);
static_assert(calcOverRunValue(150'000'000,
                               ticksPerWrap,
                               fifteenYears)
              == 4'232'117'083ULL);
static_assert(std::is_same_v<OverrunWordT<calcOverRunValue(125'000'000,
                                                           ticksPerWrap,
                                                           fifteenYears)>,
                             std::uint32_t>);
static_assert(std::is_same_v<OverrunWordT<calcOverRunValue(150'000'000,
                                                           ticksPerWrap,
                                                           fifteenYears)>,
                             std::uint32_t>);
// past ~152.2 MHz years(15) needs more than 2^32 - 1 wraps: 64 bits
static_assert(std::is_same_v<OverrunWordT<calcOverRunValue(153'000'000,
                                                           ticksPerWrap,
                                                           fifteenYears)>,
                             std::uint64_t>);
static_assert(std::is_same_v<OverrunWordT<0xFFFF'FFFFULL>,
                             std::uint32_t>);
static_assert(std::is_same_v<OverrunWordT<0x1'0000'0000ULL>,
                             std::uint64_t>);
// rounded up: one nanosecond is one wrap
static_assert(calcOverRunValue(125'000'000,
                               ticksPerWrap,
                               std::chrono::nanoseconds{1})
              == 1U);

// delay()'s chunks, recorded
std::vector<std::uint32_t> chunks;

struct RecordWait {
    static void wait(std::uint32_t n) { chunks.push_back(n); }
};

struct SimTick {
    std::uint32_t reload;
    std::uint64_t latency;   // ticks from a wrap until the handler has counted it

    [[nodiscard]] std::uint64_t period() const { return std::uint64_t{reload} + 1U; }

    // The counter reloads at t = k * period, k >= 1.
    [[nodiscard]] std::uint64_t wraps(std::uint64_t t) const { return t / period(); }

    [[nodiscard]] std::uint32_t counter(std::uint64_t t) const {
        return reload - static_cast<std::uint32_t>(t % period());
    }

    [[nodiscard]] std::uint64_t overruns(std::uint64_t t) const {
        return t < latency ? 0U : wraps(t - latency);
    }

    [[nodiscard]] bool pending(std::uint64_t t) const { return wraps(t) > overruns(t); }
};
}   // namespace

int main() {
    constexpr std::uint32_t reload = 999;

    check(!pairReading(3, 500, false, 4, reload).has_value(), "a wrap counted between: retry");
    check(pairReading(3, 500, false, 3, reload) == 3U * 1000U + 499U, "no wrap pending");
    check(pairReading(3, 998, true, 3, reload) == 4U * 1000U + 1U,
          "pending, counter just reloaded: the wrap is added");
    check(pairReading(3, 2, true, 3, reload) == 3U * 1000U + 997U,
          "pending, counter near zero: the wrap came after the read");
    check(pairReading(0, reload, false, 0, reload) == 0U, "the start");

    // The 64-bit count as two halves.
    check(joinHalves(7, 0xFFFF'FFFFU, 7) == 0x7'FFFF'FFFFULL, "halves join");
    check(!joinHalves(7, 0U, 8).has_value(), "the high half moved: retry");
    {
        OverrunCounter<std::uint64_t> wide;
        wide.preset(0xFFFF'FFFEULL);
        wide.increment();
        check(wide.load() == 0xFFFF'FFFFULL, "below the low half's wrap");
        wide.increment();
        check(wide.load() == 0x1'0000'0000ULL,
              "the low half wraps into the high one",
              static_cast<long long>(wide.load()));
        wide.increment();
        check(wide.load() == 0x1'0000'0001ULL, "and counts on");
        OverrunCounter<std::uint32_t> narrow;
        narrow.increment();
        narrow.increment();
        check(narrow.load() == 2U, "the 32-bit counter");
    }

    // ShortInterval::since(): the counts between two readings of the down-counter, across a reload too.
    check(countsBetween(500U, 400U, reload) == 100U,
          "100 counts down",
          countsBetween(500U, 400U, reload),
          100);
    check(countsBetween(500U, 500U, reload) == 0U, "the same reading: 0");
    check(countsBetween(10U, 909U, reload) == 101U,
          "across a reload: 10 + 1000 - 909 = 101",
          countsBetween(10U, 909U, reload),
          101);
    check(countsBetween(0U, reload, reload) == 1U, "0 to reload: one count");
    check(countsBetween(reload, 0U, reload) == reload, "a full reload but one: reload counts");
    {
        constexpr std::uint32_t full = (1U << 24U) - 1U;   // the real SysTick, 2^24 counts a reload
        check(countsBetween(5U, full - 4U, full) == 10U, "24 bits, across a reload: 10");
        static_assert(countsBetween(5U, (1U << 24U) - 5U, (1U << 24U) - 1U) == 10U, "constexpr");
    }

    // delay_ticks' stop rule: counts gone (also across a reload), or the overrun counter says so.
    check(!delayReached(500U, 400U, std::uint32_t{0}, 101U, reload), "100 counts of 101");
    check(delayReached(500U, 399U, std::uint32_t{0}, 101U, reload), "101 counts of 101");
    check(delayReached(10U, 909U, std::uint32_t{1}, 100U, reload),
          "across a reload: 10 + 1000 - 909 = 101 counts");
    check(!delayReached(10U, 920U, std::uint32_t{0}, 100U, reload),
          "across a reload: 90 counts, no wrap counted yet");
    check(delayReached(500U, 499U, std::uint32_t{2}, 900U, reload),
          "two wraps: done whatever the counter says");
    check(delayReached(500U, 499U, std::uint64_t{1}, 900U, reload),
          "one wrap and the counter below its start");
    check(!delayReached(500U, 600U, std::uint32_t{1}, 901U, reload),
          "one wrap, the counter above its start: 900 counts gone of 901");
    check(delayReached(0U, 0U, std::uint32_t{0}, 0U, reload), "a wait of 0 ends at once");

    // delay(): waits of at most one chunk, the rest last (also 0 when the time is a whole number of chunks).
    waitInChunks<RecordWait>(25U, 10U);
    check(chunks == std::vector<std::uint32_t>{10U, 10U, 5U}, "25 in 10s");
    chunks.clear();
    waitInChunks<RecordWait>(20U, 10U);
    check(chunks == std::vector<std::uint32_t>{10U, 10U, 0U}, "20 in 10s");
    chunks.clear();
    waitInChunks<RecordWait>(3U, 0xFF'FFFFU);
    check(chunks == std::vector<std::uint32_t>{3U}, "below one reload: one wait");
    chunks.clear();
    waitInChunks<RecordWait>(std::uint64_t{0xFF'FFFFU} * 3U + 7U, 0xFF'FFFFU);
    check(chunks.size() == 4U && chunks.back() == 7U,
          "three reloads and 7",
          static_cast<long long>(chunks.size()));
    chunks.clear();

    // Every instant of four periods, latencies up to 40 % of a reload, sample gaps up to 3 ticks.
    long long accepted = 0;
    long long retried  = 0;
    for(std::uint64_t latency = 0; latency <= 400; latency += 7) {
        SimTick const sim{reload, latency};
        for(std::uint64_t t = 0; t < 4 * sim.period(); ++t) {
            for(std::uint64_t gap = 0; gap <= 3; ++gap) {
                std::uint64_t const tBefore  = t;
                std::uint64_t const tCount   = t + gap;
                std::uint64_t const tPending = tCount + gap;
                std::uint64_t const tAfter   = tPending + gap;
                auto const          r        = pairReading(sim.overruns(tBefore),
                                                           sim.counter(tCount),
                                                           sim.pending(tPending),
                                                           sim.overruns(tAfter),
                                                           reload);
                if(!r) {
                    ++retried;
                    continue;
                }
                ++accepted;
                check(*r == tCount,
                      "an accepted reading is the instant the counter was read",
                      static_cast<long long>(*r),
                      static_cast<long long>(tCount));
            }
        }
    }
    check(accepted > 0 && retried > 0, "both paths were exercised", accepted, retried);

    // Consecutive readings never go backwards, even with the handler late for most of a reload.
    {
        SimTick const sim{reload, 450};
        std::uint64_t last = 0;
        for(std::uint64_t t = 0; t < 10 * sim.period(); t += 3) {
            auto const r = pairReading(sim.overruns(t),
                                       sim.counter(t),
                                       sim.pending(t),
                                       sim.overruns(t),
                                       reload);
            if(!r) { continue; }
            check(*r >= last,
                  "monotonic",
                  static_cast<long long>(*r),
                  static_cast<long long>(last));
            last = *r;
        }
    }

    if(failures == 0) { std::printf("systick_reading_test: ok\n"); }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
