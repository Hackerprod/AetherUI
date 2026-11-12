#include "native_frame.h"
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace aether::rendering {
namespace {
bool valid(Viewport v) noexcept {
    return v.width >= 0 && v.height >= 0 && v.width <= 8192 && v.height <= 8192 &&
        std::isfinite(v.dpi) && v.dpi > 0 &&
        std::uint64_t(v.width) * std::uint64_t(v.height) * 4 <= 64ULL * 1024 * 1024;
}
}
FrameStore::FrameStore(AllocationHook hook) : before_allocation_(std::move(hook)) {}
FrameStatus FrameStore::thread_status() const noexcept {
    return owner_ == std::this_thread::get_id() ? FrameStatus::ok : FrameStatus::wrong_thread;
}
FrameStatus FrameStore::current_viewport(Viewport& out) const noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    out = viewport_; return FrameStatus::ok;
}
FrameStatus FrameStore::resize(Viewport v) noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    if (!valid(v)) return FrameStatus::invalid_viewport;
    viewport_ = v;
    candidate_.reset(); active_ = 0;
    return FrameStatus::ok;
}
FrameStatus FrameStore::prepare(Token& out) noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    if (!viewport_.width || !viewport_.height) return FrameStatus::suspended;
    if (issued_ == std::numeric_limits<std::uint64_t>::max()) return FrameStatus::exhausted;
    try {
        Frame next;
        next.viewport = viewport_;
        next.stride = std::size_t(viewport_.width) * 4;
        if (before_allocation_) before_allocation_();
        next.pixels.resize(next.stride * std::size_t(viewport_.height));
        candidate_ = std::move(next);
        active_ = ++issued_;
        out = Token(this, active_);
        return FrameStatus::ok;
    } catch (const std::bad_alloc&) { return FrameStatus::allocation_failed; }
}
FrameStatus FrameStore::candidate(Token token, std::span<std::uint8_t>& out) noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    if (!active_ || (token.owner_ != this || token.serial_ != active_) || !candidate_) return FrameStatus::stale_token;
    out = candidate_->pixels; return FrameStatus::ok;
}
FrameStatus FrameStore::commit(Token token) noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    if (!active_ || (token.owner_ != this || token.serial_ != active_) || !candidate_) return FrameStatus::stale_token;
    if (sequence_ == std::numeric_limits<std::uint64_t>::max()) return FrameStatus::exhausted;
    candidate_->sequence = ++sequence_;
    confirmed_ = std::move(candidate_);
    candidate_.reset(); active_ = 0;
    return FrameStatus::ok;
}
FrameStatus FrameStore::discard(Token token) noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    if (!active_ || (token.owner_ != this || token.serial_ != active_) || !candidate_) return FrameStatus::stale_token;
    candidate_.reset(); active_ = 0; return FrameStatus::ok;
}
FrameStatus FrameStore::copy_confirmed(std::optional<Frame>& out) const noexcept {
    if (thread_status() != FrameStatus::ok) return FrameStatus::wrong_thread;
    try {
        if (confirmed_ && before_allocation_) before_allocation_();
        auto copy = confirmed_;
        out = std::move(copy);
        return FrameStatus::ok;
    } catch (const std::bad_alloc&) { return FrameStatus::allocation_failed; }
}
} // namespace aether::rendering
