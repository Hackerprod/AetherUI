#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>

#include <algorithm>
#include <array>
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
        std::fprintf(stderr, "FAIL stretch-private [%s] %s:%d: %s\n", scenario, __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

using namespace aether::style;
using namespace aether::layout;
static int scenarios = 0, callback_cases = 0;
static const char* scenario = "setup";
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
    std::string text;
};
static std::vector<BoxSnapshot> capture_boxes(aether_layout_result_t frame) {
    std::vector<BoxSnapshot> boxes;
    for (auto* box : frame->allocated_boxes) {
        boxes.push_back({box, box->border_box, box->margin_box, box->content_box, box->children, box->text});
    }
    return boxes;
}
static void intact_boxes(const std::vector<BoxSnapshot>& boxes) {
    for (const auto& saved : boxes) {
        REQUIRE(same(saved.box->border_box, saved.border));
        REQUIRE(same(saved.box->margin_box, saved.margin));
        REQUIRE(same(saved.box->content_box, saved.content));
        REQUIRE(saved.box->children == saved.children);
        REQUIRE(saved.box->text == saved.text);
    }
}

// The first/last fixed-basis items have no intrinsic measurement dependency.
// Their callbacks therefore exercise final deferred construction, with retained
// table/fixed-height items and required content-basis work interleaved.
static const char* html =
    "<html><body><div id='p'>"
    "<div id='a'><div id='leaf'>DEFERA</div><div id='anchor'></div></div>"
    "<table id='table'><tr><td id='cell'>TABLE</td><td><div id='tall'></div></td></tr></table>"
    "<div id='content'>CONTENT<div id='inside'></div>TAIL</div>"
    "ANONYMOUS<div id='fixed'>FIXED</div>"
    "<div id='z'><div id='last'>DEFERZ</div><table id='nested'><tr><td>NESTED</td></tr></table></div>"
    "<div id='absolute'></div></div></body></html>";
