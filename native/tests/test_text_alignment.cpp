#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static bool nearly_equal(float actual, float expected) {
    return std::fabs(actual - expected) < 0.001f;
}

static aether_text_metrics_t measure_monospace(const char* text,
                                               const aether_computed_style_t*,
                                               float, void*) {
    const float width = text ? static_cast<float>(std::strlen(text)) : 0.f;
    return {width, 1.f, 0.8f};
}

struct layout_fixture_s {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t result = nullptr;

    layout_fixture_s() = default;
    layout_fixture_s(const layout_fixture_s&) = delete;
    layout_fixture_s& operator=(const layout_fixture_s&) = delete;
    layout_fixture_s(layout_fixture_s&& other) noexcept
        : doc(other.doc), runtime(other.runtime), styles(other.styles),
          engine(other.engine), result(other.result) {
        other.doc = nullptr;
        other.runtime = nullptr;
        other.styles = nullptr;
        other.engine = nullptr;
        other.result = nullptr;
    }

    ~layout_fixture_s() {
        if (result) aether_layout_result_destroy(result);
        if (engine) aether_layout_destroy(engine);
        if (styles) aether_style_map_destroy(styles);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
};

static layout_fixture_s layout_fixture(const char* markup) {
    layout_fixture_s fixture;
    require(aether_doc_create(&fixture.doc) == AETHER_OK, "document creation failed");
    require(aether_doc_parse_html(fixture.doc, markup) == AETHER_OK, "fixture parsing failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{100.f, 100.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &fixture.runtime) == AETHER_OK,
        "style runtime creation failed");
    int64_t stylesheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(fixture.runtime,
        "body, #paragraph { display: block; } "
        "#paragraph { width: 10px; font-size: 1px; line-height: 1; text-align: justify; }",
        0, "text-alignment.css", &stylesheet, &rule_count) == AETHER_OK,
        "stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const int32_t update_count = aether_style_runtime_resolve_subtree(
        fixture.runtime, aether_doc_get_body(fixture.doc), &updates);
    require(update_count > 0, "style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(fixture.runtime, fixture.doc, &fixture.styles) == AETHER_OK,
        "style-map creation failed");
    require(aether_layout_create(measure_monospace, nullptr, &fixture.engine) == AETHER_OK,
        "layout engine creation failed");
    require(aether_layout_layout(fixture.engine, fixture.doc, fixture.styles, 100.f, 100.f,
        nullptr, nullptr, &fixture.result) == AETHER_OK,
        "layout execution failed");
    return fixture;
}

static std::string text_of(aether_layout_box_t box) {
    char* text = aether_layout_box_get_text(box);
    std::string copy = text ? text : "";
    aether_string_free(text);
    return copy;
}

static aether_layout_box_t paragraph_box(layout_fixture_s& fixture) {
    auto* element = aether_doc_get_element_by_id(fixture.doc, "paragraph");
    require(element != nullptr, "paragraph lookup failed");
    auto* box = aether_layout_result_find_primary_box(fixture.result, element);
    require(box != nullptr, "paragraph layout box missing");
    return box;
}

static void expect_fragment(aether_layout_box_t paragraph, int index, const char* text,
                            float x, float y, float width) {
    auto* fragment = aether_layout_box_get_child(paragraph, index);
    require(fragment && text_of(fragment) == text, "explicit fragment text/order");
    const auto host = aether_layout_box_get_border_box(paragraph);
    const auto actual = aether_layout_box_get_border_box(fragment);
    require(nearly_equal(actual.x - host.x, x) && nearly_equal(actual.y - host.y, y) &&
            nearly_equal(actual.width, width), "explicit fragment line-edge geometry");
}

int main() {
    {
        auto fixture = layout_fixture("<body><div id=\"paragraph\">aa bb cc dd ee</div></body>");
        auto* paragraph = paragraph_box(fixture);
        require(aether_layout_box_get_child_count(paragraph) == 8,
            "wrapped paragraph retains interior gaps but discards the soft-break separator");
        auto* first_gap = aether_layout_box_get_child(paragraph, 1);
        auto* second_gap = aether_layout_box_get_child(paragraph, 3);
        auto* final_gap = aether_layout_box_get_child(paragraph, 6);
        require(text_of(first_gap) == " " && text_of(second_gap) == " ",
            "interior gaps must remain individual layout fragments");
        require(nearly_equal(aether_layout_box_get_width(first_gap), 2.f) &&
                nearly_equal(aether_layout_box_get_width(second_gap), 2.f),
            "only interior gaps of a non-final line must receive distributed width");
        // Six glyph units and two expanded gaps fill width 10. The separator
        // before dd is discarded, not stored as a ninth trailing fragment.
        expect_fragment(paragraph, 0, "aa", 0, 0, 2);
        expect_fragment(paragraph, 1, " ", 2, 0, 2);
        expect_fragment(paragraph, 2, "bb", 4, 0, 2);
        expect_fragment(paragraph, 3, " ", 6, 0, 2);
        expect_fragment(paragraph, 4, "cc", 8, 0, 2);
        expect_fragment(paragraph, 5, "dd", 0, 1, 2);
        expect_fragment(paragraph, 6, " ", 2, 1, 1);
        expect_fragment(paragraph, 7, "ee", 3, 1, 2);
        require(nearly_equal(aether_layout_box_get_width(final_gap), 1.f),
            "the final line must remain start aligned");
    }

    {
        auto fixture = layout_fixture("<body><div id=\"paragraph\">aa bb cc<br>dd ee ff gg</div></body>");
        auto* paragraph = paragraph_box(fixture);
        require(aether_layout_box_get_child_count(paragraph) == 11,
            "forced-break paragraph discards only the automatic-break separator");
        auto* forced_break_gap = aether_layout_box_get_child(paragraph, 1);
        auto* second_line_gap = aether_layout_box_get_child(paragraph, 6);
        require(nearly_equal(aether_layout_box_get_width(forced_break_gap), 1.f),
            "a line before br must not justify by default");
        require(nearly_equal(aether_layout_box_get_width(second_line_gap), 2.f),
            "a non-terminal automatic line after br must still justify");
        expect_fragment(paragraph, 0, "aa", 0, 0, 2);
        expect_fragment(paragraph, 1, " ", 2, 0, 1);
        expect_fragment(paragraph, 2, "bb", 3, 0, 2);
        expect_fragment(paragraph, 3, " ", 5, 0, 1);
        expect_fragment(paragraph, 4, "cc", 6, 0, 2);
        expect_fragment(paragraph, 5, "dd", 0, 1, 2);
        expect_fragment(paragraph, 6, " ", 2, 1, 2);
        expect_fragment(paragraph, 7, "ee", 4, 1, 2);
        expect_fragment(paragraph, 8, " ", 6, 1, 2);
        expect_fragment(paragraph, 9, "ff", 8, 1, 2);
        expect_fragment(paragraph, 10, "gg", 0, 2, 2);
    }

    {
        auto fixture = layout_fixture("<body><div id=\"paragraph\">aa bb</div></body>");
        auto* paragraph = paragraph_box(fixture);
        auto* only_gap = aether_layout_box_get_child(paragraph, 1);
        require(nearly_equal(aether_layout_box_get_width(only_gap), 1.f),
            "a single final line must not justify");
    }

    return 0;
}
