// Include the authentic final validator without exporting a test-only entrypoint.
// This target compiles the remaining core sources directly, never the core DSO.
#include "../src/layout/layout_engine.cpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::abort(); } } while (false)

static_assert(!std::is_copy_constructible_v<aether_layout_box_s>);
static_assert(!std::is_copy_assignable_v<aether_layout_box_s>);
static_assert(!std::is_move_constructible_v<aether_layout_box_s>);
static_assert(!std::is_move_assignable_v<aether_layout_box_s>);
static_assert(!std::is_copy_constructible_v<aether_layout_result_s>);
static_assert(!std::is_copy_assignable_v<aether_layout_result_s>);
static_assert(!std::is_move_constructible_v<aether_layout_result_s>);
static_assert(!std::is_move_assignable_v<aether_layout_result_s>);
static_assert(!std::is_copy_constructible_v<LayoutBoxRegistry>);
static_assert(!std::is_copy_assignable_v<LayoutBoxRegistry>);
static_assert(!std::is_move_constructible_v<LayoutBoxRegistry>);
static_assert(!std::is_move_assignable_v<LayoutBoxRegistry>);
static_assert(std::is_nothrow_destructible_v<aether_layout_box_s>);
static_assert(std::is_nothrow_destructible_v<aether_layout_result_s>);
static_assert(noexcept(std::declval<const LayoutBoxRegistry&>().begin()));
static_assert(noexcept(std::declval<const LayoutBoxRegistry&>().end()));
static_assert(noexcept(std::declval<const LayoutBoxRegistry&>().empty()));
static_assert(noexcept(++std::declval<LayoutBoxRegistry::iterator&>()));
static_assert(noexcept(*std::declval<LayoutBoxRegistry::iterator&>()));
static_assert(noexcept(std::declval<aether_layout_result_s&>().unregister_box(nullptr)));

// Delimited C++ allocation ledger. No assertion/logging/container allocates on
// successful paths. This does NOT instrument direct malloc/calloc/realloc.
struct Allocation { void* pointer; size_t bytes; };
static Allocation ledger[32768]{};
static bool tracking = false, injected = false;
static long fail_at = -1, calls = 0;
static size_t live_allocations = 0, live_bytes = 0;
static aether_layout_result_s* observed_result = nullptr;
static aether_layout_box_s* observed_parent = nullptr;
static aether_layout_box_s* observed_child = nullptr;
static aether_layout_box_s* watched_boxes[16]{};
static unsigned watched_deletes[16]{};
static bool child_observed = false;
static aether_layout_result_s* final_result = nullptr;