static const char* base_css =
    "*{margin:0;padding:0;border:0;box-sizing:border-box;}"
    "html,body,div{display:block;}table{display:table;}tr{display:table-row;}td{display:table-cell;}"
    "#p{display:flex;flex-wrap:nowrap;align-items:stretch;position:relative;width:400px;height:160px;}"
    "#a,#z{display:flex;flex-direction:column;flex:0 0 70px;min-width:0;position:relative;}"
    "#leaf,#last{flex:0 0 30px;min-height:0;}"
    "#a{background:red;overflow:auto;}#z{margin-top:-3px;margin-bottom:7px;}"
    "#table{flex:0 0 70px;min-width:0;}#tall{width:10px;height:35px;}"
    "#content{display:flex;flex:0 0 content;min-width:0;align-items:stretch;}"
    "#inside{width:15px;}#fixed{flex:0 0 45px;min-width:0;height:24px;}"
    "#nested{width:60px;}#anchor{position:absolute;left:2px;bottom:4px;width:5px;height:7px;}"
    "#absolute{position:absolute;right:3px;bottom:5px;width:11px;height:13px;}";

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t painter = nullptr;
    MapMode mode;
    MapVersionRef base;
    struct FrozenSnapshot { FrozenNodeRef owner; std::array<unsigned char, sizeof(StyleScalars)> bytes; };
    std::vector<FrozenSnapshot> frozen;
    std::vector<NodeSnapshot> dom;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;

    explicit Fixture(MapMode m, const char* extra = "") : mode(m) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc, html) == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{500, 400, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        add(std::string(base_css) + extra);
        resolve();
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
    aether_node_t node(const char* id) const {
        auto* n = aether_doc_get_element_by_id(doc, id); REQUIRE(n); return n;
    }
    aether_layout_box_s* box(aether_layout_result_t r, const char* id) const {
        auto* b = r->find_primary_box(node(id)); REQUIRE(b); return b;
    }
    void add(const std::string& css) {
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
            "stretch-deferred-private", &sheet, nullptr) == AETHER_OK);
    }
    void resolve() {
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        REQUIRE(count >= 0); aether_style_updates_free(updates, count);
        aether_style_map_t next = nullptr;
        if (mode == MapMode::contextual) {
            aether_style_revision_t revision = nullptr;
            REQUIRE(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK);
            REQUIRE(aether_style_revision_create_map(revision, &next) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else if (mode == MapMode::legacy_complete) {
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime, doc, &next) == AETHER_OK);
        } else {
            REQUIRE(aether_css_resolve(doc, nullptr, 500, 400, &next) == AETHER_OK);
            std::vector<NodeSnapshot> nodes; capture(doc, nodes);
            std::vector<aether_node_t> elements;
            for (const auto& n : nodes) if (n.type == AETHER_NODE_ELEMENT) elements.push_back(n.node);
            REQUIRE(aether_style_runtime_update_resolved_style_map(runtime, next, elements.data(),
                static_cast<int32_t>(elements.size())) == AETHER_OK);
        }
        if (map) { map->current = next->current; aether_style_map_destroy(next); }
        else map = next;
        REQUIRE(map->current && map->current->mode() == mode);
        base = map->current; base_count = base->backing()->size();
        // Keep both old and new frozen owners: neither publication may mutate.
        for (const auto& entry : base->backing()->entries()) {
            FrozenSnapshot saved{entry.second,{}};
            std::memcpy(saved.bytes.data(), &entry.second->style_view().scalars(), sizeof(StyleScalars));
            frozen.push_back(saved);
        }
        dom.clear(); capture(doc, dom);
        structure = doc->identity->structure_epoch;
        mutation = doc->identity->mutation_epoch;
        style_epoch = doc->identity->style_tree_epoch;
    }
    void frozen_intact() const {
        for (const auto& saved : frozen)
            REQUIRE(std::memcmp(saved.bytes.data(), &saved.owner->style_view().scalars(), sizeof(StyleScalars)) == 0);
    }
    void intact() const {
        frozen_intact();
        REQUIRE(doc->identity->structure_epoch == structure);
        REQUIRE(doc->identity->mutation_epoch == mutation);
        REQUIRE(doc->identity->style_tree_epoch == style_epoch);
        for (const auto& saved : dom) {
            REQUIRE(saved.node->parent == saved.parent && saved.node->type == saved.type);
            REQUIRE(saved.node->text == saved.text && saved.node->children == saved.children);
            REQUIRE(saved.node->attributes == saved.attributes);
            REQUIRE(aether_node_identity(saved.node) == saved.identity && saved.node->identity->alive);
        }
        REQUIRE(base->backing()->size() == base_count);
        if (map) REQUIRE(map->current == base);
    }
    void prepare(aether_layout_result_t prior = nullptr) const {
        aether_bool_t changed = 0;
        REQUIRE(aether_layout_prepare_transition_context(engine, nullptr, map, prior, &changed) == AETHER_OK);
        REQUIRE(engine->transition_context_prepared);
    }
    aether_layout_result_t layout(bool prepared = false, aether_layout_result_t prior = nullptr) const {
        if (prepared) prepare(prior);
        aether_layout_result_t r = nullptr;
        REQUIRE(aether_layout_layout(engine, doc, map, 500, 400, nullptr, nullptr, &r) == AETHER_OK);
        REQUIRE(r && r->assets->base == base && !r->build_overlay);
        REQUIRE(!r->anonymous_flex_items.empty());
        return r;
    }
    void paint(aether_layout_result_t r) const {
        aether_display_list_t list = nullptr;
        REQUIRE(aether_display_list_build(painter, r, nullptr, &list) == AETHER_OK);
        REQUIRE(list && aether_display_list_get_count(list) > 0);
        aether_display_list_destroy(list);
    }
};

