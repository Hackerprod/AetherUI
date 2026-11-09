#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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

static void check_dimensional_fixture(const char* declarations, float expected_width, float expected_line_height,
                                      bool exact_literal = false, bool padding_atomic = false,
                                      bool width_initial = false) {
    aether_doc_t doc = nullptr;
    require(aether_doc_create(&doc) == AETHER_OK &&
        aether_doc_parse_html(doc, "<!doctype html><html><body><div id='target'></div></body></html>") == AETHER_OK,
        "dimensional fixture document");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{800.f, 600.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
        "dimensional fixture runtime");
    const std::string css = std::string("html,body{display:block;margin:0;padding:0;border:0;}") +
        (width_initial ? "body{width:211px;line-height:19px;}" : "") +
        "#target{display:block;width:73px;height:10px;font-size:16px;line-height:7px;}#target{" +
        std::string(declarations) + "}";
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "dimensional.css", &sheet, &rules) == AETHER_OK,
        "dimensional fixture stylesheet");
    aether_style_update_t* updates = nullptr;
    const auto count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(doc), &updates);
    require(count >= 0, "dimensional fixture resolve");
    const auto target = aether_doc_get_element_by_id(doc, "target");
    require(target != nullptr, "dimensional target exists");
    bool found = false;
    for (int32_t i = 0; i < count; ++i) {
        aether_node_t node = nullptr;
        aether_computed_style_handle_t handle = nullptr;
        require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "dimensional get_update");
        if (node == target) {
            aether_computed_style_t style{};
            require(aether_computed_style_copy(handle, &style) == AETHER_OK, "dimensional computed style");
            require(same(style.line_height, expected_line_height) && same(style.font_size, 16.f), declarations);
            if (exact_literal) {
                require(style.line_height == expected_line_height, "literal line-height exact fallback/control");
                require(style.width.unit == (width_initial ? AETHER_CSS_UNIT_AUTO : AETHER_CSS_UNIT_PX) &&
                        style.width.value == (width_initial ? 0.f : expected_width),
                    "literal width exact unit and fallback/control");
            }
            if (padding_atomic) {
                require(style.padding.top.unit == AETHER_CSS_UNIT_PX && style.padding.top.value == 11.f &&
                        style.padding.right.unit == AETHER_CSS_UNIT_PX && style.padding.right.value == 13.f &&
                        style.padding.bottom.unit == AETHER_CSS_UNIT_PX && style.padding.bottom.value == 17.f &&
                        style.padding.left.unit == AETHER_CSS_UNIT_PX && style.padding.left.value == 19.f,
                    "invalid second shorthand component preserves all four previous sides");
            }
            aether_computed_style_free(&style);
            found = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    aether_style_updates_free(updates, count);
    require(found, "dimensional target update");
    aether_style_map_t map = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK, "dimensional style map");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK, "dimensional layout engine");
    aether_layout_result_t layout = nullptr;
    require(aether_layout_layout(engine, doc, map, 800.f, 600.f, nullptr, nullptr, &layout) == AETHER_OK, "dimensional layout");
    const auto box = aether_layout_result_find_primary_box(layout, target);
    require(box && same(aether_layout_box_get_border_box(box).width, expected_width), declarations);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(map);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}

static void check_dimensional_contract() {
    struct width_case { const char* css; bool valid; float width; };
    const width_case cases[] = {
        {"10px", true, 10.f}, {"0", true, 0.f},
        {"calc(10px + 0px)", true, 10.f}, {"calc(0px + 10px)", true, 10.f},
        {"calc(10px * 0)", true, 0.f}, {"calc(0 * 10px)", true, 0.f},
        {"calc(10px / 2)", true, 5.f}, {"calc((1 - 1) * 10px)", true, 0.f},
        {"calc(10px + 0)", false, 73.f}, {"calc(0 + 10px)", false, 73.f},
        {"calc(10px - 0)", false, 73.f}, {"calc(0 - 10px)", false, 73.f},
        {"calc(0)", false, 73.f}, {"calc(1 - 1)", false, 73.f},
        {"calc(10px + (2 - 2))", false, 73.f}, {"calc(10px + 1)", false, 73.f}
    };
    for (const auto& c : cases) {
        aether_css_length value = aether_css_length_zero();
        require((aether_css_length_try_parse(c.css, &value) == AETHER_TRUE) == c.valid, c.css);
        if (!c.valid) require(value.unit == AETHER_CSS_UNIT_AUTO, "failed calc output must be AUTO");
        check_dimensional_fixture((std::string("width:") + c.css + ";").c_str(), c.width, 7.f);
    }
    for (const auto css : {"calc(0)", "calc(1 - 1)", "calc(0px)"})
        check_dimensional_fixture((std::string("line-height:") + css + ";").c_str(), 73.f, 0.f);

    // Policy guards are separate from the measured 16-width/3-line-height set.
    require(same(aether_css_length_resolve(parse("calc(10px - 10px)"), 800.f, 16.f, 16.f, 800.f, 600.f), 0.f),
        "a zero-valued LENGTH keeps its type");
    check_dimensional_fixture("width:calc(10px - 10px);", 0.f, 7.f);
    check_dimensional_fixture("line-height:calc(10px + 0);", 73.f, 7.f);
    // CSS line-height accepts a computed <number>; calc(2) is Number(2),
    // not an invalid length. Its descendant-relative use is covered in
    // test_measure_computed alongside literal Number(2).
    check_dimensional_fixture("line-height:calc(2);", 73.f, 2.f);
    check_dimensional_fixture("--lh:calc(1 - 1);line-height:var(--lh);", 73.f, 0.f);
}

