#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
static void require(bool condition, int line) {
    if (!condition) { std::fprintf(stderr, "failed line %d\n", line); std::abort(); }
}
#define REQUIRE(x) require(bool(x), __LINE__)
int main() {
    aether_doc_t document{};
    REQUIRE(aether_doc_create(&document) == AETHER_OK);
    REQUIRE(aether_doc_parse_html(document,
        "<!doctype html><html><body><div id='scroll'><div id='clip'><div id='desc'></div></div></div></body></html>") == AETHER_OK);
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    aether_style_runtime_t runtime{};
    REQUIRE(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK);
    int64_t sheet{};
    REQUIRE(aether_style_runtime_add_stylesheet(runtime,
        "*{display:block;margin:0;padding:0}#scroll{width:100px;height:100px;overflow:auto}"
        "#clip{width:100px;height:50px;overflow-x:clip;overflow-y:visible;transform:rotate(-90deg);transform-origin:0 0}"
        "#desc{width:20px;height:20px;position:relative;top:200px}",
        0, "rotated-clip", &sheet, nullptr) == AETHER_OK);
    aether_style_transaction_t transaction{};
    aether_style_revision_t revision{};
    REQUIRE(aether_style_runtime_prepare_revision(runtime, aether_doc_get_document_element(document), &environment, &transaction) == AETHER_OK);
    REQUIRE(aether_style_runtime_commit_revision(runtime, transaction, &revision) == AETHER_OK);
    aether_style_transaction_destroy(transaction);
    aether_style_map_t map{};
    REQUIRE(aether_style_revision_create_map(revision, &map) == AETHER_OK);
    aether_layout_engine_t engine{};
    REQUIRE(aether_layout_create(nullptr, nullptr, &engine) == AETHER_OK);
    aether_layout_result_t layout{};
    REQUIRE(aether_layout_layout(engine, document, map, 800, 600, nullptr, nullptr, &layout) == AETHER_OK);
    const auto scroll = aether_layout_result_find_primary_box(layout, aether_doc_get_element_by_id(document, "scroll"));
    const auto overflow = aether_layout_box_get_scrollable_overflow_x(scroll);
    std::fprintf(stderr, "rotated one-axis clip overflow: %f (expected120)\n", overflow);
    REQUIRE(std::abs(overflow - 120) < .01f);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(map);
    aether_style_revision_destroy(revision);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
}
