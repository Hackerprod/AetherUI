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

#define REQUIRE(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        std::abort(); \
    } \
} while (false)

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
static bool same(float a, float b) { return std::isfinite(a) && std::fabs(a - b) < .001f; }
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
    struct StyleSnapshot { FrozenNodeRef owner; std::array<unsigned char, sizeof(StyleScalars)> scalars; };
    std::vector<StyleSnapshot> frozen;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;

    Fixture(MapMode mode, const std::string& extra = "") {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,
            "<html><body id='p'><div id='a'><div id='ca'><div id='x'></div><div id='y'></div></div></div>"
            "<div id='b'><div id='cb'></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{500, 500, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        std::string css = "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}"
            "#p{display:flex;width:100%;height:100%;align-items:stretch;}"
            "#a,#b{min-width:0;flex:0 1 content;}"
            "#ca,#cb{width:100px;height:20px;}#ca{padding-left:10%;padding-right:10%;}#b{background:red;}";
        css += extra;
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "cyclic-edges-lifetime", &sheet, nullptr) == AETHER_OK);
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
        for (const auto& entry : base->backing()->entries()) {
            StyleSnapshot saved{entry.second, {}};
            std::memcpy(saved.scalars.data(), &entry.second->style_view().scalars(), sizeof(StyleScalars));
            frozen.push_back(saved);
        }
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
        for (const auto& saved : frozen) {

            REQUIRE(std::memcmp(saved.scalars.data(), &saved.owner->style_view().scalars(), sizeof(StyleScalars)) == 0);
        }
    }
    aether_layout_result_t layout(float width, float height) {
        aether_style_map_t used_map=map, contextual_map=nullptr;
        if(base->contextual()) {
            // Contextual maps retain their viewport. Resize obtains a new
            // revision while preserving the old map/record owners for checks.
            aether_style_environment_t env{width,height,1,16,0,0,0};
            aether_style_transaction_t transaction=nullptr;
            aether_style_revision_t revision=nullptr;
            REQUIRE(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),
                    &env,&transaction)==AETHER_OK);
            REQUIRE(aether_style_runtime_commit_revision(runtime,transaction,&revision)==AETHER_OK);
            aether_style_transaction_destroy(transaction);
            REQUIRE(aether_style_revision_create_map(revision,&contextual_map)==AETHER_OK);
            aether_style_revision_destroy(revision);
            used_map=contextual_map;
            style_epoch=doc->identity->style_tree_epoch;
        }
        const auto used_base=used_map->current;
        if (contextual_map) for (const auto& entry : used_base->backing()->entries()) {
            StyleSnapshot saved{entry.second, {}};
            std::memcpy(saved.scalars.data(), &entry.second->style_view().scalars(), sizeof(StyleScalars));
            frozen.push_back(saved);
        }
        aether_layout_result_t frame = nullptr;
        REQUIRE(aether_layout_layout(engine, doc, used_map, width, height, nullptr, nullptr, &frame) == AETHER_OK);
        REQUIRE(frame && frame->assets->base == used_base && !frame->build_overlay);
        if(contextual_map) aether_style_map_destroy(contextual_map);
        return frame;
    }
    void change(const char* declarations) {
        const auto mode = base->mode();
        int64_t sheet = 0;
        const std::string css = std::string("#ca{") + declarations + "}";
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "alternate-cyclic-edges", &sheet, nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,
                aether_doc_get_document_element(doc), &updates);
        REQUIRE(count >= 0); aether_style_updates_free(updates, count);
        aether_style_map_t next = nullptr;
        if (mode == MapMode::contextual) {
            aether_style_revision_t revision = nullptr;
            REQUIRE(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK);
            REQUIRE(aether_style_revision_create_map(revision, &next) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else if (mode == MapMode::legacy_complete) {
            // Updating a subset intentionally changes a complete map to partial;
            // retain complete mode by publishing a fresh resolved map instead.
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&next)==AETHER_OK);
        } else {
            // Legacy publication advances the version inside the same map.
            // Keep old version owners alive for retained-frame byte checks.
            next = map;
            std::vector<aether_node_t> elements;
            for (const auto& n : dom) if (n.type == AETHER_NODE_ELEMENT) elements.push_back(n.node);
            REQUIRE(aether_style_runtime_update_resolved_style_map(runtime, next, elements.data(),
                    static_cast<int32_t>(elements.size())) == AETHER_OK);
        }
        if (next != map) aether_style_map_destroy(map);
        map = next; base = next->current;
        REQUIRE(base->mode() == mode);
        base_count = base->backing()->size();
        for (const auto& entry : base->backing()->entries()) {
            StyleSnapshot saved{entry.second, {}};
            std::memcpy(saved.scalars.data(), &entry.second->style_view().scalars(), sizeof(StyleScalars));
            frozen.push_back(saved);
        }
        style_epoch = doc->identity->style_tree_epoch;
    }
    void paint(aether_layout_result_t frame) const {
        aether_display_list_t list = nullptr;
        REQUIRE(aether_display_list_build(painter, frame, nullptr, &list) == AETHER_OK);
        REQUIRE(list != nullptr);
        aether_display_list_destroy(list);
    }
};

