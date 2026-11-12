#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <thread>
#include <span>
#include <vector>

namespace aether::rendering {
enum class FrameStatus { ok, invalid_viewport, suspended, allocation_failed,
    wrong_thread, stale_token, exhausted };
struct Viewport { int width{}; int height{}; float dpi{1}; };
struct Frame {
    Viewport viewport{};
    std::size_t stride{};
    std::uint64_t sequence{};
    std::vector<std::uint8_t> pixels;
};
// An owning copy of pixels in top-down premultiplied BGRA8. DPI does not
// multiply the physical dimensions. The caller must preserve this format.
class FrameStore {
public:
    class Token {
        friend class FrameStore;
        const FrameStore* owner_{};
        std::uint64_t serial_{};
        Token(const FrameStore* owner, std::uint64_t serial) : owner_(owner), serial_(serial) {}
    public:
        Token() = default;
    };
    // Internal test hook invoked immediately before pixel allocation. May throw
    // bad_alloc; it must not call this store or throw other exception types.
    using AllocationHook = std::function<void()>;
    explicit FrameStore(AllocationHook hook = {});
    FrameStore(const FrameStore&) = delete;
    FrameStore& operator=(const FrameStore&) = delete;
    FrameStore(FrameStore&&) = delete;
    FrameStore& operator=(FrameStore&&) = delete;
    FrameStatus current_viewport(Viewport& out) const noexcept;
    FrameStatus resize(Viewport viewport) noexcept;
    FrameStatus prepare(Token& out) noexcept;
    FrameStatus commit(Token token) noexcept;
    FrameStatus discard(Token token) noexcept;
    FrameStatus copy_confirmed(std::optional<Frame>& out) const noexcept;
    // Tokens are scoped to this store and its lifetime. Do not use after
    // destruction (including address reuse). Foreign live-store tokens reject.
    // Borrowed pixels expire on the next successful prepare/resize/commit/
    // discard, or destruction. Never retain aliases after commit.
    FrameStatus candidate(Token token, std::span<std::uint8_t>& out) noexcept;
private:
    friend struct FrameStoreTestAccess;
    FrameStatus thread_status() const noexcept;
    std::thread::id owner_{std::this_thread::get_id()};
    Viewport viewport_{};
    std::optional<Frame> candidate_, confirmed_;
    std::uint64_t issued_{}, active_{};
    std::uint64_t sequence_{};
    AllocationHook before_allocation_;
};
} // namespace aether::rendering
