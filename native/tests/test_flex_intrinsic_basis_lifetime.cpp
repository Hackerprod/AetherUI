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

using namespace aether::style;
using namespace aether::layout;
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
    struct StyleSnapshot { FrozenNodeRef owner; std::array<unsigned char, sizeof(StyleScalars)> scalars; };
    std::vector<StyleSnapshot> frozen;
    size_t base_count = 0;
    uint64_t structure = 0, mutation = 0, style_epoch = 0;

    Fixture(MapMode mode, bool col, bool content, const std::string& extra = "") {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,
            "<html><body id='p'><div id='a'><div id='ca'><div id='x'></div><div id='y'></div></div></div>"
            "<div id='b'><div id='cb'></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{500, 500, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        const std::string cross = col ? "width" : "height";
        const std::string main = col ? "height" : "width";
        std::string css = "*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}"
            "#p{display:flex;width:100%;height:100%;align-items:stretch;flex-direction:" +
            std::string(col ? "column" : "row") + ";}#a,#b{min-"+main+":0;flex:1 1 " +
            (content ? "content;" : "auto;") + main+":50px;}#a{max-"+main+":100px;}"
            "#ca,#cb{"+main+":200px;"+cross+":20px;}#b{background:red;}";
        css += extra;
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
            REQUIRE(base->backing()->find_owner(saved.owner->node()) == saved.owner);
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
        aether_layout_result_t frame = nullptr;
        REQUIRE(aether_layout_layout(engine, doc, used_map, width, height, nullptr, nullptr, &frame) == AETHER_OK);
        REQUIRE(frame && frame->assets->base == used_base && !frame->build_overlay);
        if(contextual_map) aether_style_map_destroy(contextual_map);
        return frame;
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
static void retained(MapMode mode, bool col, bool content) {
    std::printf("retained mode=%d col=%d content=%d\n",int(mode),col,content);std::fflush(stdout);
    Fixture f(mode,col,content);
    auto* first=f.layout(500,500);
    const auto boxes=capture_boxes(first);
    const auto primary=first->primary_boxes;
    const auto order=first->paint_order;
    const auto b=rect(f,first,"b");
    // Growing bases 200/200 or 50/50: A freezes at max=100, B gets 400.
    REQUIRE(same(col?b.height:b.width,400));
    for (float available : {250.f,500.f,250.f,500.f}) {
        auto* next=f.layout(col?300:available,col?available:300);
        const auto a=rect(f,next,"a"), current_b=rect(f,next,"b");
        REQUIRE(same(col?a.height:a.width,100));
        REQUIRE(same(col?current_b.height:current_b.width,available-100));
        REQUIRE(same(col?a.width:a.height,300));
        REQUIRE(same(col?current_b.width:current_b.height,300));
        consumers(f,next,current_b); consumers(f,first,b);
        f.intact(); intact_boxes(boxes);
        REQUIRE(first->primary_boxes==primary && first->paint_order==order);
        aether_layout_result_destroy(next);
    }
    aether_layout_result_t clone=nullptr;
    REQUIRE(aether_layout_clone_snapshot(first,&clone)==AETHER_OK);
    aether_layout_result_destroy(first);
    consumers(f,clone,b); f.intact();
    aether_layout_result_destroy(clone);
}
// Exercise the layout contract, not the separate runtime parser limitation:
// min-width/min-height:auto is rejected by bi_try_non_negative_length today.
// Freeze a new authentic legacy record rather than editing a frozen style.
static void set_automatic_minimum(Fixture& f, bool col) {
    auto* node=aether_doc_get_element_by_id(f.doc,"a");
    auto original=f.base->backing()->find_owner(node);
    auto scalars=original->style_view().scalars();
    (col?scalars.min_height:scalars.min_width)=aether_css_length_auto();
    NodeRecordInput input;
    input.origin=original->origin(); input.node=node;
    input.style=std::make_shared<const FrozenStyle>(scalars,original->style()->payload_owner());
    input.measures=original->measures(); input.custom_properties=original->custom_properties();
    input.color_references=original->color_references();
    input.declared_impact=original->declared_impact();
    input.declared_inherited_impact=original->declared_inherited_impact();
    input.inherited_change_impact=original->inherited_change_impact();
    input.document_identity=original->document_identity();input.node_identity=original->node_identity();
    input.node_lifetime=original->node_lifetime();input.document_lifetime=original->document_lifetime();
    FrozenNodeRef record;
    REQUIRE(freeze_node_record(input,record)==AETHER_OK);
    BackingBuildInput backing{f.base->backing()->entries()}; backing.entries[node]=record;
    MapVersionInput version;
    REQUIRE(freeze_backing(backing,version.backing)==AETHER_OK);
    version.mode=MapMode::legacy_partial;
    REQUIRE(make_map_version(version,f.map->current)==AETHER_OK);
    f.base=f.map->current; f.frozen.clear();
    for(const auto& entry:f.base->backing()->entries()) {
        Fixture::StyleSnapshot saved{entry.second,{}};
        std::memcpy(saved.scalars.data(),&entry.second->style_view().scalars(),sizeof(StyleScalars));
        f.frozen.push_back(saved);
    }
}
static void automatic_minima(bool col, bool hidden, bool nested) {
    std::printf("auto minimum col=%d hidden=%d nested=%d\n",col,hidden,nested);std::fflush(stdout);
    const std::string main=col?"height":"width";
    // With content 200/200 and preferred size cap 80, visible auto-min
    // freezes A at 80; hidden permits equal shrink to 50/50.
    // The nested column variant measures a 100px-wide row: two 60px-wide
    // children wrap, each height100, making the same intrinsic height200.
    std::string css="#a,#b{flex:0 1 content;max-"+main+":none;}#a{"+main+":80px;overflow:"+
        (hidden?"hidden;":"visible;")+"}";
    if(nested) css+="#a{width:100px;}#ca{display:flex;flex-wrap:wrap;width:100%;height:auto;}#x,#y{width:60px;height:100px;flex:0 0 auto;}";
    Fixture f(MapMode::legacy_partial,col,true,css);
    set_automatic_minimum(f,col);
    auto* frame=f.layout(col?300:100,col?100:300);
    const auto a=rect(f,frame,"a"),b=rect(f,frame,"b");
    REQUIRE(same(col?a.height:a.width,hidden?50:80));
    REQUIRE(same(col?b.height:b.width,hidden?50:20));
    f.intact(); aether_layout_result_destroy(frame);
}
int main() {
    for (auto mode : {MapMode::legacy_partial,MapMode::legacy_complete,MapMode::contextual})
        for (bool col : {false,true}) for (bool content : {false,true}) retained(mode,col,content);
    for(bool col:{false,true}) for(bool hidden:{false,true}) automatic_minima(col,hidden,false);
    for(bool hidden:{false,true}) automatic_minima(true,hidden,true);
    std::puts("intrinsic basis: three maps, resize, frozen styles/DOM, retained frames, clone, paint/hit passed");
}
