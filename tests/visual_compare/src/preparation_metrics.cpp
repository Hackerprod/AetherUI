#include "aether_visual_compare/preparation_metrics.hpp"

namespace avc::preparation {

std::optional<std::size_t> PreparationMetrics::index(Phase phase) noexcept {
    switch (phase) {
    case Phase::snapshot_copy: return 0;
    case Phase::preflight: return 1;
    }
    return std::nullopt;
}

bool PreparationMetrics::begin(Phase phase) noexcept {
    const auto i = index(phase);
    if (!i || slots_[*i].sample.state != PhaseState::not_run) return false;
    if (*i == 1 && slots_[0].sample.state != PhaseState::succeeded) return false;
    auto& slot = slots_[*i];
    slot.start_ns = clock_ ? clock_->now_ns() : std::nullopt;
    slot.sample.state = PhaseState::running;
    return true;
}

bool PreparationMetrics::finish(Phase phase, PhaseState terminal_state) noexcept {
    const auto i = index(phase);
    if (!i || slots_[*i].sample.state != PhaseState::running) return false;
    if (terminal_state != PhaseState::succeeded && terminal_state != PhaseState::failed &&
        terminal_state != PhaseState::cancelled) return false;
    auto& slot = slots_[*i];
    const auto end_ns = clock_ ? clock_->now_ns() : std::nullopt;
    slot.sample.state = terminal_state;
    // Missing either endpoint takes precedence over invalid endpoint values.
    if (!slot.start_ns || !end_ns) {
        slot.sample.timing_reason = TimingReason::clock_unavailable;
    } else if (*slot.start_ns < 0 || *end_ns < 0 || *end_ns < *slot.start_ns) {
        slot.sample.timing_reason = TimingReason::clock_invalid;
    } else {
        // Both operands are nonnegative int64, end >= start: no overflow.
        slot.sample.duration_ns = *end_ns - *slot.start_ns;
    }
    return true;
}

} // namespace avc::preparation
