#include "transition_runtime.h"
#include "../rendering/transform_matrix.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>
#include "../dom/dom_types.h"

namespace {

struct active_transition_s {
    aether_node_t node = nullptr;
    std::shared_ptr<node_identity_s> identity;
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
    aether_node_t node = nullptr;
    std::shared_ptr<node_identity_s> identity;
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

/* The runtime handle type is deliberately defined at global scope: it is the
   C ABI's opaque object, and the header only forward-declares it. The helper
   entry structs it stores live in the anonymous namespace above. */
struct aether_transition_runtime_s {
    std::vector<active_transition_s> active;
    std::vector<finished_transition_s> finished;
    aether::style::ContextOwnersRef owners;
    bool owners_certified=true;
};

extern "C" {

AETHER_API aether_status_t aether_transition_runtime_create(aether_transition_runtime_t* out_runtime) {
    if (!out_runtime) return AETHER_ERROR_NULL_ARG;
    auto* runtime = new (std::nothrow) aether_transition_runtime_s();
    if (!runtime) return AETHER_ERROR_OUT_OF_MEMORY;
    try { runtime->owners=aether::style::publish_context_owners({},{}); }
    catch(const std::bad_alloc&) { delete runtime; return AETHER_ERROR_OUT_OF_MEMORY; }
    *out_runtime = runtime;
    return AETHER_OK;
}

AETHER_API void aether_transition_runtime_destroy(aether_transition_runtime_t runtime) {
    delete runtime;
}

AETHER_API aether_status_t aether_transition_runtime_observe(
    aether_transition_runtime_t runtime,
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
                if (entry.node != node || entry.identity!=node->identity || entry.property != AETHER_TRANSITION_PROPERTY_TRANSFORM)
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

    aether::style::ContextOwnersRef next_owners;
    try {
        if (runtime->owners_certified) {
            bool active_after=false;
            if (transform_eligible && !transform_changed) {
                for(const auto& entry:runtime->active) {
                    aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
                    if(entry.node==node && entry.identity==node->identity && entry.property==AETHER_TRANSITION_PROPERTY_TRANSFORM) active_after=true;
                }
            } else if (transform_changed && !matrix_equal(transform_from,transform_to)) {
                active_transition_s candidate{}; candidate.observed_ms=now_ms; candidate.delay_ms=delay_ms; candidate.duration_ms=duration_ms;
                active_after=transition_elapsed_ms(candidate,now_ms)<static_cast<uint64_t>(duration_ms);
            }
            auto index=aether::style::update_context_owner(runtime->owners->owners,
                {node,node->identity,static_cast<uint8_t>(active_after?aether::style::ActiveTransform:0)});
            next_owners=aether::style::publish_context_owners(runtime->owners,std::move(index));
        }
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
    // Legacy Observe is not jointly transactional. Invalidate certification if
    // a later legacy vector allocation fails, rather than serving stale owners.
    runtime->owners_certified=false;
    try {
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
            if (it->node != node || it->identity!=node->identity || it->property != property) continue;
            sample_transition(*it, now_ms);
            if (opacity) *opacity = it->current_opacity;
            if (matrix) *matrix = it->current_matrix;
            runtime->active.erase(it);
            break;
        }
    };
    const auto install = [&](active_transition_s transition) {
        transition.observed_ms = now_ms;
        transition.delay_ms = delay_ms;
        if (sample_transition(transition, now_ms) >= 1.f) {
            runtime->finished.push_back({node, node->identity, transition.property,
                transition.current_opacity, transition.current_matrix});
        } else {
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
        transition.identity = node->identity;
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
        transition.identity = node->identity;
            transition.property = AETHER_TRANSITION_PROPERTY_TRANSFORM;
            transition.timing = after->transition_timing;
            transition.duration_ms = duration_ms;
            transition.matrix_from = from;
            transition.matrix_to = to;
            install(transition);
        }
    }
    runtime->owners=std::move(next_owners); runtime->owners_certified=bool(runtime->owners);
    return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
}

AETHER_API aether_bool_t aether_transition_runtime_tick(aether_transition_runtime_t runtime, int64_t now_ms) {
    if (!runtime) return AETHER_FALSE;
    aether::style::ContextOwnersRef next_owners;
    try {
        if(runtime->owners_certified) {
            auto index=runtime->owners->owners;
            for(const auto& entry:runtime->active) {
                aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
                if(entry.property==AETHER_TRANSITION_PROPERTY_TRANSFORM &&
                    (!entry.identity->alive || !entry.identity->attached || transition_elapsed_ms(entry,now_ms)>=static_cast<uint64_t>(entry.duration_ms)))
                    index=aether::style::erase_context_owner(std::move(index),reinterpret_cast<uintptr_t>(entry.identity.get()));
            }
            next_owners=aether::style::publish_context_owners(runtime->owners,std::move(index));
        }
    } catch(const std::bad_alloc&) { runtime->owners_certified=false; }
    runtime->owners_certified=false;
    try {
    auto& active = runtime->active;
    for (auto it = active.begin(); it != active.end();) {
        active_transition_s& transition = *it;
        if(!transition.identity->alive || !transition.identity->attached) { it=active.erase(it); continue; }
        const float p = sample_transition(transition, now_ms);
        if (p >= 1.f) {
            finished_transition_s done{};
            done.node = transition.node;
            done.identity = transition.identity;
            done.property = transition.property;
            done.opacity = transition.current_opacity;
            done.matrix = transition.current_matrix;
            runtime->finished.push_back(done);
            it = active.erase(it);
        } else {
            ++it;
        }
    }
    runtime->owners=std::move(next_owners); runtime->owners_certified=bool(runtime->owners);
    return active.empty() ? AETHER_FALSE : AETHER_TRUE;
    } catch(const std::bad_alloc&) { return runtime->active.empty()?AETHER_FALSE:AETHER_TRUE; }
}

AETHER_API aether_bool_t aether_transition_runtime_has_active(aether_transition_runtime_t runtime) {
    return runtime && !runtime->active.empty() ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_transition_runtime_opacity_override(
    aether_transition_runtime_t runtime, aether_node_t node, float* out_opacity) {
    if (!runtime || !node || !out_opacity) return AETHER_FALSE;
    for (const auto& transition : runtime->active) {
        if (transition.node == node && transition.identity->owner==node && transition.identity->alive && transition.identity->attached &&
            transition.property == AETHER_TRANSITION_PROPERTY_OPACITY) {
            *out_opacity = transition.current_opacity;
            return AETHER_TRUE;
        }
    }
    for (const auto& transition : runtime->finished) {
        if (transition.node == node && transition.identity->owner==node && transition.identity->alive && transition.identity->attached &&
            transition.property == AETHER_TRANSITION_PROPERTY_OPACITY) {
            *out_opacity = transition.opacity;
            return AETHER_TRUE;
        }
    }
    return AETHER_FALSE;
}

AETHER_API aether_bool_t aether_transition_runtime_transform_override(
    aether_transition_runtime_t runtime, aether_node_t node, aether_transform_matrix* out_matrix) {
    if (!runtime || !node || !out_matrix) return AETHER_FALSE;
    for (const auto& transition : runtime->active) {
        if (transition.node == node && transition.identity->owner==node && transition.identity->alive && transition.identity->attached &&
            transition.property == AETHER_TRANSITION_PROPERTY_TRANSFORM) {
            *out_matrix = transition.current_matrix;
            return AETHER_TRUE;
        }
    }
    for (const auto& transition : runtime->finished) {
        if (transition.node == node && transition.identity->owner==node && transition.identity->alive && transition.identity->attached &&
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
    if (runtime) for (const auto& entry : runtime->active) {
        aether::diagnostics::count_work(aether::diagnostics::WorkCounter::context_active_visits);
        if (entry.property == AETHER_TRANSITION_PROPERTY_TRANSFORM && entry.identity->alive && entry.identity->attached) owners.insert(entry.node);
    }
    return owners;
}

aether::style::ContextOwnersRef aether_active_transform_snapshot(aether_transition_runtime_t runtime) noexcept {
    return runtime && runtime->owners_certified?runtime->owners:aether::style::ContextOwnersRef{};
}
