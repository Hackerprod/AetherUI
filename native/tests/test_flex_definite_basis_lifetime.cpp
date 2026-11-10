#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>

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
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

using namespace aether::style;
using namespace aether::layout;
static int scenarios = 0, callback_cases = 0;
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
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_display_list_builder_t painter = nullptr;
    MapVersionRef base;
    struct FrozenSnapshot { FrozenNodeRef owner; std::array<unsigned char, sizeof(StyleScalars)> bytes; };
    std::vector<FrozenSnapshot> frozen;
    std::vector<NodeSnapshot> dom;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;

    Fixture(MapMode mode, bool col, const char* basis, int depth = 1, bool positioned = false, const std::string& extra = "") {
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
        css += extra;
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "definite-basis-lifetime", &sheet, nullptr) == AETHER_OK);
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
        remember_styles();
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
    void remember_styles() {
        for (const auto& entry : base->backing()->entries()) {
            FrozenSnapshot saved{entry.second,{}};
            std::memcpy(saved.bytes.data(), &entry.second->style_view().scalars(), sizeof(StyleScalars));
            frozen.push_back(saved);
        }
    }
    void frozen_intact() const {
        for (const auto& saved : frozen)
            REQUIRE(std::memcmp(saved.bytes.data(), &saved.owner->style_view().scalars(), sizeof(StyleScalars)) == 0);
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
        frozen_intact();
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
    REQUIRE(frame->assets->synthetic);
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
// The first item is eligible, the second deliberately remains content-based.
// Anonymous nested rows and positioned descendants exercise overlay publication.
static void mixed_lifetime(MapMode mode, bool column, bool stretch, const char* basis) {
    const std::string extra = std::string("#s{flex-basis:content;background:red;}") +
        "#c{width:100px;}#p{align-items:" + (stretch ? "stretch" : "flex-start") + ";}";
    Fixture f(mode, column, basis, 1, true, extra);
    auto* source = f.layout(); ownership(source); f.intact();
    const auto geometry = capture_boxes(source);
    const auto primary = source->primary_boxes;
    const auto paint_order = source->paint_order;
    const auto anonymous = source->anonymous_flex_items;
    const auto assets = source->assets;
    std::vector<std::pair<const aether_node_s*, std::string>> synthetic;
    for (const auto& entry : assets->synthetic->ordered_entries())
        synthetic.emplace_back(entry->node(),entry->node()->text);
    REQUIRE(assets->synthetic->ordered_entries().size() == 2);
    for (bool clone : {false,true}) {
        bool completed = false; long failures = 0;
        for (long ordinal = 0; ordinal < 20000; ++ordinal) {
            auto* output = source;
            REQUIRE(live_allocations == 0 && live_bytes == 0);
            allocation_count = 0; injected = false; tracking = true; fail_at = ordinal;
            const auto status = clone ? aether_layout_clone_snapshot(source,&output) :
                aether_layout_layout(f.engine,f.doc,f.map,400,400,nullptr,nullptr,&output);
            fail_at = -1; tracking = false;
            f.intact(); intact_boxes(geometry);
            REQUIRE(source->primary_boxes == primary && source->paint_order == paint_order);
            REQUIRE(source->anonymous_flex_items == anonymous && source->assets == assets);
            if (status == AETHER_OK) {
                REQUIRE(!injected && output && output != source);
                ownership(output); f.paint(output);
                for (const auto& node : f.dom) {
                    const auto found = primary.find(node.node);
                    if (found != primary.end())
                        REQUIRE(same(output->primary_boxes.at(node.node)->border_box,found->second->border_box));
                }
                if (clone) REQUIRE(output->assets == assets);
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
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(source,&clone) == AETHER_OK);
    const auto saved = capture_boxes(clone);
    const auto sibling = primary.at(aether_doc_get_element_by_id(f.doc,"s"))->border_box;
    const auto hit = aether_layout_result_hit_test(source,sibling.x+1,sibling.y+1);
    REQUIRE(hit);
    REQUIRE(aether_layout_result_hit_test(clone,sibling.x+1,sibling.y+1) == hit);
    // New frames and public source teardown cannot retire the clone's styles or
    // anonymous-node storage. Source text remains owned by the shared assets.
    auto* next = f.layout(); ownership(next); aether_layout_result_destroy(next);
    aether_layout_result_destroy(source);
    aether_style_map_destroy(f.map); f.map = nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime = nullptr; f.base.reset();
    intact_boxes(saved); ownership(clone); f.paint(clone); f.frozen_intact();
    REQUIRE(aether_layout_result_hit_test(clone,sibling.x+1,sibling.y+1) == hit);
    aether_doc_destroy(f.doc); f.doc = nullptr;
    intact_boxes(saved); f.frozen_intact();
    for (const auto& saved_node : synthetic) REQUIRE(saved_node.first->text == saved_node.second);
    REQUIRE(aether_layout_result_hit_test(clone,sibling.x+1,sibling.y+1) == nullptr);
    auto* output = clone;
    REQUIRE(aether_layout_clone_snapshot(clone,&output) == AETHER_ERROR_INVALID_STATE && output == clone);
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter,clone,nullptr,&list) == AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_destroy(clone); ++scenarios;
}

// Explicit min-width:auto is not accepted by every producer yet. Publish a
// fresh authentic immutable legacy record instead of mutating frozen storage.
static void automatic_minimum(Fixture& f) {
    REQUIRE(f.base->mode() == MapMode::legacy_partial);
    auto* node = aether_doc_get_element_by_id(f.doc,"c");
    const auto original = f.base->backing()->find_owner(node);
    auto scalars = original->style_view().scalars();
    scalars.min_width = aether_css_length_auto();
    NodeRecordInput input;
    input.origin = original->origin(); input.node = node;
    input.style = std::make_shared<const FrozenStyle>(scalars,original->style()->payload_owner());
    input.measures = original->measures(); input.custom_properties = original->custom_properties();
    input.color_references = original->color_references();
    input.declared_impact = original->declared_impact();
    input.declared_inherited_impact = original->declared_inherited_impact();
    input.inherited_change_impact = original->inherited_change_impact();
    input.document_identity = original->document_identity(); input.node_identity = original->node_identity();
    input.node_lifetime = original->node_lifetime(); input.document_lifetime = original->document_lifetime();
    FrozenNodeRef record; REQUIRE(freeze_node_record(input,record) == AETHER_OK);
    BackingBuildInput backing{f.base->backing()->entries()}; backing.entries[node] = record;
    MapVersionInput version; REQUIRE(freeze_backing(backing,version.backing) == AETHER_OK);
    version.mode = MapMode::legacy_partial;
    REQUIRE(make_map_version(version,f.map->current) == AETHER_OK);
    f.base = f.map->current; f.remember_styles();
}

static void automatic_minimum_geometry() {
    struct Case { const char* css; float width; };
    for (const auto& test : {
            Case{"",25},
            Case{"#c{overflow-x:hidden;}",10},
            // CSS computes the opposite visible axis to auto here.
            Case{"#c{overflow-y:hidden;}",10},
            Case{"#c{overflow-x:auto;}",10},
            Case{"#c{overflow-x:scroll;}",10},
            Case{"#c{width:20px;}",20},
            Case{"#c{max-width:15px;}",15},
            Case{"#c{width:20px;max-width:15px;}",15}}) {
        Fixture f(MapMode::legacy_partial,false,"10px",0,false,
            std::string("#c{max-width:none;}#leaf{width:auto;height:auto;}#s{flex-basis:70px;}") + test.css);
        automatic_minimum(f);
        auto* frame = f.layout();
        const auto* child = frame->primary_boxes.at(aether_doc_get_element_by_id(f.doc,"c"));
        if (!same(child->border_box.width,test.width)) std::fprintf(stderr,"auto min case %s got=%g expected=%g\n",test.css,child->border_box.width,test.width);
        REQUIRE(same(child->border_box.width,test.width));
        f.intact(); ownership(frame); f.paint(frame);
        aether_layout_result_destroy(frame);
    }
}

enum class Action { mutate, same_engine, nested_engine, copy_style, bad_width, bad_height, bad_baseline };
struct Callback {
    Fixture* outer = nullptr;
    Fixture* inner = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t retained = nullptr;
    aether_node_t detached = nullptr;
    Action action = Action::copy_style;
    int calls = 0, trigger = 1;
    float bad = 0;
    bool entered = false;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t* style,
                                         float, void* data) {
        auto& c = *static_cast<Callback*>(data); ++c.calls;
        aether_text_metrics_t metrics{float(std::strlen(text))*5,10,8};
        if (c.calls != c.trigger) return metrics;
        c.entered = true;
        if (c.action == Action::mutate) {
            auto* leaf = aether_doc_get_element_by_id(c.outer->doc,"leaf");
            c.detached = aether_node_get_child(leaf,0);
            REQUIRE(aether_node_remove_child(leaf,c.detached) == AETHER_OK);
        } else if (c.action == Action::same_engine) {
            auto* output = c.retained;
            REQUIRE(aether_layout_layout(c.engine,c.outer->doc,c.outer->map,400,400,
                nullptr,nullptr,&output) == AETHER_ERROR_INVALID_STATE);
            REQUIRE(output == c.retained);
        } else if (c.action == Action::nested_engine) {
            auto* nested = c.inner->layout(); ownership(nested); c.inner->intact();
            aether_layout_result_destroy(nested);
        } else if (c.action == Action::copy_style) {
            // CallbackStyleCopy is private mutable storage despite the public
            // const view. Scribbling it must not change a frozen style or frame.
            auto* copy = const_cast<aether_computed_style_t*>(style);
            copy->font_size = 9876;
            copy->font_family[0] = '!';
            copy->width = aether_css_length_auto();
        } else if (c.action == Action::bad_width) metrics.width = c.bad;
        else if (c.action == Action::bad_height) metrics.height = c.bad;
        else metrics.baseline = c.bad;
        return metrics;
    }
    static aether_text_metrics_t contextual(const char* text, const aether_computed_style_t* style,
                                            float, float width, void* data) {
        return measure(text,style,width,data);
    }
};
static void callback_case(MapMode mode, bool minimum, bool contextual, Action action, float bad = 0,
                          int trigger = 1) {
    Fixture f(mode,false,"60px",0,false,
        "#p{align-items:flex-start;}#c{max-width:none;}#leaf{width:auto;height:auto;}#s{flex-basis:70px;}");
    if (minimum) automatic_minimum(f);
    Fixture inner(mode,false,"auto",1,false,"#c{width:60px;}#s{flex-basis:content;}");
    auto* retained = f.layout(); const auto saved = capture_boxes(retained);
    const auto primary = retained->primary_boxes;
    Callback c; c.outer = &f; c.inner = &inner; c.retained = retained;
    c.action = action; c.bad = bad; c.trigger = trigger;
    REQUIRE(aether_layout_create(Callback::measure,&c,&c.engine) == AETHER_OK);
    if (contextual) aether_layout_set_contextual_text_measurer(c.engine,Callback::contextual,&c);
    auto* output = retained;
    const auto status = aether_layout_layout(c.engine,f.doc,f.map,400,400,nullptr,nullptr,&output);
    REQUIRE(c.entered);
    const bool invalid = action == Action::mutate || action == Action::bad_width ||
        action == Action::bad_height || action == Action::bad_baseline;
    if (invalid) {
        REQUIRE(status == AETHER_ERROR_INVALID_STATE && output == retained);
        REQUIRE(c.calls == trigger);
    } else {
        REQUIRE(status == AETHER_OK && output != retained);
        ownership(output); f.paint(output);
        for (const auto& entry : primary)
            REQUIRE(same(output->primary_boxes.at(entry.first)->border_box,entry.second->border_box));
        aether_layout_result_destroy(output);
    }
    intact_boxes(saved); f.frozen_intact();
    REQUIRE(retained->primary_boxes == primary);
    if (action != Action::mutate) { f.intact(); f.paint(retained); }
    aether_layout_destroy(c.engine); aether_layout_result_destroy(retained);
    // Removal leaves the node alive for retained consumers but outside document
    // ownership. Restore fixture ownership only after those consumers are gone.
    if (c.detached) REQUIRE(aether_node_append_child(
        aether_doc_get_element_by_id(f.doc,"leaf"), c.detached) == AETHER_OK);
    ++callback_cases;
}
int main() {
    scopes(); automatic_minimum_geometry();
    for (auto mode : {MapMode::legacy_partial,MapMode::legacy_complete,MapMode::contextual}) {
        for (bool column : {false,true}) for (bool stretch : {false,true})
            for (const char* basis : {"60px","auto"}) mixed_lifetime(mode,column,stretch,basis);
        for (bool contextual : {false,true}) {
            for (auto action : {Action::mutate,Action::same_engine,Action::nested_engine,Action::copy_style})
                callback_case(mode,false,contextual,action);
            for (float bad : {-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
                callback_case(mode,false,contextual,Action::bad_width,bad);
                callback_case(mode,false,contextual,Action::bad_height,bad);
                if (bad != -1.f) callback_case(mode,false,contextual,Action::bad_baseline,bad);
            }
        }
    }
    // With no anonymous prefix and a definite basis, the first callback is
    // final layout for min-width:0, but required min-content for auto minimum.
    // Trigger 2 also guards abort after a successfully measured prefix.
    for (bool contextual : {false,true}) for (int trigger : {1,2}) {
        callback_case(MapMode::legacy_partial,true,contextual,Action::mutate,0,trigger);
        callback_case(MapMode::legacy_partial,true,contextual,Action::bad_width,
            std::numeric_limits<float>::quiet_NaN(),trigger);
        callback_case(MapMode::legacy_partial,true,contextual,Action::same_engine,0,trigger);
        callback_case(MapMode::legacy_partial,true,contextual,Action::nested_engine,0,trigger);
        callback_case(MapMode::legacy_partial,true,contextual,Action::copy_style,0,trigger);
    }
    std::printf("flex definite basis lifetime: scenarios=%d layout OOM=%ld clone OOM=%ld callbacks=%d auto-min geometry=8; metrics, frozen styles, ownership and retained consumers PASS\n",
        scenarios,layout_failures,clone_failures,callback_cases);
}
