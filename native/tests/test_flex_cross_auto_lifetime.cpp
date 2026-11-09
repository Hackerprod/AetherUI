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
void* operator new(std::size_t size) {
    const auto ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) {
        injected = true;
        throw std::bad_alloc();
    }
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

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
static bool same(float a, float b) { return std::isfinite(a) && std::fabs(a - b) < .01f; }
static bool same(aether_rectf a, aether_rectf b) {
    return same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height);
}
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
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

    Fixture(MapMode mode, bool col, int lines, bool stretch, bool forced = false, int limit_case = 0) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        std::string body;
        if (lines) body = "<div id='c'><div id='d'></div></div>";
        if (lines > 1) body += "<div id='s'></div><div id='t'></div>";
        const std::string html = "<html><body><div id='p'>" + body + "</div></body></html>";
        REQUIRE(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{400, 400, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        const std::string cross = col ? "width" : "height";
        const std::string main = col ? "height" : "width";
        std::string css = "*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}"
            "#p{display:flex;width:100px;height:100px;flex-direction:" + std::string(col ? "column" : "row") +
            ";flex-wrap:wrap;align-items:" + (stretch ? "stretch;" : "flex-end;") + "}" +
            "#c,#s,#t{flex:0 0 auto;" + main + ":60px;}#s{" + cross + ":30px;}#t{" + cross + ":20px;}";
        if (stretch) {
            css += "#c{" + cross + ":auto;min-" + cross + ":10px;" +
                (col ? "margin-top:auto;" : "margin-left:auto;") + "background:red;}";
        } else {
            css += "#c{" + cross + ":20px;" + (col ? "margin-left:auto;margin-right:auto;" :
                "margin-top:auto;margin-bottom:auto;") + "background:red;}";
        }
        css += "#d{width:5px;height:5px;}";
        if (forced) css += "#p{height:auto;flex-wrap:nowrap;}#c{height:50%;margin-top:auto;margin-bottom:auto;}#d{height:20px;}";
        if (limit_case == 1) css += "#p{height:auto;min-height:100px;}";
        if (limit_case == 2) {
            css += "#p{height:auto;max-height:120px;}";
            // Force natural cross extent above the parent's maximum while
            // retaining auto height (and stretch eligibility) in that variant.
            if (lines > 0) {
                css += stretch ? "#c{height:auto;min-height:160px;}"
                               : "#c{height:160px;min-height:160px;}";
            }
        }
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
        base_count = base->size();
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
        REQUIRE(base->size() == base_count);
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
static void sweeps(MapMode mode, bool col, int lines, bool stretch, int limit_case = 0) {
    Fixture f(mode, col, lines, stretch, false, limit_case);
    auto* source = f.layout();
    if (limit_case != 0) {
        auto* parent = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "p"));
        REQUIRE(same(parent->content_box.height, limit_case == 1 ? 100.f : lines > 0 ? 120.f : 0.f));
        if (limit_case == 2 && lines > 0) {
            auto* child = source->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "c"));
            REQUIRE(same(child->border_box.height, 160.f));
        }
    }
    const auto geometry = capture_boxes(source);
    const auto primary = source->primary_boxes;
    const auto paint_order = source->paint_order;
    const auto assets = source->assets;
    long failures = 0;
    bool completed = false;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        aether_layout_result_t output = source;
        allocation_count = 0;
        injected = false;
        fail_at = ordinal;
        const auto status = aether_layout_layout(f.engine, f.doc, f.map, 400, 400,
                                                nullptr, nullptr, &output);
        fail_at = -1;
        f.intact();
        intact_boxes(geometry);
        REQUIRE(source->primary_boxes == primary && source->paint_order == paint_order);
        REQUIRE(source->assets == assets);
        if (status == AETHER_OK) {
            REQUIRE(!injected && output && output != source);
            f.paint(output);
            aether_layout_result_destroy(output);
            completed = true;
            break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source);
        ++failures;
    }
    REQUIRE(completed && failures > 0);
    layout_failures += failures;
    std::printf("mode=%d axis=%s lines=%d stretch=%d limit=%d layout OOM ordinals=%ld\n",
                static_cast<int>(mode), col ? "column" : "row", lines, stretch, limit_case, failures);
    completed = false;
    failures = 0;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        aether_layout_result_t output = source;
        allocation_count = 0;
        injected = false;
        fail_at = ordinal;
        const auto status = aether_layout_clone_snapshot(source, &output);
        fail_at = -1;
        f.intact();
        intact_boxes(geometry);
        if (status == AETHER_OK) {
            REQUIRE(!injected && output && output != source && output->assets == assets);
            f.paint(output);
            aether_layout_result_destroy(output);
            completed = true;
            break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source);
        ++failures;
    }
    REQUIRE(completed && failures > 0);
    clone_failures += failures;
    f.paint(source);
    // Repeated relayouts must not modify the retained source snapshot.
    for (int pass = 0; pass < 3; ++pass) {
        auto* next = f.layout();
        f.paint(next);
        aether_layout_result_destroy(next);
        f.intact();
        intact_boxes(geometry);
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(source, &clone) == AETHER_OK);
    const auto clone_geometry = capture_boxes(clone);
    aether_layout_result_destroy(source);
    f.intact();
    aether_style_map_destroy(f.map);
    f.map = nullptr;
    aether_style_runtime_destroy(f.runtime);
    f.runtime = nullptr;
    f.base.reset();
    intact_boxes(clone_geometry);
    f.paint(clone);
    aether_doc_destroy(f.doc);
    f.doc = nullptr;
    REQUIRE(!StyleOperationPin(clone->assets, {}, false).structure_valid());
    aether_layout_result_t output = clone;
    REQUIRE(aether_layout_clone_snapshot(clone, &output) == AETHER_ERROR_INVALID_STATE && output == clone);
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, clone, nullptr, &list) == AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_destroy(clone);
    ++scenarios;
}
static void forced_height(MapMode mode) {
    Fixture f(mode, false, 1, false, true);
    auto* source = f.layout();
    StyleLookup styles(source->assets, {}, false);
    for (auto kind : {forced_height_kind_e::definite, forced_height_kind_e::indefinite}) {
        // Exercise the internal forced-height contract without changing styles
        // or making a public API promise about numerical available height.
        auto* output = new aether_layout_result_s();
        output->assets = source->assets;
        output->viewport = {400, 400};
        auto* p = aether_doc_get_element_by_id(f.doc, "p");
        auto* box = layout_element(f.engine, &styles, p, {0, 0, 100, 400}, std::nullopt,
                                  0, true, 100, true, 100, kind, true, output);
        REQUIRE(box != nullptr);
        output->root = box;
        auto* c = output->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "c"));
        REQUIRE(same(box->content_box.height, 100));
        const bool definite = kind == forced_height_kind_e::definite;
        REQUIRE(same(c->border_box.height, definite ? 50.f : 20.f));
        REQUIRE(same(c->border_box.y - box->content_box.y, definite ? 25.f : 40.f));
        f.intact();
        aether_layout_result_destroy(output);
    }
    aether_layout_result_destroy(source);
    std::printf("mode=%d forced definite/indefinite: 2 fixtures\n", static_cast<int>(mode));
}
static void changing_revisions() {
    // One DOM and one runtime throughout. Replacing only the override sheet
    // exercises actual revision/map changes rather than fresh-document layout.
    Fixture f(MapMode::contextual, false, 1, false);
    struct SavedRevision {
        aether_layout_result_t frame;
        aether_style_map_t map;
        MapVersionRef base;
        size_t base_count;
        std::vector<BoxSnapshot> boxes;
    };
    std::vector<SavedRevision> retained;
    auto retain = [&](aether_layout_result_t frame) {
        aether_layout_result_t clone = nullptr;
        REQUIRE(aether_layout_clone_snapshot(frame, &clone) == AETHER_OK);
        REQUIRE(clone->assets == frame->assets);
        retained.push_back({clone, f.map, f.base, f.base_count, capture_boxes(clone)});
        aether_layout_result_destroy(frame);
    };
    auto intact = [&]() {
        f.intact();
        for (const auto& saved : retained) {
            REQUIRE(saved.map->current == saved.base);
            REQUIRE(saved.base->size() == saved.base_count);
            REQUIRE(saved.frame->assets->base == saved.base);
            REQUIRE(StyleOperationPin(saved.frame->assets, {}, false).structure_valid());
            intact_boxes(saved.boxes);
            f.paint(saved.frame);
        }
    };
    retain(f.layout());
    int64_t override_sheet = 0;
    int transitions = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (float size : {40.f, 160.f}) {
            for (bool wrap : {false, true}) {
                for (int margin = 0; margin < 4; ++margin) {
                    for (int sizing = 0; sizing < 3; ++sizing) {
                        f.intact();
                        if (override_sheet) {
                            REQUIRE(aether_style_runtime_remove_stylesheet(f.runtime, override_sheet));
                        }
                        const char* parent = sizing == 0 ? "height:100px;" : sizing == 1
                            ? "height:auto;min-height:100px;" : "height:auto;max-height:100px;";
                        std::string css = std::string("#p{align-items:center;min-height:0;max-height:none;") +
                            parent + (wrap ? "flex-wrap:wrap;}" : "flex-wrap:nowrap;}") +
                            "#c{height:" + std::to_string(size) + "px;margin-top:" +
                            (margin == 1 || margin == 2 ? "auto;" : "0;") + "margin-bottom:" +
                            (margin == 1 || margin == 3 ? "auto;}" : "0;}");
                        REQUIRE(aether_style_runtime_add_stylesheet(f.runtime, css.c_str(), 0,
                                "changing-cross-auto", &override_sheet, nullptr) == AETHER_OK);
                        aether_style_update_t* updates = nullptr;
                        const int count = aether_style_runtime_resolve_subtree(
                            f.runtime, aether_doc_get_document_element(f.doc), &updates);
                        REQUIRE(count >= 0);
                        aether_style_updates_free(updates, count);
                        aether_style_revision_t revision = nullptr;
                        REQUIRE(aether_style_runtime_retain_revision(f.runtime, &revision) == AETHER_OK);
                        aether_style_map_t next_map = nullptr;
                        REQUIRE(aether_style_revision_create_map(revision, &next_map) == AETHER_OK);
                        aether_style_revision_destroy(revision);
                        REQUIRE(next_map->current != f.base);
                        // Editing/resolving the sheet may advance style_tree_epoch,
                        // but must preserve structure, mutation and actual DOM.
                        // Accept that authorized epoch change before any layout;
                        // all later intact() calls still reject layout mutations.
                        f.dom_intact();
                        f.style_epoch = f.doc->identity->style_tree_epoch;
                        // Previous map handles stay alive in retained; they must
                        // not silently acquire the newly resolved style version.
                        f.map = next_map;
                        f.base = next_map->current;
                        f.base_count = f.base->size();
                        auto* frame = f.layout();
                        auto* p = frame->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "p"));
                        auto* c = frame->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "c"));
                        const float used = sizing == 0 ? 100.f : sizing == 1
                            ? std::max(size, 100.f) : std::min(size, 100.f);
                        const float line = wrap ? std::max(size, used) : used;
                        const float free = line - size;
                        const float offset = margin == 0 ? free / 2.f : margin == 1
                            ? std::max(free, 0.f) / 2.f : margin == 2 ? std::max(free, 0.f) : 0.f;
                        REQUIRE(same(p->content_box.height, used));
                        REQUIRE(same(c->border_box.height, size));
                        REQUIRE(same(c->border_box.y - p->content_box.y, offset));
                        REQUIRE(same(c->margin_box.height, margin ? std::max(line, size) : size));
                        // A second layout on this same revision must not consume
                        // auto margins twice or mutate the first frame's boxes.
                        const auto boxes = capture_boxes(frame);
                        auto* repeated = f.layout();
                        auto* again = repeated->primary_boxes.at(aether_doc_get_element_by_id(f.doc, "c"));
                        REQUIRE(same(again->border_box, c->border_box));
                        REQUIRE(same(again->margin_box, c->margin_box));
                        intact_boxes(boxes);
                        aether_layout_result_destroy(repeated);
                        intact();
                        retain(frame);
                        ++transitions;
                    }
                }
            }
        }
    }
    intact();
    // Fixture owns the most recent map; release every older retained handle.
    for (const auto& saved : retained) {
        aether_layout_result_destroy(saved.frame);
        if (saved.map != f.map) aether_style_map_destroy(saved.map);
    }
    REQUIRE(transitions == 96);
    std::printf("same DOM contextual revisions: transitions=%d retained snapshots=%zu relayouts=%d\n",
                transitions, retained.size(), transitions * 2);
}
static void nonfinite_pair() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    int count = 0;
    for (float available : {nan, inf, -inf}) {
        for (float fixed : {-12.f, 0.f, 12.f}) {
            for (bool before_auto : {false, true}) {
                for (bool after_auto : {false, true}) {
                    const auto pair = aether_resolve_auto_margin_pair(
                        available, 40, fixed, -fixed, before_auto, after_auto);
                    REQUIRE(same(pair.before, before_auto ? 0.f : fixed));
                    REQUIRE(same(pair.after, after_auto ? 0.f : -fixed));
                    ++count;
                }
            }
        }
    }
    std::printf("nonfinite shared auto-margin helper: %d cases\n", count);
}
int main() {
    nonfinite_pair();
    changing_revisions();
    for (auto mode : {MapMode::contextual, MapMode::legacy_complete, MapMode::legacy_partial}) {
        for (bool col : {false, true}) {
            for (int lines : {0, 1, 3}) {
                for (bool stretch : {false, true}) sweeps(mode, col, lines, stretch);
            }
        }
        for (int limit_case : {1, 2}) {
            for (int lines : {0, 1, 3}) {
                for (bool stretch : {false, true}) sweeps(mode, false, lines, stretch, limit_case);
            }
        }
        forced_height(mode);
    }
    std::printf("flex cross auto lifetime: scenarios=%d layout OOM=%ld clone OOM=%ld (NDEBUG-safe)\n",
                scenarios, layout_failures, clone_failures);
}
