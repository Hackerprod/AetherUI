#include "hit_order_oom_helpers.h"
// Authentic private-core regression. Checks are deliberately active under NDEBUG.
// The construction-only block_static_anchors field is part of the cycle8 private
// implementation contract, not a public header or ABI requirement.
#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <vector>

static long fail_at = -1, allocation_count = 0, checks = 0;
static bool injected = false;
static unsigned scenarios = 0;
#define REQUIRE(c) do { ++checks; if (!(c)) { \
    std::fprintf(stderr,"FAIL block-static lifetime %s:%d: %s\n",__FILE__,__LINE__,#c); \
    std::abort(); } } while (false)

// The existing authentic aether_flex_text_test_core OBJECT pool must supply all
// core objects. Linking this harness to the DSO would defeat fault interception.
void* operator new(std::size_t n) {
    const auto ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) { injected = true; throw std::bad_alloc(); }
    if (auto* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n); } catch (...) { return nullptr; }
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

static bool same(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b)<.01f; }
static bool same(aether_rectf a, aether_rectf b) {
    return same(a.x,b.x) && same(a.y,b.y) && same(a.width,b.width) && same(a.height,b.height);
}
static void rect(aether_rectf a, aether_rectf e) { REQUIRE(same(a,e)); }
struct CallbackState {
    unsigned calls = 0;
    int action = 0; // 1: replace map publication; 2: mutate DOM through public API.
    bool fired = false;
    aether_style_map_t map = nullptr;
    aether_node_t node = nullptr;
};
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void* data) {
    auto* s = static_cast<CallbackState*>(data);
    if (s) {
        ++s->calls;
        if (!s->fired && s->action) {
            s->fired = true;
            if (s->action == 1) s->map->current.reset();
            else REQUIRE(aether_element_set_attribute(s->node,"class","changed-during-measure") == AETHER_OK);
        }
    }
    return {static_cast<float>(std::strlen(text))*10,10,8};
}
static const char* markup =
    "<html><body id='body'><div id='host'><div id='prev'></div>"
    "<div id='a'></div><div id='b'></div>ab<div id='c'></div>cd</div></body></html>";
