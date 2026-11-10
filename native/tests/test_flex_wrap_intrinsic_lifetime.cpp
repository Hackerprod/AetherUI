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
static long text_measurements = 0;
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    ++text_measurements;
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
            "<html><body id='p'><div id='inner'>zz<div id='a'><div id='ca'><div id='x'>alpha beta</div><div id='y'></div></div></div>"
            "<div id='b'><div id='cb'></div></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{500, 500, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        std::string css = "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}"
            "#p{display:flex;width:100%;height:100%;align-items:stretch;}"
            "#inner{display:flex;flex:0 0 content;min-width:0;flex-wrap:wrap;padding:10px;border:2px solid black;}#a,#b{min-width:0;flex:0 0 50px;}"
            "#ca{display:flex;}#x{width:100px;flex:0 0 auto;}#y{width:0;}#cb{width:200px;}#ca,#cb{height:20px;}#b{background:red;}";
        css += extra;
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "wrap-intrinsic-lifetime", &sheet, nullptr) == AETHER_OK);
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
        const std::string css = declarations;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "alternate-wrap-basis", &sheet, nullptr) == AETHER_OK);
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
    REQUIRE(same(rect(f, first, "inner").width, 224));
    REQUIRE(same(rect(f, first, "a").width, 50));
    REQUIRE(same(rect(f, first, "b").width, 50));
    auto* inner = aether_doc_get_element_by_id(f.doc, "inner");
    REQUIRE(first->anonymous_flex_items.at(inner).size() == 3);
    auto* anonymous = first->anonymous_flex_items.at(inner).front();
    REQUIRE(anonymous != inner->children.front());
    const auto anonymous_text = anonymous->text;
    const auto boxes = capture_boxes(first);
    const auto primary = first->primary_boxes;
    const auto order = first->paint_order;
    const auto b = rect(f, first, "b");
    struct Case { const char* css; float inner; float item; };
    for (const auto& test : {
            Case{"flex:0 0 70px;margin:0;",224,70},
            Case{"flex:1 0 250px;margin:0;",334,250},
            Case{"flex:0 0 50px;margin-left:-80px;",224,50},
            Case{"flex:0 0 50px;margin:0;min-width:70px;max-width:80px;",224,70},
            Case{"flex:0 0 50px;margin:0;min-width:0;max-width:none;",224,50}}) {
        f.change((std::string("#a{")+test.css+"}").c_str());
        for (float available : {500.f, 800.f, 500.f}) {
            auto* next = f.layout(available,300);
            REQUIRE(same(rect(f,next,"inner").width,test.inner));
            REQUIRE(same(rect(f,next,"a").width,test.item));
            REQUIRE(same(rect(f,next,"b").width,50));
            consumers(f,next,rect(f,next,"b")); consumers(f,first,b);
            f.intact(); intact_boxes(boxes);
            REQUIRE(first->primary_boxes == primary && first->paint_order == order);
            REQUIRE(anonymous->text == anonymous_text);
            aether_layout_result_destroy(next);
        }
    }
    aether_layout_result_t clone = nullptr;
    REQUIRE(aether_layout_clone_snapshot(first,&clone)==AETHER_OK);
    REQUIRE(clone->assets==first->assets);
    const auto clone_boxes = capture_boxes(clone);
    aether_layout_result_destroy(first);
    consumers(f,clone,b); f.intact();
    aether_style_map_destroy(f.map); f.map=nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime=nullptr;
    consumers(f,clone,b);
    // Document death invalidates DOM-facing consumers, but not immutable box,
    // style or synthetic-node storage retained by the result and its clone.
    aether_doc_destroy(f.doc); f.doc=nullptr;
    intact_boxes(clone_boxes);
    REQUIRE(anonymous->text == anonymous_text);
    for (const auto& saved : f.frozen)
        REQUIRE(std::memcmp(saved.scalars.data(), &saved.owner->style_view().scalars(), sizeof(StyleScalars)) == 0);
    REQUIRE(aether_layout_result_hit_test(clone,b.x+1,b.y+1)==nullptr);
    aether_display_list_t list=nullptr;
    REQUIRE(aether_display_list_build(f.painter,clone,nullptr,&list)==AETHER_ERROR_INVALID_STATE);
    REQUIRE(list==nullptr);
    aether_layout_result_destroy(clone);
}
static void allocation_sweeps(MapMode mode) {
    Fixture f(mode,"#a{flex:0 0 50px;min-width:70px;max-width:80px;}#inner{gap:10px;}");
    auto* source = f.layout(500,500);
    REQUIRE(same(rect(f,source,"inner").width,224));
    aether_style_map_s source_map; source_map.current=source->assets->base;
    const auto boxes=capture_boxes(source);
    const auto anonymous=source->anonymous_flex_items;
    for (bool clone : {false,true}) {
        bool completed=false;
        long failures=0;
        for (long ordinal=0; ordinal<20000; ++ordinal) {
            aether_layout_result_t output=source;
            allocation_count=0; injected=false; fail_at=ordinal;
            const auto status=clone ? aether_layout_clone_snapshot(source,&output)
                : aether_layout_layout(f.engine,f.doc,&source_map,500,500,nullptr,nullptr,&output);
            fail_at=-1;
            f.intact(); intact_boxes(boxes);
            REQUIRE(source->anonymous_flex_items==anonymous);
            if (status==AETHER_OK) {
                REQUIRE(!injected && output!=source);
                REQUIRE(same(rect(f,output,"inner").width,224));
                consumers(f,output,rect(f,output,"b"));
                aether_layout_result_destroy(output); completed=true; break;
            }
            REQUIRE(injected && status==AETHER_ERROR_OUT_OF_MEMORY && output==source);
            ++failures;
        }
        REQUIRE(completed && failures>0);
        consumers(f,source,rect(f,source,"b"));
        std::printf("mode=%d %s allocation failures=%ld\n",int(mode),clone?"clone":"layout",failures);
    }
    aether_layout_result_destroy(source);
}

