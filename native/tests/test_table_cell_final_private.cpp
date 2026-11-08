// Link the existing aether_flex_text_test_core objects, not the shared DSO.
#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_layout.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <vector>

static long fail_at = -1, allocation_count = 0;
static bool injected = false;
void* operator new(std::size_t size) {
    const long ordinal = allocation_count++;
    if (fail_at >= 0 && ordinal == fail_at) { injected = true; throw std::bad_alloc(); }
    if (void* p = std::malloc(size ? size : 1)) return p;
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

static const char* scenario = "setup";
static long checks = 0;
#define REQUIRE(c) do { ++checks; if (!(c)) { \
    std::fprintf(stderr,"FAIL table-private [%s] line=%d: %s\n",scenario,__LINE__,#c); \
    std::abort(); } } while (false)
static bool near(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a-b)<.01f; }
static bool same(aether_rectf a, aether_rectf b) {
    return near(a.x,b.x) && near(a.y,b.y) && near(a.width,b.width) && near(a.height,b.height);
}
static void number(const char* label, float a, float b) {
    if (!near(a,b)) std::fprintf(stderr,"[%s] %s actual=%g expected=%g\n",scenario,label,a,b);
    REQUIRE(near(a,b));
}
static bool bits(float a, float b) { return std::memcmp(&a,&b,sizeof(float))==0; }

struct Meter {
    long calls = 0, flow = 0, positioned = 0;
    aether_style_map_t publication_target = nullptr;
    aether::style::MapVersionRef publication;
    aether_node_t mutation_parent = nullptr, mutation_child = nullptr;
    bool fired = false;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void* data) {
        auto& m = *static_cast<Meter*>(data);
        ++m.calls;
        if (text && std::strchr(text,'F')) ++m.flow;
        if (text && std::strchr(text,'P')) ++m.positioned;
        if (!m.fired && (m.publication_target || m.mutation_child)) {
            m.fired = true;
            if (m.publication_target) m.publication_target->current = m.publication;
            if (m.mutation_child)
                REQUIRE(aether_node_append_child(m.mutation_parent,m.mutation_child)==AETHER_OK);
        }
        return {text ? float(std::strlen(text))*5.f : 0.f,10,8};
    }
};
static const char* base_css =
    "*{margin:0;padding:0;border:0;box-sizing:content-box}"
    "html,body,div{display:block}table{display:table;width:200px}"
    "tr{display:table-row}td{display:table-cell;width:100px}"
    "#short{position:relative;background:#ff0000}"
    "#small{width:20px;height:20px}#large{width:20px;height:80px}"
    "#anchor{position:absolute;left:0;bottom:0;width:5px;height:5px}";
static const char* simple_html =
    "<table id='table'><tr id='row'><td id='short'><div id='small'></div>"
    "<div id='anchor'></div></td><td id='tall'><div id='large'></div></td></tr></table>";
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    Meter meter;
    Fixture(const std::string& html = simple_html, const std::string& css = "") {
        REQUIRE(aether_doc_create(&doc)==AETHER_OK);
        const std::string document = "<html><body id='body'>"+html+"</body></html>";
        REQUIRE(aether_doc_parse_html(doc,document.c_str())==AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{400,300,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime)==AETHER_OK);
        add(std::string(base_css)+css);
        resolve();
        REQUIRE(aether_layout_create(Meter::measure,&meter,&engine)==AETHER_OK);
    }
    ~Fixture() {
        aether_layout_destroy(engine); aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void add(const std::string& css) {
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"table-private",&sheet,nullptr)==AETHER_OK);
    }
    void resolve() {
        aether_style_update_t* updates = nullptr;
        const int n = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        REQUIRE(n>=0); aether_style_updates_free(updates,n);
        aether_style_map_t next = nullptr;
        REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&next)==AETHER_OK);
        if (map) { map->current = next->current; aether_style_map_destroy(next); }
        else map = next;
    }
    aether_node_t node(const char* id) { auto* n=aether_doc_get_element_by_id(doc,id); REQUIRE(n); return n; }
    aether_layout_box_s* box(aether_layout_result_t r, const char* id) {
        auto* b=r->find_primary_box(node(id));
        if (!b) std::fprintf(stderr,"[%s] missing primary box id=%s\n",scenario,id);
        REQUIRE(b); return b;
    }
    void prepare(aether_layout_result_t previous = nullptr) {
        aether_bool_t changed = false;
        REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,map,previous,&changed)==AETHER_OK);
        REQUIRE(engine->transition_context_prepared);
    }
    aether_layout_result_t layout(bool prepared, aether_layout_result_t previous = nullptr) {
        if (prepared) prepare(previous);
        aether_layout_result_t r = nullptr;
        REQUIRE(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&r)==AETHER_OK);
        REQUIRE(r && !engine->transition_context_prepared && !engine->measure_styles);
        REQUIRE(!engine->measure_document && !engine->layout_in_progress);
        REQUIRE(r->effective_context_enabled==prepared);
        return r;
    }
};

