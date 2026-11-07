#include "../src/layout/layout_types.h"
#include "../src/layout/input_hit_test.h"
#include "../src/css/style_freeze.h"
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_transitions.h>
#include <cstdio>
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (false)
#include <cstdlib>
#include <new>
#include <iostream>

// This sweep covers C++ allocations in cloning, not the malloc callback ledger.
static long fail_at = -1, allocation_count = 0;
static bool injected = false;
void* operator new(std::size_t n) {
    const auto ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) { injected = true; throw std::bad_alloc(); }
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new(n); } catch (...) { return nullptr; } }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { try { return ::operator new[](n); } catch (...) { return nullptr; } }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace aether::style;
using namespace aether::layout;
static FrozenStyleRef style(float opacity, aether_bool_t visible = AETHER_TRUE) {
    auto source = aether_computed_style_default();
    source.opacity = opacity; source.visible = visible;
    aether_shadow shadow{}; shadow.offset_x = 3.f;
    source.box_shadows = {&shadow, 1};
    FrozenStyleRef result;
    REQUIRE(freeze_style(source, result) == AETHER_OK);
    shadow.offset_x = 999.f;
    REQUIRE(result->view().box_shadows()[0].offset_x == 3.f);
    return result;
}
static FrozenNodeRef record(aether_node_s* node, FrozenStyleRef value) {
    NodeRecordInput input; input.node = node; input.style = std::move(value);
    input.node_lifetime = node->identity; input.node_identity = aether_node_identity(node);
    FrozenNodeRef result; REQUIRE(freeze_node_record(input, result) == AETHER_OK); return result;
}
static MapVersionRef version(std::initializer_list<FrozenNodeRef> records, aether_doc_s* doc = nullptr) {
    BackingBuildInput backing;
    for (const auto& item : records) backing.entries.emplace(item->node(), item);
    MapVersionInput input; REQUIRE(freeze_backing(backing, input.backing) == AETHER_OK);
    if (doc) {
        input.document_lifetime = doc->identity; input.document_identity = aether_node_identity(doc);
        input.structure_epoch = doc->identity->structure_epoch;
        input.input_epoch = doc->identity->mutation_epoch; input.style_tree_epoch = doc->identity->style_tree_epoch;
    }
    MapVersionRef result; REQUIRE(make_map_version(input, result) == AETHER_OK); return result;
}
static aether_layout_result_s* layout(LayoutAssetsRef assets, aether_node_s* node, FrozenStyleRef snapshot) {
    auto* result = new aether_layout_result_s(); result->assets = std::move(assets);
    result->root = result->create_box(); result->root->element = node;
    result->root->style = BoxStyleSnapshot(std::move(snapshot));
    result->root->style.opacity = .25f;
    result->root->border_box = {0, 0, 100, 100};
    result->root->padding_box = result->root->border_box;
    result->root->scroll.scrollable_overflow_x = 40;
    result->root->visual_sample_valid = true;
    result->root->visual_identity = node->identity;
    result->primary_boxes.emplace(node, result->root); result->paint_order.push_back(result->root);
    return result;
}
static void selection_and_clone() {
    aether_node_s node, host; node.type = host.type = AETHER_NODE_ELEMENT;
    const auto base_style = style(.5f), host_style = style(.8f, AETHER_FALSE), snapshot = style(.9f);
    const auto base = version({record(&node, base_style), record(&host, host_style)});
    auto anonymous = std::make_shared<aether_node_s>(); anonymous->type = AETHER_NODE_ELEMENT;
    anonymous->anonymous_flex_owner = &host;
    auto* anonymous_raw = anonymous.get();
    MutableLayoutOverlay builder;
    builder.insert(std::move(anonymous), style(.6f), {});
    const auto frozen = builder.freeze();
    REQUIRE(frozen->ordered_entries().size() == 1 && frozen->ordered_entries()[0]->node() == anonymous_raw);
    auto later = std::make_shared<aether_node_s>(); later->type = AETHER_NODE_ELEMENT;
    auto* later_raw = later.get(); builder.insert(std::move(later), base_style, {});
    const auto later_frozen = builder.freeze();
    REQUIRE(frozen->ordered_entries().size() == 1 && !frozen->find(later_raw));
    REQUIRE(later_frozen->ordered_entries().size() == 2 && later_frozen->ordered_entries()[0]->node() == anonymous_raw &&
        later_frozen->ordered_entries()[1]->node() == later_raw);
    const auto assets = std::make_shared<const LayoutAssets>(base, frozen, base_style);
    const StyleLookup own(assets, {}, false), empty_external(assets, {}, true);
    const auto external_version = version({record(&node, style(.7f)), record(&host, style(.3f))});
    const StyleLookup external(assets, external_version, true);
    auto* original = layout(assets, &node, snapshot);
    const auto& box = *original->root;
    REQUIRE(own.lookup_box(box, BoxStylePolicy::snapshot).scalars().opacity == .25f);
    REQUIRE(own.lookup_box(box, BoxStylePolicy::effective_map).scalars().opacity == .5f);
    REQUIRE(external.lookup_box(box, BoxStylePolicy::effective_map).scalars().opacity == .7f);
    REQUIRE(empty_external.lookup_box(box, BoxStylePolicy::effective_map).scalars().opacity == .25f);
    REQUIRE(!empty_external.lookup_node(&node));
    REQUIRE(own.lookup_record_owner(&node) == base->backing()->find_owner(&node));
    aether_layout_box_s synthetic;
    synthetic.element = anonymous_raw; synthetic.presentation_owner = &host;
    synthetic.style = BoxStyleSnapshot(snapshot); synthetic.style.opacity = .12f;
    REQUIRE(own.lookup_box(synthetic, BoxStylePolicy::snapshot).scalars().opacity == .12f);
    REQUIRE(own.lookup_box(synthetic, BoxStylePolicy::effective_map).scalars().opacity == .6f);
    REQUIRE(external.lookup_box(synthetic, BoxStylePolicy::effective_map).scalars().opacity == .12f);
    REQUIRE(!external.lookup_node(anonymous_raw));
    REQUIRE(!own.lookup_record_owner(anonymous_raw));
    REQUIRE(!own.presentation(synthetic, BoxStylePolicy::effective_map).visible);
    REQUIRE(external.presentation(synthetic, BoxStylePolicy::effective_map).visible);
    REQUIRE(empty_external.presentation(synthetic, BoxStylePolicy::effective_map).visible);
    REQUIRE(own.presentation(synthetic, BoxStylePolicy::snapshot).visible);

    aether_layout_result_t first = nullptr, second = nullptr;
    REQUIRE(aether_layout_clone_snapshot(original, &first) == AETHER_OK);
    REQUIRE(aether_layout_clone_snapshot(first, &second) == AETHER_OK);
    REQUIRE(first->assets == assets && second->assets == assets);
    REQUIRE(first->root != original->root && second->root != first->root);
    first->root->style.opacity = .02f; first->root->border_box.x = 47;
    first->root->visual_opacity = .1f;
    REQUIRE(second->root->style.opacity == .25f && original->root->style.opacity == .25f);
    REQUIRE(second->root->border_box.x == 0 && second->root->visual_opacity == 1);
    REQUIRE(second->root->style.style_owner() == snapshot);
    aether_layout_result_destroy(first); aether_layout_result_destroy(original);
    REQUIRE(second->root->style.view().box_shadows()[0].offset_x == 3);
    aether_layout_result_destroy(second);
}
struct DestroyContext { aether_layout_result_t layout; aether_style_map_s* wrapper; int calls = 0; };
static aether_scroll_offset_t destroy_handles(aether_node_t, void* opaque) {
    auto& context = *static_cast<DestroyContext*>(opaque); ++context.calls;
    aether_layout_result_destroy(context.layout); context.layout = nullptr;
    delete context.wrapper; context.wrapper = nullptr;
    return {2, 0};
}
static void callback_pins() {
    aether_doc_s doc; aether_node_s node; node.type = AETHER_NODE_ELEMENT;
    const auto value = style(.8f); auto map = version({record(&node, value)}, &doc);
    const auto assets = std::make_shared<const LayoutAssets>(map, nullptr, value);
    auto* frame = layout(assets, &node, value);
    auto* wrapper = new aether_style_map_s(); wrapper->current = map;
    DestroyContext context{frame, wrapper};
    REQUIRE(aether_layout_result_hit_test_with_styles_and_scroll_offsets(frame, wrapper, 5, 5,
        destroy_handles, &context) == &node);
    REQUIRE(context.calls == 1 && !context.layout && !context.wrapper);

    const StyleLookup lookup(assets, {}, false);
    aether_layout_engine_s engine; engine.measure_styles = &lookup; engine.measure_document = doc.identity;
    engine.text_measurer = [](const char*, const aether_computed_style_t* copy, float, void*) {
        REQUIRE(copy->box_shadows.count == 1);
        copy->box_shadows.items[0].offset_x = 1000;
        return aether_text_metrics_t{10, 12, 9};
    };
    const BoxStyleSnapshot box(value);
    REQUIRE(engine_measure_text(&engine, "x", &box, 0, &node).width == 10);
    REQUIRE(value->view().box_shadows()[0].offset_x == 3);
    engine.text_measurer_data = &doc;
    engine.text_measurer = [](const char*, const aether_computed_style_t*, float, void* data) {
        ++static_cast<aether_doc_s*>(data)->identity->style_tree_epoch;
        return aether_text_metrics_t{10, 12, 9};
    };
    bool rejected = false;
    try { (void)engine_measure_text(&engine, "x", &box, 0, &node); }
    catch (const std::runtime_error&) { rejected = true; }
    REQUIRE(rejected);
}
static void clone_oom() {
    aether_node_s node; node.type = AETHER_NODE_ELEMENT;
    const auto value = style(.5f); const auto map = version({record(&node, value)});
    const auto assets = std::make_shared<const LayoutAssets>(map, nullptr, value);
    auto* original = layout(assets, &node, value);
    bool success = false;
    for (long ordinal = 0; ordinal < 1000; ++ordinal) {
        aether_layout_result_t output = original;
        allocation_count = 0; injected = false; fail_at = ordinal;
        const auto status = aether_layout_clone_snapshot(original, &output);
        fail_at = -1;
        if (status == AETHER_OK) {
            REQUIRE(!injected); REQUIRE(output != original); aether_layout_result_destroy(output); success = true; break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == original);
        REQUIRE(original->root->style.opacity == .25f && original->live_box_count == 1);
    }
    REQUIRE(success); aether_layout_result_destroy(original);
}
static void overlay_and_callback_new_oom() {
    const auto value = style(.5f);
    MutableLayoutOverlay overlay;
    auto node = std::make_shared<aether_node_s>(); node->type = AETHER_NODE_ELEMENT;
    auto* key = node.get(); overlay.insert(std::move(node), value, {});
    const auto baseline = overlay.freeze();
    bool success = false;
    for (long ordinal = 0; ordinal < 1000; ++ordinal) {
        auto output = baseline;
        allocation_count = 0; injected = false; fail_at = ordinal;
        bool failed = false;
        try { output = overlay.freeze(); } catch (const std::bad_alloc&) { failed = true; }
        fail_at = -1;
        if (!failed) { REQUIRE(!injected && output != baseline); success = true; break; }
        REQUIRE(injected && output == baseline && output->find(key)->style() == value);
    }
    REQUIRE(success);
    aether_layout_engine_s engine; int callbacks = 0;
    engine.text_measurer_data = &callbacks;
    engine.text_measurer = [](const char*, const aether_computed_style_t*, float, void* data) {
        ++*static_cast<int*>(data); return aether_text_metrics_t{10, 12, 9};
    };
    const BoxStyleSnapshot box(value);
    success = false;
    for (long ordinal = 0; ordinal < 1000; ++ordinal) {
        allocation_count = 0; injected = false; fail_at = ordinal; callbacks = 0;
        bool failed = false;
        try { (void)engine_measure_text(&engine, "x", &box, 0); } catch (const std::bad_alloc&) { failed = true; }
        fail_at = -1;
        if (!failed) { REQUIRE(!injected && callbacks == 1); success = true; break; }
        REQUIRE(injected && callbacks == 0 && value->view().box_shadows()[0].offset_x == 3);
    }
    REQUIRE(success);
}
static void runtime_context_and_anonymous() {
    aether_doc_t doc = nullptr; aether_style_runtime_t runtime = nullptr;
    REQUIRE(aether_doc_create(&doc) == AETHER_OK);
    aether_doc_parse_html(doc, "<html><body><div id='flex'>text</div></body></html>");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    REQUIRE(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK);
    int64_t sheet = 0;
    REQUIRE(aether_style_runtime_add_stylesheet(runtime,
        "*{display:block;margin:0;padding:0}#flex{display:flex;gap:7px;line-height:2;text-shadow:1px 2px red}",
        0, "layout-test", &sheet, nullptr) == AETHER_OK);
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
    REQUIRE(count >= 0); aether_style_updates_free(updates, count);
    aether_style_revision_t revision = nullptr; aether_style_map_t map = nullptr;
    REQUIRE(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK);
    REQUIRE(aether_style_revision_create_map(revision, &map) == AETHER_OK);
    aether_style_revision_destroy(revision);
    const auto before = map->current;
    const auto base_count = before->backing()->size();
    struct MeasureContext { aether_style_map_t wrapper; int count = 0; } context{map};
    aether_layout_engine_t engine = nullptr;
    REQUIRE(aether_layout_create([](const char*, const aether_computed_style_t* copy, float, void* data) {
        auto& c = *static_cast<MeasureContext*>(data); ++c.count;
        if (c.wrapper) { aether_style_map_destroy(c.wrapper); c.wrapper = nullptr; }
        if (copy->text_shadows.count) copy->text_shadows.items[0].offset_x = 999;
        return aether_text_metrics_t{10, 20, 15};
    }, &context, &engine) == AETHER_OK);
    aether_layout_result_t frame = nullptr;
    REQUIRE(aether_layout_layout(engine, doc, map, 800, 600, nullptr, nullptr, &frame) == AETHER_OK);
    REQUIRE(context.count > 0 && !context.wrapper);
    REQUIRE(frame->assets->base == before && before->backing()->size() == base_count);
    REQUIRE(!frame->build_overlay);
    const StyleLookup lookup(frame->assets, {}, false);
    auto* flex = aether_doc_get_element_by_id(doc, "flex");
    const auto& sequence = frame->anonymous_flex_items.at(flex);
    REQUIRE(sequence.size() == 1);
    auto* anonymous = sequence[0];
    REQUIRE(!before->backing()->find(anonymous));
    const auto anonymous_style = lookup.lookup_node(anonymous);
    REQUIRE(anonymous_style && anonymous_style->text_shadows().size() == 1);
    REQUIRE(anonymous_style->text_shadows()[0].offset_x == 1);
    const auto measures = lookup.lookup_measures(anonymous);
    REQUIRE(measures && measures->row_gap().value == 0 && measures->column_gap().value == 0);
    REQUIRE(lookup.lookup_measures_owner(anonymous)->document_owner() ==
        before->backing()->find(flex)->measures()->document_owner());
    aether_style_map_s empty;
    REQUIRE(StyleOperationPin(frame->assets, {}, false).accepts_override());
    REQUIRE(!StyleOperationPin(frame->assets, empty.current, true).accepts_override());
    aether_input_hit_part_t part = AETHER_INPUT_HIT_CONTENT; aether_node_t owner = flex;
    REQUIRE(aether_layout_result_hit_test_input(frame, &empty, 1, 1, nullptr, nullptr, &part, &owner) == AETHER_ERROR_INVALID_STATE);
    REQUIRE(part == AETHER_INPUT_HIT_NONE && !owner);
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(frame, &clone) == AETHER_OK);
    REQUIRE(clone->assets == frame->assets);
    aether_layout_result_destroy(frame);
    REQUIRE(StyleLookup(clone->assets, {}, false).lookup_node(anonymous)->text_shadows()[0].offset_x == 1);
    aether_layout_result_destroy(clone); aether_layout_destroy(engine);
    aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}
