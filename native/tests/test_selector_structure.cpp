#include <aether/aether.h>

#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool same(float actual, float expected) {
    return actual > expected - .001f && actual < expected + .001f;
}

} // namespace

int main() {
    const char* markup = R"(
<body>
  <div id="siblings">
    <div id="first" class="item"></div>
    <div id="second" class="item"></div>
    <div id="third" class="item"></div>
  </div>
  <div id="only-host">
    <div id="only" class="item"></div>
  </div>
  <div id="empty"></div>
  <div id="whitespace">
  </div>
</body>)";
    const char* css = R"(
        .item { color: #000000; font-size: 10px; line-height: 1; }
        .item:first-child { color: #ff0000; }
        .item:nth-child(2) { color: #00ff00; }
        .item:last-child { color: #0000ff; }
        .item:only-child { font-size: 25px; }
        .item + .item { line-height: 2; }
        .item ~ .item { font-size: 20px; }
        #empty:empty { color: #ff00ff; }
        #whitespace:empty { color: #ff00ff; }
    )";

    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK &&
            aether_doc_parse_html(doc, markup) == AETHER_OK,
        "formatted structural-selector markup must parse");

    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{640.f, 480.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "selector test style runtime must initialize");
    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "selector-structure.css", &sheet, &rule_count) == AETHER_OK,
        "selector test stylesheet must compile");

    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(update_count > 0 && updates != nullptr, "selector test must produce initial styles");

    const auto first = aether_doc_get_element_by_id(doc, "first");
    const auto second = aether_doc_get_element_by_id(doc, "second");
    const auto third = aether_doc_get_element_by_id(doc, "third");
    const auto only = aether_doc_get_element_by_id(doc, "only");
    const auto empty = aether_doc_get_element_by_id(doc, "empty");
    const auto whitespace = aether_doc_get_element_by_id(doc, "whitespace");
    require(first && second && third && only && empty && whitespace,
        "selector test IDs must resolve");

    std::unordered_map<aether_node_t, aether_computed_style_t> styles;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
            "selector test style update must be readable");
        if (node == first || node == second || node == third || node == only ||
            node == empty || node == whitespace) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK,
                "selector test style must copy");
            styles.emplace(node, style);
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, update_count);

    require(styles.size() == 6, "every selector test element must receive a computed style");
    require(same(styles[first].color.r, 1.f) && same(styles[first].color.g, 0.f),
        ":first-child must ignore leading indentation text");
    require(same(styles[second].color.g, 1.f) && same(styles[second].color.r, 0.f),
        ":nth-child must count only element siblings");
    require(same(styles[third].color.b, 1.f) && same(styles[third].color.r, 0.f),
        ":last-child must ignore trailing indentation text");
    require(same(styles[only].font_size, 25.f),
        ":only-child must ignore surrounding indentation text");
    require(same(styles[second].line_height, 2.f) && same(styles[third].line_height, 2.f),
        "adjacent sibling combinators must ignore whitespace text between elements");
    require(same(styles[second].font_size, 20.f) && same(styles[third].font_size, 20.f),
        "general sibling combinators must ignore whitespace text between elements");
    require(same(styles[empty].color.r, 1.f) && same(styles[empty].color.b, 1.f),
        ":empty must match an element without DOM children");
    require(same(styles[whitespace].color.r, 0.f) && same(styles[whitespace].color.b, 0.f),
        ":empty must not match whitespace text content");

    for (auto& [_, style] : styles) aether_computed_style_free(&style);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    return 0;
}
