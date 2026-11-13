#ifndef AETHER_TYPOGRAPHY_SCOPE_PROBE_H
#define AETHER_TYPOGRAPHY_SCOPE_PROBE_H
#include <cstdint>
namespace aether::style {
// Private opt-in measurement only. Never affects uncertainty or style decisions.
struct TypographyScopeCounters {
    std::uint64_t node_computations{}, queries{}, scopes{}, terminals{}, uncertain_exits{};
};
inline thread_local TypographyScopeCounters* typography_scope_counters = nullptr;
class TypographyScopeProbe final {
    TypographyScopeCounters* previous_;
public:
    explicit TypographyScopeProbe(TypographyScopeCounters& counters) noexcept
        : previous_(typography_scope_counters) { typography_scope_counters = &counters; }
    ~TypographyScopeProbe() { typography_scope_counters = previous_; }
    TypographyScopeProbe(const TypographyScopeProbe&) = delete;
    TypographyScopeProbe& operator=(const TypographyScopeProbe&) = delete;
};
}
#endif
