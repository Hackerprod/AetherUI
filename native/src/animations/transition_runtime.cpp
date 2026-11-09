#include "transition_runtime.h"
#include "../diagnostics/work_counters.h"
#include <aether/aether_transition_sync.h>
#include "../dom/dom_types.h"
#include <atomic>
#include <thread>
#include <limits>
#include <cstring>
#include "../rendering/transform_matrix.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace {

struct transition_claim {
    std::shared_ptr<DomNodeOwnerPin> live_owner;
    std::shared_ptr<EditableNodeStorage> document;
    std::shared_ptr<EditableNodeStorage> lease;
    std::shared_ptr<node_identity_s> identity;
    uint64_t disconnect_serial = 0;
    bool valid() const noexcept {
        return identity && identity->alive && identity->attached &&
            identity->disconnect_serial == disconnect_serial;
    }
};

struct active_transition_s {
    transition_claim claim;
    aether_node_t node = nullptr;
    int32_t property = 0;
    int32_t timing = AETHER_TRANSITION_TIMING_LINEAR;
    int64_t observed_ms = 0;
    int64_t delay_ms = 0;
    int64_t duration_ms = 1;
    float opacity_from = 0.f;
    float opacity_to = 0.f;
    aether_transform_matrix matrix_from{};
    aether_transform_matrix matrix_to{};
    float current_opacity = 0.f;
    aether_transform_matrix current_matrix{};
};

/* A transition that reached progress 1. Its exact end value is retained until
   an observation supersedes it or disables its property, or until destruction.
   An eligible observation with an unchanged value preserves the entry. */
struct finished_transition_s {
    transition_claim claim;
    aether_node_t node = nullptr;
    int32_t property = 0;
    float opacity = 0.f;
    aether_transform_matrix matrix{};
};

} // namespace

