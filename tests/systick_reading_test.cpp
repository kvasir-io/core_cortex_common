// Host test for pairReading against a simulated SysTick whose handler counts each wrap
// `latency` ticks late.

#include "SystickReading.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

using Kvasir::Systick::Detail::pairReading;

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
