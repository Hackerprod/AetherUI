#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static long assertions = 0;
static int scenarios = 0;
static const char* mode = "setup";
static const char* positioning = "setup";
#define REQUIRE(condition) do { ++assertions; if (!(condition)) { \
    std::fprintf(stderr, "FAIL flex-static contextual mode=%s position=%s %s:%d: %s\n", \
        mode, positioning, __FILE__, __LINE__, #condition); std::abort(); } } while (false)

static void equal(float actual, float expected) {
    REQUIRE(std::isfinite(actual) && std::isfinite(expected));
    if (std::fabs(actual - expected) >= .01f)
        std::fprintf(stderr, "actual=%g expected=%g\n", actual, expected);
    REQUIRE(std::fabs(actual - expected) < .01f);
}
static void rectangle(const char* label, aether_rectf actual, aether_rectf expected) {
    std::printf("contextual mode=%s position=%s %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n",
        mode, positioning, label, actual.x, actual.y, actual.width, actual.height,
        expected.x, expected.y, expected.width, expected.height);
    equal(actual.x, expected.x);
    equal(actual.y, expected.y);
    equal(actual.width, expected.width);
    equal(actual.height, expected.height);
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t result = nullptr;
    Fixture(bool fixed, bool prepared) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc,
            "<html><body><div id='host'><div id='item'></div></div></body></html>") == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime) == AETHER_OK);
        const std::string css =
            std::string("html,body,div{display:block;margin:0;padding:0;border:0;box-sizing:content-box;}"
            "#host{position:relative;display:flex;width:300px;height:200px;"
            "padding:10px 40px 30px 20px;border:5px solid black;overflow:hidden;"
            "justify-content:flex-start;align-items:flex-start;}"
            "#item{width:400px;height:300px;position:") + (fixed ? "fixed" : "absolute") + ";}";
        int64_t sheet = 0;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,
                                                   "flex-static-contextual",&sheet,nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,
                           aether_doc_get_document_element(doc),&updates);
        REQUIRE(count > 0);
        aether_style_updates_free(updates,count);
        // A revision-created map selects actual contextual used-geometry and
        // visual finalization, unlike a legacy resolved map with preparation.
        aether_style_revision_t revision = nullptr;
        REQUIRE(aether_style_runtime_retain_revision(runtime,&revision) == AETHER_OK);
        REQUIRE(aether_style_revision_create_map(revision,&map) == AETHER_OK);
        aether_style_revision_destroy(revision);
        REQUIRE(aether_layout_create(nullptr,nullptr,&engine) == AETHER_OK);
        if (prepared) {
            aether_bool_t changed = -1;
            REQUIRE(aether_layout_prepare_transition_context(engine,nullptr,map,nullptr,&changed) == AETHER_OK);
        }
        REQUIRE(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&result) == AETHER_OK);
        REQUIRE(result != nullptr);
    }
    ~Fixture() {
        aether_layout_result_destroy(result);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    aether_layout_box_t box(const char* id) const {
        auto node = aether_doc_get_element_by_id(doc,id);
        REQUIRE(node != nullptr);
        auto found = aether_layout_result_find_primary_box(result,node);
        REQUIRE(found != nullptr);
        return found;
    }
};
int main() {
    for (int prepared = 0; prepared < 2; ++prepared) {
        mode = prepared ? "revision-map+prepare" : "revision-map";
        for (int fixed = 0; fixed < 2; ++fixed) {
            positioning = fixed ? "viewport-fixed" : "absolute";
            Fixture fixture(fixed != 0, prepared != 0);
            const auto host = fixture.box("host");
            const auto child = fixture.box("item");
            rectangle("host-border",aether_layout_box_get_border_box(host),{0,0,370,250});
            rectangle("host-padding",aether_layout_box_get_padding_box(host),{5,5,360,240});
            rectangle("host-content",aether_layout_box_get_content_box(host),{25,15,300,200});
            rectangle("child-border",aether_layout_box_get_border_box(child),{25,15,400,300});
            const auto scroll = aether_layout_box_get_scroll_metrics(host);
            const float expected = fixed ? 0.f : 100.f;
            std::printf("contextual mode=%s position=%s overflow actual=(%g,%g) expected=(%g,%g)\n",
                mode, positioning, scroll.scrollable_overflow_x, scroll.scrollable_overflow_y,
                expected, expected);
            // No transform owner exists: contextual finalization must exclude
            // viewport-fixed overflow, while the same absolute box contributes.
            equal(scroll.scrollable_overflow_x,expected);
            equal(scroll.scrollable_overflow_y,expected);
            ++scenarios;
        }
    }
    REQUIRE(scenarios == 4 && assertions > 0);
    std::printf("flex-static contextual: scenarios=%d assertions=%ld (NDEBUG-safe)\n",scenarios,assertions);
}