namespace {

constexpr float kOpacityEpsilon = 1e-4f;
constexpr float kMatrixEpsilon = 1e-4f;

/* The runtime is pre-layout and observes computed styles, never layout boxes.
   Transform matrices are therefore resolved against a neutral placeholder box
   with a neutral sizing context; the display list builder applies the
   interpolated matrix over the element's real, post-layout geometry. */
constexpr aether_rectf kPlaceholderBox{0.f, 0.f, 100.f, 100.f};
constexpr aether_sizef kPlaceholderViewport{100.f, 100.f};
constexpr float kPlaceholderRootFontSize = 16.f;

aether_transform_matrix matrix_lerp(const aether_transform_matrix& from,
                                    const aether_transform_matrix& to, float t) {
    const auto lerp = [t](float a, float b) { return a + (b - a) * t; };
    return {lerp(from.m11, to.m11), lerp(from.m12, to.m12),
            lerp(from.m21, to.m21), lerp(from.m22, to.m22),
            lerp(from.dx, to.dx),   lerp(from.dy, to.dy)};
}

bool matrix_equal(const aether_transform_matrix& a, const aether_transform_matrix& b) {
    return std::fabs(a.m11 - b.m11) <= kMatrixEpsilon &&
           std::fabs(a.m12 - b.m12) <= kMatrixEpsilon &&
           std::fabs(a.m21 - b.m21) <= kMatrixEpsilon &&
           std::fabs(a.m22 - b.m22) <= kMatrixEpsilon &&
           std::fabs(a.dx - b.dx) <= kMatrixEpsilon &&
           std::fabs(a.dy - b.dy) <= kMatrixEpsilon;
}

/* Numeric cubic-bezier evaluator for the CSS timing keywords. Solve x(t)=p
   for t in [0,1] by bisection, then return y(t). The CSS control points for
   each keyword are given in aether.h. */
float cubic_bezier_y(float p, float x1, float y1, float x2, float y2) {
    p = std::clamp(p, 0.f, 1.f);
    const auto sample_x = [&](float t) {
        const float u = 1.f - t;
        return 3.f * u * u * t * x1 + 3.f * u * t * t * x2 + t * t * t;
    };
    const auto sample_y = [&](float t) {
        const float u = 1.f - t;
        return 3.f * u * u * t * y1 + 3.f * u * t * t * y2 + t * t * t;
    };
    float lower = 0.f;
    float upper = 1.f;
    float t = p;
    for (int i = 0; i < 12; ++i) {
        const float x = sample_x(t);
        if (std::fabs(x - p) < 1e-5f) break;
        if (x < p) lower = t;
        else upper = t;
        t = (lower + upper) * 0.5f;
    }
    return sample_y(t);
}

float apply_timing(int32_t timing, float p) {
    switch (timing) {
        case AETHER_TRANSITION_TIMING_EASE:
            return cubic_bezier_y(p, 0.25f, 0.1f, 0.25f, 1.0f);
        case AETHER_TRANSITION_TIMING_EASE_IN:
            return cubic_bezier_y(p, 0.42f, 0.0f, 1.0f, 1.0f);
        case AETHER_TRANSITION_TIMING_EASE_OUT:
            return cubic_bezier_y(p, 0.0f, 0.0f, 0.58f, 1.0f);
        case AETHER_TRANSITION_TIMING_EASE_IN_OUT:
            return cubic_bezier_y(p, 0.42f, 0.0f, 0.58f, 1.0f);
        case AETHER_TRANSITION_TIMING_LINEAR:
        default:
            return p;
    }
}

aether_transform_matrix resolve_matrix(const aether_computed_style_t& style) {
    return aether_transform_matrix_resolve(
        style.transforms, style.transform_origin, kPlaceholderBox,
        style.font_size > 0.f ? style.font_size : 16.f,
        kPlaceholderRootFontSize, kPlaceholderViewport);
}

// Preserve the historical float product/truncation for duration. The wide
// prefilter avoids floating overflow; the actual float product decides range.
bool transition_times_ms(float duration, float delay, int64_t& duration_ms,
                         int64_t& delay_ms) {
    if (!std::isfinite(duration) || static_cast<double>(duration) * 1000.0 >= 0x1p64)
        return false;
    const float duration_product = duration * 1000.0f;
    if (!std::isfinite(duration_product) || duration_product >= 0x1p63f)
        return false;
    const double delay_product = static_cast<double>(delay) * 1000.0;
    if (!std::isfinite(delay_product) || delay_product < -0x1p63 ||
        delay_product >= 0x1p63)
        return false;
    const auto ms = static_cast<int64_t>(duration_product);
    duration_ms = ms > 0 ? ms : 1;
    delay_ms = static_cast<int64_t>(delay_product);
    return true;
}

// Clamp mathematical (now - observed - delay) without forming a signed
// difference, an absolute start timestamp, or a potentially 65-bit sum.
uint64_t transition_elapsed_ms(const active_transition_s& transition, int64_t now_ms) {
    const auto duration = static_cast<uint64_t>(transition.duration_ms);
    const bool forward = now_ms >= transition.observed_ms;
    const auto magnitude = forward ?
        static_cast<uint64_t>(now_ms) - static_cast<uint64_t>(transition.observed_ms) :
        static_cast<uint64_t>(transition.observed_ms) - static_cast<uint64_t>(now_ms);
    if (transition.delay_ms >= 0) {
        const auto delay = static_cast<uint64_t>(transition.delay_ms);
        if (!forward || magnitude <= delay) return 0;
        return std::min(magnitude - delay, duration);
    }
    const auto delay = uint64_t{0} - static_cast<uint64_t>(transition.delay_ms);
    if (!forward) {
        if (magnitude >= delay) return 0;
        return std::min(delay - magnitude, duration);
    }
    if (magnitude >= duration || delay >= duration - magnitude) return duration;
    return magnitude + delay; // Proven strictly below duration.
}

// One clock sample is shared by interruption, initial negative delay and tick.
float sample_transition(active_transition_s& transition, int64_t now_ms) {
    const float p = std::clamp(static_cast<float>(transition_elapsed_ms(transition, now_ms)) /
        static_cast<float>(transition.duration_ms), 0.f, 1.f);
    const float eased = apply_timing(transition.timing, p);
    if (transition.property == AETHER_TRANSITION_PROPERTY_OPACITY)
        transition.current_opacity = transition.opacity_from +
            (transition.opacity_to - transition.opacity_from) * eased;
    else
        transition.current_matrix = matrix_lerp(transition.matrix_from, transition.matrix_to, eased);
    return p;
}

} // namespace

