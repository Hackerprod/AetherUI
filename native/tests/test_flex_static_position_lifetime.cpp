#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>
#include <aether/aether.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>
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

static long assertions = 0;
#define REQUIRE(condition) do { ++assertions; if (!(condition)) { \
    std::fprintf(stderr, "FAIL prepared flex-static lifetime %s:%d: %s\n", \
                 __FILE__, __LINE__, #condition); std::abort(); } } while (false)

static bool same(float a, float b) {
    return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) < .01f;
}
static bool same(aether_rectf a, aether_rectf b) {
    return same(a.x,b.x) && same(a.y,b.y) && same(a.width,b.width) && same(a.height,b.height);
}
static void rect(const char* label, aether_rectf actual, aether_rectf expected, bool log) {
    if (log || !same(actual, expected))
        std::printf("prepared flex-static %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n",
            label, actual.x, actual.y, actual.width, actual.height,
            expected.x, expected.y, expected.width, expected.height);
    REQUIRE(same(actual, expected));
}
static aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) {
    return {0,0,0};
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    Fixture() {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,
            "<html id='html'><body id='body'><div id='prefix'></div>"
            "<div id='owner'><div id='parent'><div id='fixed'>"
            "<div id='ordinary'></div><div id='nested'></div></div>"
            "<div id='sibling'></div></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK);
        const char* css =
            "*{margin:0;padding:0;border:0;box-sizing:content-box;}"
            "html,body,div{display:block;}#prefix{height:50px;}"
            "#owner{margin-left:30px;width:400px;height:auto;padding:10px;"
            "border:5px solid black;transform:translate(0px,0px);}"
            "#parent{display:flex;width:300px;height:120px;padding:10px 20px 30px 40px;"
            "border:5px solid black;justify-content:center;align-items:center;}"
            "#fixed{position:fixed;display:flex;width:50%;height:50%;"
            "justify-content:flex-start;align-items:flex-start;background:red;}"
            "#ordinary{width:10px;height:6px;flex-shrink:0;background:green;}"
            "#nested{position:fixed;left:7px;top:9px;width:12px;height:8px;background:blue;}"
            "#sibling{position:absolute;width:40px;height:20px;background:yellow;}";
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,css,0,"flex-static-lifetime",
                                                    &sheet,nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,
                           aether_doc_get_document_element(doc), &updates);
        REQUIRE(count > 0);
        aether_style_updates_free(updates,count);
        REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&map) == AETHER_OK);
        REQUIRE(aether_layout_create(measure,nullptr,&engine) == AETHER_OK);
    }
    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    void prepare(aether_layout_result_t previous) {
        aether_bool_t changed = -1;
        REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,map,previous,&changed) == AETHER_OK);
    }
    aether_layout_result_t layout(aether_layout_result_t previous) {
        prepare(previous);
        aether_layout_result_t output = nullptr;
        REQUIRE(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&output) == AETHER_OK);
        REQUIRE(output != nullptr);
        return output;
    }
    aether_layout_box_s* box(aether_layout_result_t result, const char* id) {
        auto* node = aether_doc_get_element_by_id(doc,id);
        REQUIRE(node != nullptr);
        const auto it = result->primary_boxes.find(node);
        REQUIRE(it != result->primary_boxes.end());
        return it->second;
    }
};
static void walk(aether_layout_box_s* box, const std::set<aether_layout_box_s*>& owned,
                 std::set<aether_layout_box_s*>& reached) {
    REQUIRE(owned.count(box) == 1);
    REQUIRE(reached.insert(box).second);
    for (auto* child : box->children) walk(child,owned,reached);
}
static void validate(Fixture& f, aether_layout_result_t result, bool log = false) {
    std::set<aether_layout_box_s*> owned, reached, painted;
    std::set<aether_node_s*> elements;
    for (auto* b : result->allocated_boxes) {
        REQUIRE(owned.insert(b).second);
        REQUIRE(b->owner == result);
        REQUIRE(b->element != nullptr);
        REQUIRE(elements.insert(b->element).second);
    }
    REQUIRE(owned.size() == 9);
    REQUIRE(owned.size() == result->live_box_count);
    walk(result->root,owned,reached);
    REQUIRE(reached == owned);
    REQUIRE(result->primary_boxes.size() == owned.size());
    for (const auto& entry : result->primary_boxes) {
        REQUIRE(owned.count(entry.second) == 1);
        REQUIRE(entry.second->element == entry.first);
    }
    for (auto* b : result->paint_order) {
        REQUIRE(owned.count(b) == 1);
        REQUIRE(painted.insert(b).second);
    }
    REQUIRE(painted == owned);
    auto* owner = f.box(result,"owner");
    auto* parent = f.box(result,"parent");
    auto* fixed = f.box(result,"fixed");
    auto* ordinary = f.box(result,"ordinary");
    auto* nested = f.box(result,"nested");
    auto* sibling = f.box(result,"sibling");
    REQUIRE(parent->children == std::vector<aether_layout_box_s*>({fixed,sibling}));
    REQUIRE(fixed->children == std::vector<aether_layout_box_s*>({ordinary,nested}));
    REQUIRE(owner->children == std::vector<aether_layout_box_s*>({parent}));
    // Independent literals: owner final padding is 420x190, but the child's
    // static center comes from its immediate parent's 300x120 content box.
    rect("owner-border",owner->border_box,{30,50,430,200},log);
    rect("owner-padding",owner->padding_box,{35,55,420,190},log);
    rect("owner-content",owner->content_box,{45,65,400,170},log);
    rect("parent-border",parent->border_box,{45,65,370,170},log);
    rect("parent-padding",parent->padding_box,{50,70,360,160},log);
    rect("parent-content",parent->content_box,{90,80,300,120},log);
    rect("fixed",fixed->border_box,{135,92.5f,210,95},log);
    rect("ordinary",ordinary->border_box,{135,92.5f,10,6},log);
    rect("nested-explicit",nested->border_box,{42,64,12,8},log);
    rect("sibling",sibling->border_box,{220,130,40,20},log);
}
struct SavedBox {
    aether_layout_box_s* box;
    aether_rectf border, padding, content, margin;
    std::vector<aether_layout_box_s*> children;
};
int main() {
    Fixture f;
    auto* prior = f.layout(nullptr);
    validate(f,prior,true);
    std::vector<SavedBox> saved;
    for (auto* b : prior->allocated_boxes)
        saved.push_back({b,b->border_box,b->padding_box,b->content_box,b->margin_box,b->children});
    const auto primary = prior->primary_boxes;
    const auto paint = prior->paint_order;
    const auto assets = prior->assets;
    const auto base = f.map->current;
    const auto structure = f.doc->identity->structure_epoch;
    const auto mutation = f.doc->identity->mutation_epoch;
    const auto style = f.doc->identity->style_tree_epoch;
    long failures = 0;
    bool completed = false;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        // Preparation is one-shot and deliberately outside the fault window.
        f.prepare(prior);
        aether_layout_result_t output = prior;
        allocation_count = 0;
        injected = false;
        fail_at = ordinal;
        const auto status = aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&output);
        fail_at = -1;
        REQUIRE(prior->primary_boxes == primary && prior->paint_order == paint);
        REQUIRE(prior->assets == assets && f.map->current == base);
        REQUIRE(f.doc->identity->structure_epoch == structure);
        REQUIRE(f.doc->identity->mutation_epoch == mutation);
        REQUIRE(f.doc->identity->style_tree_epoch == style);
        for (const auto& s : saved) {
            REQUIRE(same(s.box->border_box,s.border));
            REQUIRE(same(s.box->padding_box,s.padding));
            REQUIRE(same(s.box->content_box,s.content));
            REQUIRE(same(s.box->margin_box,s.margin));
            REQUIRE(s.box->children == s.children);
        }
        validate(f,prior);
        if (status == AETHER_OK) {
            REQUIRE(!injected && output && output != prior);
            validate(f,output);
            aether_layout_result_destroy(output);
            completed = true;
            break;
        }
        REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == prior);
        ++failures;
        // A new successful layout immediately after every failure proves engine
        // reuse; it does not attempt to resume a partially constructed result.
        auto* fresh = f.layout(prior);
        validate(f,fresh);
        aether_layout_result_destroy(fresh);
    }
    REQUIRE(completed && failures > 0);
    auto* fresh = f.layout(prior);
    validate(f,fresh,true);
    validate(f,prior);
    aether_layout_result_destroy(fresh);
    aether_layout_result_destroy(prior);
    const int scenarios = 1;
    REQUIRE(scenarios == 1 && assertions > 0);
    std::printf("flex-static lifetime: mode=prepared scenarios=%d assertions=%ld injected-layout-failures=%ld; "
                "registry/reachability checks are not leak validation\n",scenarios,assertions,failures);
}