static size_t reachable(aether_layout_box_s* root, aether_layout_box_s* needle) {
    size_t n = root == needle;
    for (auto* child : root->children) { REQUIRE(child); n += reachable(child, needle); }
    return n;
}
static void engine_clean(aether_layout_engine_t e) {
    REQUIRE(!e->layout_in_progress && !e->measure_styles && !e->measure_document);
    REQUIRE(!e->transition_context_prepared);
}
static void ownership(aether_layout_result_t r) {
    REQUIRE(r->root && r->records_boxes && !r->abandoned && !r->build_overlay);
    size_t count = 0;
    for (auto* b : r->allocated_boxes) {
        REQUIRE(b->owner == r && reachable(r->root, b) == 1);
        REQUIRE(b->block_static_anchors.empty());
        REQUIRE(++count <= r->live_box_count);
        REQUIRE(std::count(r->paint_order.begin(), r->paint_order.end(), b) == 1);
    }
    REQUIRE(count == r->live_box_count && count == r->paint_order.size());
    for (auto* b : r->paint_order) REQUIRE(b->owner == r && reachable(r->root, b) == 1);
    for (const auto& entry : r->primary_boxes)
        REQUIRE(entry.second->owner == r && entry.second->element == entry.first && reachable(r->root, entry.second) == 1);
    REQUIRE(r->assets->synthetic);
    const auto entries = r->assets->synthetic->ordered_entries();
    REQUIRE(entries.size() >= 3);
    for (size_t i = 0; i < entries.size(); ++i) {
        REQUIRE(entries[i]->node() && entries[i]->style() && entries[i]->measures());
        REQUIRE(r->assets->synthetic->find(entries[i]->node()) == entries[i].get());
        for (size_t j = 0; j < i; ++j) REQUIRE(entries[i]->node() != entries[j]->node());
    }
}
static void compare_clone(aether_layout_result_t source, aether_layout_result_t clone) {
    REQUIRE(source != clone && source->assets == clone->assets);
    REQUIRE(source->has_finalized_table_cells && clone->has_finalized_table_cells);
    REQUIRE(source->paint_order.size() == clone->paint_order.size());
    REQUIRE(source->primary_boxes.size() == clone->primary_boxes.size());
    // Anonymous item lookup is a construction cache, not snapshot state.
    // Synthetic nodes/styles remain owned by the shared immutable assets.
    REQUIRE(clone->anonymous_flex_items.empty());
    // paint_order need not be DOM preorder. Clone must preserve its exact map.
    for (size_t i = 0; i < source->paint_order.size(); ++i) {
        auto* a = source->paint_order[i]; auto* b = clone->paint_order[i];
        REQUIRE(a != b && a->element == b->element && a->kind == b->kind);
        REQUIRE(a->style.style_owner() == b->style.style_owner());
        REQUIRE(same(a->border_box, b->border_box) && same(a->content_box, b->content_box));
        REQUIRE(a->text == b->text && a->children.size() == b->children.size());
        for (size_t j = 0; j < a->children.size(); ++j) {
            auto where = std::find(source->paint_order.begin(), source->paint_order.end(), a->children[j]);
            REQUIRE(where != source->paint_order.end());
            REQUIRE(b->children[j] == clone->paint_order[static_cast<size_t>(where - source->paint_order.begin())]);
        }
    }
    for (const auto& entry : source->primary_boxes) {
        auto where = std::find(source->paint_order.begin(), source->paint_order.end(), entry.second);
        REQUIRE(where != source->paint_order.end());
        REQUIRE(clone->primary_boxes.at(entry.first) == clone->paint_order[static_cast<size_t>(where - source->paint_order.begin())]);
    }
}