// The root decorations are intentionally absent from the probe arguments.
// This observes the natural content floor independently from the final border
// box, whose 24px decoration must be added exactly once by ordinary layout.
static void intrinsic(const Fixture& f, aether_layout_result_t frame,
                      float minimum, float maximum) {
    const StyleLookup lookup(frame->assets, nullptr, false);
    auto* inner = aether_doc_get_element_by_id(f.doc, "inner");
    const auto style = get_style(&lookup, inner);
    for (auto mode : {aether_intrinsic_size_mode_e::min_content,
                      aether_intrinsic_size_mode_e::max_content}) {
        const float width = measure_intrinsic_width(f.engine, &lookup, inner, style,
                                                    {}, {}, 500, frame, mode);
        REQUIRE(same(width, mode == aether_intrinsic_size_mode_e::min_content
                            ? minimum : maximum));
    }
}
static void signed_floor_cases(MapMode mode) {
    struct Case { const char* css; float minimum; float maximum; };
    for (const auto& test : {
        Case{"",200,200},
        Case{"#a,#b{flex:1 0 250px;}",200,510},
        Case{"#inner{gap:100px;}#a,#b{flex:0 0 auto;}#a{margin-left:-150px;}",200,360},
        Case{"#inner{gap:100px;}#a,#b{flex:0 0 auto;margin-left:-250px;}",10,10},
        Case{"#inner{gap:150px;}#a,#b{flex:0 0 auto;margin-left:-250px;}",10,110},
        Case{"#a,#b{flex:0 0 150.25px;}",200,310.5f}}) {
        Fixture f(mode,test.css);
        auto* frame=f.layout(800,500);
        const auto boxes=capture_boxes(frame);
        intrinsic(f,frame,test.minimum,test.maximum);
        REQUIRE(same(rect(f,frame,"inner").width,test.maximum+24));
        intact_boxes(boxes); f.intact();
        aether_layout_result_destroy(frame);
    }
}
static void overflow_transaction(MapMode mode) {
    // Test overflow in the selected signed sum and in root decoration. A
    // finite max-width must not hide either error, even in shrink-to-fit paths.
    for (const char* flow : {"", "#inner{position:absolute;}",
                             "#p{display:block;}#inner{position:absolute;}"}) {
        for (bool decoration : {false,true}) {
            Fixture f(mode);
            auto* source=f.layout(500,500);
            const auto boxes=capture_boxes(source);
            const auto old_base=source->assets->base;
            const auto anonymous=source->anonymous_flex_items;
            const auto primary=source->primary_boxes;
            const auto order=source->paint_order;
            std::string css="#inner{max-width:100px;min-width:0;}"
                            "#a,#b{flex:0 0 2e38px;min-width:0;}";
            if (decoration) css+="#inner{padding-right:2e38px;}#b{display:none;}";
            css+=flow;
            f.change(css.c_str());
            REQUIRE(f.base!=old_base);
            REQUIRE(structure_valid(f.base.get()) && inputs_current(f.base.get()));
            aether_layout_result_t output=source;
            const auto status=aether_layout_layout(f.engine,f.doc,f.map,500,500,
                                                   nullptr,nullptr,&output);
            REQUIRE(status==AETHER_ERROR_INVALID_STATE);
            REQUIRE(output==source); // No partial result may be published.
            f.intact(); intact_boxes(boxes);
            REQUIRE(source->anonymous_flex_items==anonymous);
            REQUIRE(source->primary_boxes==primary && source->paint_order==order);
            REQUIRE(source->assets->base==old_base);
            consumers(f,source,rect(f,source,"b"));
            // Legacy maps may be reused after a stylesheet revision. Contextual
            // maps deliberately reject a new layout once their input/style
            // epoch is stale; retained result consumers above remain valid.
            aether_style_map_s old_map; old_map.current=old_base;
            output=source;
            const auto old_status=aether_layout_layout(f.engine,f.doc,&old_map,500,500,
                                                       nullptr,nullptr,&output);
            if (mode==MapMode::contextual) {
                REQUIRE(!inputs_current(old_base.get()));
                REQUIRE(old_status==AETHER_ERROR_INVALID_STATE && output==source);
            } else {
                REQUIRE(old_status==AETHER_OK && output && output!=source);
                REQUIRE(same(rect(f,output,"inner").width,224));
                aether_layout_result_destroy(output);
            }
            f.change("#p{display:flex;}#inner{position:static;max-width:none;padding:10px;}"
                     "#a,#b{display:block;flex:0 0 50px;}");
            output=f.layout(500,500);
            REQUIRE(same(rect(f,output,"inner").width,224));
            intrinsic(f,output,200,200);
            f.intact(); intact_boxes(boxes);
            aether_layout_result_destroy(output);
            aether_layout_result_destroy(source);
        }
    }
}
int main() {
    for (auto mode : {MapMode::legacy_partial, MapMode::legacy_complete, MapMode::contextual}) {
        std::printf("wrap intrinsic lifetime mode=%d\n",int(mode)); std::fflush(stdout);
        retained(mode);
        signed_floor_cases(mode);
        overflow_transaction(mode);
        allocation_sweeps(mode);
    }
    std::puts("wrap intrinsic: natural floor, signed gaps, decoration, retained versions, DOM/overlay invariance, destruction, OOM and overflow transactions passed");
}
