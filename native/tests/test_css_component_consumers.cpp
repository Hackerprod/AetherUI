#include "../src/style_runtime/css_component_values.h"
#include "../src/css/border_radii.h"
#include "../src/css/background_layers.h"
#include "../src/css/backdrop_filters.h"
#include "../src/css/transforms.h"
#include "../src/css/linear_gradient.h"
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
static void pipeline() {
    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK, "pipeline document");
    require(aether_doc_parse_html(doc, "<html><body><div id='box'></div></body></html>") == AETHER_OK, "pipeline HTML");
    const auto root = aether_doc_get_document_element(doc);
    const auto box = aether_doc_get_element_by_id(doc, "box");
    aether_style_runtime_t runtime = nullptr;
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK, "pipeline runtime");
    int64_t sheet = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
        "#box { --x: 7px; --y: 11px; --p: 7%; --f: Times; --g: New; --s: 1s; --e: ease;"
        "margin: var(--x)var(--y); padding: var(--p)var(--y); font-family: var(--f)var(--g);"
        "box-shadow: var(--x)var(--y); border-radius: var(--x)var(--y); transition: opacity var(--s)var(--e); }",
        2, "components.css", &sheet, nullptr) == AETHER_OK, "pipeline stylesheet");
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
    require(count > 0, "pipeline resolve");
    bool found = false;
    for (int i = 0; i < count; ++i) {
        aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr; uint32_t impact = 0;
        require(aether_style_runtime_get_update(updates[i], &node, &handle, &impact) == AETHER_OK, "pipeline update");
        if (node == box) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "pipeline style");
            require(style.margin.top.value == 7 && style.margin.right.value == 11, "pipeline margin adjacent dimensions");
            require(style.padding.top.value == 7 && style.padding.right.value == 11, "pipeline padding adjacent percentage");
            require(std::string(style.font_family) == "Times New", "pipeline family reconstructed");
            require(style.box_shadows.count == 1 && style.box_shadows.items[0].offset_y == 11, "pipeline shadow components");
            require(style.border_radii.top_right.horizontal.value == 11, "pipeline border radius comment slash");
            require(style.transition_duration == 1 && style.transition_timing == AETHER_TRANSITION_TIMING_EASE, "pipeline transition components");
            aether_computed_style_free(&style); found = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    require(found, "pipeline target update");
    aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
int main() {
    require(css_split_components("7px/**/11px") == std::vector<std::string>{"7px", "11px"}, "comment-separated dimensions");
    require(css_split_components("7%11px") == std::vector<std::string>{"7%", "11px"}, "adjacent percentage dimension");
    require(css_split_components("10/**/px") == std::vector<std::string>{"10", "px"}, "do not manufacture dimensions");
    require(css_split_components("calc/**/(10px)").size() == 2, "do not manufacture functions");
    require(css_split_components("url(a/b) 'a,b' calc(1px/**/ + 2px)").size() == 3, "opaque URL string function");
    require(css_split_components("a\\ b c").size() == 2, "escaped space belongs to identifier");
    require(css_split_components("[1px)").empty(), "unbalanced block rejected");
    const std::string trivia = "a/**/b\f/**/c";
    css_component_cursor cursor(trivia); css_component_value component;
    require(cursor.next(component) && !component.whitespace_before, "first component");
    require(cursor.next(component) && !component.whitespace_before, "comment not whitespace");
    require(cursor.next(component) && component.whitespace_before, "form feed is whitespace");
    aether_border_radii radii{};
    require(aether_border_radii_try_parse("7px/**/11px", &radii), "radius ignores synthetic slash");
    require(radii.top_left.horizontal.value == 7 && radii.top_right.horizontal.value == 11, "radius components retained");
    require(aether_border_radii_try_parse("7%11px / 3px/**/4px", &radii), "radius lexical slash");
    require(radii.top_left.vertical.value == 3 && radii.top_right.vertical.value == 4, "radius vertical components");
    require(!aether_border_radii_try_parse("10/**/px", &radii), "radius number ident invalid");
    require(!aether_border_radii_try_parse("1px / 2px / 3px", &radii), "duplicate slash invalid");
    std::vector<aether_background_position_s> positions;
    require(aether_background_position_list_try_parse("7%11px", positions), "background adjacent position");
    require(positions.size() == 1 && positions[0].y.value == 11, "background position values");
    aether_background_shorthand_s background;
    require(aether_background_shorthand_try_parse("7px/**/11px / 20px/**/30px", background), "background synthetic slash");
    aether_transform_list transform{};
    require(aether_transform_list_try_parse("translate(7%,11px)/**/scale(2)", &transform), "transform components and functions");
    aether_transform_list_free(&transform);
    require(!aether_transform_list_try_parse("translate(7%11px)", &transform), "transform arguments require commas even at token boundaries");
    require(!aether_transform_list_try_parse("translate/**/(7px)", &transform), "transform function boundary");
    require(!aether_transform_list_try_parse("translate(10/**/px)", &transform), "transform dimension boundary");
    aether_backdrop_filter_list filters{};
    require(aether_backdrop_filter_list_try_parse("blur(1px)/**/blur(2px)", &filters), "filter function components");
    aether_backdrop_filter_list_free(&filters);
    require(!aether_backdrop_filter_list_try_parse("blur/**/(1px)", &filters), "filter function boundary");
    aether_linear_gradient gradient{};
    require(aether_linear_gradient_try_parse("linear-gradient(to/**/right, red/**/7%, blue 100%)", &gradient, nullptr), "gradient keyword and stop components");
    aether_linear_gradient_free(&gradient);
    require(!aether_linear_gradient_try_parse("linear-gradient(red 10/**/px, blue)", &gradient, nullptr), "gradient dimension boundary");
    aether_color_rgba color{};
    require(aether_color_rgba_try_parse("rgb(10/**/,20,30)", &color), "color channel peripheral comment");
    require(!aether_color_rgba_try_parse("rgb(10/**/%,20,30)", &color), "color percentage boundary");
    require(!aether_color_rgba_try_parse("rgb(1/**/2,20,30)", &color), "color adjacent numbers invalid");
    pipeline();
    std::puts("CSS component consumer tests passed");
}