static void allocation_sweeps(MapMode mode, bool prepared) {
    scenario = prepared ? "OOM prepared" : "OOM ordinary";
    Fixture f(mode);
    auto* source = f.layout(prepared); ownership(source); f.intact(); f.paint(source);
    REQUIRE(same(f.box(source, "a")->border_box.height, 160));
    REQUIRE(same(f.box(source, "z")->border_box.height, 156));
    const auto geometry = capture_boxes(source);
    const auto primary = source->primary_boxes;
    const auto order = source->paint_order;
    const auto anonymous = source->anonymous_flex_items;
    const auto assets = source->assets;
    for (bool clone : {false, true}) {
        bool completed = false; long failures = 0;
        for (long ordinal = 0; ordinal < 20000; ++ordinal) {
            auto* output = source;
            if (prepared && !clone) f.prepare(source);
            REQUIRE(live_allocations == 0 && live_bytes == 0);
            allocation_count = 0; injected = false; tracking = true; fail_at = ordinal;
            const auto status = clone ? aether_layout_clone_snapshot(source, &output) :
                aether_layout_layout(f.engine, f.doc, f.map, 500, 400, nullptr, nullptr, &output);
            fail_at = -1; tracking = false;
            f.intact(); intact_boxes(geometry); engine_clean(f.engine);
            REQUIRE(source->primary_boxes == primary && source->paint_order == order);
            REQUIRE(source->anonymous_flex_items == anonymous && source->assets == assets);
            if (status == AETHER_OK) {
                REQUIRE(!injected && output && output != source);
                ownership(output); f.paint(output);
                if (clone) compare_clone(source, output);
                for (const auto& node : f.dom) {
                    const auto entry = primary.find(node.node);
                    if (entry != primary.end())
                        REQUIRE(same(output->primary_boxes.at(entry->first)->border_box, entry->second->border_box));
                }
                aether_layout_result_destroy(output); completed = true;
            } else {
                REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source);
                ++failures;
            }
            REQUIRE(live_allocations == 0 && live_bytes == 0);
            if (completed) break;
        }
        REQUIRE(completed && failures > 0);
        (clone ? clone_failures : layout_failures) += failures;
        std::printf("stretch private OOM: mode=%d prepared=%d clone=%d ordinals=%ld\n",
            static_cast<int>(mode), prepared, clone, failures);
    }
    aether_layout_result_destroy(source); ++scenarios;
}

static void retained_sequence(MapMode mode, bool prepared) {
    scenario = "retained publication and teardown";
    Fixture f(mode);
    auto* source = f.layout(prepared); ownership(source);
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(source, &clone) == AETHER_OK);
    compare_clone(source, clone); ownership(clone);
    const auto source_boxes = capture_boxes(source), clone_boxes = capture_boxes(clone);
    const auto assets = clone->assets;
    const auto saved_box = f.box(source, "a")->border_box;
    const auto hit = aether_layout_result_hit_test(source, saved_box.x + 1, saved_box.y + 90);
    REQUIRE(hit == f.node("a"));
    REQUIRE(aether_layout_result_hit_test(clone, saved_box.x + 1, saved_box.y + 90) == hit);
    std::vector<std::pair<const aether_node_s*, std::string>> synthetic;
    for (const auto& entry : assets->synthetic->ordered_entries())
        synthetic.emplace_back(entry->node(), entry->node()->text);
    f.add("#p{width:450px;height:220px;}#a{background:blue;}#tall{height:50px;}"); f.resolve();
    auto* next = f.layout(prepared, source); ownership(next); f.intact(); f.paint(next);
    REQUIRE(next->assets->base != assets->base);
    REQUIRE(same(f.box(next, "a")->border_box.height, 220));
    REQUIRE(same(f.box(next, "z")->border_box.height, 216));
    intact_boxes(source_boxes); intact_boxes(clone_boxes); f.paint(source); f.paint(clone);
    aether_layout_result_destroy(next);
    aether_layout_result_destroy(source);
    aether_style_map_destroy(f.map); f.map = nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime = nullptr; f.base.reset();
    intact_boxes(clone_boxes); ownership(clone); f.paint(clone); f.frozen_intact();
    aether_layout_result_t second = nullptr;
    REQUIRE(aether_layout_clone_snapshot(clone, &second) == AETHER_OK);
    compare_clone(clone, second); ownership(second); aether_layout_result_destroy(clone);
    f.paint(second);
    REQUIRE(aether_layout_result_hit_test(second, saved_box.x + 1, saved_box.y + 90) == hit);
    aether_doc_destroy(f.doc); f.doc = nullptr;
    f.frozen_intact();
    for (const auto& entry : synthetic) REQUIRE(entry.first->text == entry.second);
    REQUIRE(aether_layout_result_hit_test(second, saved_box.x + 1, saved_box.y + 90) == nullptr);
    auto* output = second;
    REQUIRE(aether_layout_clone_snapshot(second, &output) == AETHER_ERROR_INVALID_STATE && output == second);
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, second, nullptr, &list) == AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_destroy(second); ++scenarios;
}

enum class Action { detach, attribute, text, same_engine, nested_engine, copy_style,
                    bad_width, bad_height, bad_baseline };
