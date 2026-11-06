#include <aether/aether.h>
#include "../src/style_runtime/style_runtime_types.h"
#include <climits>

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


// Finite An+B guards use the public style runtime; integer matcher bounds stay private.
void nth_fixture(const char* name, const char* css, int expected_rules,
                 const char* mask, float selected = .8f, float other = .2f,
                 float parent_expected = 1.f) {
    const char* markup = "<!doctype html><html><head></head><body><div id='parent'><div id='a' class='item'></div><div id='b' class='item'></div><div id='c' class='item'></div></div></body></html>";
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc, markup) == AETHER_OK, "nth markup");
    aether_style_runtime_options_t options{};
    const aether_style_environment_t environment{800.f, 600.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK, "nth runtime");
    int64_t sheet = 0; int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime, ".item{opacity:.2;}", 0, "base", &sheet, &rules) == AETHER_OK && rules == 1, "nth base count");
    require(aether_style_runtime_add_stylesheet(runtime, css, 1, "nth", &sheet, &rules) == AETHER_OK, "nth compile status");
    if (rules != expected_rules) { std::fprintf(stderr, "%s: rule count %d expected %d\n", name, rules, expected_rules); require(false, "nth rule count"); }
    const aether_node_t nodes[] = {aether_doc_get_element_by_id(doc,"a"),aether_doc_get_element_by_id(doc,"b"),aether_doc_get_element_by_id(doc,"c"),aether_doc_get_element_by_id(doc,"parent")};
    for (auto node : nodes) require(node != nullptr, "nth IDs");
    aether_style_update_t* updates = nullptr;
    const auto count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(count > 0 && updates != nullptr, "nth updates");
    bool seen[4]{};
    for (int32_t i = 0; i < count; ++i) {
        aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "nth get_update");
        for (int j = 0; j < 4; ++j) if (node == nodes[j]) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "nth style copy");
            const float expected = j == 3 ? parent_expected : (mask[j] == 'T' ? selected : other);
            const bool pass = style.opacity > expected - .0001f && style.opacity < expected + .0001f;
            if (!pass) std::fprintf(stderr, "%s: node %d opacity %g expected %g\n", name, j, style.opacity, expected);
            require(pass, "nth opacity"); seen[j] = true;
            aether_computed_style_free(&style);
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, count);
    for (bool found : seen) require(found, "nth missing style");
    aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
}
void nth_expression_guards() {
    nth_fixture("corpus4 baseline", "", 0, "FFF");
    nth_fixture("corpus4 odd", ".item:nth-child(odd){opacity:.8;}", 1, "TFT");
    nth_fixture("corpus4 2n-1", ".item:nth-child(2n-1){opacity:.8;}", 1, "TFT");
    nth_fixture("corpus4 2n+1", ".item:nth-child(2n+1){opacity:.8;}", 1, "TFT");
    nth_fixture("guards even", ".item:nth-child(even){opacity:.8;}", 1, "FTF");
    nth_fixture("guards ODD", ".item:nth-child(ODD){opacity:.8;}", 1, "TFT");
    nth_fixture("guards n", ".item:nth-child(n){opacity:.8;}", 1, "TTT");
    nth_fixture("guards +n", ".item:nth-child(+n){opacity:.8;}", 1, "TTT");
    nth_fixture("guards +1n", ".item:nth-child(+1n){opacity:.8;}", 1, "TTT");
    nth_fixture("guards n+0", ".item:nth-child(n+0){opacity:.8;}", 1, "TTT");
    nth_fixture("guards -n", ".item:nth-child(-n){opacity:.8;}", 1, "FFF");
    nth_fixture("guards -n+3", ".item:nth-child(-n+3){opacity:.8;}", 1, "TTT");
    nth_fixture("guards -2n+3", ".item:nth-child(-2n+3){opacity:.8;}", 1, "TFT");
    nth_fixture("guards 2n+0", ".item:nth-child(2n+0){opacity:.8;}", 1, "FTF");
    nth_fixture("guards -2n-1", ".item:nth-child(-2n-1){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 2", ".item:nth-child(2){opacity:.8;}", 1, "FTF");
    nth_fixture("guards +2", ".item:nth-child(+2){opacity:.8;}", 1, "FTF");
    nth_fixture("guards 0n+2", ".item:nth-child(0n+2){opacity:.8;}", 1, "FTF");
    nth_fixture("guards 0", ".item:nth-child(0){opacity:.8;}", 1, "FFF");
    nth_fixture("guards -2", ".item:nth-child(-2){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 0n", ".item:nth-child(0n){opacity:.8;}", 1, "FFF");
    nth_fixture("guards +0n+0", ".item:nth-child(+0n+0){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 3", ".item:nth-child(3){opacity:.8;}", 1, "FFT");
    nth_fixture("guards  \t2N\r\n+\f1 ", ".item:nth-child( \t2N\r\n+\f1 ){opacity:.8;}", 1, "TFT");
    nth_fixture("guards n- 1", ".item:nth-child(n- 1){opacity:.8;}", 1, "TTT");
    nth_fixture("guards n - 1", ".item:nth-child(n - 1){opacity:.8;}", 1, "TTT");
    nth_fixture("guards + 2", ".item:nth-child(+ 2){opacity:.8;}", 0, "FFF");
    nth_fixture("guards - 2", ".item:nth-child(- 2){opacity:.8;}", 0, "FFF");
    nth_fixture("guards + n", ".item:nth-child(+ n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards - n", ".item:nth-child(- n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2 n", ".item:nth-child(2 n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2n 1", ".item:nth-child(2n 1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2n1", ".item:nth-child(2n1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n--1", ".item:nth-child(n--1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n+-1", ".item:nth-child(n+-1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n-+1", ".item:nth-child(n-+1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n++1", ".item:nth-child(n++1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n + -1", ".item:nth-child(n + -1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n - +1", ".item:nth-child(n - +1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards +", ".item:nth-child(+){opacity:.8;}", 0, "FFF");
    nth_fixture("guards -", ".item:nth-child(-){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n+", ".item:nth-child(n+){opacity:.8;}", 0, "FFF");
    nth_fixture("guards nn", ".item:nth-child(nn){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2nx", ".item:nth-child(2nx){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2n-1x", ".item:nth-child(2n-1x){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2x", ".item:nth-child(2x){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2.0n", ".item:nth-child(2.0n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2e0n", ".item:nth-child(2e0n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards o dd", ".item:nth-child(o dd){opacity:.8;}", 0, "FFF");
    nth_fixture("guards ", ".item:nth-child(){opacity:.8;}", 0, "FFF");
    nth_fixture("guards  \t\r\n\f", ".item:nth-child( \t\r\n\f){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2n\v+1", ".item:nth-child(2n\v+1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards \v2n+1\v", ".item:nth-child(\v2n+1\v){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2147483647", ".item:nth-child(2147483647){opacity:.8;}", 1, "FFF");
    nth_fixture("guards -2147483648", ".item:nth-child(-2147483648){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 2147483647n", ".item:nth-child(2147483647n){opacity:.8;}", 1, "FFF");
    nth_fixture("guards -2147483648n", ".item:nth-child(-2147483648n){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 2147483647n+1", ".item:nth-child(2147483647n+1){opacity:.8;}", 1, "TFF");
    nth_fixture("guards -2147483648n+1", ".item:nth-child(-2147483648n+1){opacity:.8;}", 1, "TFF");
    nth_fixture("guards n-2147483648", ".item:nth-child(n-2147483648){opacity:.8;}", 1, "TTT");
    nth_fixture("guards -n+2147483647", ".item:nth-child(-n+2147483647){opacity:.8;}", 1, "TTT");
    nth_fixture("guards -1n+2147483647", ".item:nth-child(-1n+2147483647){opacity:.8;}", 1, "TTT");
    nth_fixture("guards 0n-2147483648", ".item:nth-child(0n-2147483648){opacity:.8;}", 1, "FFF");
    nth_fixture("guards 2147483648", ".item:nth-child(2147483648){opacity:.8;}", 0, "FFF");
    nth_fixture("guards -2147483649", ".item:nth-child(-2147483649){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 2147483648n", ".item:nth-child(2147483648n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards -2147483649n", ".item:nth-child(-2147483649n){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n+2147483648", ".item:nth-child(n+2147483648){opacity:.8;}", 0, "FFF");
    nth_fixture("guards n-2147483649", ".item:nth-child(n-2147483649){opacity:.8;}", 0, "FFF");
    nth_fixture("guards 9999999999999999999999999999999999999999", ".item:nth-child(9999999999999999999999999999999999999999){opacity:.8;}", 0, "FFF");
    nth_fixture("guards .item:is(:nth-child(2n+1))", ".item:is(:nth-child(2n+1)){opacity:.8;}", 1, "TFT");
    nth_fixture("guards .item:where(:nth-child(2n+1))", ".item:where(:nth-child(2n+1)){opacity:.8;}", 1, "TFT");
    nth_fixture("guards .item:not(:nth-child(2n+1))", ".item:not(:nth-child(2n+1)){opacity:.8;}", 1, "FTF");
    nth_fixture("guards .item:nth-child(2n+1 of .item)", ".item:nth-child(2n+1 of .item){opacity:.8;}", 0, "FFF");
    nth_fixture("guards .item:nth-last-child(2n+1)", ".item:nth-last-child(2n+1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards .item:nth-of-type(2n+1)", ".item:nth-of-type(2n+1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards .item:nth-last-of-type(2n+1)", ".item:nth-last-of-type(2n+1){opacity:.8;}", 0, "FFF");
    nth_fixture("guards .item:nth-child(n+-1),#b", ".item:nth-child(n+-1),#b{opacity:.8;}", 1, "FTF");
    nth_fixture("guards .item:is(:nth-child(n+-1),#b)", ".item:is(:nth-child(n+-1),#b){opacity:.8;}", 1, "FTF");
    nth_fixture("guards .item:is(:nth-child(n+-1))", ".item:is(:nth-child(n+-1)){opacity:.8;}", 1, "FFF");
    nth_fixture("has parent", "#parent:has(.item:nth-child(2n+1)){opacity:.8;}", 1, "FFF", .8f, .2f, .8f);
    nth_fixture("cascade specificity", ".item:nth-child(2n+1){opacity:.8;}.item{opacity:.4;}", 2, "TFT", .8f, .4f);
    nth_fixture("cascade order", ".item:nth-child(2n+1){opacity:.8;}.item{opacity:.4;}.item:nth-child(2n+1){opacity:.6;}", 3, "TFT", .6f, .4f);
    nth_fixture("where zero specificity", ".item:where(:nth-child(2n+1)){opacity:.8;}.item{opacity:.4;}", 2, "FFF", .8f, .4f);
    require((nth_expression_s{1, INT_MIN}).matches(INT_MAX), "nth widened positive difference");
    require((nth_expression_s{-1, INT_MAX}).matches(INT_MAX), "nth negative coefficient boundary");
    std::puts("PASS nth corpus4 and closed guards (new DLL only)");
}

} // namespace

int main() {
    nth_expression_guards();
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
