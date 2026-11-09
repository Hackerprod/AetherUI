#include <aether/aether_contextual_transitions.h>
#include "../css/computed_style_handle.h"
#include "../layout/operation_pin.h"
#include "../dom/node_identity.h"
#include <array>
#include <vector>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
namespace {

    // Closed 2D affine representation. Linear coefficients are constant;
    // each translation is constant + width coefficient + height coefficient.
    // Origin is deliberately excluded and applied once when sampling a box.
    using ContextualAffine = std::array<double, 10>;
    // a, b, c, d, tx_constant, tx_width, tx_height, ty_constant, ty_width, ty_height
    constexpr int32_t kTransformInvalidation = AETHER_VISUAL_INVALIDATION_LAYOUT |
        AETHER_VISUAL_INVALIDATION_DISPLAY_LIST;
    constexpr int32_t kOpacityInvalidation = AETHER_VISUAL_INVALIDATION_DISPLAY_LIST;
    ContextualAffine identity() {
        return { 1, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
    }
    void require_finite(double x) {
        if (!std::isfinite(x)
            || std::abs(x) > std::numeric_limits<float>::max()) throw std::invalid_argument("nonrepresentable transform");
    }
    void require_finite(const ContextualAffine& f) {
        for (double v : f) require_finite(v);
    }
    std::array<double, 2> computed_length_parts(aether_css_length x) {
        require_finite(x.value);
        if (x.unit == AETHER_CSS_UNIT_PX) return { x.value, 0 };
        if (x.unit == AETHER_CSS_UNIT_PERCENT) return { 0, x.value / 100.0 };
        if (x.unit == AETHER_CSS_UNIT_LINEAR && x.linear_em == 0 && x.linear_rem == 0 && x.linear_vw == 0 && x.linear_vh == 0) {
            require_finite(x.linear_percent);
            return { x.value, x.linear_percent / 100.0 };
        }
        throw std::invalid_argument("uncomputed transform computed_length_parts");
    }
    ContextualAffine compose(const ContextualAffine& a, const ContextualAffine& b) {
        ContextualAffine r { a[0] * b[0] + a[1] * b[2], a[0] * b[1] + a[1] * b[3], a[2] * b[0] + a[3] * b[2], a[2] * b[1] + a[3] * b[3] };
        for (int i = 0; i < 3; ++i) {
            r[4 + i] = a[4 + i] * b[0] + a[7 + i] * b[2] + b[4 + i];
            r[7 + i] = a[4 + i] * b[1] + a[7 + i] * b[3] + b[7 + i];
        }
        require_finite(r);
        return r;
    }
    ContextualAffine operation_form(const aether_transform_operation& o) {
        ContextualAffine f = identity();
        switch (o.kind) {
            case AETHER_TRANSFORM_TRANSLATE:
                { auto x = computed_length_parts(o.x), y = computed_length_parts(o.y);
            f[4] = x[0];
            f[5] = x[1];
            f[7] = y[0];
            f[9] = y[1];
            break;
            }
            case AETHER_TRANSFORM_SCALE:
                f[0] = o.scalar_x;
            f[3] = o.scalar_y;
            break;
            case AETHER_TRANSFORM_ROTATE:
                f[0] = f[3] = std::cos(o.angle_radians);
            f[1] = std::sin(o.angle_radians);
            f[2] = - f[1];
            break;
            case AETHER_TRANSFORM_SKEW_X:
                f[2] = std::tan(o.angle_radians);
            break;
            case AETHER_TRANSFORM_SKEW_Y:
                f[1] = std::tan(o.angle_radians);
            break;
            case AETHER_TRANSFORM_MATRIX:
                f = { o.matrix_a, o.matrix_b, o.matrix_c, o.matrix_d, o.matrix_e, 0, 0, o.matrix_f, 0, 0 };
            break;
            default:
                throw std::invalid_argument("unsupported operation_form");
        }
        require_finite(f);
        return f;
    }
    // An interrupted incompatible list owns only its ten coefficients, never
    // prior endpoints or revisions. Compatible lists keep typed operations.
    struct Endpoint {
        std::vector<aether_transform_operation> ops;
        ContextualAffine form = identity();
        bool canonical = false;
    };
    // Two genuine read-only ranges: public C storage is contiguous, frozen
    // storage is not. No borrowed public flat or pretend contiguous array.
    class TransformReadInputs {
        const aether_transform_operation* raw_ = nullptr;
        int32_t raw_count_ = 0;
        aether::style::ReadOnlyArrayView<aether_transform_operation> frozen_;
        bool is_frozen_ = false;
    public:
        explicit TransformReadInputs(const aether_transform_list& list) noexcept
            : raw_(list.items), raw_count_(list.count) {}
        explicit TransformReadInputs(aether::style::ReadOnlyArrayView<aether_transform_operation> list) noexcept
            : frozen_(list), is_frozen_(true) {}
        size_t checked_size() const {
            if (!is_frozen_) {
                if (raw_count_ < 0 || (raw_count_ && !raw_)) throw std::invalid_argument("list");
                return static_cast<size_t>(raw_count_);
            }
            // Preserve the representability checks of the former C exporter.
            if (frozen_.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
                frozen_.size() > std::numeric_limits<size_t>::max() / sizeof(aether_transform_operation))
                throw std::invalid_argument("list size");
            return frozen_.size();
        }
        const aether_transform_operation& operator[](size_t i) const noexcept {
            return is_frozen_ ? frozen_[i] : raw_[i];
        }
    };
    struct SampleReadInputs {
        float opacity;
        aether_transform_origin origin;
        TransformReadInputs transforms;
        uint64_t document_identity, node_identity;
    };
    SampleReadInputs sample_inputs(const aether_computed_style_handle_s& handle) noexcept {
        return {handle.style.opacity, handle.style.transform_origin, TransformReadInputs(handle.style.transforms),
            handle.document_identity, handle.node_identity};
    }
    SampleReadInputs sample_inputs(aether::style::ComputedStyleReadView style,
        uint64_t document_identity, uint64_t node_identity) noexcept {
        return {style.scalars().opacity, style.scalars().transform_origin, TransformReadInputs(style.transforms()),
            document_identity, node_identity};
    }
    SampleReadInputs sample_inputs(const aether::style::MapVersion& binding,
        const aether::style::BoundNodeView& record) {
        if (!record) throw std::invalid_argument("sample record");
        const auto member = binding.lookup(record.payload_owner()->node());
        const auto same_owner = [](const auto& a, const auto& b) noexcept {
            return a == b && !a.owner_before(b) && !b.owner_before(a);
        };
        if (!member || !same_owner(member.payload_owner(), record.payload_owner()) ||
            !same_owner(member.document_owner(), record.document_owner()) ||
            !same_owner(member.revision_owner(), record.revision_owner()) ||
            !same_owner(member.legacy_record(), record.legacy_record()))
            throw std::invalid_argument("sample record owner");
        // Global identity comes from the captured binding, not from a payload
        // which might have survived in a partial legacy map with no global ID.
        return sample_inputs(record.style_view(), binding.document_identity(), record.payload_owner()->node_identity());
    }
    Endpoint copy_endpoint(const TransformReadInputs& transforms) {
        Endpoint e;
        const size_t count = transforms.checked_size();
        e.ops.reserve(count);
        for (size_t i = 0; i < count; ++i) e.ops.push_back(transforms[i]);
        for (auto i = e.ops.rbegin(); i != e.ops.rend(); ++i) e.form = compose(e.form, operation_form(* i));
        return e;
    }
    Endpoint copy_endpoint(const aether_computed_style_t& s) {
        return copy_endpoint(TransformReadInputs(s.transforms));
    }
    ContextualAffine blend_coefficients(ContextualAffine a, const ContextualAffine& b, double p) {
        for (size_t i = 0; i < a.size(); ++i) a[i] = a[i] *(1 - p) + b[i] * p;
        require_finite(a);
        return a;
    }
    bool compatible_operations(const Endpoint& a, const Endpoint& b) {
        if (a.canonical || b.canonical || a.ops.size() != b.ops.size()) return false;
        for (size_t i = 0; i < a.ops.size(); ++i) if (a.ops[i].kind != b.ops[i].kind) return false;
        return true;
    }
    aether_css_length mix_length(aether_css_length a, aether_css_length b, double p) {
        auto x = computed_length_parts(a), y = computed_length_parts(b);
        aether_css_length r {};
        r.unit = AETHER_CSS_UNIT_LINEAR;
        r.value = float(x[0] *(1 - p) + y[0] * p);
        r.linear_percent = float((x[1] *(1 - p) + y[1] * p) * 100);
        return r;
    }
    Endpoint interpolate_endpoint(const Endpoint& a, const Endpoint& b, double p) {
        if (p <= 0) return a;
        if (p >= 1) return b;
        Endpoint e;
        if (!compatible_operations(a, b)) {
            e.canonical = true;
            e.form = blend_coefficients(a.form, b.form, p);
            return e;
        }
        e.ops = a.ops;
        for (size_t i = 0; i < e.ops.size(); ++i) {
            auto& o = e.ops[i];
            const auto& t = b.ops[i];
            if (o.kind == AETHER_TRANSFORM_TRANSLATE) {
                o.x = mix_length(o.x, t.x, p);
                o.y = mix_length(o.y, t.y, p);
            }
            auto mix = [p](float x, float y) {
                return float(x *(1 - p) + y * p);
            };
            o.scalar_x = mix(o.scalar_x, t.scalar_x);
            o.scalar_y = mix(o.scalar_y, t.scalar_y);
            o.angle_radians = mix(o.angle_radians, t.angle_radians);
            o.matrix_a = mix(o.matrix_a, t.matrix_a);
            o.matrix_b = mix(o.matrix_b, t.matrix_b);
            o.matrix_c = mix(o.matrix_c, t.matrix_c);
            o.matrix_d = mix(o.matrix_d, t.matrix_d);
            o.matrix_e = mix(o.matrix_e, t.matrix_e);
            o.matrix_f = mix(o.matrix_f, t.matrix_f);
        }
        for (auto i = e.ops.rbegin(); i != e.ops.rend(); ++i) e.form = compose(e.form, operation_form(* i));
        return e;
    }
    double apply_timing(int timing, double progress) {
        if (progress <= 0) return 0;
        if (progress >= 1) return 1;
        double x1 = 0, y1 = 0, x2 = 1, y2 = 1;
        switch (timing) {
        case AETHER_TRANSITION_TIMING_EASE:
            x1 = .25; y1 = .1; x2 = .25;
            break;
        case AETHER_TRANSITION_TIMING_EASE_IN:
            x1 = .42;
            break;
        case AETHER_TRANSITION_TIMING_EASE_OUT:
            x2 = .58;
            break;
        case AETHER_TRANSITION_TIMING_EASE_IN_OUT:
            x1 = .42; x2 = .58;
            break;
        default:
            return progress;
        }
        double lower = 0, upper = 1, parameter = progress;
        for (int iteration = 0; iteration < 40; ++iteration) {
            const double inverse = 1 - parameter;
            const double x = 3 * inverse * inverse * parameter * x1 +
                3 * inverse * parameter * parameter * x2 + parameter * parameter * parameter;
            if (x < progress) lower = parameter;
            else upper = parameter;
            parameter = (lower + upper) / 2;
        }
        const double inverse = 1 - parameter;
        return 3 * inverse * inverse * parameter * y1 +
            3 * inverse * parameter * parameter * y2 + parameter * parameter * parameter;
    }

    struct Channel {
        bool active = false;
        double start = 0, duration = 1;
        int timing = 0;
        Endpoint from, to;
        double opacity_from = 1, opacity_to = 1;
    };
    struct Entry {
        uint64_t doc = 0, node = 0;
        aether_node_t pointer = nullptr;
        std::shared_ptr<node_identity_s> lifetime, document_lifetime;
        Channel transform, opacity;
    };
    double progress(const Channel& c, int64_t t) {
        return std::clamp((double(t) - c.start) / c.duration, 0.0, 1.0);
    }
    double eased_progress(const Channel& c, int64_t t) {
        return apply_timing(c.timing, progress(c, t));
    }
    struct State {
        std::vector<Entry> entries;
        uint64_t consumed = 0;
        int64_t event_time = std::numeric_limits<int64_t>::min();
    };
}
// Published state is immutable. Prepare deep-copies it; commit only assigns a
// shared pointer. A retained batch therefore survives future commits/runtime death.
struct aether_contextual_transition_runtime_s {
    std::shared_ptr<State> state = std::make_shared<State>();
    uint64_t generation = 0;
};
struct aether_contextual_transition_batch_s {
    std::shared_ptr<State> state;
    uint64_t generation = 0;
    int64_t clock = 0;
    int32_t invalidation = 0;
    bool committed = false;
    aether_contextual_transition_runtime_t owner = nullptr;
};
namespace {
    // Caller pins all owners for this synchronous borrow. Neither endpoints nor
    // the result retain a view, handle, map wrapper or operation pin.
    aether_status_t sample_box_read(aether_contextual_transition_batch_t batch,
        const SampleReadInputs& current, aether_rectf box, aether_contextual_transition_sample* out) {
        if (!out) return AETHER_ERROR_NULL_ARG;
        try {
            require_finite(box.x);
            require_finite(box.y);
            require_finite(box.width);
            require_finite(box.height);
            if (box.width < 0 || box.height < 0 || !current.node_identity
                || !current.document_identity) throw std::invalid_argument("box identity");
            auto e = copy_endpoint(current.transforms);
            double opacity = current.opacity;
            bool active = false, has = !e.ops.empty();
            if (batch) for (const auto& v : batch->state->entries) if (v.doc == current.document_identity && v.node == current.node_identity) {
                if (v.transform.active) {
                    e = interpolate_endpoint(v.transform.from, v.transform.to, eased_progress(v.transform, batch->clock));
                    has = true;
                    active = true;
                }
                if (v.opacity.active) {
                    double p = eased_progress(v.opacity, batch->clock);
                    opacity = v.opacity.opacity_from *(1 - p) + v.opacity.opacity_to * p;
                    active = true;
                }
                break;
            }
            auto ox = computed_length_parts(current.origin.x), oy = computed_length_parts(current.origin.y);
            double x = box.x + ox[0] + ox[1] * box.width, y = box.y + oy[0] + oy[1] * box.height;
            const auto& f = e.form;
            double dx = f[4] + f[5] * box.width + f[6] * box.height + x - x * f[0] - y * f[2],
                dy = f[7] + f[8] * box.width + f[9] * box.height + y - x * f[1] - y * f[3];
            require_finite(dx);
            require_finite(dy);
            require_finite(opacity);
            aether_contextual_transition_sample s { { float(f[0]), float(f[1]), float(f[2]), float(f[3]), float(dx),
                float(dy) }, float(opacity), has ? AETHER_TRUE : AETHER_FALSE, active ? AETHER_TRUE : AETHER_FALSE,
                batch ? batch->clock : 0 };
            *out = s;
            return AETHER_OK;
        }
        catch (const std::bad_alloc&) {
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        catch (...) {
            return AETHER_ERROR_INVALID_STATE;
        }
    }
}
extern "C" {

    uint32_t aether_contextual_transition_capability(void) {
        return 1;
    }
    uint32_t aether_contextual_transition_version(void) {
        return 1;
    }
    aether_bool_t aether_contextual_transition_batch_has_active(aether_contextual_transition_batch_t batch) {
        if (!batch) return AETHER_FALSE;
        for (const auto& e : batch->state->entries) if ((e.transform.active
            || e.opacity.active) && (!e.lifetime || (e.lifetime->alive && e.lifetime->attached && e.document_lifetime
            && e.document_lifetime->alive))) return AETHER_TRUE;
        return AETHER_FALSE;
    }
    aether_status_t aether_contextual_transition_create(aether_contextual_transition_runtime_t *out) {
        if (!out) return AETHER_ERROR_NULL_ARG;
        try {
            auto p = new aether_contextual_transition_runtime_s;
            *out = p;
            return AETHER_OK;
        }
        catch (...) {
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
    }
    void aether_contextual_transition_destroy(aether_contextual_transition_runtime_t runtime) {
        delete runtime;
    }
    void aether_contextual_transition_batch_destroy(aether_contextual_transition_batch_t batch) {
        delete batch;
    }
    aether_status_t aether_contextual_transition_prepare(aether_contextual_transition_runtime_t runtime,
        const aether_contextual_transition_event * events, size_t count, int64_t clock,
        aether_contextual_transition_batch_t *out) {
        if (out) *out = nullptr;
        if (!runtime || !out || (count && !events)) return AETHER_ERROR_NULL_ARG;
        try {
            // No effect reaches the published runtime until explicit commit.
            auto batch = std::make_unique<aether_contextual_transition_batch_s>();
            batch->state = std::make_shared<State>(*runtime->state);
            batch->generation = runtime->generation;
            batch->clock = clock;
            batch->owner = runtime;
            for (size_t i = 0; i < count; ++i) {
                const auto& e = events[i];
                // Replay of a durable command is idempotent. New commands must
                // preserve event time/order even when presentation was delayed.
                if (!e.event_id) throw std::invalid_argument("zero event identity");
                if (e.event_id <= runtime->state->consumed) continue;
                if (e.event_id <= batch->state->consumed) throw std::invalid_argument("unordered new events");
                if (!e.before || !e.after || !e.node
                    || e.change_timestamp_ms < batch->state->event_time
                    || e.change_timestamp_ms > clock) throw std::invalid_argument("event order");
                if (!e.after->node_identity || !e.after->document_identity
                    || e.before->node_identity != e.after->node_identity
                    || e.before->document_identity != e.after->document_identity) throw std::invalid_argument("identity");
                const auto& token = e.after->node_lifetime;
                if (!token || !e.before->node_lifetime || token != e.before->node_lifetime || !token->alive
                    || !token->attached || token->owner != e.node || !e.after->document_lifetime
                    || !e.after->document_lifetime->alive
                    || e.before->document_lifetime != e.after->document_lifetime) throw std::invalid_argument("node provenance");
                auto it = std::find_if(batch->state->entries.begin(), batch->state->entries.end(), [&](const Entry& x) {
                    return x.doc == e.after->document_identity && x.node == e.after->node_identity;
                });
                if (it == batch->state->entries.end()) {
                    batch->state->entries.push_back({ e.after->document_identity, e.after->node_identity, e.node,
                        e.after->node_lifetime, e.after->document_lifetime, {}, {} });
                    it = std::prev(batch->state->entries.end());
                }
                const auto&before = e.before->style;
                const auto&after = e.after->style;
                require_finite(after.transition_duration);
                require_finite(after.transition_delay);
                require_finite(after.opacity);
                require_finite(before.opacity);
                auto from = copy_endpoint(before), to = copy_endpoint(after);
                bool changed = !aether_transform_list_equal(&before.transforms, &after.transforms);
                const auto setup = [&](Channel& c) {
                    c.active = true;
                    c.start = double(e.change_timestamp_ms) + double(after.transition_delay) * 1000;
                    c.duration = std::max(double(after.transition_duration) * 1000, 1.0);
                    c.timing = after.transition_timing;
                    require_finite(c.start);
                    require_finite(c.duration);
                };
                if (!(after.transition_properties & AETHER_TRANSITION_PROPERTY_TRANSFORM) || after.transition_duration <= 0) {
                    if (it->transform.active) batch->invalidation |= kTransformInvalidation;
                    it->transform = Channel{};
                }
                else if (changed) {
                    if (it->transform.active) from = interpolate_endpoint(it->transform.from, it->transform.to,
                        eased_progress(it->transform, e.change_timestamp_ms));
                    setup(it->transform);
                    it->transform.from = std::move(from);
                    it->transform.to = std::move(to);
                    batch->invalidation |= kTransformInvalidation;
                }
                if (!(after.transition_properties & AETHER_TRANSITION_PROPERTY_OPACITY) || after.transition_duration <= 0) {
                    if (it->opacity.active) batch->invalidation |= kOpacityInvalidation;
                    it->opacity = Channel{};
                }
                else if (before.opacity != after.opacity) {
                    double x = before.opacity;
                    if (it->opacity.active) {
                        double p = eased_progress(it->opacity, e.change_timestamp_ms);
                        x = it->opacity.opacity_from *(1 - p) + it->opacity.opacity_to * p;
                    }
                    setup(it->opacity);
                    it->opacity.opacity_from = x;
                    it->opacity.opacity_to = after.opacity;
                    batch->invalidation |= kOpacityInvalidation;
                }
                batch->state->consumed = e.event_id;
                batch->state->event_time = e.change_timestamp_ms;
            }
            for (auto& e : batch->state->entries) {
                if (e.lifetime && (!e.lifetime->alive || !e.lifetime->attached || !e.document_lifetime
                    || !e.document_lifetime->alive)) {
                    if (e.transform.active) batch->invalidation |= kTransformInvalidation;
                    if (e.opacity.active) batch->invalidation |= kOpacityInvalidation;
                    e.transform = Channel{};
                    e.opacity = Channel{};
                }
                if (e.transform.active) {
                    batch->invalidation |= kTransformInvalidation;
                    if (progress(e.transform, clock) >= 1) e.transform = Channel{};
                }
                if (e.opacity.active) {
                    batch->invalidation |= kOpacityInvalidation;
                    if (progress(e.opacity, clock) >= 1) e.opacity = Channel{};
                }
            }
            batch->state->entries.erase(std::remove_if(batch->state->entries.begin(), batch->state->entries.end(), [](const Entry& e) {
                return !e.transform.active && !e.opacity.active;
            }), batch->state->entries.end());
            *out = batch.release();
            return AETHER_OK;
        }
        catch (const std::bad_alloc&) {
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        catch (...) {
            return AETHER_ERROR_INVALID_STATE;
        }
    }
    aether_status_t aether_contextual_transition_commit(aether_contextual_transition_runtime_t runtime,
        aether_contextual_transition_batch_t batch) {
        if (!runtime || !batch) return AETHER_ERROR_NULL_ARG;
        if (batch->owner != runtime || batch->committed || batch->generation != runtime->generation) return AETHER_ERROR_INVALID_STATE;
        // No allocation, callback, or fallible work is allowed after this point.
        runtime->state = batch->state;
        ++runtime->generation;
        batch->committed = true;
        return AETHER_OK;
    }
    int32_t aether_contextual_transition_invalidation(aether_contextual_transition_batch_t batch) {
        return batch ? batch->invalidation : 0;
    }
    aether_bool_t aether_contextual_transition_active_transform(aether_contextual_transition_batch_t batch, aether_node_t node) {
        if (!batch) return AETHER_FALSE;
        for (const auto& e : batch->state->entries) if (e.pointer == node
            && e.transform.active && (!e.lifetime || (e.lifetime->alive && e.lifetime->attached && e.document_lifetime
            && e.document_lifetime->alive))) return AETHER_TRUE;
        return AETHER_FALSE;
    }
    aether_status_t aether_contextual_transition_sample_box(aether_contextual_transition_batch_t batch,
        aether_computed_style_handle_t current, aether_rectf box, aether_contextual_transition_sample *out) {
        if (!current || !out) return AETHER_ERROR_NULL_ARG;
        return sample_box_read(batch, sample_inputs(*current), box, out);
    }
}
#include "../layout/layout_types.h"
#include "../layout/layout_internal.h"
extern "C" {

    aether_status_t aether_contextual_transition_prepare_layout(aether_layout_engine_t engine,
        aether_contextual_transition_batch_t batch) {
        if (!engine || !batch) return AETHER_ERROR_NULL_ARG;
        if (batch->committed) return AETHER_ERROR_INVALID_STATE;
        try {
            std::unordered_set < aether_node_s * > nodes;
            for (const auto& e : batch->state->entries) if (e.transform.active && e.lifetime && e.lifetime->alive
                && e.lifetime->attached && e.document_lifetime && e.document_lifetime->alive) nodes.insert(e.pointer);
            engine->prepared_active_nodes.swap(nodes);
            engine->prepared_active_snapshot.reset();
            engine->prepared_context_owners.reset();
            engine->prepared_context_map.reset();
            engine->transition_context_prepared = true;
            return AETHER_OK;
        }
        catch (...) {
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
    }
    aether_status_t aether_contextual_transition_finalize_layout(aether_layout_result_t layout,
        aether_contextual_transition_batch_t batch) {
        if (!layout) return AETHER_ERROR_NULL_ARG;
        // Even a failed/rolled-back presentation attempt invalidates prior hits.
        layout->invalidate_hit_acceleration();
        aether::layout::LayoutOperationPin geometry_pin(layout);
        aether::layout::StyleOperationPin style_pin(layout->assets, {}, false);
        if (!style_pin.structure_valid()) return AETHER_ERROR_INVALID_STATE;
        try {
            struct Sample {
                aether_layout_box_s * box;
                aether_contextual_transition_sample value;
            };
            std::vector<Sample> samples;
            samples.reserve(layout->paint_order.size());
            for (auto * box : layout->paint_order) {
                if (!box->element) continue;
                if (box->element->anonymous_flex_owner || box->kind == LAYOUT_BOX_TEXT) {
                    samples.push_back({ box, { { 1, 0, 0, 1, 0, 0 }, 1, AETHER_FALSE, AETHER_FALSE, batch ? batch->clock : 0 } });
                    continue;
                }
                const auto record = style_pin.lookup().lookup_record_owner(box->element);
                if (!record) continue;
                const auto& binding = style_pin.selected_version();
                if (!binding) return AETHER_ERROR_INVALID_STATE;
                const auto inputs = sample_inputs(*binding, record);
                aether_contextual_transition_sample value {};
                auto status = sample_box_read(batch, inputs, box->border_box, &value);
                if (status != AETHER_OK) return status;
                if (box->kind != LAYOUT_BOX_ELEMENT) {
                    value.has_transform = AETHER_FALSE;
                    value.matrix = { 1, 0, 0, 1, 0, 0 };
                }
                samples.push_back({ box, value });
            }
            struct Previous {
                aether_layout_box_s * box;
                bool sample_valid, transform;
                aether_transform_matrix matrix;
                float opacity;
                int64_t timestamp;
                std::weak_ptr<node_identity_s> identity;
                aether_scroll_metrics_t scroll;
            };
            // Samples and all rollback storage are allocated before mutation.
            // Overflow may still reject numeric overflow; restore the candidate.
            struct Rollback {
                std::vector<Previous> previous;
                bool done = false;
                ~Rollback() {
                    if (!done) for (const auto& old : previous) {
                        auto * box = old.box;
                        box->visual_sample_valid = old.sample_valid;
                        box->visual_has_transform = old.transform;
                        box->visual_transform = old.matrix;
                        box->visual_opacity = old.opacity;
                        box->visual_timestamp = old.timestamp;
                        box->visual_identity = old.identity;
                        box->scroll = old.scroll;
                    }
                }
            }
            rollback;
            rollback.previous.reserve(layout->paint_order.size());
            for (auto * box : layout->paint_order) rollback.previous.push_back({ box, box->visual_sample_valid,
                box->visual_has_transform, box->visual_transform, box->visual_opacity, box->visual_timestamp,
                box->visual_identity, box->scroll });
            for (const auto& s : samples) {
                auto * b = s.box;
                b->visual_sample_valid = true;
                b->visual_has_transform = s.value.has_transform;
                b->visual_transform = s.value.matrix;
                b->visual_opacity = s.value.opacity;
                b->visual_timestamp = s.value.presentation_timestamp_ms;
                b->visual_identity = b->element->identity;
            }
            for (auto * box : layout->paint_order) {
                const bool x = aether_overflow_is_scroll_container(layout_scroll_overflow_axis(layout, box, true)),
                    y = aether_overflow_is_scroll_container(layout_scroll_overflow_axis(layout, box, false));
                if (!x && !y) continue;
                const auto& content = box->content_box;
                require_finite(content.x);
                require_finite(content.y);
                require_finite(content.width);
                require_finite(content.height);
                const double initial_right = double(content.x) + content.width;
                const double initial_bottom = double(content.y) + content.height;
                require_finite(initial_right);
                require_finite(initial_bottom);
                float right = float(initial_right), bottom = float(initial_bottom);
                accumulate_scrollable_descendant_extent(box, x, y, right, bottom);
                require_finite(right);
                require_finite(bottom);
                // Validate the raw result before clamping: max(0, NaN) would
                // otherwise turn invalid geometry into an apparently valid zero.
                if (x) {
                    const double overflow = double(right) - content.x - content.width;
                    require_finite(overflow);
                    box->scroll.scrollable_overflow_x = float(std::max(0.0, overflow));
                }
                if (y) {
                    const double height = layout->root == box ? layout->viewport.height : content.height;
                    require_finite(height);
                    const double overflow = double(bottom) - content.y - height;
                    require_finite(overflow);
                    box->scroll.scrollable_overflow_y = float(std::max(0.0, overflow));
                }
                require_finite(box->scroll.scrollable_overflow_x);
                require_finite(box->scroll.scrollable_overflow_y);
            }
            rollback.done = true;
            return AETHER_OK;
        }
        catch (const std::bad_alloc&) {
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        catch (...) {
            return AETHER_ERROR_INVALID_STATE;
        }
    }
}
