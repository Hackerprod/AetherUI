#include <aether/aether_transition_sync.h>
#include "animations/transition_runtime.h"
#include "dom/dom_types.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <thread>

static aether_transition_token_t token(aether_transition_runtime_t r) {
    aether_transition_token_t t{};
    assert(aether_transition_runtime_read_token(r, &t) == AETHER_OK);
    return t;
}
static float opacity(aether_transition_runtime_t r, aether_node_t n) {
    float f = -1;
    assert(aether_transition_runtime_opacity_override(r, n, &f));
    return f;
}
int main() {
    aether_node_s node, other;
    node.type = other.type = AETHER_NODE_ELEMENT;
    auto before = aether_computed_style_default();
    auto after = before;
    after.opacity = 0;
    after.transition_properties = AETHER_TRANSITION_PROPERTY_OPACITY;
    after.transition_duration = 1;
    after.transition_timing = AETHER_TRANSITION_TIMING_LINEAR;
    aether_transition_runtime_t r = nullptr;
    assert(aether_transition_runtime_create(&r) == AETHER_OK);
    assert(aether_transition_runtime_observe(r, &node, &before, &after, 0) == AETHER_OK);
    assert(aether_transition_runtime_tick(r, 500));
    assert(std::fabs(opacity(r, &node) - .5f) < .001f);
    auto initial = token(r);
    aether_transition_candidate_t candidate = nullptr;
    // Empty preparation allocates only the candidate, not an entry/state clone.
    aether_transition_test_fail_after(1);
    assert(aether_transition_runtime_prepare(r, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    aether_transition_test_fail_after(-1);
    assert(aether_transition_candidate_commit(candidate, nullptr) == AETHER_OK);
    assert(token(r).mutation_serial == initial.mutation_serial);
    aether_transition_candidate_destroy(candidate);
    // Every deterministic allocation boundary of Observe leaves live samples unchanged.
    const aether_transition_observation_t observation{&other, &before, &after};
    bool succeeded = false;
    for (int i = 0; i < 16; ++i) {
        aether_transition_test_fail_after(i);
        auto status = aether_transition_runtime_prepare(r, &observation, 1, nullptr, 0, 0, &candidate);
        aether_transition_test_fail_after(-1);
        assert(token(r).mutation_serial == initial.mutation_serial);
        assert(opacity(r, &node) == .5f);
        if (status == AETHER_OK) { aether_transition_candidate_destroy(candidate); succeeded = true; break; }
        assert(status == AETHER_ERROR_OUT_OF_MEMORY && candidate == nullptr);
    }
    assert(succeeded);
    assert(aether_transition_runtime_prepare(r, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    // Tick allocation failure is sticky, preserves activity/samples and invalidates candidate.
    aether_transition_test_fail_after(0);
    assert(aether_transition_runtime_tick(r, 1000));
    aether_transition_test_fail_after(-1);
    auto failed = token(r);
    assert(failed.tick_error == AETHER_ERROR_OUT_OF_MEMORY);
    assert(failed.status_revision > initial.status_revision);
    assert(failed.mutation_serial == initial.mutation_serial && opacity(r, &node) == .5f);
    assert(aether_transition_candidate_commit(candidate, nullptr) == AETHER_ERROR_INVALID_STATE);
    // Unrelated successful Observe does not acknowledge the full-sample error.
    assert(aether_transition_runtime_observe(r, &other, &before, &after, 0) == AETHER_OK);
    assert(token(r).tick_error == failed.tick_error && token(r).status_revision == failed.status_revision);
    aether_bool_t active = 99;
    assert(aether_transition_runtime_try_tick(r, 1000, &active) == AETHER_OK && !active);
    assert(token(r).tick_error == AETHER_OK && token(r).status_revision > failed.status_revision);
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_INVALID_STATE);
    aether_transition_candidate_destroy(candidate);
    assert(opacity(r, &node) == 0); // finished endpoint survives inactive tick.
    // Finished state survives a candidate unrelated to that node.
    assert(aether_transition_runtime_observe(r, &other, &after, &before, 1500) == AETHER_OK);
    assert(opacity(r, &node) == 0);
    assert(aether_transition_runtime_prepare(r, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    ++node.identity->disconnect_serial; // remove+reattach cannot revive the endpoint.
    float output = 42;
    assert(!aether_transition_runtime_opacity_override(r, &node, &output) && output == 42);
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_INVALID_STATE);
    aether_transition_candidate_destroy(candidate);
    assert(aether_transition_runtime_prepare(r, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    assert(aether_transition_candidate_commit(candidate, nullptr) == AETHER_OK);
    aether_transition_candidate_destroy(candidate);
    // Owner-thread checks are pure and cannot mutate metadata or consume staging.
    assert(aether_transition_runtime_prepare(r, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    std::thread wrong([&] {
        aether_transition_token_t t{};
        assert(aether_transition_runtime_read_token(r, &t) == AETHER_ERROR_INVALID_STATE);
        assert(aether_transition_candidate_commit(candidate, nullptr) == AETHER_ERROR_INVALID_STATE);
    });
    wrong.join();
    aether_transition_runtime_destroy(r);
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_DISPOSED);
    // A new instance cannot validate a candidate retained across destruction/address reuse.
    aether_transition_runtime_t replacement = nullptr;
    assert(aether_transition_runtime_create(&replacement) == AETHER_OK);
    assert(token(replacement).instance_id != initial.instance_id);
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_DISPOSED);
    aether_transition_candidate_destroy(candidate);
    // Sweep every Tick staging boundary, including active-to-finished insertion.
    assert(aether_transition_runtime_observe(replacement, &other, &before, &after, 0) == AETHER_OK);
    const auto mutation_before_tick = token(replacement).mutation_serial;
    bool tick_succeeded = false;
    for (int i = 0; i < 16; ++i) {
        aether_transition_test_fail_after(i);
        active = 99;
        auto status = aether_transition_runtime_try_tick(replacement, 1000, &active);
        aether_transition_test_fail_after(-1);
        if (status == AETHER_OK) { assert(!active); tick_succeeded = true; break; }
        assert(status == AETHER_ERROR_OUT_OF_MEMORY && !active);
        assert(token(replacement).mutation_serial == mutation_before_tick);
        assert(opacity(replacement, &other) == 1);
        assert(aether_transition_runtime_has_active(replacement));
    }
    assert(tick_succeeded && opacity(replacement, &other) == 0);
    // Factory storage is physically pinned after logical document retirement.
    aether_doc_t doc = nullptr;
    assert(aether_doc_create(&doc) == AETHER_OK);
    assert(aether_doc_parse_html(doc, "<div id='leased'></div>") == AETHER_OK);
    auto leased = aether_doc_get_element_by_id(doc, "leased");
    assert(leased && aether_editable_lease(leased));
    std::weak_ptr<EditableNodeStorage> storage = aether_editable_lease(leased);
    assert(aether_transition_runtime_observe(replacement, leased, &before, &after, 0) == AETHER_OK);
    assert(aether_transition_runtime_prepare(replacement, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    aether_doc_destroy(doc);
    assert(!storage.expired());
    assert(!aether_transition_runtime_has_active(replacement));
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_INVALID_STATE);
    aether_transition_candidate_destroy(candidate);
    assert(aether_transition_runtime_try_tick(replacement, 1000, &active) == AETHER_OK);
    assert(storage.expired());
    // Counter exhaustion never wraps or resurrects an old certificate.
    assert(aether_transition_runtime_observe(replacement, &other, &before, &after, 0) == AETHER_OK);
    aether_transition_test_set_counters(replacement, UINT64_MAX, UINT64_MAX);
    assert(aether_transition_runtime_prepare(replacement, nullptr, 0, nullptr, 0, 0, &candidate) == AETHER_OK);
    assert(aether_transition_runtime_try_tick(replacement, 500, &active) == AETHER_ERROR_INVALID_STATE);
    aether_transition_token_t exhausted{};
    assert(aether_transition_runtime_read_token(replacement, &exhausted) == AETHER_ERROR_INVALID_STATE);
    assert(exhausted.mutation_serial == UINT64_MAX && exhausted.status_revision == UINT64_MAX);
    assert(exhausted.tick_error == AETHER_ERROR_INVALID_STATE);
    assert(aether_transition_candidate_validate(candidate) == AETHER_ERROR_INVALID_STATE);
    aether_transition_candidate_destroy(candidate);
    aether_transition_runtime_destroy(replacement);
    return 0;
}