static size_t count_boxes(const aether_layout_result_s& result) {
    size_t count = 0;
    for (auto* box : result.allocated_boxes) {
        CHECK(box->owner == &result);
        CHECK(++count <= result.live_box_count);
    }
    CHECK(count == result.live_box_count);
    return count;
}
static bool contains(const aether_layout_result_s& result, const void* pointer) {
    for (auto* box : result.allocated_boxes) if (box == pointer) return true;
    return false;
}
static void* allocate(size_t size) {
    if (tracking) {
        const long ordinal = calls++;
        if (ordinal == fail_at) { injected = true; throw std::bad_alloc(); }
    }
    void* pointer = std::malloc(size ? size : 1);
    if (!pointer) throw std::bad_alloc();
    if (tracking) {
        bool recorded = false;
        for (auto& slot : ledger) if (!slot.pointer) {
            slot = {pointer, size}; ++live_allocations; live_bytes += size;
            recorded = true; break;
        }
        CHECK(recorded);
    }
    return pointer;
}
static void deallocate(void* pointer) noexcept {
    if (!pointer) return;
    if (tracking) {
        for (size_t i = 0; i < 16; ++i) if (watched_boxes[i] == pointer) {
            ++watched_deletes[i];
            if (final_result) {
                // Box lifetime has ended; inspect only the still-live result
                // members and remaining registry, never the reclaimed box.
                CHECK(final_result->paint_order.empty());
                CHECK(final_result->primary_boxes.empty());
                CHECK(!contains(*final_result, pointer));
                count_boxes(*final_result);
            }
        }
        if (pointer == observed_child) {
            CHECK(observed_result && !contains(*observed_result, observed_parent));
            CHECK(!contains(*observed_result, observed_child));
            CHECK(count_boxes(*observed_result) == 0);
            CHECK(observed_result->paint_order.empty());
            CHECK(observed_result->primary_boxes.empty());
            child_observed = true;
        }
        for (auto& slot : ledger) if (slot.pointer == pointer) {
            --live_allocations; live_bytes -= slot.bytes; slot = {}; break;
        }
    }
    std::free(pointer);
}
void* operator new(size_t size) { return allocate(size); }
void* operator new[](size_t size) { return allocate(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { try { return allocate(size); } catch (...) { return nullptr; } }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { try { return allocate(size); } catch (...) { return nullptr; } }
void operator delete(void* pointer) noexcept { deallocate(pointer); }
void operator delete[](void* pointer) noexcept { deallocate(pointer); }
void operator delete(void* pointer, size_t) noexcept { deallocate(pointer); }
void operator delete[](void* pointer, size_t) noexcept { deallocate(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { deallocate(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { deallocate(pointer); }

static void membership_and_allocations() {
    aether_layout_box_s unowned;
    CHECK(!unowned.owner);
    aether_layout_result_s first, second;
    aether_layout_box_s* boxes[64]{};
    const size_t before_count = live_allocations, before_bytes = live_bytes;
    calls = 0;
    for (auto& box : boxes) box = first.create_box();
    CHECK(calls == 64 && count_boxes(first) == 64);
    CHECK(live_allocations == before_count + 64);
    CHECK(live_bytes == before_bytes + 64 * sizeof(aether_layout_box_s));
    auto* other = second.create_box();
    CHECK(count_boxes(second) == 1 && !contains(first, other));
    // Newest=head, oldest=tail, interior=middle. Repeated unlink must leave
    // traversal intact and never touch another result.
    delete boxes[63]; boxes[63] = nullptr;
    delete boxes[0]; boxes[0] = nullptr;
    delete boxes[31]; boxes[31] = nullptr;
    CHECK(count_boxes(first) == 61 && count_boxes(second) == 1);
    calls = 0; fail_at = 0; injected = false;
    bool failed = false;
    try { (void)first.create_box(); } catch (const std::bad_alloc&) { failed = true; }
    fail_at = -1;
    CHECK(failed && injected && count_boxes(first) == 61);
    calls = 0; fail_at = 1; injected = false;
    auto* retry = first.create_box();
    fail_at = -1;
    CHECK(!injected && calls == 1 && count_boxes(first) == 62);
    calls = 0; fail_at = 0; injected = false;
    delete retry;
    for (auto* box : boxes) delete box;
    delete other;
    fail_at = -1;
    CHECK(!injected && calls == 0 && first.allocated_boxes.empty() && second.allocated_boxes.empty());
    CHECK(count_boxes(first) == 0 && count_boxes(second) == 0);
    CHECK(live_allocations == before_count && live_bytes == before_bytes);
}

static void unlink_before_children() {
    aether_layout_result_s result;
    auto* parent = result.create_box(); auto* child = result.create_box();
    aether_node_s node;
    parent->element = child->element = &node;
    parent->children.push_back(child);
    result.primary_boxes.emplace(&node, parent);
    result.paint_order = {parent, child};
    observed_result = &result; observed_parent = parent; observed_child = child;
    child_observed = false;
    delete parent;
    CHECK(child_observed && count_boxes(result) == 0);
    observed_result = nullptr; observed_parent = observed_child = nullptr;
}

static void disconnected_abandonment(bool with_root) {
    const auto before_count = live_allocations, before_bytes = live_bytes;
    {
        aether_layout_result_s result;
        result.abandoned = true;
        for (size_t i = 0; i < 6; ++i) {
            watched_boxes[i] = result.create_box(); watched_deletes[i] = 0;
        }
        if (with_root) result.root = watched_boxes[0];
        watched_boxes[0]->children = {watched_boxes[1], watched_boxes[2]};
        watched_boxes[3]->children = {watched_boxes[4], watched_boxes[2]};
        aether_node_s nodes[2];
        watched_boxes[0]->element = &nodes[0];
        watched_boxes[3]->element = &nodes[1];
        result.primary_boxes = {{&nodes[0], watched_boxes[0]}, {&nodes[1], watched_boxes[3]}};
        result.paint_order = {watched_boxes[0], watched_boxes[1], watched_boxes[3], watched_boxes[5]};
        final_result = &result;
        // A duplicate edge and isolated box are safe only with two-phase cleanup.
        CHECK(count_boxes(result) == 6);
        calls = 0; fail_at = 0; injected = false;
    }
    final_result = nullptr;
    fail_at = -1;
    CHECK(calls == 0 && !injected);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(watched_deletes[i] == 1); watched_boxes[i] = nullptr;
    }
    CHECK(live_allocations == before_count && live_bytes == before_bytes);
}

// Deallocation is after each box destructor, not its entry. The wide tree
// ensures later siblings remain indexed in the unoptimized implementation.
static void final_release_with_pins() {
    aether_node_s nodes[6];
    const auto before_count = live_allocations, before_bytes = live_bytes;
    auto* result = new aether_layout_result_s();
    for (size_t i = 0; i < 6; ++i) {
        auto* box = result->create_box();
        watched_boxes[i] = box; watched_deletes[i] = 0;
        box->element = &nodes[i]; box->border_box = {float(i), 2, 30, 40};
        if (i) result->root->children.push_back(box); else result->root = box;
        result->paint_order.push_back(box); result->primary_boxes.emplace(&nodes[i], box);
    }
    {
        LayoutOperationPin outer(result);
        {
            LayoutOperationPin inner(result);
            aether_layout_result_destroy(result); // Release external ownership exactly once.
            CHECK(result->references == 2);
            for (size_t i = 0; i < 6; ++i) {
                CHECK(!watched_deletes[i]);
                CHECK(result->paint_order[i] == watched_boxes[i]);
                CHECK(result->find_primary_box(&nodes[i]) == watched_boxes[i]);
                CHECK(watched_boxes[i]->border_box.x == float(i));
                CHECK(watched_boxes[i]->border_box.y == 2 && watched_boxes[i]->border_box.width == 30 &&
                    watched_boxes[i]->border_box.height == 40);
            }
            CHECK(result->root == watched_boxes[0] && count_boxes(*result) == 6);
        }
        CHECK(result->references == 1 && result->paint_order.size() == 6 && count_boxes(*result) == 6);
        for (size_t i = 0; i < 6; ++i) CHECK(!watched_deletes[i]);
        final_result = result;
        calls = 0; fail_at = 0; injected = false;
    }
    // No access to result or boxes after the last pin. Addresses are only
    // compared by the ledger while the owning result's members are alive.
    final_result = nullptr; fail_at = -1;
    CHECK(calls == 0 && !injected);
    for (size_t i = 0; i < 6; ++i) {
        CHECK(watched_deletes[i] == 1); watched_boxes[i] = nullptr;
    }
    CHECK(live_allocations == before_count && live_bytes == before_bytes);
}

static void disconnected_validation() {
    aether_layout_result_s result;
    result.abandoned = true;
    result.root = result.create_box();
    auto* detached = result.create_box();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    float* checked[] = {&detached->border_box.x, &detached->padding_box.width,
        &detached->content_box.height, &detached->margin_box.y,
        &detached->scroll.scrollable_overflow_x, &detached->scroll.scrollable_overflow_y,
        &detached->scroll.scroll_offset_x, &detached->scroll.scroll_offset_y,
        &detached->visual_opacity, &detached->used_border.left, &detached->used_border.top,
        &detached->used_border.right, &detached->used_border.bottom,
        &detached->visual_transform.m11, &detached->visual_transform.m12,
        &detached->visual_transform.m21, &detached->visual_transform.m22,
        &detached->visual_transform.dx, &detached->visual_transform.dy};
    detached->visual_sample_valid = true;
    for (auto* field : checked) {
        const float saved = *field;
        *field = nan;
        bool rejected = false;
        try { validate_used_geometry(&result); } catch (const std::overflow_error&) { rejected = true; }
        CHECK(rejected);
        *field = saved;
        validate_used_geometry(&result);
    }
    detached->margin_box.width = -1; // Negative margin remains legal.
    validate_used_geometry(&result);
    detached->border_box.width = -1;
    bool rejected = false;
    try { validate_used_geometry(&result); } catch (const std::overflow_error&) { rejected = true; }
    CHECK(rejected);
}

static void clone_oom_and_independence() {
    aether_layout_result_s source;
    aether_node_s nodes[3];
    source.root = source.create_box();
    source.root->text.assign(256, 'r');
    for (size_t i = 0; i < 2; ++i) {
        auto* child = source.create_box(); source.root->children.push_back(child);
        child->element = &nodes[i]; child->text.assign(128, char('a' + i));
        source.primary_boxes.emplace(child->element, child);
        source.paint_order.push_back(child);
    }
    const auto original_order = source.paint_order;
    const auto original_primary = source.primary_boxes;
    const auto original_children = source.root->children;
    const auto before_count = live_allocations, before_bytes = live_bytes;
    bool complete = false;
    long failures = 0;
    for (long ordinal = 0; ordinal < 256; ++ordinal) {
        auto* output = &source;
        calls = 0; injected = false; fail_at = ordinal;
        const auto status = aether_layout_clone_snapshot(&source, &output);
        fail_at = -1;
        if (status == AETHER_OK) {
            CHECK(!injected && output != &source && count_boxes(*output) == 3);
            for (auto* box : output->allocated_boxes) CHECK(!contains(source, box));
            CHECK(output->paint_order[0] == output->root->children[0]);
            CHECK(output->primary_boxes.at(&nodes[0]) == output->root->children[0]);
            aether_layout_result_destroy(output); complete = true;
        } else {
            CHECK(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == &source);
            ++failures;
        }
        CHECK(source.paint_order == original_order && source.primary_boxes == original_primary &&
            source.root->children == original_children);
        CHECK(count_boxes(source) == 3 && source.root->text.size() == 256 &&
            std::all_of(source.root->text.begin(), source.root->text.end(), [](char c) { return c == 'r'; }));
        CHECK(source.root->children[0]->text.size() == 128 && source.paint_order.size() == 2);
        CHECK(live_allocations == before_count && live_bytes == before_bytes);
        // Every failed allocation permits a fresh successful retry, not just
        // the first ordinal beyond the total allocation count.
        if (!complete) {
            output = nullptr;
            CHECK(aether_layout_clone_snapshot(&source, &output) == AETHER_OK);
            CHECK(count_boxes(*output) == 3);
            aether_layout_result_destroy(output);
            CHECK(live_allocations == before_count && live_bytes == before_bytes);
        } else break;
    }
    CHECK(complete && failures > 10);
    std::printf("clone OOM: %ld reachable C++ allocation failures with retry\n", failures);
}

static void clone_stress_plateau() {
    const auto initial_count = live_allocations, initial_bytes = live_bytes;
    {
        aether_layout_result_s source;
        source.root = source.create_box();
        source.root->text.assign(256, 's');
        for (int i = 0; i < 7; ++i) {
            auto* child = source.create_box();
            child->text.assign(128, 'c');
            source.root->children.push_back(child);
            source.paint_order.push_back(child);
        }
        const auto source_count = live_allocations, source_bytes = live_bytes;
        aether_layout_result_s* retained[3]{};
        for (auto& frame : retained) CHECK(aether_layout_clone_snapshot(&source, &frame) == AETHER_OK);
        const auto plateau_count = live_allocations, plateau_bytes = live_bytes;
        for (size_t iteration = 0; iteration < 4096; ++iteration) {
            const size_t slot = (iteration * 7 + iteration / 3) % 3;
            aether_layout_result_destroy(retained[slot]); retained[slot] = nullptr;
            // Clone from another clone to exercise direct asset sharing and
            // independent links, rather than only cloning the original.
            auto* from = iteration % 2 ? retained[(slot + 1) % 3] : &source;
            CHECK(aether_layout_clone_snapshot(from, &retained[slot]) == AETHER_OK);
            CHECK(count_boxes(source) == 8);
            for (auto* frame : retained) {
                CHECK(count_boxes(*frame) == 8 && frame->paint_order.size() == 7);
                CHECK(frame->root->text.size() == 256 && frame->root->text.front() == 's');
                for (auto* box : frame->allocated_boxes) {
                    CHECK(!contains(source, box));
                    for (auto* other : retained) if (other != frame) CHECK(!contains(*other, box));
                }
                for (size_t i = 0; i < 7; ++i) CHECK(frame->paint_order[i] == frame->root->children[i]);
            }
            CHECK(live_allocations == plateau_count && live_bytes == plateau_bytes);
        }
        // Different release order from creation, returning exactly to source.
        for (auto index : {2, 0, 1}) aether_layout_result_destroy(retained[index]);
        CHECK(live_allocations == source_count && live_bytes == source_bytes);
    }
    CHECK(live_allocations == initial_count && live_bytes == initial_bytes);
    std::puts("stress: 4096 replacements, 3 retained clones, exact allocation/byte plateau PASS");
}

int main() {
    tracking = true;
    membership_and_allocations();
    unlink_before_children();
    disconnected_abandonment(false);
    disconnected_abandonment(true);
    final_release_with_pins();
    disconnected_validation();
    clone_oom_and_independence();
    clone_stress_plateau();
    CHECK(live_allocations == 0 && live_bytes == 0);
    tracking = false;
    std::puts("intrusive registry: membership, allocation, unlink, disconnected validation/cleanup, clone OOM PASS");
}