static void visit(aether_layout_box_s* b, const std::set<aether_layout_box_s*>& owned,
                  std::set<aether_layout_box_s*>& reachable) {
    REQUIRE(owned.count(b)==1); REQUIRE(reachable.insert(b).second);
    REQUIRE(std::isfinite(b->border_box.x) && std::isfinite(b->border_box.y));
    REQUIRE(std::isfinite(b->border_box.width) && std::isfinite(b->border_box.height));
    for (auto* child:b->children) visit(child,owned,reachable);
}
static void validate(aether_layout_result_t r) {
    REQUIRE(r && r->root && !r->abandoned && r->records_boxes);
    std::set<aether_layout_box_s*> owned, reachable, painted;
    for (auto* b:r->allocated_boxes) { REQUIRE(b->owner==r); REQUIRE(owned.insert(b).second); }
    REQUIRE(owned.size()==r->live_box_count);
    visit(r->root,owned,reachable); REQUIRE(reachable==owned);
    for (const auto& p:r->primary_boxes) { REQUIRE(owned.count(p.second)==1); REQUIRE(p.first==p.second->element); }
    for (auto* b:r->paint_order) { REQUIRE(owned.count(b)==1); REQUIRE(painted.insert(b).second); }
    REQUIRE(painted==owned); // Includes every individual text fragment, not just primaries.
    for (auto* b:owned) if (b->kind==LAYOUT_BOX_ELEMENT && b->element)
        REQUIRE(r->find_primary_box(b->element)==b);
}
static void expect_order(Fixture& f, aether_layout_result_t r, std::initializer_list<const char*> ids) {
    size_t previous=0; bool first=true;
    for (const char* id:ids) {
        auto it=std::find(r->paint_order.begin(),r->paint_order.end(),f.box(r,id));
        REQUIRE(it!=r->paint_order.end());
        const size_t current=static_cast<size_t>(it-r->paint_order.begin());
        REQUIRE(first || current>previous); previous=current; first=false;
    }
}
static void preorder(aether_layout_box_s* box, std::vector<aether_layout_box_s*>& ordered) {
    ordered.push_back(box);
    for (auto* child:box->children) preorder(child,ordered);
}
static void expect_flow_completion_order(aether_layout_result_t result) {
    // These callers have no effective-fixed replacement. Each cell's retained
    // flow and completed positioned segment must precede the next cell,
    // including all nested table boxes and each individual text fragment.
    std::vector<aether_layout_box_s*> expected;
    preorder(result->root,expected);
    REQUIRE(result->paint_order==expected);
}
static void compare_clone(aether_layout_box_s* a, aether_layout_box_s* b) {
    REQUIRE(a!=b && a->element==b->element && a->kind==b->kind);
    REQUIRE(a->style.style_owner()==b->style.style_owner());
    REQUIRE(same(a->border_box,b->border_box) && same(a->content_box,b->content_box));
    REQUIRE(a->scroll_completion_flags==b->scroll_completion_flags);
    REQUIRE(bits(a->scroll_flow_height,b->scroll_flow_height));
    REQUIRE(bits(a->scroll_base_height,b->scroll_base_height));
    REQUIRE(bits(a->scroll.scroll_offset_x,b->scroll.scroll_offset_x));
    REQUIRE(bits(a->scroll.scroll_offset_y,b->scroll.scroll_offset_y));
    REQUIRE(bits(a->scroll.scrollable_overflow_x,b->scroll.scrollable_overflow_x));
    REQUIRE(bits(a->scroll.scrollable_overflow_y,b->scroll.scrollable_overflow_y));
    REQUIRE(a->children.size()==b->children.size());
    for (size_t i=0;i<a->children.size();++i) compare_clone(a->children[i],b->children[i]);
}
static void painted_red(Fixture& f, aether_layout_result_t r, float height) {
    aether_display_list_builder_t builder=nullptr; aether_display_list_t list=nullptr;
    REQUIRE(aether_display_list_builder_create(Meter::measure,&f.meter,&builder)==AETHER_OK);
    REQUIRE(aether_display_list_build(builder,r,nullptr,&list)==AETHER_OK);
    int count=0;
    for (int i=0;i<aether_display_list_get_count(list);++i) {
        aether_paint_command_t c{}; REQUIRE(aether_display_list_get_command(list,i,&c)==AETHER_OK);
        if (c.tag==AETHER_PAINT_FILL_ROUNDED_RECT && c.data.fill_rounded_rect.color.r==1.f &&
            c.data.fill_rounded_rect.color.g==0.f && c.data.fill_rounded_rect.color.b==0.f) {
            ++count; number("retained red fill height",c.data.fill_rounded_rect.rect.height,height);
        }
        aether_paint_command_free(&c);
    }
    REQUIRE(count==1); aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
}
static void retained_and_clone(bool prepared) {
    scenario=prepared?"retained/prepared":"retained/legacy";
    Fixture f; auto* old=f.layout(prepared); validate(old); expect_flow_completion_order(old);
    REQUIRE(old->has_finalized_table_cells);
    expect_order(f,old,{"body","table","row","short","small","anchor","tall","large"});
    auto* short_box=f.box(old,"short");
    short_box->scroll.scroll_offset_x=-13.25f; short_box->scroll.scroll_offset_y=1000.5f;
    aether_layout_result_t clone=nullptr;
    REQUIRE(aether_layout_clone_snapshot(old,&clone)==AETHER_OK); validate(clone);
    REQUIRE(clone->has_finalized_table_cells==old->has_finalized_table_cells);
    compare_clone(old->root,clone->root);
    const auto frozen=short_box->style.style_owner();
    f.add("#small{height:120px}#large{height:10px}#short{background:blue}"); f.resolve();
    auto* swapped=f.layout(prepared,old); validate(swapped);
    number("swapped row",f.box(swapped,"row")->border_box.height,120);
    number("swapped cell",f.box(swapped,"tall")->border_box.height,120);
    number("old short",short_box->border_box.height,80);
    REQUIRE(short_box->style.style_owner()==frozen);
    REQUIRE(aether_layout_result_hit_test(old,50,50)==f.node("short"));
    REQUIRE(aether_layout_result_hit_test(clone,50,50)==f.node("short"));
    painted_red(f,old,80); painted_red(f,clone,80);
    f.add("#small{height:20px}#large{height:80px}#short{background:red}"); f.resolve();
    auto* restored=f.layout(prepared,swapped); validate(restored);
    number("restored short",f.box(restored,"short")->border_box.height,80);
    number("restored anchor",f.box(restored,"anchor")->border_box.y,75);
    painted_red(f,restored,80); compare_clone(old->root,clone->root);
    aether_layout_result_destroy(restored); aether_layout_result_destroy(swapped);
    aether_layout_result_destroy(old); validate(clone); painted_red(f,clone,80);
    aether_layout_result_destroy(clone);
}

