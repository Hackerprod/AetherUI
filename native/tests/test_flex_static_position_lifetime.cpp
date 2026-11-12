#include "../src/layout/layout_internal.h"
#include <aether/aether_contextual_style.h>
#include <aether/aether.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    bool ordered;
    explicit Fixture(bool order_case = false) : ordered(order_case) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,
            "<html id='html'><body id='body'><div id='prefix'></div>"
            "<div id='owner'><div id='parent'><div id='fixed'>"
            "<div id='ordinary'></div><div id='nested'></div></div>"
            "<div id='sibling'></div></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK);
        if (ordered) {
            REQUIRE(aether_doc_parse_html(doc,
                "<html id='html'><body id='body'><div id='prefix'></div>"
                "<div id='owner'><div id='parent'><div id='fixed'>"
                "<div id='ordinary'></div><div id='item_b'></div>"
                "<div id='item_c'></div><div id='item_d'></div><div id='nested'></div></div>"
                "<div id='sibling'></div></div></div></body></html>") == AETHER_OK);
        }
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
        if (ordered) {
            REQUIRE(aether_style_runtime_add_stylesheet(runtime,
                "#ordinary{order:2;}#item_b,#item_c,#item_d{width:10px;height:6px;flex-shrink:0;}"
                "#item_b{order:1;}#item_c{order:2;}#item_d{order:-1;}",
                0,"ordered-fallback",&sheet,nullptr) == AETHER_OK);
        }
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
    // layout_layout selects body when present, falling back to html only when
    // body is absent. The baseline has eight boxes; the ordered fixture adds
    // three. html is a style/overflow source, not an extra layout root wrapper.
    const size_t expected_boxes = f.ordered ? 11 : 8;
    if (log || result->live_box_count != expected_boxes) {
        std::printf("prepared flex-static registry: live=%zu expected=%zu (body-root contract)\n",
                    static_cast<size_t>(result->live_box_count), expected_boxes);
        for (auto* b : result->allocated_boxes) {
            const auto* node = b->element;
            const char* id = "(none)";
            if (node) {
                const auto attr = node->attributes.find("id");
                if (attr != node->attributes.end()) id = attr->second.c_str();
            }
            std::printf("  box=%p kind=%d node=%p tag=%s id=%s root=%d\n",
                        static_cast<void*>(b), static_cast<int>(b->kind),
                        static_cast<const void*>(node), node ? node->tag_name.c_str() : "(none)",
                        id, b == result->root);
        }
    }
    std::set<aether_layout_box_s*> owned, reached, painted;
    std::set<aether_node_s*> elements;
    for (auto* b : result->allocated_boxes) {
        REQUIRE(owned.insert(b).second);
        REQUIRE(b->owner == result);
        REQUIRE(b->element != nullptr);
        REQUIRE(elements.insert(b->element).second);
    }
    REQUIRE(owned.size() == expected_boxes);
    std::set<aether_node_s*> expected_elements;
    for (const char* id : {"body", "prefix", "owner", "parent", "fixed", "ordinary", "nested", "sibling"}) {
        auto* node = aether_doc_get_element_by_id(f.doc,id);
        REQUIRE(node != nullptr);
        REQUIRE(expected_elements.insert(node).second);
    }
    if (f.ordered) for (const char* id : {"item_b","item_c","item_d"})
        REQUIRE(expected_elements.insert(aether_doc_get_element_by_id(f.doc,id)).second);
    REQUIRE(elements == expected_elements);
    REQUIRE(result->root == f.box(result,"body"));
    REQUIRE(result->root->kind == LAYOUT_BOX_ELEMENT);
    REQUIRE(result->primary_boxes.count(aether_doc_get_document_element(f.doc)) == 0);
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
    if (f.ordered) {
        // Geometric order is tested separately: physical child registry is not
        // the CSS order-modified sequence.
        REQUIRE(fixed->children.size() == 5);
        for (auto* child : {ordinary,nested,f.box(result,"item_b"),f.box(result,"item_c"),f.box(result,"item_d")})
            REQUIRE(std::count(fixed->children.begin(),fixed->children.end(),child) == 1);
    } else REQUIRE(fixed->children == std::vector<aether_layout_box_s*>({ordinary,nested}));
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
    rect("ordinary",ordinary->border_box,{f.ordered ? 155.f : 135.f,92.5f,10,6},log);
    if (f.ordered) {
        rect("order-minus-one",f.box(result,"item_d")->border_box,{135,92.5f,10,6},log);
        rect("order-one",f.box(result,"item_b")->border_box,{145,92.5f,10,6},log);
        rect("order-two-stable-tie",f.box(result,"item_c")->border_box,{165,92.5f,10,6},log);
    }
    rect("nested-explicit",nested->border_box,{42,64,12,8},log);
    rect("sibling",sibling->border_box,{220,130,40,20},log);
}
struct SavedBox {
    aether_layout_box_s* box;
    aether_rectf border, padding, content, margin;
    std::vector<aether_layout_box_s*> children;
};
static void sweep(bool ordered) {
    Fixture f(ordered);
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
    long failures = 0, recoveries = 0, attempts = 0;
    long last_recovery = -1, terminal_ordinal = -1;
    bool completed = false;
    for (long ordinal = 0; ordinal < 20000; ++ordinal) {
        ++attempts;
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
            REQUIRE(output && output != prior);
            validate(f,output);
            aether_layout_result_destroy(output);
            if (!injected) {
                completed = true;
                terminal_ordinal = ordinal;
                break;
            }
            ++recoveries;
            last_recovery = ordinal;
        } else {
            REQUIRE(injected && status == AETHER_ERROR_OUT_OF_MEMORY && output == prior);
            ++failures;
        }
        // A fresh successful layout after every OOM or recovered allocation proves
        // reuse; it never resumes a partially constructed result.
        auto* fresh = f.layout(prior);
        validate(f,fresh);
        aether_layout_result_destroy(fresh);
    }
    REQUIRE(completed && failures > 0);
    REQUIRE(attempts == failures + recoveries + 1);
    // No library-dependent fallback count/ordinal is required. If one occurs,
    // prove the sweep proceeded beyond it rather than stopping prematurely.
    REQUIRE(last_recovery < terminal_ordinal);
    auto* fresh = f.layout(prior);
    validate(f,fresh,true);
    validate(f,prior);
    aether_layout_result_destroy(fresh);
    aether_layout_result_destroy(prior);
    std::printf("flex-static lifetime: ordered=%d assertions=%ld attempts=%ld oom=%ld recovered=%ld terminal=%ld; "
                "registry/reachability checks are not leak validation\n",
                ordered,assertions,attempts,failures,recoveries,terminal_ordinal);
}
int main(int argc, char** argv) {
    // Deliberately corrupt a successful result to prove the oracle catches
    // geometry errors and reversal of equal-order DOM siblings. No engine
    // behavior is changed by these test-only negative controls.
    if (argc == 2) {
        Fixture f(true);
        auto* result = f.layout(nullptr);
        validate(f,result);
        if (std::strcmp(argv[1],"--negative-order") == 0) {
            std::swap(f.box(result,"ordinary")->border_box.x,
                      f.box(result,"item_c")->border_box.x);
        } else if (std::strcmp(argv[1],"--negative-geometry") == 0) {
            f.box(result,"fixed")->border_box.y += 1;
        } else return 2;
        validate(f,result); // Must abort; runner checks the assertion signature.
        aether_layout_result_destroy(result);
        return 0;
    }
    REQUIRE(argc == 1);
    sweep(false);
    sweep(true);
}