struct Callback {
    Fixture* outer = nullptr;
    Fixture* inner = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t retained = nullptr;
    aether_node_t detached = nullptr;
    Action action = Action::copy_style;
    const char* target = "DEFERA";
    int calls = 0, target_calls = 0, trigger = 1, fired_at = 0;
    float bad = 0;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style,
                                         float, void* data) {
        auto& c = *static_cast<Callback*>(data); ++c.calls;
        aether_text_metrics_t metrics{float(std::strlen(text)) * 5, 10, 8};
        if (!std::strstr(text, c.target) || ++c.target_calls != c.trigger) return metrics;
        c.fired_at = c.calls;
        if (c.action == Action::detach) {
            c.detached = aether_node_get_child(c.outer->node("leaf"), 0);
            REQUIRE(aether_node_remove_child(c.outer->node("leaf"), c.detached) == AETHER_OK);
        } else if (c.action == Action::attribute) {
            REQUIRE(aether_element_set_attribute(c.outer->node("z"), "class", "changed") == AETHER_OK);
        } else if (c.action == Action::text) {
            // No public text setter exists. Exercise the private DOM mutation
            // contract with its real epoch helper, not an untracked edit.
            auto* node = aether_node_get_child(c.outer->node("last"), 0);
            REQUIRE(node && node->type == AETHER_NODE_TEXT);
            node->text = "changed"; aether_node_touch(node);
        } else if (c.action == Action::same_engine) {
            auto* output = c.retained;
            REQUIRE(aether_layout_layout(c.engine, c.outer->doc, c.outer->map, 500, 400,
                nullptr, nullptr, &output) == AETHER_ERROR_INVALID_STATE && output == c.retained);
        } else if (c.action == Action::nested_engine) {
            const auto* lookup = c.engine->measure_styles;
            const auto document = c.engine->measure_document;
            auto* nested = c.inner->layout(true); ownership(nested); c.inner->intact();
            aether_layout_result_destroy(nested); engine_clean(c.inner->engine);
            REQUIRE(c.engine->measure_styles == lookup && c.engine->measure_document == document);
            REQUIRE(c.engine->layout_in_progress);
        } else if (c.action == Action::copy_style) {
            auto* copy = const_cast<aether_computed_style_t*>(style);
            copy->font_size = 9876; copy->font_family[0] = '!';
            copy->width = aether_css_length_auto();
        } else if (c.action == Action::bad_width) metrics.width = c.bad;
        else if (c.action == Action::bad_height) metrics.height = c.bad;
        else metrics.baseline = c.bad;
        return metrics;
    }
    static aether_text_metrics_t contextual(const char* text, const aether_computed_style_t* style,
                                            float, float width, void* data) {
        return measure(text, style, width, data);
    }
};
static void callback_case(MapMode mode, bool contextual, Action action, const char* target,
                          float bad = 0, int trigger = 1) {
    scenario = target;
    Fixture f(mode), inner(mode, "#p{height:90px;}");
    auto* retained = f.layout(); const auto saved = capture_boxes(retained);
    const auto primary = retained->primary_boxes;
    const auto order = retained->paint_order;
    Callback c; c.outer = &f; c.inner = &inner; c.retained = retained;
    c.action = action; c.bad = bad; c.target = target; c.trigger = trigger;
    REQUIRE(aether_layout_create(Callback::measure, &c, &c.engine) == AETHER_OK);
    if (contextual) aether_layout_set_contextual_text_measurer(c.engine, Callback::contextual, &c);
    auto* output = retained;
    REQUIRE(live_allocations == 0 && live_bytes == 0);
    // Observe cleanup for errors after several other deferred slots completed.
    // DOM edits intentionally allocate persistent document state, so track only
    // callbacks that do not change document ownership.
    const bool mutation = action == Action::detach || action == Action::attribute || action == Action::text;
    tracking = !mutation;
    const auto status = aether_layout_layout(c.engine, f.doc, f.map, 500, 400, nullptr, nullptr, &output);
    tracking = false;
    REQUIRE(c.fired_at > 0); engine_clean(c.engine);
    const bool invalid = mutation || action == Action::bad_width || action == Action::bad_height ||
                         action == Action::bad_baseline;
    if (invalid) {
        REQUIRE(status == AETHER_ERROR_INVALID_STATE && output == retained);
        REQUIRE(c.calls == c.fired_at);
    } else {
        REQUIRE(status == AETHER_OK && output != retained);
        ownership(output); f.paint(output);
        for (const auto& node : f.dom) {
            const auto entry = primary.find(node.node);
            if (entry != primary.end())
                REQUIRE(same(output->primary_boxes.at(entry->first)->border_box, entry->second->border_box));
        }
        aether_layout_result_destroy(output);
    }
    REQUIRE(live_allocations == 0 && live_bytes == 0);
    intact_boxes(saved); f.frozen_intact();
    REQUIRE(retained->primary_boxes == primary && retained->paint_order == order);
    if (!mutation) { f.intact(); f.paint(retained); }
    aether_layout_destroy(c.engine); aether_layout_result_destroy(retained);
    if (c.detached) REQUIRE(aether_node_append_child(f.node("leaf"), c.detached) == AETHER_OK);
    ++callback_cases;
}