static void guards_and_detached() {
    scenario="real/no-table/standalone/detached guards";
    for (bool standalone:{false,true}) {
        scenario=standalone?"standalone root cell guard":"no-table block guard";
        // Generic inline collection does not create a box for a TABLE_CELL
        // child outside a table. Style the actual layout root instead so this
        // control really enters ordinary layout_element with TABLE_CELL style.
        const char* owner=standalone?"body":"owner";
        const std::string selector=std::string("#")+owner;
        Fixture f(standalone?"<div id='fixed'></div>":"<div id='owner'><div id='fixed'></div></div>",
            selector+"{width:100px;height:80px;transform:translate(0px,0px)}"+
            (standalone?"#body{display:table-cell}":"")+"#fixed{position:fixed;top:50%;width:5px;height:5px}");
        auto* r=f.layout(true); validate(r); REQUIRE(!r->has_finalized_table_cells);
        number("no-table effective fixed reconstruction",f.box(r,"fixed")->border_box.y,40);
        auto* owner_box=f.box(r,owner);
        REQUIRE((owner_box->scroll_completion_flags&LAYOUT_SCROLL_TABLE_CELL)==0);
        if (standalone) {
            REQUIRE(owner_box==r->root);
            REQUIRE(owner_box->style.display==AETHER_DISPLAY_TABLE_CELL);
        }
        aether_layout_result_destroy(r);
    }
    scenario="detached nested table guard";
    Fixture f("<table id='table'><tr><td><table><tr><td><div style='height:20px'></div>"
              "</td><td><div style='height:40px'></div></td></tr></table></td>"
              "<td><div style='height:80px'></div></td></tr></table>");
    auto* reference=f.layout(false); validate(reference); REQUIRE(reference->has_finalized_table_cells);
    aether_layout_result_s temporary;
    temporary.build_overlay=std::make_unique<aether::layout::MutableLayoutOverlay>();
    const StyleLookup lookup(f.map->current,temporary.build_overlay.get(),reference->assets->default_style);
    f.engine->measure_styles=&lookup; f.engine->measure_document=f.doc->identity;
    {
        detached_layout_scope_s detached(&temporary);
        auto* tree=layout_element(f.engine,&lookup,f.node("table"),{0,0,200,300},std::nullopt,
            0,true,0,false,0,forced_height_kind_e::none,false,&temporary);
        REQUIRE(!temporary.records_boxes && !temporary.has_finalized_table_cells);
        REQUIRE(temporary.primary_boxes.empty() && temporary.paint_order.empty());
        number("detached completed table height",tree->border_box.height,80);
        REQUIRE(temporary.live_box_count>0);
        delete tree;
        REQUIRE(temporary.live_box_count==0 && temporary.allocated_boxes.empty());
    }
    f.engine->measure_styles=nullptr; f.engine->measure_document.reset();
    REQUIRE(temporary.records_boxes && !temporary.has_finalized_table_cells);
    aether_layout_result_destroy(reference);
}
static void fixed_subtree_contains_table() {
    scenario="reconstructed fixed subtree contains table";
    Fixture f("<div id='owner'><div id='fixed'><table id='table'><tr id='row'>"
        "<td id='short'><div id='small'></div><div id='anchor'></div></td>"
        "<td id='tall'><div id='large'></div></td></tr></table></div></div>",
        "#owner{width:200px;height:100px;overflow:auto;transform:translate(0px,0px)}"
        "#fixed{position:fixed;left:0;top:100%;width:200px;height:50%}");
    auto* r=f.layout(true); validate(r);
    REQUIRE(r->has_finalized_table_cells);
    number("fixed subtree final y",f.box(r,"fixed")->border_box.y,100);
    number("fixed subtree final height",f.box(r,"fixed")->border_box.height,50);
    number("nested table height",f.box(r,"table")->border_box.height,80);
    number("nested finalized cell",f.box(r,"short")->border_box.height,80);
    number("nested final positioned child",f.box(r,"anchor")->border_box.y,175);
    number("outside ancestor repaired after table replacement",
        f.box(r,"owner")->scroll.scrollable_overflow_y,80);
    REQUIRE((f.box(r,"short")->scroll_completion_flags&8)!=0);
    REQUIRE((f.box(r,"owner")->scroll_completion_flags&8)==0);
    aether_layout_result_t clone=nullptr;
    REQUIRE(aether_layout_clone_snapshot(r,&clone)==AETHER_OK);
    validate(clone); compare_clone(r->root,clone->root);
    aether_layout_result_destroy(r); validate(clone); aether_layout_result_destroy(clone);
}
static aether_scroll_offset_t count_offset(aether_node_t, void* data) {
    ++*static_cast<long*>(data); return {123,456};
}
static void saved_scroll_repair() {
    scenario="saved scroll inputs/offset preservation";
    Fixture f(simple_html,"#short{overflow:hidden}#anchor{bottom:auto;top:70px;height:30px;width:150px}");
    auto* r=f.layout(true); validate(r); auto* b=f.box(r,"short");
    REQUIRE((b->scroll_completion_flags&15)==15);
    number("original raw flow",b->scroll_flow_height,20);
    number("final numeric viewport",b->scroll_base_height,80);
    number("vertical range",b->scroll.scrollable_overflow_y,20);
    b->scroll.scroll_offset_x=-13.25f; b->scroll.scroll_offset_y=1000.5f;
    const float x=b->scroll.scroll_offset_x, y=b->scroll.scroll_offset_y;
    long calls=0; f.engine->scroll_offset_fn=count_offset; f.engine->scroll_offset_data=&calls;
    // Move the live tree before repair: the helper must use its current origin.
    offset_box(r->root,17,29);
    b->scroll.scrollable_overflow_x=999; b->scroll.scrollable_overflow_y=999;
    layout_recompute_saved_scroll(b);
    number("repaired vertical range",b->scroll.scrollable_overflow_y,20);
    number("repaired horizontal range",b->scroll.scrollable_overflow_x,50);
    REQUIRE(bits(x,b->scroll.scroll_offset_x) && bits(y,b->scroll.scroll_offset_y));
    REQUIRE(calls==0);
    aether_layout_result_t clone=nullptr;
    REQUIRE(aether_layout_clone_snapshot(r,&clone)==AETHER_OK); validate(clone); compare_clone(r->root,clone->root);
    REQUIRE(clone->has_finalized_table_cells); aether_layout_result_destroy(clone); aether_layout_result_destroy(r);
}

