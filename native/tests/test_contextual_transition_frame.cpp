#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_transitions.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cstring>
#include <initializer_list>

static void require_test(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "requirement failed at line %d\n", line);
        std::abort();
    }
}
#define REQUIRE(condition) require_test(bool(condition), __LINE__)


static int fail_allocation_after = -1;
void* operator new(std::size_t size) {
    if (fail_allocation_after == 0) throw std::bad_alloc();
    if (fail_allocation_after > 0) --fail_allocation_after;
    if (void* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

// Route nothrow and array allocations through the same fault allocator. This
// keeps ASan allocation families matched instead of suppressing its checks.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }


static void equal(float actual, float expected) {
    if (std::abs(actual - expected) > .01f) {
        std::fprintf(stderr, "expected %f, got %f\n", expected, actual);
        std::abort();
    }
}

int main() {
    aether_doc_t document{};
    REQUIRE(aether_doc_create(&document) == AETHER_OK);
    REQUIRE(aether_doc_parse_html(document,
        "<!doctype html><html><body><div id='parent'><div id='child'><div id='fixed'></div></div></div></body></html>") == AETHER_OK);
    auto root = aether_doc_get_document_element(document);
    auto parent = aether_doc_get_element_by_id(document, "parent");
    auto child = aether_doc_get_element_by_id(document, "child");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    aether_style_runtime_t styles{};
    REQUIRE(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &styles) == AETHER_OK);
    int64_t sheet{};
    REQUIRE(aether_style_runtime_add_stylesheet(styles,
        "*{display:block;margin:0;padding:0}#parent{width:100px;height:100px;overflow:auto}"
        "#fixed{display:none}#child{width:100px;height:30px;position:relative;z-index:1;transform:translateX(0%);background:red;transition:transform 1s linear}",
        0, "frame", &sheet, nullptr) == AETHER_OK);
    auto revision = [&]() {
        aether_style_transaction_t transaction{};
        aether_style_revision_t result{};
        REQUIRE(aether_style_runtime_prepare_revision(styles, root, &environment, &transaction) == AETHER_OK);
        REQUIRE(aether_style_runtime_commit_revision(styles, transaction, &result) == AETHER_OK);
        aether_style_transaction_destroy(transaction);
        return result;
    };
    auto before_revision = revision();
    aether_style_map_t before_map{};
    REQUIRE(aether_style_revision_create_map(before_revision, &before_map) == AETHER_OK);
    auto before = aether_style_map_get(before_map, child);
    REQUIRE(before);
    REQUIRE(aether_element_set_attribute(child, "style", "transform:translateX(150%)") == AETHER_OK);
    auto after_revision = revision();
    aether_style_map_t after_map{};
    REQUIRE(aether_style_revision_create_map(after_revision, &after_map) == AETHER_OK);
    auto after = aether_style_map_get(after_map, child);
    REQUIRE(after);

    aether_contextual_transition_runtime_t transitions{};
    REQUIRE(aether_contextual_transition_create(&transitions) == AETHER_OK);
    aether_contextual_transition_event event{1, 0, child, before, after};
    aether_contextual_transition_batch_t batch{};
    REQUIRE(aether_contextual_transition_prepare(transitions, &event, 1, 500, &batch) == AETHER_OK);
    aether_layout_engine_t engine{};
    REQUIRE(aether_layout_create(nullptr, nullptr, &engine) == AETHER_OK);
    REQUIRE(aether_contextual_transition_prepare_layout(engine, batch) == AETHER_OK);
    aether_layout_result_t layout{};
    REQUIRE(aether_layout_layout(engine, document, after_map, 800, 600, nullptr, nullptr, &layout) == AETHER_OK);
    REQUIRE(aether_contextual_transition_finalize_layout(layout, batch) == AETHER_OK);
    const auto child_box = aether_layout_result_find_primary_box(layout, child);
    const auto parent_box = aether_layout_result_find_primary_box(layout, parent);
    auto bounds = aether_layout_box_get_border_box(child_box);
    equal(bounds.x, 0);
    equal(bounds.width, 100);
    equal(aether_layout_box_get_scrollable_overflow_x(parent_box), 75);
    REQUIRE(aether_layout_result_hit_test(layout, 90, 10) == child);
    REQUIRE(aether_layout_result_hit_test(layout, 30, 10) != child);
    aether_input_hit_part_t part{};
    aether_node_t owner{};
    REQUIRE(aether_layout_result_hit_test_input(layout, nullptr, 90, 10, nullptr, nullptr, &part, &owner) == AETHER_OK);
    REQUIRE(owner == child);

    // Stacking-order allocation failure never crosses the C ABI.
    part = AETHER_INPUT_HIT_CONTENT;
    owner = child;
    fail_allocation_after = 0;
    const auto input_failure = aether_layout_result_hit_test_input(
        layout, nullptr, 90, 10, nullptr, nullptr, &part, &owner);
    fail_allocation_after = -1;
    REQUIRE(input_failure == AETHER_ERROR_OUT_OF_MEMORY);
    REQUIRE(part == AETHER_INPUT_HIT_NONE && owner == nullptr);
    fail_allocation_after = 0;
    const auto raw_failure = aether_layout_result_hit_test(layout, 90, 10);
    fail_allocation_after = -1;
    REQUIRE(raw_failure == nullptr);
    fail_allocation_after = 0;
    const auto styled_failure = aether_layout_result_hit_test_with_styles_and_scroll_offsets(
        layout, after_map, 90, 10, nullptr, nullptr);
    fail_allocation_after = -1;
    REQUIRE(styled_failure == nullptr);

    aether_display_list_builder_t builder{};
    REQUIRE(aether_display_list_builder_create(nullptr, nullptr, &builder) == AETHER_OK);
    aether_display_list_t list{};
    REQUIRE(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK);
    bool found_transform = false;
    for (int32_t i = 0; i < aether_display_list_get_count(list); ++i) {
        aether_paint_command_t command{};
        REQUIRE(aether_display_list_get_command(list, i, &command) == AETHER_OK);
        if (command.tag == AETHER_PAINT_PUSH_TRANSFORM) {
            equal(command.data.push_transform.matrix.dx, 75);
            found_transform = true;
        }
        aether_paint_command_free(&command);
    }
    REQUIRE(found_transform);
    REQUIRE(aether_contextual_transition_commit(transitions, batch) == AETHER_OK);
    aether_contextual_transition_sample sample{};
    REQUIRE(aether_contextual_transition_sample_box(batch, after, bounds, &sample) == AETHER_OK);
    equal(sample.matrix.dx, 75);
    REQUIRE(sample.presentation_timestamp_ms == 500);

    // Frame preparation can fail after style commit. Neither old samples nor
    // overflow are changed, and retry uses the original event start/deadline.
    aether_contextual_transition_batch_t next_batch{};
    REQUIRE(aether_contextual_transition_prepare(transitions, &event, 1, 750, &next_batch) == AETHER_OK);
    int failures = 0, successes = 0;
    for (int allocation = 0; allocation < 100; ++allocation) {
        fail_allocation_after = allocation;
        const auto status = aether_contextual_transition_finalize_layout(layout, next_batch);
        fail_allocation_after = -1;
        if (status == AETHER_ERROR_OUT_OF_MEMORY) {
            ++failures;
            equal(aether_layout_box_get_scrollable_overflow_x(parent_box), 75);
            REQUIRE(aether_layout_result_hit_test(layout, 90, 10) == child);
        } else {
            REQUIRE(status == AETHER_OK);
            ++successes;
            equal(aether_layout_box_get_scrollable_overflow_x(parent_box), 112.5f);
            REQUIRE(aether_contextual_transition_finalize_layout(layout, batch) == AETHER_OK);
        }
    }
    REQUIRE(failures > 3 && successes > 0);
    aether_contextual_transition_batch_destroy(next_batch);

    // A rotated AABB is not an input hit. Singular matrices are non-hittable.
    for (const char* transform : {"transform:rotate(45deg);transform-origin:0 0", "transform:scale(0)"}) {
        REQUIRE(aether_element_set_attribute(child, "style", transform) == AETHER_OK);
        auto static_revision = revision();
        aether_style_map_t static_map{};
        REQUIRE(aether_style_revision_create_map(static_revision, &static_map) == AETHER_OK);
        aether_layout_result_t static_layout{};
        REQUIRE(aether_layout_layout(engine, document, static_map, 800, 600, nullptr, nullptr, &static_layout) == AETHER_OK);
        REQUIRE(aether_layout_result_hit_test(static_layout, 65, 5) != child);
        // Cross-revision style overrides are rejected before paint or input.
        REQUIRE(aether_layout_result_hit_test_with_styles_and_scroll_offsets(
            layout, static_map, 90, 10, nullptr, nullptr) == nullptr);
        part = AETHER_INPUT_HIT_CONTENT;
        owner = child;
        REQUIRE(aether_layout_result_hit_test_input(layout, static_map, 90, 10,
            nullptr, nullptr, &part, &owner) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(part == AETHER_INPUT_HIT_NONE && owner == nullptr);
        aether_display_list_t rejected_list{};
        REQUIRE(aether_display_list_build_with_styles(builder, layout, static_map,
            nullptr, &rejected_list) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(rejected_list == nullptr);
        // Old frame retains its own transform and overflow after new style commit.
        equal(aether_layout_box_get_scrollable_overflow_x(parent_box), 75);
        REQUIRE(aether_layout_result_hit_test(layout, 90, 10) == child);
        aether_layout_result_destroy(static_layout);
        aether_style_map_destroy(static_map);
        aether_style_revision_destroy(static_revision);
    }

    // Actual computed handles, rather than manually supplied px endpoints,
    // preserve their document viewport/root bases through transition completion.
    REQUIRE(aether_style_runtime_add_stylesheet(styles, "html{font-size:32px}",
        0, "root-font", &sheet, nullptr) == AETHER_OK);
    for (const auto* transform : {"transform:translateX(10vw)", "transform:translateX(1rem)"}) {
        REQUIRE(aether_element_set_attribute(child, "style", "transform:translateX(0px)") == AETHER_OK);
        auto start_revision = revision();
        aether_style_map_t start_map{};
        REQUIRE(aether_style_revision_create_map(start_revision, &start_map) == AETHER_OK);
        auto start_style = aether_style_map_get(start_map, child);
        REQUIRE(aether_element_set_attribute(child, "style", transform) == AETHER_OK);
        auto end_revision = revision();
        aether_style_map_t end_map{};
        REQUIRE(aether_style_revision_create_map(end_revision, &end_map) == AETHER_OK);
        auto end_style = aether_style_map_get(end_map, child);
        aether_contextual_transition_runtime_t unit_runtime{};
        REQUIRE(aether_contextual_transition_create(&unit_runtime) == AETHER_OK);
        aether_contextual_transition_event unit_event{1, 0, child, start_style, end_style};
        aether_contextual_transition_batch_t unit_batch{};
        REQUIRE(aether_contextual_transition_prepare(unit_runtime, &unit_event, 1, 1000, &unit_batch) == AETHER_OK);
        aether_contextual_transition_sample unit_sample{};
        REQUIRE(aether_contextual_transition_sample_box(unit_batch, end_style, {0, 0, 200, 50}, &unit_sample) == AETHER_OK);
        equal(unit_sample.matrix.dx, std::strstr(transform, "vw") != nullptr ? 80 : 32);
        aether_contextual_transition_batch_destroy(unit_batch);
        aether_contextual_transition_destroy(unit_runtime);
        aether_computed_style_handle_destroy(start_style);
        aether_computed_style_handle_destroy(end_style);
        aether_style_map_destroy(start_map);
        aether_style_map_destroy(end_map);
        aether_style_revision_destroy(start_revision);
        aether_style_revision_destroy(end_revision);
    }

    // Transform presence must establish the fixed containing block during
    // delay, even when the current static endpoint is transform:none.
    auto fixed = aether_doc_get_element_by_id(document, "fixed");
    REQUIRE(aether_element_set_attribute(fixed, "style",
        "display:block;position:fixed;left:0;top:0;width:20px;height:20px") == AETHER_OK);
    REQUIRE(aether_element_set_attribute(child, "style",
        "margin-left:40px;transform:translateX(0px);transition-delay:1s") == AETHER_OK);
    auto delay_before_revision = revision();
    aether_style_map_t delay_before_map{};
    REQUIRE(aether_style_revision_create_map(delay_before_revision, &delay_before_map) == AETHER_OK);
    auto delay_before = aether_style_map_get(delay_before_map, child);
    REQUIRE(aether_element_set_attribute(child, "style",
        "margin-left:40px;transform:none;transition-delay:1s") == AETHER_OK);
    auto delay_after_revision = revision();
    aether_style_map_t delay_after_map{};
    REQUIRE(aether_style_revision_create_map(delay_after_revision, &delay_after_map) == AETHER_OK);
    auto delay_after = aether_style_map_get(delay_after_map, child);
    aether_contextual_transition_runtime_t delay_runtime{};
    REQUIRE(aether_contextual_transition_create(&delay_runtime) == AETHER_OK);
    aether_contextual_transition_event delay_event{1, 0, child, delay_before, delay_after};
    aether_contextual_transition_batch_t delay_batch{};
    REQUIRE(aether_contextual_transition_prepare(delay_runtime, &delay_event, 1, 500, &delay_batch) == AETHER_OK);
    REQUIRE(aether_contextual_transition_active_transform(delay_batch, child) == AETHER_TRUE);
    REQUIRE(aether_contextual_transition_prepare_layout(engine, delay_batch) == AETHER_OK);
    aether_layout_result_t delay_layout{};
    REQUIRE(aether_layout_layout(engine, document, delay_after_map, 800, 600, nullptr, nullptr, &delay_layout) == AETHER_OK);
    REQUIRE(aether_contextual_transition_finalize_layout(delay_layout, delay_batch) == AETHER_OK);
    const auto fixed_during_delay = aether_layout_result_find_primary_box(delay_layout, fixed);
    equal(aether_layout_box_get_border_box(fixed_during_delay).x, 40);
    aether_layout_result_destroy(delay_layout);
    aether_contextual_transition_batch_destroy(delay_batch);
    REQUIRE(aether_contextual_transition_prepare(delay_runtime, &delay_event, 1, 2001, &delay_batch) == AETHER_OK);
    REQUIRE(aether_contextual_transition_active_transform(delay_batch, child) == AETHER_FALSE);
    REQUIRE(aether_contextual_transition_prepare_layout(engine, delay_batch) == AETHER_OK);
    REQUIRE(aether_layout_layout(engine, document, delay_after_map, 800, 600, nullptr, nullptr, &delay_layout) == AETHER_OK);
    equal(aether_layout_box_get_border_box(aether_layout_result_find_primary_box(delay_layout, fixed)).x, 0);
    aether_layout_result_destroy(delay_layout);
    aether_contextual_transition_batch_destroy(delay_batch);
    aether_contextual_transition_destroy(delay_runtime);
    aether_computed_style_handle_destroy(delay_before);
    aether_computed_style_handle_destroy(delay_after);
    aether_style_map_destroy(delay_before_map);
    aether_style_map_destroy(delay_after_map);
    aether_style_revision_destroy(delay_before_revision);
    aether_style_revision_destroy(delay_after_revision);

    // Replacing the DOM inside a user offset callback must abort immediately,
    // before traversing any of the frame's now-retired raw element pointers.
    struct MutationContext { aether_doc_t document; bool called = false; } mutation{document};
    const auto replace_document = [](aether_node_t, void* raw) -> aether_scroll_offset_t {
        auto& context = *static_cast<MutationContext*>(raw);
        context.called = true;
        REQUIRE(aether_doc_parse_html(context.document,
            "<!doctype html><html><body>replacement</body></html>") == AETHER_OK);
        return {};
    };
    part = AETHER_INPUT_HIT_CONTENT;
    owner = child;
    REQUIRE(aether_layout_result_hit_test_input(layout, nullptr, 90, 10,
        replace_document, &mutation, &part, &owner) == AETHER_ERROR_INVALID_STATE);
    REQUIRE(mutation.called && part == AETHER_INPUT_HIT_NONE && owner == nullptr);

    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_contextual_transition_batch_destroy(batch);
    aether_contextual_transition_destroy(transitions);
    aether_computed_style_handle_destroy(before);
    aether_computed_style_handle_destroy(after);
    aether_style_map_destroy(before_map);
    aether_style_map_destroy(after_map);
    aether_style_revision_destroy(before_revision);
    aether_style_revision_destroy(after_revision);
    aether_style_runtime_destroy(styles);
    aether_doc_destroy(document);
    std::puts("contextual transition frame: shared paint/input/overflow sample PASS");
}
