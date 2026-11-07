#include "../src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

// Deliberately independent of assert: these checks must run under NDEBUG.
#define REQUIRE(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::abort(); } } while (false)

// Compile the core sources into this executable, not a DSO, so every C++
// allocation in text joining, overlay publication and clone is intercepted.
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
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace aether::style;
using namespace aether::layout;
static constexpr const char* joined = "Alphabetabcdefghijklmnopqrstuvwxyzzebra";
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {static_cast<float>(std::strlen(text)) * 5.f, 20.f, 15.f};
}
struct NodeSnapshot {
    aether_node_s* node;
    aether_node_s* parent;
    aether_node_type_t type;
    std::string text;
    std::vector<aether_node_s*> children;
    uint64_t identity;
};
static void capture(aether_node_s* node, std::vector<NodeSnapshot>& out) {
    out.push_back({node, node->parent, node->type, node->text, node->children, aether_node_identity(node)});
    for (auto* child : node->children) capture(child, out);
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t painter = nullptr;
    aether_node_s* flex = nullptr;
    std::vector<NodeSnapshot> dom;
    MapVersionRef base;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;
    explicit Fixture(MapMode mode) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc, "<html><body><div id='flex'>alpha<!--one-->bet<span style='display:none'>HIDDEN</span>abcdefghijklmnopqrstuvwxyzzebra<span id='separator'></span>tail</div></body></html>") == AETHER_OK);
        flex = aether_doc_get_element_by_id(doc, "flex");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,
            "*{margin:0;padding:0}html,body{display:block}#flex{display:flex;width:600px;gap:7px;line-height:20px;text-transform:capitalize;text-shadow:1px 2px red;opacity:.5}#separator{display:block;width:10px;height:10px}",
            0, "flex-lifetime", &sheet, nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        REQUIRE(count >= 0); aether_style_updates_free(updates, count);
        if (mode == MapMode::contextual) {
            aether_style_revision_t revision = nullptr;
            REQUIRE(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK);
            REQUIRE(aether_style_revision_create_map(revision, &map) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else if (mode == MapMode::legacy_complete) {
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK);
        } else {
            REQUIRE(aether_css_resolve(doc, nullptr, 800, 600, &map) == AETHER_OK);
            std::vector<NodeSnapshot> all; capture(doc, all);
            std::vector<aether_node_t> elements;
            for (const auto& n : all) if (n.type == AETHER_NODE_ELEMENT) elements.push_back(n.node);
            REQUIRE(aether_style_runtime_update_resolved_style_map(runtime, map, elements.data(), static_cast<int32_t>(elements.size())) == AETHER_OK);
        }
        REQUIRE(map->current && map->current->mode() == mode);
        base = map->current; base_count = base->backing()->size();
        REQUIRE(aether_layout_create(measure, nullptr, &engine) == AETHER_OK);
        REQUIRE(aether_display_list_builder_create(measure, nullptr, &painter) == AETHER_OK);
        capture(doc, dom);
        structure = doc->identity->structure_epoch; mutation = doc->identity->mutation_epoch; style_epoch = doc->identity->style_tree_epoch;
    }
    ~Fixture() {
        aether_display_list_builder_destroy(painter); aether_layout_destroy(engine);
        if (map) aether_style_map_destroy(map);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void intact() const {
        REQUIRE(doc->identity->structure_epoch == structure);
        REQUIRE(doc->identity->mutation_epoch == mutation && doc->identity->style_tree_epoch == style_epoch);
        for (const auto& n : dom) {
            REQUIRE(n.node->parent == n.parent && n.node->type == n.type);
            REQUIRE(n.node->text == n.text && n.node->children == n.children);
            REQUIRE(aether_node_identity(n.node) == n.identity && n.node->identity->alive);
        }
        REQUIRE(base->backing()->size() == base_count);
        if (map) REQUIRE(map->current == base);
    }
    aether_layout_result_t layout() {
        aether_layout_result_t frame = nullptr;
        REQUIRE(aether_layout_layout(engine, doc, map, 800, 600, nullptr, nullptr, &frame) == AETHER_OK);
        return frame;
    }
    void wrappers(aether_layout_result_t frame) const {
        REQUIRE(frame->assets->base == base && !frame->build_overlay);
        REQUIRE(frame->assets->synthetic && frame->assets->synthetic->ordered_entries().size() == 2);
        const auto& sequence = frame->anonymous_flex_items.at(flex);
        REQUIRE(sequence.size() == 3 && sequence[1] == aether_doc_get_element_by_id(doc, "separator"));
        REQUIRE(sequence[0]->children.size() == 3 && sequence[2]->children.size() == 1);
        for (auto index : {0, 2}) {
            auto* wrapper = sequence[index];
            REQUIRE(wrapper->anonymous_flex_owner == flex && wrapper->parent == flex);
            REQUIRE(!base->backing()->find(wrapper));
            const auto* entry = frame->assets->synthetic->find(wrapper);
            REQUIRE(entry && entry->style()->view().scalars().opacity == 1.f);
            REQUIRE(entry->style()->view().text_shadows().size() == 1);
            for (auto* child : wrapper->children) REQUIRE(child->type == AETHER_NODE_TEXT && child->parent == flex);
            REQUIRE(frame->primary_boxes.at(wrapper)->presentation_owner == flex);
        }
    }
    void paint(aether_layout_result_t frame) const {
        aether_display_list_t list = nullptr;
        REQUIRE(aether_display_list_build(painter, frame, nullptr, &list) == AETHER_OK);
        std::string painted;
        int shadows = 0;
        for (int i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t command{};
            REQUIRE(aether_display_list_get_command(list, i, &command) == AETHER_OK);
            if (command.tag == AETHER_PAINT_DRAW_TEXT) painted += command.data.draw_text.text;
            if (command.tag == AETHER_PAINT_DRAW_TEXT_SHADOW) ++shadows;
            aether_paint_command_free(&command);
        }
        REQUIRE(painted == std::string(joined) + "Tail" && shadows >= 2);
        aether_display_list_destroy(list);
    }
};