static void callback_publication(bool prepared) {
    scenario=prepared?"callback publication/prepared":"callback publication/legacy";
    Fixture f("<table id='table'><tr id='row'><td id='short'><div id='small'>F</div>"
        "<div id='anchor'>P</div></td><td id='tall'><div id='large'>F</div></td></tr></table>");
    const auto initial=f.map->current;
    f.add("#large{height:120px}#short{background:blue}"); f.resolve();
    const auto replacement=f.map->current; REQUIRE(initial!=replacement);
    f.map->current=initial;
    f.meter.publication_target=f.map; f.meter.publication=replacement;
    auto* pinned=f.layout(prepared); validate(pinned); REQUIRE(f.meter.fired && f.map->current==replacement);
    REQUIRE(pinned->assets->base==initial);
    number("pinned short",f.box(pinned,"short")->border_box.height,80);
    number("pinned deferred anchor",f.box(pinned,"anchor")->border_box.y,75);
    painted_red(f,pinned,80);
    f.meter.publication_target=nullptr;
    auto* next=f.layout(prepared,pinned); validate(next);
    number("next publication short",f.box(next,"short")->border_box.height,120);
    number("next publication anchor",f.box(next,"anchor")->border_box.y,115);
    number("retained publication short",f.box(pinned,"short")->border_box.height,80);
    aether_layout_result_destroy(next); aether_layout_result_destroy(pinned);
}
static void callback_mutation() {
    scenario="callback structural mutation and reprepare";
    Fixture f("<table id='table'><tr id='row'><td id='short'><div id='small'>F</div>"
        "<div id='anchor'>P</div></td><td id='tall'><div id='large'>F</div></td></tr></table><div id='destination'></div>");
    f.prepare(); f.meter.mutation_parent=f.node("destination"); f.meter.mutation_child=f.node("large");
    aether_layout_result_t output=nullptr;
    REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,400,300,nullptr,nullptr,&output)==AETHER_ERROR_INVALID_STATE);
    REQUIRE(f.meter.fired && !output && !f.engine->transition_context_prepared);
    REQUIRE(!f.engine->measure_styles && !f.engine->measure_document && !f.engine->layout_in_progress);
    f.meter.mutation_child=nullptr;
    REQUIRE(aether_node_append_child(f.node("tall"),f.node("large"))==AETHER_OK);
    f.resolve(); auto* recovered=f.layout(true); validate(recovered);
    number("recovered height",f.box(recovered,"short")->border_box.height,80);
    // Preparation is consumed once: a subsequent call without prepare is legacy.
    auto* legacy=f.layout(false); REQUIRE(!legacy->effective_context_enabled); validate(legacy);
    aether_layout_result_destroy(legacy); aether_layout_result_destroy(recovered);
}

