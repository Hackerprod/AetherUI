#include "aether_visual_compare/preparation_metrics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

using namespace avc::preparation;

namespace {
// All durations in this suite have synthetic provenance. No engine is invoked.
class FakeClock final : public Clock {
public:
    explicit FakeClock(std::span<const std::optional<std::int64_t>> samples) noexcept
        : samples_(samples) {}
    std::optional<std::int64_t> now_ns() noexcept override {
        const auto position = calls++;
        return position < samples_.size() ? samples_[position] : std::nullopt;
    }
    std::size_t calls{};
private:
    std::span<const std::optional<std::int64_t>> samples_;
};

int failures{};
void check(bool condition) { if (!condition) ++failures; }

void lifecycle() {
    const std::array<std::optional<std::int64_t>, 4> values{100, 160, 200, 290};
    FakeClock clock(values);
    PreparationMetrics metrics(&clock);
    check(metrics.snapshot_copy().state == PhaseState::not_run);
    check(!metrics.snapshot_copy().duration_ns);
    check(!metrics.preflight().duration_ns);
    check(!metrics.begin(Phase::preflight));
    check(!metrics.finish(Phase::snapshot_copy, PhaseState::succeeded));
    check(!metrics.begin(static_cast<Phase>(2)));
    check(!metrics.finish(static_cast<Phase>(2), PhaseState::succeeded));
    check(clock.calls == 0);
    check(metrics.begin(Phase::snapshot_copy));
    check(metrics.snapshot_copy().state == PhaseState::running);
    check(!metrics.snapshot_copy().duration_ns);
    check(!metrics.begin(Phase::snapshot_copy));
    check(!metrics.begin(Phase::preflight));
    check(!metrics.finish(Phase::snapshot_copy, PhaseState::running));
    check(!metrics.finish(Phase::snapshot_copy, PhaseState::not_run));
    check(!metrics.finish(Phase::snapshot_copy, static_cast<PhaseState>(5)));
    check(clock.calls == 1);
    check(metrics.finish(Phase::snapshot_copy, PhaseState::succeeded));
    check(metrics.snapshot_copy().duration_ns == 60);
    check(metrics.snapshot_copy().timing_reason == TimingReason::none);
    check(!metrics.finish(Phase::snapshot_copy, PhaseState::failed));
    check(!metrics.begin(Phase::snapshot_copy));
    check(clock.calls == 2);
    check(metrics.snapshot_copy().state == PhaseState::succeeded);
    check(metrics.snapshot_copy().duration_ns == 60);
    check(metrics.begin(Phase::preflight));
    check(metrics.finish(Phase::preflight, PhaseState::succeeded));
    check(metrics.preflight().duration_ns == 90);
    check(!metrics.begin(Phase::preflight));
    check(!metrics.finish(Phase::preflight, PhaseState::cancelled));
    check(metrics.preflight().state == PhaseState::succeeded);
    check(metrics.preflight().duration_ns == 90);
    check(clock.calls == 4);
}

void terminal_failure(PhaseState state) {
    const std::array<std::optional<std::int64_t>, 2> values{10, 30};
    FakeClock clock(values);
    PreparationMetrics metrics(&clock);
    check(metrics.begin(Phase::snapshot_copy));
    check(metrics.finish(Phase::snapshot_copy, state));
    check(metrics.snapshot_copy().state == state);
    check(metrics.snapshot_copy().duration_ns == 20);
    check(!metrics.begin(Phase::preflight));
    check(metrics.preflight().state == PhaseState::not_run);
    check(!metrics.preflight().duration_ns);
    check(clock.calls == 2);
}

void preflight_terminal(PhaseState state) {
    const std::array<std::optional<std::int64_t>, 4> values{1, 2, 3, 5};
    FakeClock clock(values);
    PreparationMetrics metrics(&clock);
    check(metrics.begin(Phase::snapshot_copy));
    check(metrics.finish(Phase::snapshot_copy, PhaseState::succeeded));
    check(metrics.begin(Phase::preflight));
    check(metrics.finish(Phase::preflight, state));
    check(metrics.preflight().state == state);
    check(metrics.preflight().duration_ns == 2);
    check(clock.calls == 4);
}

void endpoints(std::optional<std::int64_t> start, std::optional<std::int64_t> end,
               std::optional<std::int64_t> duration, TimingReason reason) {
    const std::array<std::optional<std::int64_t>, 2> values{start, end};
    FakeClock clock(values);
    PreparationMetrics metrics(&clock);
    check(metrics.begin(Phase::snapshot_copy));
    check(metrics.finish(Phase::snapshot_copy, PhaseState::succeeded));
    check(metrics.snapshot_copy().state == PhaseState::succeeded);
    check(metrics.snapshot_copy().duration_ns == duration);
    check(metrics.snapshot_copy().timing_reason == reason);
    check(clock.calls == 2);
    // Clock quality never changes the functional prerequisite for preflight.
    check(metrics.begin(Phase::preflight));
    check(metrics.finish(Phase::preflight, PhaseState::failed));
    check(metrics.preflight().state == PhaseState::failed);
    check(!metrics.preflight().duration_ns);
    check(metrics.preflight().timing_reason == TimingReason::clock_unavailable);
    check(clock.calls == 4);
}
} // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<PreparationMetrics>);
    static_assert(noexcept(std::declval<Clock&>().now_ns()));
    lifecycle();
    terminal_failure(PhaseState::failed);
    terminal_failure(PhaseState::cancelled);
    preflight_terminal(PhaseState::failed);
    preflight_terminal(PhaseState::cancelled);
    endpoints(std::nullopt, 4, std::nullopt, TimingReason::clock_unavailable);
    endpoints(4, std::nullopt, std::nullopt, TimingReason::clock_unavailable);
    endpoints(std::nullopt, std::nullopt, std::nullopt, TimingReason::clock_unavailable);
    endpoints(-1, 4, std::nullopt, TimingReason::clock_invalid);
    endpoints(4, -1, std::nullopt, TimingReason::clock_invalid);
    endpoints(8, 4, std::nullopt, TimingReason::clock_invalid);
    endpoints(4, 4, 0, TimingReason::none);
    endpoints(0, std::numeric_limits<std::int64_t>::max(),
              std::numeric_limits<std::int64_t>::max(), TimingReason::none);
    endpoints(std::numeric_limits<std::int64_t>::max(),
              std::numeric_limits<std::int64_t>::max(), 0, TimingReason::none);
    PreparationMetrics absent(nullptr);
    check(absent.begin(Phase::snapshot_copy));
    check(absent.finish(Phase::snapshot_copy, PhaseState::cancelled));
    check(absent.snapshot_copy().state == PhaseState::cancelled);
    check(!absent.snapshot_copy().duration_ns);
    check(absent.snapshot_copy().timing_reason == TimingReason::clock_unavailable);
    const std::array<std::optional<std::int64_t>, 1> values{5};
    FakeClock clock(values);
    {
        PreparationMetrics unfinished(&clock);
        check(unfinished.begin(Phase::snapshot_copy));
        check(unfinished.snapshot_copy().state == PhaseState::running);
    }
    check(clock.calls == 1); // Destructor neither samples nor reports success.
    return failures == 0 ? 0 : 1;
}