static void snapshots_and_relayout(MapMode mode) {
    Fixture f(mode);
    auto* frame = f.layout(); f.wrappers(frame); f.paint(frame); f.intact();
    auto* first_wrapper = frame->anonymous_flex_items.at(f.flex)[0];
    for (int pass = 0; pass != 3; ++pass) {
        auto* next = f.layout(); f.wrappers(next); f.paint(next); f.intact();
        REQUIRE(next->anonymous_flex_items.at(f.flex)[0] != first_wrapper);
        aether_layout_result_destroy(next); f.intact();
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(frame, &clone) == AETHER_OK);
    REQUIRE(clone->assets == frame->assets);
    const auto before = frame->primary_boxes.at(first_wrapper)->border_box;
    aether_layout_result_destroy(frame); f.intact();
    aether_style_map_destroy(f.map); f.map = nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime = nullptr;
    f.base.reset(); // Only snapshot assets now own the resolved backing.
    REQUIRE(clone->assets->synthetic->find(first_wrapper));
    const auto after = clone->primary_boxes.at(first_wrapper)->border_box;
    REQUIRE(before.x == after.x && before.y == after.y && before.width == after.width && before.height == after.height);
    f.paint(clone);
    aether_style_map_s empty;
    REQUIRE(StyleOperationPin(clone->assets, empty.current, true).accepts_override() == (mode != MapMode::contextual));
    aether_display_list_t list = nullptr;
    const auto status = aether_display_list_build_with_styles(f.painter, clone, &empty, nullptr, &list);
    REQUIRE(status == (mode == MapMode::contextual ? AETHER_ERROR_INVALID_STATE : AETHER_OK));
    if (list) aether_display_list_destroy(list);
    aether_doc_destroy(f.doc); f.doc = nullptr;
    REQUIRE(!StyleOperationPin(clone->assets, {}, false).structure_valid());
    list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, clone, nullptr, &list) == AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_t output = clone;
    REQUIRE(aether_layout_clone_snapshot(clone, &output) == AETHER_ERROR_INVALID_STATE && output == clone);
    aether_layout_result_destroy(clone);
}