static std::string nested(int depth, int level=0, bool unequal=false) {
    const std::string i=std::to_string(level);
    const std::string flow=level+1==depth?"<div style='height:20px'>F</div>":nested(depth,level+1,false);
    return "<table id='t"+i+"'><tr id='r"+i+"'><td id='c"+i+"' style='position:relative'>"+flow+
        "<div id='a"+i+"' style='position:absolute;bottom:0;left:0;width:5px;height:5px'>P</div></td>"
        "<td><div style='height:"+std::string(unequal?"80":"20")+"px'>F</div></td></tr></table>";
}
static void bounded_work_control() {
    scenario="bounded nested callback work";
    for (int depth:{1,2,4}) for (bool prepared:{false,true}) {
        Fixture equal(nested(depth)), unequal(nested(depth,0,true));
        auto* a=equal.layout(prepared); auto* b=unequal.layout(prepared);
        validate(a); validate(b); expect_flow_completion_order(a); expect_flow_completion_order(b);
        REQUIRE(equal.meter.calls>0 && equal.meter.flow>0 && equal.meter.positioned>0);
        std::printf("table-private work depth=%d prepared=%d equal=%ld/%ld/%ld unequal=%ld/%ld/%ld (total/flow/positioned)\n",
            depth,prepared,equal.meter.calls,equal.meter.flow,equal.meter.positioned,
            unequal.meter.calls,unequal.meter.flow,unequal.meter.positioned);
        REQUIRE(equal.meter.calls==unequal.meter.calls);
        REQUIRE(equal.meter.flow==unequal.meter.flow);
        REQUIRE(equal.meter.positioned==unequal.meter.positioned);
        for (int i=0;i<depth;++i) {
            const std::string cell="c"+std::to_string(i), anchor="a"+std::to_string(i);
            number("equal nested cell",equal.box(a,cell.c_str())->border_box.height,20);
            number("unequal nested cell",unequal.box(b,cell.c_str())->border_box.height,i==0?80:20);
            number("equal positioned",equal.box(a,anchor.c_str())->border_box.y,15);
            number("unequal positioned",unequal.box(b,anchor.c_str())->border_box.y,i==0?75:15);
        }
        aether_layout_result_destroy(a); aether_layout_result_destroy(b);
    }
}

