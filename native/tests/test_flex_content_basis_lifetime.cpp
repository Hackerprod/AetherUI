#include "hit_order_oom_helpers.h"
#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <limits>
#include <new>
#include <string>
#include <vector>

// Link actual core objects into this executable, never the shared library.
// Fault injection must intercept core allocations as well as test allocations.
static long fail_at = -1;
static long allocation_count = 0;
static bool injected = false;
// A bounded, allocation-free ledger observes C++ new/delete, not malloc.
static bool tracking = false;
struct Allocation { void* pointer; size_t bytes; };
static Allocation ledger[65536]{};
static size_t live_allocations = 0, live_bytes = 0, ledger_end = 0;
static void release(void* p) noexcept {
    if (!p) return;
    for (size_t i = 0; i < ledger_end; ++i) if (ledger[i].pointer == p) {
        --live_allocations; live_bytes -= ledger[i].bytes; ledger[i] = {};
        while (ledger_end && !ledger[ledger_end - 1].pointer) --ledger_end;
        break;
    }
    std::free(p);
}
void* operator new(std::size_t size) {
    const auto ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* p = std::malloc(size ? size : 1)) {
        if (tracking) {
            size_t i = 0;
            while (i < ledger_end && ledger[i].pointer) ++i;
            if (i == 65536) std::abort();
            ledger[i] = {p, size};
            if (i == ledger_end) ++ledger_end;
            ++live_allocations; live_bytes += size;
        }
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

using namespace aether::style;
using namespace aether::layout;
static int scenarios = 0;
static long layout_failures = 0;
static long clone_failures = 0;
static long text_calls = 0;
static bool same(float a, float b) { return std::isfinite(a) && std::fabs(a - b) < .01f; }
static bool same(aether_rectf a, aether_rectf b) {
    return same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height);
}
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    ++text_calls;
    return {static_cast<float>(std::strlen(text)) * 5.f, 10.f, 8.f};
}
struct NodeSnapshot {
    aether_node_s* node;
    aether_node_s* parent;
    aether_node_type_t type;
    std::string text;
    std::vector<aether_node_s*> children;
    std::map<std::string, std::string, std::less<>> attributes;
    uint64_t identity;
};
static void capture(aether_node_s* node, std::vector<NodeSnapshot>& out) {
    out.push_back({node, node->parent, node->type, node->text, node->children,
                   node->attributes, aether_node_identity(node)});
    for (auto* child : node->children) capture(child, out);
}
struct BoxSnapshot {
    aether_layout_box_s* box;
    aether_rectf border;
    aether_rectf margin;
    aether_rectf content;
    std::vector<aether_layout_box_s*> children;
};
static std::vector<BoxSnapshot> capture_boxes(aether_layout_result_t frame) {
    std::vector<BoxSnapshot> boxes;
    for (auto* box : frame->allocated_boxes) {
        boxes.push_back({box, box->border_box, box->margin_box, box->content_box, box->children});
    }
    return boxes;
}
static void intact_boxes(const std::vector<BoxSnapshot>& boxes) {
    for (const auto& saved : boxes) {
        REQUIRE(same(saved.box->border_box, saved.border));
        REQUIRE(same(saved.box->margin_box, saved.margin));
        REQUIRE(same(saved.box->content_box, saved.content));
        REQUIRE(saved.box->children == saved.children);
    }
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t painter = nullptr;
    MapVersionRef base;
    std::vector<NodeSnapshot> dom;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;

    Fixture(MapMode mode, bool col, const char* basis, int depth = 1, bool positioned = false) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        std::string body = "<div id='leaf'>alpha beta gamma delta</div>";
        for (int i = 0; i < depth; ++i)
            body = "<div class='nested'>anon <div class='inner'>" + body + "</div> tail</div>";
        const std::string html = "<html><body><div id='p'><div id='c'>" + body +
            "</div><div id='s'><div id='d'></div></div><div id='abs'></div>"
            "<div id='fixed'></div></div></body></html>";
        REQUIRE(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{400, 400, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        const std::string main = col ? "height" : "width";
        std::string css = "*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}"
            "#p{display:flex;position:relative;width:250px;height:250px;align-items:flex-start;flex-direction:" +
            std::string(col ? "column" : "row") + ";}#c,#s{flex:0 1 " + basis + ";min-" + main +
            ":0;}#c{max-" + main + ":100px;}#leaf,#d{width:200px;height:200px;}"
            ".nested{display:flex;flex-direction:row;align-items:flex-start;min-width:0;}"
            ".inner{min-width:0;flex:0 1 auto;}";
        if (col) css += "#p{height:auto;max-height:250px;}#c{width:250px;}";
        if (std::strcmp(basis, "content") == 0) css += "#c,#s{" + main + ":50px;}";
        css += positioned ? "#abs{position:absolute;left:7px;top:11px;width:13px;height:17px;}"
                            "#fixed{position:fixed;left:19px;top:23px;width:29px;height:31px;}"
                          : "#abs,#fixed{display:none;}";
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "cross-auto-lifetime", &sheet, nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        REQUIRE(count > 0);
        aether_style_updates_free(updates, count);
        if (mode == MapMode::contextual) {
            aether_style_revision_t revision = nullptr;
            REQUIRE(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK);
            REQUIRE(aether_style_revision_create_map(revision, &map) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else if (mode == MapMode::legacy_complete) {
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK);
        } else {
            REQUIRE(aether_css_resolve(doc, nullptr, 400, 400, &map) == AETHER_OK);
            std::vector<NodeSnapshot> nodes;
            capture(doc, nodes);
            std::vector<aether_node_t> elements;
            for (const auto& n : nodes) if (n.type == AETHER_NODE_ELEMENT) elements.push_back(n.node);
            REQUIRE(aether_style_runtime_update_resolved_style_map(runtime, map, elements.data(),
                    static_cast<int32_t>(elements.size())) == AETHER_OK);
        }
        REQUIRE(map->current && map->current->mode() == mode);
        base = map->current;
        base_count = base->backing()->size();
        capture(doc, dom);
        structure = doc->identity->structure_epoch;
        mutation = doc->identity->mutation_epoch;
        style_epoch = doc->identity->style_tree_epoch;
        REQUIRE(aether_layout_create(measure, nullptr, &engine) == AETHER_OK);
        REQUIRE(aether_display_list_builder_create(measure, nullptr, &painter) == AETHER_OK);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        aether_display_list_builder_destroy(painter);
        aether_layout_destroy(engine);
        if (map) aether_style_map_destroy(map);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void dom_intact() const {
        REQUIRE(doc->identity->structure_epoch == structure);
        REQUIRE(doc->identity->mutation_epoch == mutation);
        for (const auto& saved : dom) {
            REQUIRE(saved.node->parent == saved.parent && saved.node->type == saved.type);
            REQUIRE(saved.node->text == saved.text && saved.node->children == saved.children);
            REQUIRE(saved.node->attributes == saved.attributes);
            REQUIRE(aether_node_identity(saved.node) == saved.identity && saved.node->identity->alive);
        }
    }
    void intact() const {
        dom_intact();
        REQUIRE(doc->identity->style_tree_epoch == style_epoch);
        REQUIRE(base->backing()->size() == base_count);
        if (map) REQUIRE(map->current == base);
    }
    aether_layout_result_t layout() const {
        aether_layout_result_t frame = nullptr;
        REQUIRE(aether_layout_layout(engine, doc, map, 400, 400, nullptr, nullptr, &frame) == AETHER_OK);
        REQUIRE(frame && frame->assets->base == base && !frame->build_overlay);
        return frame;
    }
    void paint(aether_layout_result_t frame) const {
        aether_display_list_t list = nullptr;
        REQUIRE(aether_display_list_build(painter, frame, nullptr, &list) == AETHER_OK);
        REQUIRE(list != nullptr);
        aether_display_list_destroy(list);
    }
};
static size_t reachable(aether_layout_box_s* root, aether_layout_box_s* needle) {
    size_t n = root == needle;
    for (auto* c : root->children) n += reachable(c, needle);
    return n;
}
static void ownership(aether_layout_result_t frame) {
    REQUIRE(frame->records_boxes && !frame->abandoned && !frame->build_overlay);
    size_t count = 0;
    for (auto* box : frame->allocated_boxes) {
        REQUIRE(box->owner == frame && reachable(frame->root, box) == 1);
        REQUIRE(++count <= frame->live_box_count);
    }
    REQUIRE(count == frame->live_box_count);
    for (auto* box : frame->paint_order) REQUIRE(box->owner == frame && reachable(frame->root, box) == 1);
    for (const auto& entry : frame->primary_boxes)
        REQUIRE(entry.second->owner == frame && entry.second->element == entry.first && reachable(frame->root, entry.second) == 1);
    REQUIRE(frame->assets->synthetic && frame->assets->synthetic->ordered_entries().size() > 0);
    const auto entries = frame->assets->synthetic->ordered_entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        REQUIRE(entries[i]->node() && entries[i]->style() && entries[i]->measures());
        REQUIRE(frame->assets->synthetic->find(entries[i]->node()) == entries[i].get());
        for (size_t j = 0; j < i; ++j) REQUIRE(entries[i]->node() != entries[j]->node());
    }
}
static void scopes() {
    aether_layout_result_s result;
    REQUIRE(result.records_boxes);
    try {
        detached_layout_scope_s outer(&result);
        REQUIRE(!result.records_boxes);
        {
            detached_layout_scope_s inner(&result);
            REQUIRE(!result.records_boxes);
        }
        REQUIRE(!result.records_boxes);
        throw std::bad_alloc();
    } catch (const std::bad_alloc&) {}
    REQUIRE(result.records_boxes && result.live_box_count == 0);
}
static void run(MapMode mode, bool col, const char* basis, bool positioned) {
    Fixture f(mode, col, basis, 1, positioned);
    auto* source = f.layout();
    ownership(source);
    REQUIRE(source->assets->synthetic->ordered_entries().size() == 2);
    auto* parent = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "p"));
    auto* child = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "c"));
    REQUIRE(same(parent->content_box.width, 250) && same(parent->content_box.height, 250));
    REQUIRE(same(col ? child->border_box.height : child->border_box.width, 100));
    if (!col) {
        auto* sibling = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "s"));
        REQUIRE(same(sibling->border_box.width, 150));
        REQUIRE(same(sibling->border_box.x - parent->content_box.x, 100));
    }
    if (positioned) {
        const auto a = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "abs"))->border_box;
        const auto fixed = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "fixed"))->border_box;
        REQUIRE(same(a, {parent->padding_box.x + 7, parent->padding_box.y + 11, 13, 17}));
        REQUIRE(same(fixed, {19, 23, 29, 31}));
    }
    const auto geometry = capture_boxes(source);
    const auto primary = source->primary_boxes;
    const auto paint_order = source->paint_order;
    const auto assets = source->assets;
    for (bool clone : {false, true}) {
        bool completed = false;
        long failures = 0;
        for (long ordinal = 0; ordinal < 20000; ++ordinal) {
            aether_layout_result_t output = source;
            REQUIRE(live_allocations == 0 && live_bytes == 0);
            allocation_count = 0; injected = false; tracking = true; fail_at = ordinal;
            const auto status = clone ? aether_layout_clone_snapshot(source, &output) :
                aether_layout_layout(f.engine, f.doc, f.map, 400, 400, nullptr, nullptr, &output);
            fail_at = -1; tracking = false;
            f.intact(); intact_boxes(geometry);
            REQUIRE(source->primary_boxes == primary && source->paint_order == paint_order && source->assets == assets);
            if (status == AETHER_OK) {
                REQUIRE((!injected || (!clone && hit_oom_optional_success(output,source))) && output && output != source);
                ownership(output);
                if (clone) REQUIRE(output->assets == assets);
                f.paint(output);
                aether_layout_result_destroy(output);
                completed = !injected;
            } else {
                REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source);
                ++failures;
            }
            REQUIRE(live_allocations == 0 && live_bytes == 0);
            if (completed) break;
        }
        REQUIRE(completed && failures > 0);
        (clone ? clone_failures : layout_failures) += failures;
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(source, &clone) == AETHER_OK);
    auto retained = capture_boxes(clone);
    for (int pass = 0; pass < 3; ++pass) {
        auto* next = f.layout(); ownership(next);
        // Synthetic keys belong to each frame's overlay, not to the real DOM.
        for (const auto& node : f.dom) {
            const auto entry = primary.find(node.node);
            if (entry != primary.end())
                REQUIRE(same(next->primary_boxes.at(node.node)->border_box, entry->second->border_box));
        }
        intact_boxes(geometry); intact_boxes(retained); f.paint(next);
        aether_layout_result_destroy(next);
    }
    aether_layout_result_destroy(source);
    aether_style_map_destroy(f.map); f.map = nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime = nullptr; f.base.reset();
    intact_boxes(retained); ownership(clone); f.paint(clone);
    aether_doc_destroy(f.doc); f.doc = nullptr;
    REQUIRE(!StyleOperationPin(clone->assets, {}, false).structure_valid());
    auto* output = clone;
    REQUIRE(aether_layout_clone_snapshot(clone, &output) == AETHER_ERROR_INVALID_STATE && output == clone);
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, clone, nullptr, &list) == AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_destroy(clone);
    ++scenarios;
}
static void counters() {
    struct Counts { long text = 0, allocations = 0; };
    constexpr int depths[] = {1, 2, 4, 6};
    for (bool col : {false, true}) {
        Counts auto_counts[4]{};
        for (const char* basis : {"auto", "content"}) {
            Counts previous{};
            for (size_t index = 0; index < 4; ++index) {
                const int depth = depths[index];
                Fixture f(MapMode::contextual, col, basis, depth);
                auto* warm = f.layout(); aether_layout_result_destroy(warm);
                allocation_count = 0; text_calls = 0;
                auto* frame = f.layout();
                const Counts current{text_calls, allocation_count};
                std::printf("COUNTERS axis=%s basis=%s depth=%d text=%ld allocations=%ld boxes=%zu synthetic=%zu\n",
                    col ? "column" : "row", basis, depth, current.text, current.allocations,
                    frame->live_box_count, frame->assets->synthetic->ordered_entries().size());
                REQUIRE(current.text > 0 && current.allocations > 0);
                if (std::strcmp(basis, "auto") == 0) auto_counts[index] = current;
                else {
                    // This fixture gives auto and content the same intrinsic
                    // measurement path. Compare within this executable instead
                    // of depending on absolute counts from a particular STL.
                    REQUIRE(current.text == auto_counts[index].text);
                    REQUIRE(current.allocations == auto_counts[index].allocations);
                }
                if (index >= 2) {
                    // Fixture-specific budget for depth 2->4 and 4->6. Existing
                    // nested layout grows roughly as 2^depth, not linearly.
                    // A third recursive pass would approach 9x per two levels;
                    // <5x retains observed headroom without fixing exact counts.
                    REQUIRE(static_cast<double>(current.text) / previous.text < 5.0);
                    REQUIRE(static_cast<double>(current.allocations) / previous.allocations < 5.0);
                }
                previous = current;
                REQUIRE(frame->assets->synthetic->ordered_entries().size() == static_cast<size_t>(2 * depth));
                ownership(frame); f.intact(); aether_layout_result_destroy(frame);
            }
        }
    }
}
int main(int argc, char**) {
    scopes(); counters();
    // Counter-only mode retains the same allocation/callback regression guards.
    if (argc > 1) return 0;
    for (auto mode : {MapMode::contextual, MapMode::legacy_complete, MapMode::legacy_partial})
        for (bool col : {false, true}) for (const char* basis : {"auto", "content"})
            for (bool positioned : {false, true}) run(mode, col, basis, positioned);
    std::printf("flex content basis lifetime: scenarios=%d layout OOM=%ld clone OOM=%ld PASS (NDEBUG-safe)\n",
        scenarios, layout_failures, clone_failures);
}
