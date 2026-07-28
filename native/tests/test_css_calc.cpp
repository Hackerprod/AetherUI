#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static bool same(float actual, float expected) {
    return std::fabs(actual - expected) < .001f;
}

static aether_css_length parse(const char* source) {
    aether_css_length result{};
    require(aether_css_length_try_parse(source, &result) == AETHER_TRUE, source);
    return result;
}

static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 1.f, .8f};
}

int main() {
    const auto half_minus_eight = parse("calc(50% - 8px)");
    require(half_minus_eight.unit == AETHER_CSS_UNIT_LINEAR &&
            same(half_minus_eight.value, -8.f) && same(half_minus_eight.linear_percent, 50.f),
        "calc() must preserve a percent-plus-pixel expression symbolically");
    require(same(aether_css_length_resolve(half_minus_eight, 400.f, 16.f, 16.f, 0.f, 0.f), 192.f),
        "calc(50% - 8px) must resolve against the supplied percentage reference");

    const auto scaled = parse("calc((10px + 2em) * 2 / 4)");
    require(same(aether_css_length_resolve(scaled, 0.f, 5.f, 16.f, 0.f, 0.f), 10.f),
        "calc multiplication and division by unitless scalars must scale every linear term");

    const auto third = parse("calc(100% / 3)");
    require(same(aether_css_length_resolve(third, 300.f, 16.f, 16.f, 0.f, 0.f), 100.f),
        "calc percentage division must remain symbolic until the containing reference is known");

    const auto negative = parse("calc(-4px + (3 * 2em))");
    require(same(aether_css_length_resolve(negative, 0.f, 5.f, 16.f, 0.f, 0.f), 26.f),
        "calc must support unary signs and number-times-length expressions");

    aether_css_length invalid{};
    require(aether_css_length_try_parse("calc(10px * 2px)", &invalid) == AETHER_FALSE,
        "calc must reject multiplication of two dimensional values");
    require(aether_css_length_try_parse("calc(10px / 0)", &invalid) == AETHER_FALSE,
        "calc must reject division by zero");
    require(aether_css_length_try_parse("calc(10px / 2px)", &invalid) == AETHER_FALSE,
        "calc must reject division by a dimensional value");
    require(aether_css_length_try_parse("calc(10px+2px)", &invalid) == AETHER_FALSE,
        "calc must enforce CSS whitespace around binary addition and subtraction");
    require(aether_css_length_try_parse("calc(2)", &invalid) == AETHER_FALSE,
        "a non-zero unitless calc result is not a CSS length");

    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK &&
            aether_doc_parse_html(doc, "<body><div id=\"grid\"><div id=\"card\"></div><div id=\"card-two\"></div></div></body>") == AETHER_OK,
        "calc integration document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{400.f, 300.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "calc integration runtime creation failed");
    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(runtime,
            "body { display: block; } #grid { display: flex; flex-wrap: wrap; gap: 16px; } "
            "#card, #card-two { display: block; width: calc(50% - 8px); box-sizing: border-box; }",
            0, "calc.css", &sheet, &rule_count) == AETHER_OK,
        "calc integration stylesheet compilation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    const auto card = aether_doc_get_element_by_id(doc, "card");
    bool found_card = false;
    for (int32_t index = 0; index < update_count; ++index) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[index], &node, &handle, nullptr) == AETHER_OK,
            "calc integration style update access failed");
        if (node == card) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK,
                "calc integration computed-style copy failed");
            require(style.width.unit == AETHER_CSS_UNIT_LINEAR && same(style.width.value, -8.f) &&
                    same(style.width.linear_percent, 50.f),
                "a length-valued property must retain the parsed calc expression in computed style");
            aether_computed_style_free(&style);
            found_card = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    require(found_card, "calc integration card did not receive a computed-style update");
    aether_style_updates_free(updates, update_count);

    aether_style_map_t styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
        "calc layout style-map creation failed");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK,
        "calc layout engine creation failed");
    aether_layout_result_t layout = nullptr;
    require(aether_layout_layout(engine, doc, styles, 400.f, 300.f, nullptr, nullptr, &layout) == AETHER_OK,
        "calc layout execution failed");
    const auto card_box = aether_layout_result_find_primary_box(layout, card);
    require(card_box != nullptr && same(aether_layout_box_get_border_box(card_box).width, 192.f),
        "a calc percentage flex-item width must resolve against the flex container's main size during layout");
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
    return 0;
}