struct SavedBox {
    aether_layout_box_s* box;
    aether_rectf border, padding, content, margin;
    std::vector<aether_layout_box_s*> children;
};
static void oom_sweep() {
    scenario="prepared nested/decorated/positioned OOM sweep";
    Fixture f("<table id='table'><tr id='row'><td id='short'><table id='inner'><tr>"
        "<td><div style='height:10px'></div></td><td><div style='height:20px'></div></td></tr></table>"
        "<div id='wrapper'><div id='fixed'></div></div><div id='anchor'></div></td>"
        "<td id='tall'><div id='large'></div></td></tr></table>",
        "#short{padding:4px 2px;border:2px solid black;transform:translate(0px,0px);overflow:hidden}"
        "#inner{width:80px}#inner td{width:40px}#wrapper{height:10px;width:20px}"
        "#fixed{position:fixed;left:0;top:100%;width:5px;height:30px}");
    auto* prior=f.layout(true); validate(prior); REQUIRE(prior->has_finalized_table_cells);
    number("OOM seed row",f.box(prior,"row")->border_box.height,80);
    const auto primary=prior->primary_boxes; const auto paint=prior->paint_order;
    const auto assets=prior->assets; const auto version=f.map->current;
    const auto structure=f.doc->identity->structure_epoch, mutation=f.doc->identity->mutation_epoch,
        style=f.doc->identity->style_tree_epoch;
    std::vector<SavedBox> saved;
    for (auto* b:prior->allocated_boxes) saved.push_back({b,b->border_box,b->padding_box,b->content_box,b->margin_box,b->children});
    long failures=0; bool completed=false;
    for (long ordinal=0;ordinal<20000;++ordinal) {
        f.prepare(prior);
        aether_layout_result_t output=prior;
        allocation_count=0; injected=false; fail_at=ordinal;
        const auto status=aether_layout_layout(f.engine,f.doc,f.map,400,300,nullptr,nullptr,&output);
        fail_at=-1;
        REQUIRE(!f.engine->transition_context_prepared && !f.engine->layout_in_progress);
        REQUIRE(!f.engine->measure_styles && !f.engine->measure_document);
        REQUIRE(prior->primary_boxes==primary && prior->paint_order==paint && prior->assets==assets);
        REQUIRE(f.map->current==version && f.doc->identity->structure_epoch==structure);
        REQUIRE(f.doc->identity->mutation_epoch==mutation && f.doc->identity->style_tree_epoch==style);
        for (const auto& s:saved) {
            REQUIRE(same(s.box->border_box,s.border) && same(s.box->padding_box,s.padding));
            REQUIRE(same(s.box->content_box,s.content) && same(s.box->margin_box,s.margin));
            REQUIRE(s.box->children==s.children);
        }
        validate(prior);
        if (status==AETHER_OK) {
            REQUIRE(!injected && output && output!=prior); validate(output);
            number("sweep completion height",f.box(output,"short")->border_box.height,80);
            aether_layout_result_destroy(output); completed=true; break;
        }
        REQUIRE(injected && status==AETHER_ERROR_OUT_OF_MEMORY && output==prior); ++failures;
        auto* recovered=f.layout(true,prior); validate(recovered);
        number("OOM recovery height",f.box(recovered,"short")->border_box.height,80);
        aether_layout_result_destroy(recovered);
    }
    REQUIRE(completed && failures>0);
    std::printf("table-private OOM exhaustive contiguous ordinals=%ld; includes full prepared reconstruction; registry checks are not leak validation\n",failures);
    aether_layout_result_destroy(prior);
}
int main() {
    for (bool prepared:{false,true}) { retained_and_clone(prepared); callback_publication(prepared); }
    guards_and_detached(); fixed_subtree_contains_table(); saved_scroll_repair(); callback_mutation(); bounded_work_control(); oom_sweep();
    REQUIRE(checks>0);
    std::printf("table-private PASS active_checks=%ld sizeof_box=%zu\n",checks,sizeof(aether_layout_box_s));
}