static void epochs(MapMode mode) {
    Fixture f(mode); auto* frame = f.layout();
    REQUIRE(aether_element_set_attribute(f.flex, "title", "changed") == AETHER_OK);
    REQUIRE(StyleOperationPin(frame->assets, {}, false).structure_valid());
    REQUIRE(StyleOperationPin(frame->assets, {}, false).inputs_current() == (mode != MapMode::contextual));
    // Moving a visually inert comment still changes the structural contract.
    auto* comment = f.flex->children[1]; REQUIRE(comment->type == AETHER_NODE_COMMENT);
    REQUIRE(aether_node_append_child(f.flex, comment) == AETHER_OK);
    REQUIRE(!StyleOperationPin(frame->assets, {}, false).structure_valid());
    aether_layout_result_t output = frame;
    REQUIRE(aether_layout_clone_snapshot(frame, &output) == AETHER_ERROR_INVALID_STATE && output == frame);
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, frame, nullptr, &list) == AETHER_ERROR_INVALID_STATE && !list);
    // Re-resolve after mutation and build a new result while the invalidated
    // result remains alive. The new collector must not reuse its old wrappers.
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(f.runtime, aether_doc_get_document_element(f.doc), &updates);
    REQUIRE(count >= 0); aether_style_updates_free(updates, count);
    aether_style_map_destroy(f.map); f.map = nullptr;
    if (mode == MapMode::contextual) {
        aether_style_revision_t revision = nullptr;
        REQUIRE(aether_style_runtime_retain_revision(f.runtime, &revision) == AETHER_OK);
        REQUIRE(aether_style_revision_create_map(revision, &f.map) == AETHER_OK);
        aether_style_revision_destroy(revision);
    } else if (mode == MapMode::legacy_complete) {
        REQUIRE(aether_style_runtime_create_resolved_style_map(f.runtime, f.doc, &f.map) == AETHER_OK);
    } else {
        REQUIRE(aether_css_resolve(f.doc, nullptr, 800, 600, &f.map) == AETHER_OK);
        std::vector<aether_node_t> elements;
        for (const auto& n : f.dom) if (n.type == AETHER_NODE_ELEMENT) elements.push_back(n.node);
        REQUIRE(aether_style_runtime_update_resolved_style_map(f.runtime, f.map, elements.data(), static_cast<int32_t>(elements.size())) == AETHER_OK);
    }
    REQUIRE(f.map->current->mode() == mode);
    f.base = f.map->current; f.base_count = f.base->backing()->size();
    f.dom.clear(); capture(f.doc, f.dom);
    f.structure = f.doc->identity->structure_epoch; f.mutation = f.doc->identity->mutation_epoch;
    f.style_epoch = f.doc->identity->style_tree_epoch;
    auto* rebuilt = f.layout(); f.wrappers(rebuilt); f.paint(rebuilt); f.intact();
    REQUIRE(rebuilt->anonymous_flex_items.at(f.flex)[0] != frame->anonymous_flex_items.at(f.flex)[0]);
    REQUIRE(!StyleOperationPin(frame->assets, {}, false).structure_valid());
    aether_layout_result_destroy(frame); f.intact();
    aether_layout_result_destroy(rebuilt); f.intact();
}

static void allocation_sweeps(MapMode mode) {
    Fixture f(mode); auto* source = f.layout(); f.wrappers(source);
    bool completed = false; long failures = 0;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        aether_layout_result_t output = source;
        allocation_count = 0; injected = false; fail_at = ordinal;
        const auto status = aether_layout_layout(f.engine, f.doc, f.map, 800, 600, nullptr, nullptr, &output);
        fail_at = -1; f.intact();
        if (status == AETHER_OK) {
            REQUIRE(!injected && output != source); f.wrappers(output); f.paint(output);
            aether_layout_result_destroy(output); completed = true; break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source); ++failures;
    }
    REQUIRE(completed && failures > 20);
    std::printf("mode=%d layout OOM allocations=%ld\n", static_cast<int>(mode), failures); f.paint(source);
    completed = false; failures = 0;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        aether_layout_result_t output = source;
        allocation_count = 0; injected = false; fail_at = ordinal;
        const auto status = aether_layout_clone_snapshot(source, &output);
        fail_at = -1; f.intact();
        if (status == AETHER_OK) {
            REQUIRE(!injected && output != source && output->assets == source->assets);
            f.paint(output); aether_layout_result_destroy(output); completed = true; break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source); ++failures;
    }
    REQUIRE(completed && failures > 0);
    std::printf("mode=%d clone OOM allocations=%ld\n", static_cast<int>(mode), failures); f.paint(source);
    // Isolate publication itself, using the same text aliases and frozen style
    // owners as the real layout; all failed freezes leave the old store intact.
    MutableLayoutOverlay overlay;
    const auto* original = source->assets->synthetic->ordered_entries()[0].get();
    auto owner = std::make_shared<aether_node_s>(); owner->type = AETHER_NODE_ELEMENT;
    owner->children = original->node()->children;
    auto* key = owner.get();
    overlay.insert(std::move(owner), original->style(), original->measures());
    const auto baseline = overlay.freeze();
    completed = false; failures = 0;
    for (long ordinal = 0; ordinal < 1000; ++ordinal) {
        auto output = baseline; bool failed = false;
        allocation_count = 0; injected = false; fail_at = ordinal;
        try { output = overlay.freeze(); } catch (const std::bad_alloc&) { failed = true; }
        fail_at = -1; f.intact();
        if (!failed) { REQUIRE(!injected && output != baseline); completed = true; break; }
        REQUIRE(injected && output == baseline && output->find(key)); ++failures;
    }
    REQUIRE(completed && failures > 0);
    std::printf("mode=%d freeze OOM allocations=%ld\n", static_cast<int>(mode), failures);
    aether_layout_result_destroy(source); f.intact();
}
int main() {
    for (auto mode : {MapMode::contextual, MapMode::legacy_complete, MapMode::legacy_partial}) {
        snapshots_and_relayout(mode); epochs(mode); allocation_sweeps(mode);
    }
    std::puts("flex text DOM ownership, snapshots, map contracts and allocation sweeps passed");
}