static const char* css =
    "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}"
    "#host{position:relative;transform:translate(0px,0px);width:300px;height:200px;line-height:20px;}"
    "#prev{width:80px;height:30px;}#a,#b,#c{position:absolute;width:40px;height:20px;}"
    "#a{background:#123456;}#b{width:20px;height:10px;}";
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_display_list_builder_t painter{};
    CallbackState callback;
    Fixture(const char* html = markup, const char* styles = css) {
        REQUIRE(aether_doc_create(&doc)==AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,html)==AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
        int64_t sheet{};
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,styles,0,"block-static-lifetime",&sheet,nullptr)==AETHER_OK);
        resolve();
        REQUIRE(aether_layout_create(measure,&callback,&engine)==AETHER_OK);
        REQUIRE(aether_display_list_builder_create(measure,nullptr,&painter)==AETHER_OK);
    }
    ~Fixture() {
        aether_display_list_builder_destroy(painter);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    void resolve() {
        aether_style_update_t* updates{};
        const auto n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        REQUIRE(n>=0); aether_style_updates_free(updates,n);
        aether_style_map_destroy(map); map=nullptr;
        REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK);
    }
    void prepare(aether_layout_result_t prior) {
        aether_bool_t changed=-1;
        REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,map,prior,&changed)==AETHER_OK);
    }
    aether_layout_result_t layout(bool prepared, aether_layout_result_t prior=nullptr) {
        if (prepared) prepare(prior);
        aether_layout_result_t r{};
        REQUIRE(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&r)==AETHER_OK);
        REQUIRE(r); return r;
    }
    aether_node_t node(const char* id) { auto* n=aether_doc_get_element_by_id(doc,id); REQUIRE(n); return n; }
    aether_layout_box_s* box(aether_layout_result_t r,const char* id) {
        auto* b=r->find_primary_box(node(id)); REQUIRE(b); return b;
    }
};
static void walk(aether_layout_box_s* b,const std::set<aether_layout_box_s*>& owned,
                 std::set<aether_layout_box_s*>& reached) {
    REQUIRE(owned.count(b)==1); REQUIRE(reached.insert(b).second);
    for (auto* child:b->children) walk(child,owned,reached);
}
static void registry(aether_layout_result_t r,bool clone=false) {
    REQUIRE(r && r->root && !r->abandoned && !r->build_overlay);
    std::set<aether_layout_box_s*> owned,reached,painted;
    for (auto* b:r->allocated_boxes) {
        REQUIRE(owned.insert(b).second); REQUIRE(b->owner==r);
        REQUIRE(b->block_static_anchors.empty());
        if (clone) REQUIRE(b->block_static_anchors.capacity()==0);
    }
    REQUIRE(!owned.empty() && owned.size()==r->live_box_count);
    walk(r->root,owned,reached); REQUIRE(owned==reached);
    for (auto* b:r->paint_order) { REQUIRE(owned.count(b)==1); REQUIRE(painted.insert(b).second); }
    REQUIRE(painted==owned);
    for (const auto& entry:r->primary_boxes) {
        REQUIRE(owned.count(entry.second)==1); REQUIRE(entry.second->element==entry.first);
    }
}
static void validate(Fixture& f,aether_layout_result_t r,bool clone=false) {
    registry(r,clone);
    rect(f.box(r,"host")->border_box,{0,0,300,200});
    rect(f.box(r,"prev")->border_box,{0,0,80,30});
    rect(f.box(r,"a")->border_box,{0,30,40,20});
    rect(f.box(r,"b")->border_box,{0,30,20,10});
    rect(f.box(r,"c")->border_box,{0,50,40,20});
    REQUIRE(aether_layout_result_hit_test(r,30,35)==f.node("a"));
    REQUIRE(aether_layout_result_hit_test(r,85,5)!=f.node("a"));
}
struct Saved {
    aether_layout_box_s* box;
    aether_rectf border,padding,content,margin;
    std::vector<aether_layout_box_s*> children;
};
struct Snapshot {
    aether_layout_result_t r;
    std::vector<Saved> boxes;
    decltype(aether_layout_result_s::primary_boxes) primary;
    decltype(aether_layout_result_s::paint_order) paint;
    decltype(aether_layout_result_s::assets) assets;
    explicit Snapshot(aether_layout_result_t p):r(p),primary(p->primary_boxes),paint(p->paint_order),assets(p->assets) {
        for(auto* b:p->allocated_boxes) boxes.push_back({b,b->border_box,b->padding_box,b->content_box,b->margin_box,b->children});
    }
    void intact() const {
        REQUIRE(r->primary_boxes==primary && r->paint_order==paint && r->assets==assets);
        for(const auto& s:boxes) {
            REQUIRE(same(s.box->border_box,s.border) && same(s.box->padding_box,s.padding));
            REQUIRE(same(s.box->content_box,s.content) && same(s.box->margin_box,s.margin));
            REQUIRE(s.box->children==s.children && s.box->block_static_anchors.empty());
        }
    }
};
static constexpr long ordinal_limit=20000;
static long layout_sweep(bool prepared) {
    ++scenarios; Fixture f; auto* prior=f.layout(prepared); validate(f,prior);
    const Snapshot saved(prior);
    std::printf("block-static published capacities prepared=%d host=%zu prev=%zu; sizeof(anchor)=%zu\n",
                prepared,f.box(prior,"host")->block_static_anchors.capacity(),
                f.box(prior,"prev")->block_static_anchors.capacity(),
                sizeof(decltype(aether_layout_box_s::block_static_anchors)::value_type));
    const auto base=f.map->current;
    const auto structure=f.doc->identity->structure_epoch, mutation=f.doc->identity->mutation_epoch;
    const auto style=f.doc->identity->style_tree_epoch;
    long failures=0; bool completed=false;
    for(long i=0;i<ordinal_limit;++i) {
        if(prepared) f.prepare(prior);
        aether_layout_result_t out=prior;
        allocation_count=0; injected=false; fail_at=i;
        const auto status=aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&out);
        fail_at=-1;
        saved.intact(); validate(f,prior);
        REQUIRE(f.map->current==base && f.doc->identity->structure_epoch==structure);
        REQUIRE(f.doc->identity->mutation_epoch==mutation && f.doc->identity->style_tree_epoch==style);
        if(status==AETHER_OK) {
            REQUIRE((!injected || hit_oom_optional_success(out,prior)) && out && out!=prior); validate(f,out); aether_layout_result_destroy(out);
            completed=!injected; if(completed) break; else continue;
        }
        REQUIRE(injected && status==AETHER_ERROR_OUT_OF_MEMORY && out==prior); ++failures;
        auto* fresh=f.layout(prepared,prior); validate(f,fresh); aether_layout_result_destroy(fresh);
    }
    REQUIRE(completed && failures>0);
    std::printf("block-static layout sweep prepared=%d observed-failures=%ld\n",prepared,failures);
    aether_layout_result_destroy(prior); return failures;
}
static long prepare_sweep() {
    ++scenarios; Fixture f; auto* prior=f.layout(true); const Snapshot saved(prior);
    const auto base=f.map->current;
    long failures=0; bool completed=false;
    for(long i=0;i<ordinal_limit;++i) {
        // Each ordinal starts from an unprepared engine; no state left by a
        // previous preparation can make the allocation trace shrink.
        aether_layout_engine_t engine{};
        REQUIRE(aether_layout_create(measure,&f.callback,&engine)==AETHER_OK);
        aether_bool_t changed=-1;
        allocation_count=0; injected=false; fail_at=i;
        const auto status=aether_layout_prepare_transition_context(engine,nullptr,f.map,prior,&changed);
        fail_at=-1;
        saved.intact(); REQUIRE(f.map->current==base);
        if(status==AETHER_OK) {
            REQUIRE(!injected && changed==AETHER_FALSE);
            completed=true;
        } else {
            REQUIRE(injected && status==AETHER_ERROR_OUT_OF_MEMORY);
            // The public API initializes out_changed to false before its work.
            REQUIRE(changed==AETHER_FALSE); ++failures;
            REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,f.map,prior,&changed)==AETHER_OK);
        }
        aether_layout_result_t out{};
        REQUIRE(aether_layout_layout(engine,f.doc,f.map,800,600,nullptr,nullptr,&out)==AETHER_OK);
        REQUIRE(out && out!=prior); validate(f,out);
        aether_layout_result_destroy(out); aether_layout_destroy(engine);
        if(completed) break;
    }
    REQUIRE(completed && failures>0); validate(f,prior);
    aether_layout_result_destroy(prior);
    std::printf("block-static prepare sweep observed-failures=%ld\n",failures); return failures;
}
static long clone_sweep() {
    ++scenarios; Fixture f; auto* prior=f.layout(true); const Snapshot saved(prior);
    long failures=0; bool completed=false;
    for(long i=0;i<ordinal_limit;++i) {
        aether_layout_result_t out=prior;
        allocation_count=0; injected=false; fail_at=i;
        const auto status=aether_layout_clone_snapshot(prior,&out);
        fail_at=-1; saved.intact(); validate(f,prior);
        if(status==AETHER_OK) {
            REQUIRE(!injected && out && out!=prior); validate(f,out,true);
            REQUIRE(out->assets==prior->assets && out->root!=prior->root);
            aether_layout_result_destroy(out); completed=true; break;
        }
        REQUIRE(injected && status==AETHER_ERROR_OUT_OF_MEMORY && out==prior); ++failures;
        aether_layout_result_t recovery{};
        REQUIRE(aether_layout_clone_snapshot(prior,&recovery)==AETHER_OK);
        validate(f,recovery,true); aether_layout_result_destroy(recovery);
    }
    REQUIRE(completed && failures>0); aether_layout_result_destroy(prior);
    std::printf("block-static clone sweep observed-failures=%ld\n",failures); return failures;
}
static void paint(aether_display_list_builder_t builder,aether_layout_result_t r) {
    aether_display_list_t list{};
    REQUIRE(aether_display_list_build(builder,r,nullptr,&list)==AETHER_OK);
    REQUIRE(list); unsigned matches=0;
    const int count=aether_display_list_get_count(list); REQUIRE(count>0);
    for(int i=0;i<count;++i) {
        aether_paint_command_t command{};
        REQUIRE(aether_display_list_get_command(list,i,&command)==AETHER_OK);
        if(command.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto c=command.data.fill_rounded_rect.color;
            if(same(c.r,18/255.f)&&same(c.g,52/255.f)&&same(c.b,86/255.f)) {
                ++matches; rect(command.data.fill_rounded_rect.rect,{0,30,40,20});
            }
        }
        aether_paint_command_free(&command);
    }
    REQUIRE(matches==1); aether_display_list_destroy(list);
}
static void lifetime() {
    ++scenarios; Fixture f; auto* prior=f.layout(true); aether_layout_result_t clone{};
    REQUIRE(aether_layout_clone_snapshot(prior,&clone)==AETHER_OK);
    validate(f,clone,true); validate(f,prior); paint(f.painter,clone);
    auto* held_box=f.box(clone,"a");
    aether_style_map_destroy(f.map); f.map=nullptr;
    aether_style_runtime_destroy(f.runtime); f.runtime=nullptr;
    validate(f,prior); validate(f,clone,true); paint(f.painter,prior); paint(f.painter,clone);
    aether_layout_result_destroy(prior);
    validate(f,clone,true); paint(f.painter,clone);
    aether_doc_destroy(f.doc); f.doc=nullptr;
    // Box-owned geometry survives, but DOM consumers reject a dead document.
    rect(aether_layout_box_get_border_box(held_box),{0,30,40,20});
    REQUIRE(aether_layout_result_hit_test(clone,30,35)==nullptr);
    aether_display_list_t list{};
    REQUIRE(aether_display_list_build(f.painter,clone,nullptr,&list)==AETHER_ERROR_INVALID_STATE && !list);
    aether_layout_result_t out=clone;
    REQUIRE(aether_layout_clone_snapshot(clone,&out)==AETHER_ERROR_INVALID_STATE && out==clone);
    aether_layout_result_destroy(clone);
}
static void callback_mutation(int action) {
    ++scenarios; Fixture f; auto* prior=f.layout(true); const Snapshot saved(prior);
    const auto original=f.map->current;
    f.callback.action=action; f.callback.fired=false; f.callback.map=f.map; f.callback.node=f.node("host");
    f.prepare(prior); aether_layout_result_t out=prior;
    const auto status=aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&out);
    REQUIRE(f.callback.fired); f.callback.action=0;
    saved.intact();
    if(action==1) {
        REQUIRE(!f.map->current);
        REQUIRE(status==AETHER_OK && out && out!=prior);
        REQUIRE(out->assets->base==original); validate(f,out); paint(f.painter,out);
        aether_layout_result_destroy(out);
    } else {
        REQUIRE(status==AETHER_ERROR_INVALID_STATE && out==prior);
    }
    f.resolve(); auto* fresh=f.layout(true,prior); validate(f,fresh); validate(f,prior);
    aether_layout_result_destroy(fresh); aether_layout_result_destroy(prior);
}
static void callback_nonparticipation() {
    ++scenarios;
    // Punctuation deliberately selects the same legacy inline path on both sides.
    // This isolates marker nonparticipation from cross-path tokenization differences;
    // replacing ab with a! preserves this fixture's measured width.
    Fixture plain("<html><body><div id='host'>a!<!--same text partition-->cd</div></body></html>");
    Fixture anchored("<html><body><div id='host'>a!<div id='c'></div>cd</div></body></html>");
    plain.callback.calls=anchored.callback.calls=0;
    auto* a=plain.layout(false); auto* b=anchored.layout(false);
    REQUIRE(plain.callback.calls>0 && plain.callback.calls==anchored.callback.calls);
    rect(anchored.box(b,"c")->border_box,{0,20,40,20});
    registry(a); registry(b);
    std::printf("block-static callback counts plain=%u anchored=%u\n",plain.callback.calls,anchored.callback.calls);
    aether_layout_result_destroy(a); aether_layout_result_destroy(b);
}
static void deferred_completion() {
    ++scenarios;
    Fixture f("<html><body><table id='table'><tr><td id='cell'><div id='flow'></div><div id='abs'></div></td>"
              "<td id='tall'><div id='height'></div></td></tr></table></body></html>",
              "*{margin:0;padding:0;border:0;box-sizing:content-box;}html,body,div{display:block;}"
              "table{display:table;width:200px;}tr{display:table-row;}td{display:table-cell;width:100px;}"
              "#cell{position:relative;}#flow{height:10px;width:20px;}#height{height:80px;width:20px;}"
              "#abs{position:absolute;width:30px;height:15px;}");
    auto* r=f.layout(true); registry(r);
    REQUIRE(r->has_finalized_table_cells);
    const auto cell=f.box(r,"cell")->content_box;
    REQUIRE(same(cell.height,80));
    // Hand-computed local insertion after the 10px flow child, not row bottom.
    const auto absolute=f.box(r,"abs")->border_box;
    rect({absolute.x-cell.x,absolute.y-cell.y,absolute.width,absolute.height},{0,10,30,15});
    aether_layout_result_t clone{};
    REQUIRE(aether_layout_clone_snapshot(r,&clone)==AETHER_OK); registry(clone,true);
    rect(f.box(clone,"abs")->border_box,absolute);
    aether_layout_result_destroy(r); aether_layout_result_destroy(clone);
}
int main() {
    const long legacy=layout_sweep(false), prepared=layout_sweep(true);
    const long preparation=prepare_sweep(), clone=clone_sweep();
    lifetime(); callback_mutation(1); callback_mutation(2);
    callback_nonparticipation(); deferred_completion();
    REQUIRE(scenarios==9 && checks>0);
    REQUIRE(legacy>0 && prepared>0 && preparation>0 && clone>0);
    std::printf("block-static lifetime: scenarios=%u checks=%ld layout-legacy=%ld layout-prepared=%ld "
                "prepare=%ld clone=%ld; sizeof(box)=%zu sizeof(inline_item)=%zu; "
                "registry checks are not leak validation\n",scenarios,checks,legacy,prepared,preparation,clone,
                sizeof(aether_layout_box_s),sizeof(inline_item_s));
}