struct transition_state {
    std::vector<active_transition_s> active;
    std::vector<finished_transition_s> finished;
    // D2 membership belongs to the same prepared transaction as both vectors.
    aether::style::ContextOwnersRef owners;
};
struct transition_lifetime {
    aether_transition_runtime_t owner = nullptr;
    const std::thread::id thread = std::this_thread::get_id();
    uint64_t instance = 0;
};
struct aether_transition_runtime_s {
    std::shared_ptr<transition_lifetime> lifetime;
    std::shared_ptr<transition_state> state;
    uint64_t mutation_serial = 0;
    uint64_t status_revision = 0;
    aether_status_t tick_error = AETHER_OK;
    bool exhausted = false;
};
struct aether_transition_candidate_s {
    std::shared_ptr<transition_lifetime> lifetime;
    std::shared_ptr<transition_state> state;
    aether_transition_token_t base{};
    bool changed = false;
    bool committed = false;
};
namespace {
std::atomic<uint64_t> next_instance{1};
thread_local int64_t allocation_countdown = -1;
void allocation_checkpoint() {
    if (allocation_countdown == 0) throw std::bad_alloc();
    if (allocation_countdown > 0) --allocation_countdown;
}
bool on_thread(aether_transition_runtime_t runtime) noexcept {
    return runtime && runtime->lifetime->thread == std::this_thread::get_id();
}
aether_transition_token_t token_of(aether_transition_runtime_t r) noexcept {
    return {r->lifetime->instance, r->mutation_serial, r->status_revision, r->tick_error};
}
bool active_state(const transition_state& state) noexcept {
    for (const auto& e : state.active) if (e.claim.valid()) return true;
    return false;
}
bool prune(transition_state& state) {
    auto index = state.owners->owners;
    for (const auto& entry : state.active) {
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
        if (!entry.claim.valid() && entry.property == AETHER_TRANSITION_PROPERTY_TRANSFORM)
            index = aether::style::erase_context_owner(std::move(index),
                reinterpret_cast<uintptr_t>(entry.claim.identity.get()));
    }
    state.owners = aether::style::publish_context_owners(state.owners, std::move(index));
    const auto a = state.active.size(), f = state.finished.size();
    state.active.erase(std::remove_if(state.active.begin(), state.active.end(),
        [](const active_transition_s& e) { return !e.claim.valid(); }), state.active.end());
    state.finished.erase(std::remove_if(state.finished.begin(), state.finished.end(),
        [](const finished_transition_s& e) { return !e.claim.valid(); }), state.finished.end());
    return a != state.active.size() || f != state.finished.size();
}
bool stale(const transition_state& state) noexcept {
    for (const auto& e : state.active) if (!e.claim.valid()) return true;
    for (const auto& e : state.finished) if (!e.claim.valid()) return true;
    return false;
}
std::shared_ptr<transition_state> clone(const transition_state& state) {
    allocation_checkpoint();
    auto copy = std::make_shared<transition_state>();
    copy->owners = state.owners;
    aether::diagnostics::count_work(aether::diagnostics::WorkCounter::transition_candidates_cloned);
    if (!state.active.empty()) {
        allocation_checkpoint(); copy->active = state.active;
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::transition_entries_copied, state.active.size());
    }
    if (!state.finished.empty()) {
        allocation_checkpoint(); copy->finished = state.finished;
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::transition_entries_copied, state.finished.size());
    }
    return copy;
}
bool matrix_exact(const aether_transform_matrix& a, const aether_transform_matrix& b) noexcept {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
bool float_exact(float a, float b) noexcept { return std::memcmp(&a, &b, sizeof(a)) == 0; }
bool equal_state(const transition_state& a, const transition_state& b) noexcept {
    if (a.active.size() != b.active.size() || a.finished.size() != b.finished.size()) return false;
    for (size_t i = 0; i < a.active.size(); ++i) {
        const auto& x = a.active[i]; const auto& y = b.active[i];
        if (x.node != y.node || x.claim.identity != y.claim.identity ||
            x.claim.disconnect_serial != y.claim.disconnect_serial || x.property != y.property ||
            x.timing != y.timing || x.observed_ms != y.observed_ms || x.delay_ms != y.delay_ms ||
            x.duration_ms != y.duration_ms || !float_exact(x.opacity_from, y.opacity_from) ||
            !float_exact(x.opacity_to, y.opacity_to) || !float_exact(x.current_opacity, y.current_opacity) ||
            !matrix_exact(x.matrix_from, y.matrix_from) || !matrix_exact(x.matrix_to, y.matrix_to) ||
            !matrix_exact(x.current_matrix, y.current_matrix)) return false;
    }
    for (size_t i = 0; i < a.finished.size(); ++i) {
        const auto& x = a.finished[i]; const auto& y = b.finished[i];
        if (x.node != y.node || x.claim.identity != y.claim.identity ||
            x.claim.disconnect_serial != y.claim.disconnect_serial || x.property != y.property ||
            !float_exact(x.opacity, y.opacity) || !matrix_exact(x.matrix, y.matrix)) return false;
    }
    return true;
}
void record_tick_error(aether_transition_runtime_t r, aether_status_t error) noexcept {
    r->tick_error = error;
    if (r->status_revision == UINT64_MAX) {
        r->exhausted = true;
        r->tick_error = AETHER_ERROR_INVALID_STATE;
    } else ++r->status_revision;
}

aether_status_t observe_state(
    transition_state* runtime,
    aether_node_t node,
    const aether_computed_style_t* before,
    const aether_computed_style_t* after,
    int64_t now_ms) {
    if (!runtime || !node || !before || !after) return AETHER_ERROR_NULL_ARG;
    const int32_t props = after->transition_properties;
    const bool opacity_eligible = after->transition_duration > 0.f &&
        (props & AETHER_TRANSITION_PROPERTY_OPACITY) != 0;
    const bool transform_eligible = after->transition_duration > 0.f &&
        (props & AETHER_TRANSITION_PROPERTY_TRANSFORM) != 0;

    const bool opacity_changed = opacity_eligible &&
        std::fabs(before->opacity - after->opacity) > kOpacityEpsilon;
    bool transform_changed = false;
    aether_transform_matrix transform_from{}, transform_to{};
    if (transform_eligible) {
        transform_from = resolve_matrix(*before);
        transform_to = resolve_matrix(*after);
        transform_changed = !matrix_equal(transform_from, transform_to);
        if (transform_changed) {
            // Preflight must not persist an interruption sample on rejection.
            for (const auto& entry : runtime->active) {
                if (entry.node != node || entry.property != AETHER_TRANSITION_PROPERTY_TRANSFORM)
                    continue;
                auto sampled = entry;
                sample_transition(sampled, now_ms);
                transform_from = sampled.current_matrix;
                break;
            }
        }
    }
    int64_t duration_ms = 1, delay_ms = 0;
    if ((opacity_changed || (transform_changed && !matrix_equal(transform_from, transform_to))) &&
        !transition_times_ms(after->transition_duration, after->transition_delay, duration_ms, delay_ms))
        return AETHER_ERROR_INVALID_STATE;

    const auto drop_finished = [&](int32_t property) {
        auto& finished = runtime->finished;
        finished.erase(std::remove_if(finished.begin(), finished.end(),
            [&](const finished_transition_s& entry) {
                return entry.node == node && entry.property == property;
            }), finished.end());
    };
    const auto supersede_active = [&](int32_t property, float* opacity,
                                      aether_transform_matrix* matrix) {
        for (auto it = runtime->active.begin(); it != runtime->active.end(); ++it) {
            if (it->node != node || it->property != property) continue;
            sample_transition(*it, now_ms);
            if (opacity) *opacity = it->current_opacity;
            if (matrix) *matrix = it->current_matrix;
            runtime->active.erase(it);
            break;
        }
    };
    const auto install = [&](active_transition_s transition) {
        const auto owner = node->document_owner.lock();
        transition.claim = {aether_live_owner_pin(node), owner ? owner->document.lock() : nullptr, aether_editable_lease(node), node->identity, node->identity->disconnect_serial};
        transition.observed_ms = now_ms;
        transition.delay_ms = delay_ms;
        if (sample_transition(transition, now_ms) >= 1.f) {
            allocation_checkpoint();
            runtime->finished.push_back({transition.claim, node, transition.property,
                transition.current_opacity, transition.current_matrix});
        } else {
            allocation_checkpoint();
            runtime->active.push_back(transition);
        }
    };

    // Eligibility is reconciled before comparing values: cancellation must
    // remove both active and retained opacity, even with an unchanged target.
    if (!opacity_eligible) {
        auto& active = runtime->active;
        active.erase(std::remove_if(active.begin(), active.end(),
            [&](const active_transition_s& entry) {
                return entry.node == node && entry.property == AETHER_TRANSITION_PROPERTY_OPACITY;
            }), active.end());
        drop_finished(AETHER_TRANSITION_PROPERTY_OPACITY);
    } else if (opacity_changed) {
        float from = before->opacity;
        supersede_active(AETHER_TRANSITION_PROPERTY_OPACITY, &from, nullptr);
        drop_finished(AETHER_TRANSITION_PROPERTY_OPACITY);
        active_transition_s transition{};
        transition.node = node;
        transition.property = AETHER_TRANSITION_PROPERTY_OPACITY;
        transition.timing = after->transition_timing;
        transition.duration_ms = duration_ms;
        transition.opacity_from = from;
        transition.opacity_to = after->opacity;
        install(transition);
    }

    if (!transform_eligible) {
        supersede_active(AETHER_TRANSITION_PROPERTY_TRANSFORM, nullptr, nullptr);
        drop_finished(AETHER_TRANSITION_PROPERTY_TRANSFORM);
    } else if (transform_changed) {
        const auto& from = transform_from;
        const auto& to = transform_to;
        supersede_active(AETHER_TRANSITION_PROPERTY_TRANSFORM, nullptr, nullptr);
        drop_finished(AETHER_TRANSITION_PROPERTY_TRANSFORM);
        if (!matrix_equal(from, to)) {
            active_transition_s transition{};
            transition.node = node;
            transition.property = AETHER_TRANSITION_PROPERTY_TRANSFORM;
            transition.timing = after->transition_timing;
            transition.duration_ms = duration_ms;
            transition.matrix_from = from;
            transition.matrix_to = to;
            install(transition);
        }
    }
    // Derive membership from the actual staged vector, whose insertion uses
    // sample_transition's float boundary (not an independent integer clock).
    bool active_after = false;
    for (const auto& entry : runtime->active) {
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
        if (entry.node == node && entry.claim.identity == node->identity &&
            entry.property == AETHER_TRANSITION_PROPERTY_TRANSFORM) active_after = true;
    }
    auto owner_index = aether::style::update_context_owner(runtime->owners->owners,
        {node, node->identity, static_cast<uint8_t>(active_after ? aether::style::ActiveTransform : 0)});
    auto next_owners = aether::style::publish_context_owners(runtime->owners, std::move(owner_index));
    runtime->owners = std::move(next_owners);
    return AETHER_OK;
}

aether_bool_t tick_state(transition_state* runtime, int64_t now_ms) {
    if (!runtime) return AETHER_FALSE;
    auto index = runtime->owners->owners;
    auto& active = runtime->active;
    for (auto it = active.begin(); it != active.end();) {
        active_transition_s& transition = *it;
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
        const float p = sample_transition(transition, now_ms);
        if (p >= 1.f) {
            if (transition.property == AETHER_TRANSITION_PROPERTY_TRANSFORM)
                index = aether::style::erase_context_owner(std::move(index),
                    reinterpret_cast<uintptr_t>(transition.claim.identity.get()));
            finished_transition_s done{};
            done.claim = transition.claim;
            done.node = transition.node;
            done.property = transition.property;
            done.opacity = transition.current_opacity;
            done.matrix = transition.current_matrix;
            allocation_checkpoint();
            runtime->finished.push_back(done);
            it = active.erase(it);
        } else {
            ++it;
        }
    }
    auto next_owners = aether::style::publish_context_owners(runtime->owners, std::move(index));
    runtime->owners = std::move(next_owners);
    return active.empty() ? AETHER_FALSE : AETHER_TRUE;
}


} // namespace

