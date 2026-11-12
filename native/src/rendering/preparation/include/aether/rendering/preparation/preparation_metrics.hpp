#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace aether::rendering::preparation {

// Preparation only: these phases are neither engine timings nor capture results.
enum class Phase { snapshot_copy, preflight };
enum class PhaseState { not_run, running, succeeded, failed, cancelled };
enum class TimingReason { none, clock_unavailable, clock_invalid };

struct PhaseSample {
    PhaseState state{PhaseState::not_run};
    std::optional<std::int64_t> duration_ns;
    TimingReason timing_reason{TimingReason::none};
};

class Clock {
public:
    virtual ~Clock() = default;
    // Must not throw. A value must be nonnegative and monotonic; nullopt means
    // unavailable. The clock must outlive the metrics object and its operations.
    virtual std::optional<std::int64_t> now_ns() noexcept = 0;
};

class PreparationMetrics {
public:
    // No default real clock. A null pointer explicitly means unavailable.
    explicit PreparationMetrics(Clock* clock) noexcept : clock_(clock) {}
    PreparationMetrics(const PreparationMetrics&) = delete;
    PreparationMetrics& operator=(const PreparationMetrics&) = delete;
    PreparationMetrics(PreparationMetrics&&) = delete;
    PreparationMetrics& operator=(PreparationMetrics&&) = delete;

    // Each phase runs at most once; preflight requires successful snapshot_copy.
    // Invalid transitions return false without sampling or changing any slot.
    bool begin(Phase phase) noexcept;
    bool finish(Phase phase, PhaseState terminal_state) noexcept;
    const PhaseSample& snapshot_copy() const noexcept { return slots_[0].sample; }
    const PhaseSample& preflight() const noexcept { return slots_[1].sample; }
    // Destruction never samples or implicitly finishes a running phase.
private:
    struct Slot {
        PhaseSample sample;
        std::optional<std::int64_t> start_ns;
    };
    static std::optional<std::size_t> index(Phase phase) noexcept;
    Clock* clock_;
    std::array<Slot, 2> slots_{};
};

} // namespace aether::rendering::preparation