static void check_literal_length_ranges() {
    for (const char* token : {"-1px", "-1e-46px", "-1e-46%", "-1e-46em",
                            "-1e-46rem", "-1e-46vw", "-1e-46vh", "-1e-46"}) {
        const std::string css = std::string("width:") + token + ";line-height:" + token + ";";
        check_dimensional_fixture(css.c_str(), 73.f, 7.f, true);
    }
    for (const char* token : {"0px", "-0px", "+0px", "1e-46px", "-0", "0"}) {
        const std::string css = std::string("width:") + token + ";line-height:" + token + ";";
        check_dimensional_fixture(css.c_str(), 0.f, 0.f, true);
    }
    check_dimensional_fixture("width:2px;line-height:2px;", 2.f, 2.f, true);
    check_dimensional_fixture("width:1e-46;line-height:1e-46;", 73.f, 0.f, true);
    check_dimensional_fixture("width:2;line-height:2;", 73.f, 2.f, true);
    check_dimensional_fixture("padding:11px 13px 17px 19px;padding:2px -1e-46px;",
                              105.f, 7.f, false, true);
    // A defined invalid variable wins first, then becomes unset. Width uses
    // initial auto, and inherited line-height comes from the explicit parent (19px),
    // rather than reviving the earlier target (73px/7px) or var's fallback.
    check_dimensional_fixture("--bad:-1e-46px;width:var(--bad,31px);line-height:var(--bad,5px);",
                              211.f, 19.f, true, false, true);
    for (const char* token : {"-0px", "1e-46px"}) {
        const std::string css = std::string("--ok:") + token + ";width:var(--ok);line-height:var(--ok);";
        check_dimensional_fixture(css.c_str(), 0.f, 0.f, true);
    }
    check_dimensional_fixture("width:var(--missing,2px);line-height:var(--missing,3px);", 2.f, 3.f, true);
    const auto negative_tiny = parse("-1e-46px");
    require(negative_tiny.unit == AETHER_CSS_UNIT_PX && negative_tiny.value == 0.f,
            "generic length parser retains negative tiny quantization");
}

static void check_numeric_scanner() {
    for (const auto source : {"1PX", "1pX", "1Em", "1REM", "1vW", "1VH", "1%",
             "+1px", "-1px", ".5px", "1e2px", "1E+2PX", "1e-2px", "0e-400"})
        parse(source);
    for (const auto source : {"calc(+1PX)", "calc(-1pX)", "calc(2 * -1PX)",
             "calc(1Em + 1REM + 1vW + 1VH)", "calc(50% - 50%)", "calc(0 * 50%)"})
        parse(source);
    for (const auto source : {"0x10px", "0x1p3px", "1.px", "1.", "1 px", "1e+px",
             "--1px", "+ +1px", "- -1px", "1e400", "1e-400", "1e400px", "1e40px",
             "calc(--1px)", "calc(+ +1px)", "calc(- -1px)", "calc(-(1px))",
             "calc(0x10px)", "calc(0x1p3px)", "calc(1.px)", "calc(1 px)",
             "calc(1e-400 * 1px)", "calc((1e-300 * 1e-300) * 1px)",
             "calc((1e-300 / 1e300) * 1px)", "calc((1e300 * 1e300) * 1px)",
             "calc(1e30px * 1e30)", "calc(1px / 1e-300)"}) {
        aether_css_length invalid{};
        require(aether_css_length_try_parse(source, &invalid) == AETHER_FALSE, source);
        require(invalid.unit == AETHER_CSS_UNIT_AUTO, "invalid numeric output remains AUTO");
    }
    require(parse("100").unit == AETHER_CSS_UNIT_PX && parse("100").value == 100.f,
        "ordinary wrapper preserves literal Number-to-px coercion");
    require(parse("1e-46").value == 0.f, "ordinary lengths retain float quantization");
    require(parse("16777217px").value == 16777216.f, "length literals retain float quantization");
    require(parse("calc((16777217 - 16777216) * 1px)").value == 1.f,
        "Number subtraction retains double precision before length construction");
    require(same(parse("calc((1e-30 * 1e-30) * 1e30 * 1e30 * 1px)").value, 1.f),
        "small nonzero Number products are not prematurely rounded to float zero");
    require(same(parse("calc(1e-46 * 1e30 * 1e16 * 1px)").value, 1.f),
        "small Number literals retain double precision");
    require(same(parse("calc(1e-30px / 1e-40)").value, 1e10f),
        "division does not overflow a prematurely narrowed reciprocal");
    require(parse("calc((1 - 1) * 1px)").value == 0.f,
        "exact Number cancellation is valid zero");
    require(parse("calc((0 * 1e-300) * 1px)").value == 0.f,
        "zero multiplication is not underflow");
}

int main() {
    check_numeric_scanner();
    check_dimensional_contract();
    check_literal_length_ranges();
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