static void numeric_overflow(MapMode mode) {
    scenario = "deferred cross arithmetic overflow";
    Fixture good(mode);
    auto* retained = good.layout();
    const auto saved = capture_boxes(retained);
    for (const char* css : {
        "#a{margin-top:3e38px;margin-bottom:3e38px;}",
        "#a{margin-top:-3e38px;margin-bottom:-3e38px;}",
        "#a{padding-top:3e38px;padding-bottom:3e38px;}",
        "#a{border-top:3e38px solid;border-bottom:3e38px solid;}",
        "#p{height:3e38px;}#a{margin-top:-3e38px;}",
        "#z{margin-top:3e38%;}"}) {
        Fixture bad(mode, css);
        auto* output = retained;
        REQUIRE(live_allocations == 0 && live_bytes == 0);
        tracking = true;
        const auto status = aether_layout_layout(bad.engine, bad.doc, bad.map,
            500, 400, nullptr, nullptr, &output);
        tracking = false;
        if (status != AETHER_ERROR_INVALID_STATE)
            std::fprintf(stderr, "overflow mode=%d css=%s status=%d\n", int(mode), css, int(status));
        REQUIRE(status == AETHER_ERROR_INVALID_STATE && output == retained);
        REQUIRE(live_allocations == 0 && live_bytes == 0);
        engine_clean(bad.engine); bad.intact(); good.intact(); intact_boxes(saved);
        good.paint(retained);
        ++scenarios;
    }
    aether_layout_result_destroy(retained);
}

int main() {
    for (auto mode : {MapMode::legacy_partial, MapMode::legacy_complete, MapMode::contextual}) {
        numeric_overflow(mode);
        for (bool prepared : {false, true}) {
            allocation_sweeps(mode, prepared);
            retained_sequence(mode, prepared);
        }
        for (bool contextual : {false, true}) {
            for (const char* target : {"CONTENT", "TABLE", "DEFERA", "DEFERZ"}) {
                for (auto action : {Action::detach, Action::attribute, Action::text, Action::same_engine,
                                    Action::nested_engine, Action::copy_style})
                    callback_case(mode, contextual, action, target);
                for (float bad : {-1.f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
                    callback_case(mode, contextual, Action::bad_width, target, bad);
                    callback_case(mode, contextual, Action::bad_height, target, bad);
                    // Finite negative baselines are accepted by the API; only
                    // width and height have nonnegative contracts.
                    if (bad != -1.f) callback_case(mode, contextual, Action::bad_baseline, target, bad);
                }
            }
            // Fail on a later callback from an item whose final box has already
            // begun construction rather than only its first measurement.
            callback_case(mode, contextual, Action::bad_width, "DEFERZ",
                std::numeric_limits<float>::quiet_NaN(), 2);
        }
    }
    std::printf("flex stretch deferred private: scenarios=%d layout OOM=%ld clone OOM=%ld callbacks=%d; "
        "retained tables, anonymous overlays, ownership, frozen styles and leak ledger PASS\n",
        scenarios, layout_failures, clone_failures, callback_cases);
}