static void geometry_document_without_map() {
    for (bool explicit_empty : {false, true}) {
        aether_doc_t doc = nullptr; aether_layout_engine_t engine = nullptr; aether_layout_result_t frame = nullptr;
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        aether_doc_parse_html(doc, "<html><body><input value='text'></body></html>");
        REQUIRE(aether_layout_create(nullptr, nullptr, &engine) == AETHER_OK);
        aether_style_map_s empty;
        REQUIRE(aether_layout_layout(engine, doc, explicit_empty ? &empty : nullptr,
            800, 600, nullptr, nullptr, &frame) == AETHER_OK);
        REQUIRE(!frame->assets->base && frame->assets->document_lifetime == doc->identity);
        REQUIRE(!StyleOperationPin(frame->assets, {}, false).base_contextual());
        struct Context { aether_doc_t doc; aether_node_t body; int calls = 0; bool destroy = false; } context{
            doc, aether_doc_get_body(doc)};
        const auto offsets = +[](aether_node_t, void* data) -> aether_scroll_offset_t {
            auto& c = *static_cast<Context*>(data); ++c.calls;
            if (c.destroy) { aether_doc_destroy(c.doc); c.doc = nullptr; }
            else aether_element_set_attribute(c.body, "title", "changed by callback");
            return {0, 0};
        };
        aether_input_hit_part_t part = AETHER_INPUT_HIT_CONTENT; aether_node_t owner = context.body;
        REQUIRE(aether_layout_result_hit_test_input(frame, nullptr, 0, 0, offsets, &context,
            &part, &owner) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(context.calls == 1 && part == AETHER_INPUT_HIT_NONE && !owner);
        context.calls = 0; context.destroy = true;
        REQUIRE(aether_layout_result_hit_test_input(frame, nullptr, 0, 0, offsets, &context,
            &part, &owner) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(context.calls == 1 && !context.doc && !owner);
        REQUIRE(!StyleOperationPin(frame->assets, {}, false).structure_valid());
        REQUIRE(aether_contextual_transition_finalize_layout(frame, nullptr) == AETHER_ERROR_INVALID_STATE);
        aether_layout_result_t clone = frame;
        REQUIRE(aether_layout_clone_snapshot(frame, &clone) == AETHER_ERROR_INVALID_STATE && clone == frame);
        REQUIRE(aether_layout_result_hit_test_input(frame, nullptr, 0, 0, offsets, &context,
            &part, &owner) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(context.calls == 1);
        aether_layout_result_destroy(frame); aether_layout_destroy(engine);
    }
}
int main() {
    selection_and_clone(); callback_pins(); clone_oom(); overlay_and_callback_new_oom(); runtime_context_and_anonymous(); geometry_document_without_map();
    std::cout << "immutable layout selection, clone, callback pins, and new-allocation OOM passed\n";
}