static aether_rectf rect(const Fixture& f, aether_layout_result_t frame, const char* id) {
    auto* box = aether_layout_result_find_primary_box(frame, aether_doc_get_element_by_id(f.doc,id));
    REQUIRE(box); return aether_layout_box_get_border_box(box);
}
static void consumers(const Fixture& f, aether_layout_result_t frame, aether_rectf expected) {
    // Probe the stretched region beyond the 20px descendant, so B is the hit.
    REQUIRE(aether_layout_result_hit_test(frame, expected.x+expected.width-1,
            expected.y+expected.height-1) == aether_doc_get_element_by_id(f.doc,"b"));
    aether_display_list_t list = nullptr;
    REQUIRE(aether_display_list_build(f.painter, frame, nullptr, &list) == AETHER_OK);
    int red = 0;
    for (int i=0; i<aether_display_list_get_count(list); ++i) {
        aether_paint_command_t command{};
        REQUIRE(aether_display_list_get_command(list,i,&command) == AETHER_OK);
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& fill=command.data.fill_rounded_rect;
            if (fill.color.r>.99f && fill.color.g<.01f && fill.color.b<.01f) {
                ++red; REQUIRE(same(fill.rect,expected));
            }
        }
        aether_paint_command_free(&command);
    }
    REQUIRE(red==1); aether_display_list_destroy(list);
}
static void retained(MapMode mode) {
    Fixture f(mode);
    auto* first = f.layout(500, 300);
    REQUIRE(same(rect(f, first, "a").width, 100));
    REQUIRE(same(rect(f, first, "ca").width, 120));
    const auto boxes = capture_boxes(first);
    const auto primary = first->primary_boxes;
    const auto order = first->paint_order;
    const auto b = rect(f, first, "b");
    struct Case { const char* css; float basis; float child; float offset; };
    for (const auto& test : {
            Case{"padding:0 10%;margin:0;", 100,120,0},
            Case{"padding:0 calc(10% + 5px);margin:0;", 110,132,0},
            Case{"padding:0;margin:0 10%;", 100,100,10},
            Case{"padding:0;margin:0 calc(10% + 5px);", 110,100,16},
            Case{"padding:0;margin:0 calc(10% - 5px);", 90,100,4},
            Case{"padding:0 calc(0% + 5px);margin:0;", 110,110,0},
            Case{"padding:0 calc(5px);margin:0;", 110,110,0},
            Case{"padding:0 calc(10% + 5px);margin:0;max-width:80px;",90,108,0},
            Case{"padding:0 calc(10% + 5px);margin:0;min-width:140px;max-width:80px;",150,180,0},
            Case{"box-sizing:border-box;width:20px;min-width:0;max-width:10px;padding:0 10%;border:30px solid black;margin:0;",60,72,0},
            Case{"box-sizing:content-box;width:100px;min-width:0;max-width:none;border:0;padding:0 10%;margin:0;",100,120,0}}) {
        f.change(test.css);
        const auto unchanged_map = f.map;
        if (std::strstr(test.css,"padding:0 calc(10% + 5px)")) {
            const auto owner=f.base->backing()->find_owner(aether_doc_get_element_by_id(f.doc,"ca"));
            const auto edge=owner->style_view().scalars().padding.left;
            REQUIRE(edge.unit==AETHER_CSS_UNIT_LINEAR);
            REQUIRE(same(edge.value,5) && same(edge.linear_percent,10));
        }
        for (float available : {500.f, 800.f, 500.f}) {
            auto* next = f.layout(available, 300);
            const auto a = rect(f,next,"a"), c=rect(f,next,"ca"), current_b=rect(f,next,"b");
            REQUIRE(same(a.width,test.basis));
            REQUIRE(same(c.width,test.child));
            REQUIRE(same(c.x-a.x,test.offset));
            REQUIRE(same(current_b.width,100));
            REQUIRE(same(current_b.x,a.x+a.width));
            REQUIRE(f.map == unchanged_map);
            consumers(f,next,current_b); consumers(f,first,b);
            f.intact(); intact_boxes(boxes);
            REQUIRE(first->primary_boxes==primary && first->paint_order==order);
            aether_layout_result_destroy(next);
        }
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(first,&clone)==AETHER_OK);
    REQUIRE(clone->assets==first->assets);
    aether_layout_result_destroy(first);
    consumers(f,clone,b); f.intact();
    aether_style_map_destroy(f.map); f.map=nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime=nullptr;
    consumers(f,clone,b); aether_layout_result_destroy(clone);

    // The root's percentages have a definite flex-container reference.
    Fixture root(mode,"#ca{padding:0;}#a{padding:0 10%;margin:0 5%;}");
    for(float available : {500.f,800.f,500.f}) {
        auto* frame=root.layout(available,300);
        REQUIRE(same(rect(root,frame,"a").width,100+available*.2f));
        REQUIRE(same(rect(root,frame,"a").x,available*.05f));
        REQUIRE(same(rect(root,frame,"ca").width,100));
        root.intact(); aether_layout_result_destroy(frame);
    }
}
static void allocation_sweeps(MapMode mode) {
    Fixture f(mode, "#ca{padding:0 calc(10% + 5px);min-width:70px;max-width:80px;}");
    auto* source = f.layout(500, 500);
    aether_style_map_s source_map; source_map.current = source->assets->base;
    const auto boxes = capture_boxes(source);
    const auto b = rect(f, source, "b");
    for (bool clone : {false, true}) {
        bool completed = false;
        long failures = 0;
        for (long ordinal = 0; ordinal < 20000; ++ordinal) {
            aether_layout_result_t output = source;
            allocation_count = 0; injected = false; fail_at = ordinal;
            const auto status = clone ? aether_layout_clone_snapshot(source, &output)
                : aether_layout_layout(f.engine, f.doc, &source_map, 500, 500, nullptr, nullptr, &output);
            fail_at = -1;
            f.intact(); intact_boxes(boxes);
            if (status == AETHER_OK) {
                REQUIRE(!injected && output != source);
                REQUIRE(same(rect(f, output, "a").width, 90));
                consumers(f, output, rect(f, output, "b"));
                aether_layout_result_destroy(output); completed = true; break;
            }
            REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == source);
            ++failures;
        }
        REQUIRE(completed && failures > 0);
        consumers(f, source, b);
        std::printf("mode=%d %s allocation failures=%ld\n", int(mode), clone ? "clone" : "layout", failures);
    }
    aether_layout_result_destroy(source);
}
// Authentic replacement records, never writes through FrozenStyle. The direct
// measurement probes catch overflow before a later final-geometry validator can
// hide a bad intrinsic sum by clamping it to a finite maximum.
static aether_css_length px(float v) { aether_css_length l{}; l.unit = AETHER_CSS_UNIT_PX; l.value = v; return l; }
static void numeric_edges() {
    Fixture f(MapMode::legacy_partial);
    auto* node = aether_doc_get_element_by_id(f.doc, "ca");
    const auto original = f.base->backing()->find_owner(node);
    auto* retained = f.layout(500,500);
    const auto saved_boxes = capture_boxes(retained);
    const float max = std::numeric_limits<float>::max();
    for (int kind = 0; kind < 17; ++kind) {
        auto scalars = original->style_view().scalars();
        scalars.box_sizing = AETHER_BOX_SIZING_CONTENT_BOX;
        scalars.width = px(100);
        scalars.padding.left = scalars.padding.right = px(0);
        scalars.min_width = px(0);
        scalars.max_width = px(80);
        if (kind == 0) scalars.width = px(std::numeric_limits<float>::infinity());
        if (kind == 1) scalars.width = px(std::numeric_limits<float>::quiet_NaN());
        if (kind == 2) scalars.min_width = px(std::numeric_limits<float>::infinity());
        if (kind == 3) scalars.max_width = px(std::numeric_limits<float>::quiet_NaN());
        if (kind == 4) { scalars.width = px(max); scalars.padding.left = px(max); }
        if (kind == 5) { scalars.padding.left = px(max); scalars.padding.right = px(max); }
        if (kind == 6) { scalars.min_width = px(max); scalars.padding.left = px(max); }
        if (kind == 7) { scalars.margin.left = px(max); scalars.margin.right = px(max); }
        if (kind == 8) { scalars.max_width = px(max); scalars.padding.left = px(max); }
        if (kind == 9) { scalars.padding.left = px(std::numeric_limits<float>::infinity()); }
        if (kind == 10) { scalars.margin.right = px(std::numeric_limits<float>::quiet_NaN()); }
        if (kind == 11) { scalars.padding.left.unit=AETHER_CSS_UNIT_PERCENT; scalars.padding.left.value=std::numeric_limits<float>::infinity(); }
        if (kind == 12) { scalars.margin.left.unit=AETHER_CSS_UNIT_PERCENT; scalars.margin.left.value=std::numeric_limits<float>::quiet_NaN(); }
        if (kind >= 13) {
            scalars.padding.left.unit=AETHER_CSS_UNIT_LINEAR;
            scalars.padding.left.value=kind==13 ? std::numeric_limits<float>::infinity() : max;
            scalars.padding.left.linear_percent=kind==14 ? std::numeric_limits<float>::infinity() : 10;
            if(kind==15) scalars.padding.right=px(max);
        }
        if (kind == 16) {
            scalars.width=px(200); scalars.max_width=px(200);
            scalars.padding.left=px(max); scalars.padding.left.unit=AETHER_CSS_UNIT_PERCENT;
            scalars.padding.right=scalars.padding.left;
        }
        NodeRecordInput input;
        input.origin = RecordOrigin::legacy; input.node = node;
        input.style = std::make_shared<const FrozenStyle>(scalars, original->style()->payload_owner());
        input.document_identity = original->document_identity(); input.node_identity = original->node_identity();
        input.node_lifetime = original->node_lifetime(); input.document_lifetime = original->document_lifetime();
        FrozenNodeRef record;
        REQUIRE(freeze_node_record(input, record) == AETHER_OK);
        BackingBuildInput backing{f.base->backing()->entries()}; backing.entries[node] = record;
        MapVersionInput version;
        REQUIRE(freeze_backing(backing, version.backing) == AETHER_OK);
        version.mode = MapMode::legacy_partial;
        MapVersionRef map;
        REQUIRE(make_map_version(version, map) == AETHER_OK);
        const StyleLookup lookup(map, nullptr, original->style());
        for (auto mode : {aether_intrinsic_size_mode_e::min_content, aether_intrinsic_size_mode_e::max_content}) {
            bool threw = false;
            try {
                auto* parent=aether_doc_get_element_by_id(f.doc,"a");
                const auto width=measure_intrinsic_width(f.engine,&lookup,parent,get_style(&lookup,parent),{}, {},500,nullptr,mode);
                if (kind==16) REQUIRE(same(width,200));
            }
            catch (const std::overflow_error&) { threw = true; }
            // A finite huge percentage is valid against intrinsic zero, but
            // final used geometry must still reject its overflow below.
            if (kind != 16 && !threw) std::fprintf(stderr,"numeric case %d failed to reject invalid width\n",kind);
            REQUIRE(threw == (kind != 16));
        }
        aether_style_map_s invalid_map; invalid_map.current=map;
        auto* output=retained;
        REQUIRE(aether_layout_layout(f.engine,f.doc,&invalid_map,500,500,nullptr,nullptr,&output)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(output==retained);
        intact_boxes(saved_boxes);
        REQUIRE(std::memcmp(&scalars, &record->style_view().scalars(), sizeof scalars) == 0);
        f.intact();
    }
    for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        auto* output = retained;
        REQUIRE(aether_layout_layout(f.engine, f.doc, f.map, invalid, 500, nullptr, nullptr, &output) == AETHER_ERROR_INVALID_STATE);
        REQUIRE(output == retained); f.intact();
    }
    aether_layout_result_destroy(retained);
}
int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--numeric-only") == 0) { numeric_edges(); return 0; }
    REQUIRE(argc == 1);
    for (auto mode : {MapMode::legacy_partial, MapMode::legacy_complete, MapMode::contextual}) {
        std::printf("cyclic edges lifetime mode=%d\n", int(mode)); std::fflush(stdout);
        retained(mode); allocation_sweeps(mode);
    }
    numeric_edges();
    std::puts("cyclic intrinsic edges: frozen styles, three maps, revisions, resize, retained frames, clone, paint/hit, OOM and numeric edges passed");
}