extern "C" {

AETHER_API aether_status_t aether_transition_runtime_create(aether_transition_runtime_t* out) {
    if (!out) return AETHER_ERROR_NULL_ARG;
    *out = nullptr;
    try {
        allocation_checkpoint();
        auto runtime = std::make_unique<aether_transition_runtime_s>();
        allocation_checkpoint();
        runtime->lifetime = std::make_shared<transition_lifetime>();
        allocation_checkpoint();
        runtime->state = std::make_shared<transition_state>();
        allocation_checkpoint();
        runtime->state->owners = aether::style::publish_context_owners({}, {});
        auto id = next_instance.load();
        do {
            if (id == UINT64_MAX) return AETHER_ERROR_INVALID_STATE;
        } while (!next_instance.compare_exchange_weak(id, id + 1));
        runtime->lifetime->instance = id;
        runtime->lifetime->owner = runtime.get();
        *out = runtime.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
AETHER_API void aether_transition_runtime_destroy(aether_transition_runtime_t r) {
    if (!on_thread(r)) return;
    r->lifetime->owner = nullptr;
    delete r;
}
AETHER_API aether_status_t aether_transition_runtime_read_token(
    aether_transition_runtime_t r, aether_transition_token_t* out) {
    if (out) *out = {};
    if (!r || !out) return AETHER_ERROR_NULL_ARG;
    if (!on_thread(r)) return AETHER_ERROR_INVALID_STATE;
    *out = token_of(r);
    return r->exhausted ? AETHER_ERROR_INVALID_STATE : AETHER_OK;
}
AETHER_API aether_status_t aether_transition_runtime_prepare(
    aether_transition_runtime_t r, const aether_transition_observation_t* observations,
    size_t count, const aether_node_t* retired, size_t retire_count, int64_t now,
    aether_transition_candidate_t* out) {
    if (out) *out = nullptr;
    if (!r || !out || (count && !observations) || (retire_count && !retired))
        return AETHER_ERROR_NULL_ARG;
    if (!on_thread(r) || r->exhausted) return AETHER_ERROR_INVALID_STATE;
    try {
        allocation_checkpoint();
        auto candidate = std::make_unique<aether_transition_candidate_s>();
        candidate->lifetime = r->lifetime;
        candidate->base = token_of(r);
        candidate->changed = count || retire_count || stale(*r->state);
        if (candidate->changed) {
            if (r->mutation_serial == UINT64_MAX) return AETHER_ERROR_INVALID_STATE;
            candidate->state = clone(*r->state);
            prune(*candidate->state);
            for (size_t i = 0; i < retire_count; ++i) {
                if (!retired[i]) return AETHER_ERROR_NULL_ARG;
                auto identity = retired[i]->identity;
                auto& owners = candidate->state->owners;
                owners = aether::style::publish_context_owners(owners,
                    aether::style::erase_context_owner(owners->owners, reinterpret_cast<uintptr_t>(identity.get())));
                auto& a = candidate->state->active;
                auto& f = candidate->state->finished;
                a.erase(std::remove_if(a.begin(), a.end(), [&](const active_transition_s& e) {
                    return e.claim.identity == identity; }), a.end());
                f.erase(std::remove_if(f.begin(), f.end(), [&](const finished_transition_s& e) {
                    return e.claim.identity == identity; }), f.end());
            }
            for (size_t i = 0; i < count; ++i) {
                const auto& observation = observations[i];
                if (!observation.node || !observation.before || !observation.after)
                    return AETHER_ERROR_NULL_ARG;
                if (aether_dom_access(observation.node) != AETHER_OK ||
                    !observation.node->identity->attached)
                    return AETHER_ERROR_INVALID_STATE;
                auto status = observe_state(candidate->state.get(), observation.node,
                    observation.before, observation.after, now);
                if (status != AETHER_OK) return status;
            }
            candidate->changed = !equal_state(*candidate->state, *r->state);
        } else candidate->state = r->state;
        *out = candidate.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
/* Internal validation permits direct Observe while a TickError is pending;
 * style publication validation deliberately does not. Neither path clears it. */
static aether_status_t validate_candidate(aether_transition_candidate_t c, bool require_healthy) {
    if (!c) return AETHER_ERROR_NULL_ARG;
    if (c->lifetime->thread != std::this_thread::get_id()) return AETHER_ERROR_INVALID_STATE;
    auto r = c->lifetime->owner;
    if (!r) return AETHER_ERROR_DISPOSED;
    if (c->committed || r->exhausted || c->base.instance_id != r->lifetime->instance ||
        c->base.mutation_serial != r->mutation_serial ||
        c->base.status_revision != r->status_revision ||
        c->base.tick_error != r->tick_error ||
        (require_healthy && r->tick_error != AETHER_OK)) return AETHER_ERROR_INVALID_STATE;
    if (c->changed && r->mutation_serial == UINT64_MAX) return AETHER_ERROR_INVALID_STATE;
    // A DOM disconnect between prepare and commit invalidates the candidate.
    if (stale(*c->state)) return AETHER_ERROR_INVALID_STATE;
    return AETHER_OK;
}
AETHER_API aether_status_t aether_transition_candidate_validate(aether_transition_candidate_t c) {
    return validate_candidate(c, true);
}
static aether_status_t commit_candidate(aether_transition_candidate_t c,
    aether_transition_token_t* out, bool require_healthy) {
    if (out) *out = {};
    auto status = validate_candidate(c, require_healthy);
    if (status != AETHER_OK) return status;
    auto r = c->lifetime->owner;
    if (c->changed) { r->state.swap(c->state); ++r->mutation_serial; }
    c->committed = true;
    if (out) *out = token_of(r);
    return AETHER_OK;
}
AETHER_API aether_status_t aether_transition_candidate_commit(
    aether_transition_candidate_t c, aether_transition_token_t* out) {
    return commit_candidate(c, out, true);
}
AETHER_API void aether_transition_candidate_destroy(aether_transition_candidate_t c) { delete c; }
AETHER_API aether_status_t aether_transition_runtime_observe(aether_transition_runtime_t r,
    aether_node_t node, const aether_computed_style_t* before,
    const aether_computed_style_t* after, int64_t now) {
    const aether_transition_observation_t observation{node, before, after};
    aether_transition_candidate_t c = nullptr;
    auto status = aether_transition_runtime_prepare(r, &observation, 1, nullptr, 0, now, &c);
    if (status == AETHER_OK) status = commit_candidate(c, nullptr, false);
    aether_transition_candidate_destroy(c);
    return status;
}
AETHER_API aether_status_t aether_transition_runtime_try_tick(aether_transition_runtime_t r,
    int64_t now, aether_bool_t* out) {
    if (out) *out = AETHER_FALSE;
    if (!r || !out) return AETHER_ERROR_NULL_ARG;
    if (!on_thread(r)) return AETHER_ERROR_INVALID_STATE;
    if (r->exhausted) return AETHER_ERROR_INVALID_STATE;
    const auto base = token_of(r);
    try {
        const bool changed = !r->state->active.empty() || stale(*r->state);
        if ((changed && r->mutation_serial == UINT64_MAX) ||
            (r->tick_error != AETHER_OK && r->status_revision == UINT64_MAX)) {
            record_tick_error(r, AETHER_ERROR_INVALID_STATE);
            return AETHER_ERROR_INVALID_STATE;
        }
        auto state = changed ? clone(*r->state) : r->state;
        if (changed) { prune(*state); tick_state(state.get(), now); }
        if (r->mutation_serial != base.mutation_serial || r->status_revision != base.status_revision)
            return AETHER_ERROR_INVALID_STATE;
        if (changed) { r->state.swap(state); ++r->mutation_serial; }
        if (base.tick_error != AETHER_OK) { r->tick_error = AETHER_OK; ++r->status_revision; }
        *out = active_state(*r->state) ? AETHER_TRUE : AETHER_FALSE;
        return AETHER_OK;
    } catch (const std::bad_alloc&) {
        record_tick_error(r, AETHER_ERROR_OUT_OF_MEMORY); return AETHER_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        record_tick_error(r, AETHER_ERROR_INVALID_STATE); return AETHER_ERROR_INVALID_STATE;
    }
}
AETHER_API aether_bool_t aether_transition_runtime_tick(aether_transition_runtime_t r, int64_t now) {
    if (!on_thread(r)) return AETHER_FALSE;
    const bool previous = active_state(*r->state);
    aether_bool_t active = AETHER_FALSE;
    return aether_transition_runtime_try_tick(r, now, &active) == AETHER_OK ? active :
        (previous ? AETHER_TRUE : AETHER_FALSE);
}


AETHER_API aether_bool_t aether_transition_runtime_has_active(aether_transition_runtime_t runtime) {
    return on_thread(runtime) && active_state(*runtime->state) ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_transition_runtime_opacity_override(
    aether_transition_runtime_t runtime, aether_node_t node, float* out_opacity) {
    if (!on_thread(runtime) || !node || !out_opacity) return AETHER_FALSE;
    for (const auto& transition : runtime->state->active) {
        if (transition.claim.valid() && transition.node == node &&
            transition.property == AETHER_TRANSITION_PROPERTY_OPACITY) {
            *out_opacity = transition.current_opacity;
            return AETHER_TRUE;
        }
    }
    for (const auto& transition : runtime->state->finished) {
        if (transition.claim.valid() && transition.node == node &&
            transition.property == AETHER_TRANSITION_PROPERTY_OPACITY) {
            *out_opacity = transition.opacity;
            return AETHER_TRUE;
        }
    }
    return AETHER_FALSE;
}

AETHER_API aether_bool_t aether_transition_runtime_transform_override(
    aether_transition_runtime_t runtime, aether_node_t node, aether_transform_matrix* out_matrix) {
    if (!on_thread(runtime) || !node || !out_matrix) return AETHER_FALSE;
    for (const auto& transition : runtime->state->active) {
        if (transition.claim.valid() && transition.node == node &&
            transition.property == AETHER_TRANSITION_PROPERTY_TRANSFORM) {
            *out_matrix = transition.current_matrix;
            return AETHER_TRUE;
        }
    }
    for (const auto& transition : runtime->state->finished) {
        if (transition.claim.valid() && transition.node == node &&
            transition.property == AETHER_TRANSITION_PROPERTY_TRANSFORM) {
            *out_matrix = transition.matrix;
            return AETHER_TRUE;
        }
    }
    return AETHER_FALSE;
}

} // extern "C"

std::unordered_set<aether_node_t> aether_active_transform_owners(aether_transition_runtime_t runtime) {
    std::unordered_set<aether_node_t> owners;
    if (on_thread(runtime)) for (const auto& entry : runtime->state->active) {
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
        if (entry.claim.valid() && entry.property == AETHER_TRANSITION_PROPERTY_TRANSFORM) owners.insert(entry.node);
    }
    return owners;
}

void aether_transition_test_fail_after(int64_t allocations) noexcept { allocation_countdown = allocations; }

void aether_transition_test_set_counters(aether_transition_runtime_t r,
    uint64_t mutation, uint64_t status) noexcept {
    if (on_thread(r)) { r->mutation_serial = mutation; r->status_revision = status; }
}

// Readers consume exactly the pin published with the transactional vectors.
aether::style::ContextOwnersRef aether_active_transform_snapshot(aether_transition_runtime_t runtime) noexcept {
    return on_thread(runtime) ? runtime->state->owners : aether::style::ContextOwnersRef{};
}
