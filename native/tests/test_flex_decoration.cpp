#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;
static int checks = 0;
static int layouts = 0;

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

// Collect geometric failures so the base implementation exercises both axes and
// every relayout, rather than stopping at the first incorrect shrink result.
static void expect(float actual, float expected, const char* scenario, const char* field) {
    ++checks;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > 0.001f) {
        std::fprintf(stderr, "FAIL: %s: %s = %g, expected %g (finite)\n",
                     scenario, field, actual, expected);
        ++failures;
    }
}

static aether_text_metrics_t measure_empty(const char*, const aether_computed_style_t*, float, void*) {
    return {0.f, 0.f, 0.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_style_update_t* updates = nullptr;
    int32_t update_count = 0;

    Fixture(const char* html, const char* css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0;
        int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "flex-decoration.css", &sheet, &rules)
                    == AETHER_OK, "add stylesheet");
        update_count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(update_count > 0, "resolve styles through runtime");
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK,
                "create resolved style map");
        require(aether_layout_create(measure_empty, nullptr, &engine) == AETHER_OK, "create layout engine");
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_updates_free(updates, update_count);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    aether_node_t node(const char* id) const {
        const auto element = aether_doc_get_element_by_id(doc, id);
        require(element != nullptr, "find fixture element");
        return element;
    }

    aether_layout_result_t layout(float width, float height) const {
        aether_layout_result_t result = nullptr;
        require(aether_layout_layout(engine, doc, styles, width, height, nullptr, nullptr, &result)
                    == AETHER_OK && result != nullptr, "perform layout");
        return result;
    }

    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        const auto box = aether_layout_result_find_primary_box(result, node(id));
        require(box != nullptr, "find primary layout box");
        return aether_layout_box_get_border_box(box);
    }
};

struct Axis {
    bool row;
    const char* main() const { return row ? "width" : "height"; }
    const char* cross() const { return row ? "height" : "width"; }
    const char* start() const { return row ? "left" : "top"; }
    const char* end() const { return row ? "right" : "bottom"; }
    float size(aether_rectf r) const { return row ? r.width : r.height; }
    float pos(aether_rectf r) const { return row ? r.x : r.y; }
    float cross_pos(aether_rectf r) const { return row ? r.y : r.x; }
    std::string decoration(const char* padding = "40px") const {
        return std::string("padding-") + start() + ":" + padding + ";padding-" + end() + ":" + padding +
            ";border-" + start() + ":10px solid black;border-" + end() + ":10px solid black;";
    }
    std::string css(const std::string& extra = "") const {
        return std::string("body {margin:0;display:flex;width:100%;height:100%;box-sizing:border-box;") +
            "align-items:flex-start;align-content:flex-start;flex-direction:" + (row ? "row" : "column") +
            ";} body>div {box-sizing:border-box;flex-grow:0;flex-shrink:1;flex-basis:200px;min-" + main() +
            ":0;" + cross() + ":20px;} #a {" + decoration() + "}" + extra;
    }
};

static constexpr const char* html =
    "<body id='container'><div id='a'></div><div id='b'></div></body>";

static void check(const Fixture& f, Axis axis, float available, float a_size, float b_size,
                  const char* label, float a_pos = 0, float b_pos = NAN,
                  float b_cross = 0, float viewport_cross = 400) {
    ++layouts;
    std::string scenario = std::string(axis.row ? "row " : "column ") + label;
    const auto result = f.layout(axis.row ? available : viewport_cross, axis.row ? viewport_cross : available);
    const auto c = f.rect(result, "container");
    const auto a = f.rect(result, "a");
    const auto b = f.rect(result, "b");
    expect(axis.size(c), available, scenario.c_str(), "container main size");
    expect(axis.size(a), a_size, scenario.c_str(), "A border main size");
    expect(axis.size(b), b_size, scenario.c_str(), "B border main size");
    expect(axis.pos(a) - axis.pos(c), a_pos, scenario.c_str(), "A main offset");
    expect(axis.pos(b) - axis.pos(c), std::isnan(b_pos) ? a_pos + a_size : b_pos,
           scenario.c_str(), "B main offset");
    expect(axis.cross_pos(a) - axis.cross_pos(c), 0, scenario.c_str(), "A cross offset");
    expect(axis.cross_pos(b) - axis.cross_pos(c), b_cross, scenario.c_str(), "B cross offset");
    aether_layout_result_destroy(result);
}

static void weighted_shrink(Axis axis) {
    Fixture border(html, axis.css().c_str());
    check(border, axis, 300, 500.f / 3, 400.f / 3, "border-box inner shrink weights");
    // Equivalent border bases: A content 100 + decoration 100, B content 200.
    Fixture content(html, axis.css("body>div {box-sizing:content-box;} #a {flex-basis:100px;}").c_str());
    check(content, axis, 300, 500.f / 3, 400.f / 3, "content-box inner shrink weights");
    Fixture fractional(html, axis.css("body>div {flex-shrink:.25;}").c_str());
    check(fractional, axis, 300, 550.f / 3, 500.f / 3, "fractional shrink partial budget");
    Fixture content_equal(html, axis.css("body>div {box-sizing:content-box;}").c_str());
    check(content_equal, axis, 300, 200, 100, "content-box equal content bases");
    Fixture content_fractional(html, axis.css("body>div {box-sizing:content-box;flex-shrink:.25;}").c_str());
    check(content_fractional, axis, 300, 250, 150, "content-box fractional equal content bases");
    // Auto basis must use the specified sizing box, too.
    const std::string auto_sizes = std::string("body>div {flex-basis:auto;") + axis.main() + ":200px;}";
    Fixture automatic(html, axis.css(auto_sizes).c_str());
    check(automatic, axis, 300, 500.f / 3, 400.f / 3, "auto basis specified border-box");
    Fixture auto_content(html, axis.css(auto_sizes + "body>div {box-sizing:content-box;} #a {" +
        axis.main() + ":100px;}").c_str());
    check(auto_content, axis, 300, 500.f / 3, 400.f / 3, "auto basis specified content-box");
}

static void check_retained_consumers(const Fixture& f, aether_layout_result_t retained,
                                     aether_rectf expected, const char* scenario) {
    expect(aether_layout_result_hit_test(retained, expected.x + expected.width / 2,
           expected.y + expected.height / 2) == f.node("b") ? 1.f : 0.f,
           1, scenario, "retained hit test selects B");
    aether_display_list_builder_t painter = nullptr;
    aether_display_list_t list = nullptr;
    require(aether_display_list_builder_create(measure_empty, nullptr, &painter) == AETHER_OK,
            "create retained painter");
    require(aether_display_list_build(painter, retained, nullptr, &list) == AETHER_OK,
            "build retained display list");
    int matched = 0;
    for (int i = 0; i < aether_display_list_get_count(list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read paint command");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& fill = command.data.fill_rounded_rect;
            if (fill.color.r > .99f && fill.color.g < .01f && fill.color.b < .01f) {
                ++matched;
                expect(fill.rect.x, expected.x, scenario, "retained paint B x");
                expect(fill.rect.y, expected.y, scenario, "retained paint B y");
                expect(fill.rect.width, expected.width, scenario, "retained paint B width");
                expect(fill.rect.height, expected.height, scenario, "retained paint B height");
            }
        }
        aether_paint_command_free(&command);
    }
    expect(static_cast<float>(matched), 1, scenario, "retained B background count");
    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(painter);
}

static void grow_floor_relayout(Axis axis) {
    // CSS Flexbox §§9.2, 9.7 retain a border-box base below decoration until
    // the hypothetical/target clamp steps. This is not a Chromium oracle.
    Fixture f(html, axis.css("body>div {flex-grow:1;} #a {flex-basis:0px;} #b {background:red;}").c_str());
    ++layouts;
    const auto retained = f.layout(axis.row ? 500 : 400, axis.row ? 400 : 500);
    const auto retained_a = f.rect(retained, "a");
    const auto retained_b = f.rect(retained, "b");
    expect(axis.size(retained_a), 150, "retained result", "initial A size");
    expect(axis.size(retained_b), 350, "retained result", "initial B size");
    const float available[] = {300, 350, 400, 500, 350, 300};
    const float a[] = {100, 100, 100, 150, 100, 100};
    const float b[] = {200, 250, 300, 350, 250, 200};
    for (int i = 0; i < 6; ++i) {
        char label[96];
        std::snprintf(label, sizeof(label), "grow floor relayout %d viewport %g", i, available[i]);
        check(f, axis, available[i], a[i], b[i], label);
        const auto old_a = f.rect(retained, "a");
        const auto old_b = f.rect(retained, "b");
        expect(axis.size(old_a), axis.size(retained_a), label, "retained A size unchanged");
        expect(axis.size(old_b), axis.size(retained_b), label, "retained B size unchanged");
        expect(axis.pos(old_a), axis.pos(retained_a), label, "retained A position unchanged");
        expect(axis.pos(old_b), axis.pos(retained_b), label, "retained B position unchanged");
        check_retained_consumers(f, retained, retained_b, label);
    }
    aether_layout_result_destroy(retained);
}

static void additional_boundaries(Axis axis) {
    Fixture automatic_min(html, axis.css(std::string("body>div {min-") + axis.main() + ":auto;}").c_str());
    check(automatic_min, axis, 300, 500.f / 3, 400.f / 3, "automatic minimum weighted shrink");
    check(automatic_min, axis, 50, 100, 0, "automatic minimum preserves decoration floor");
    Fixture hidden(html, axis.css(std::string("body>div {min-") + axis.main() + ":auto;overflow:hidden;}").c_str());
    check(hidden, axis, 300, 500.f / 3, 400.f / 3, "hidden overflow weighted shrink");
    check(hidden, axis, 50, 100, 0, "hidden overflow preserves decoration floor");
    Fixture zero_inner(html, axis.css("body>div {flex-shrink:.25;} #a {flex-basis:100px;}").c_str());
    check(zero_inner, axis, 200, 100, 150, "zero inner base still counts fractional factor");
    Fixture auto_zero(html, axis.css(std::string("body>div {flex-grow:1;} #a {flex-basis:auto;") +
        axis.main() + ":0px;}").c_str());
    check(auto_zero, axis, 300, 100, 200, "auto zero specified size grow gate 300");
    check(auto_zero, axis, 350, 100, 250, "auto zero specified size grow gate 350");
    check(auto_zero, axis, 400, 100, 300, "auto zero specified size grow gate 400");
    check(auto_zero, axis, 500, 150, 350, "auto zero specified size grow gate 500");
    Fixture padding(html, axis.css(std::string("#a {border:0;padding-") + axis.start() +
        ":50px;padding-" + axis.end() + ":50px;}").c_str());
    check(padding, axis, 300, 500.f / 3, 400.f / 3, "padding-only inner shrink weights");
    Fixture border(html, axis.css(std::string("#a {padding:0;border-") + axis.start() +
        ":50px solid black;border-" + axis.end() + ":50px solid black;}").c_str());
    check(border, axis, 300, 500.f / 3, 400.f / 3, "border-only inner shrink weights");
}

static void constraints(Axis axis) {
    Fixture minimum(html, axis.css(std::string("#a {min-") + axis.main() + ":180px;}").c_str());
    check(minimum, axis, 300, 180, 120, "minimum freezes and redistributes");
    Fixture maximum(html, axis.css(std::string("#a {max-") + axis.main() + ":150px;}").c_str());
    check(maximum, axis, 300, 150, 150, "maximum freezes and redistributes");
    Fixture below(html, axis.css(std::string("body>div {flex-grow:1;} #a {max-") + axis.main() + ":20px;}").c_str());
    check(below, axis, 500, 100, 400, "max below decoration keeps physical floor");
    Fixture content(html, axis.css(std::string("body>div {flex-grow:1;box-sizing:content-box;} #a {flex-basis:100px;max-") +
        axis.main() + ":30px;}").c_str());
    check(content, axis, 500, 130, 370, "content-box maximum adds decoration");
    Fixture content_min(html, axis.css(std::string("body>div {box-sizing:content-box;} #a {flex-basis:100px;min-") +
        axis.main() + ":80px;}").c_str());
    check(content_min, axis, 300, 180, 120, "content-box minimum adds decoration");
}

static void tiny_violation_redistribution(Axis axis) {
    Fixture f("<body id='container'><div id='a'></div><div id='b'></div><div id='c'></div></body>",
        axis.css("#a {flex-shrink:.75;} #b {flex-basis:300px;flex-shrink:.125;" +
                 axis.decoration() + "} #c {flex-basis:1500px;flex-shrink:10;" +
                 axis.decoration("440px") + "}").c_str());
    // A real floor violation, however small, must remove B's factor before
    // the next partial-budget iteration. At the exact boundary B stays active.
    for (float available : {600.f, std::nextafter(600.f, 0.f)}) {
        ++layouts;
        const float expected_b = available == 600.f ? 125.f : 100.f;
        const auto result = f.layout(axis.row ? available : 400, axis.row ? 400 : available);
        const auto container = f.rect(result, "container");
        const auto a = f.rect(result, "a");
        const auto b = f.rect(result, "b");
        const auto c = f.rect(result, "c");
        const char* scenario = axis.row ? "row subpixel floor redistribution" : "column subpixel floor redistribution";
        expect(axis.size(container), available, scenario, "available main size");
        expect(axis.size(a), 100, scenario, "A main size");
        expect(axis.size(b), expected_b, scenario, "B main size");
        expect(axis.size(c), 900, scenario, "C main size");
        expect(axis.pos(a) - axis.pos(container), 0, scenario, "A main offset");
        expect(axis.pos(b) - axis.pos(container), 100, scenario, "B main offset");
        expect(axis.pos(c) - axis.pos(container), 100 + expected_b, scenario, "C main offset");
        aether_layout_result_destroy(result);
    }
}

static void margins_and_gap(Axis axis) {
    Fixture signed_margin(html, axis.css(std::string("body {gap:20px;} #a {margin-") + axis.start() +
        ":-20px;margin-" + axis.end() + ":10px;}").c_str());
    check(signed_margin, axis, 300, 490.f / 3, 380.f / 3, "signed margins and gap", -20, 520.f / 3);
    Fixture auto_margin(html, axis.css(std::string("body {gap:20px;} body>div {flex-shrink:0;} #b {margin-") +
        axis.start() + ":auto;}").c_str());
    check(auto_margin, axis, 500, 200, 200, "auto margin absorbs positive remainder", 0, 300);
    check(auto_margin, axis, 350, 200, 200, "auto margin zero under overflow", 0, 220);
}

static void decorated_wrap(Axis axis) {
    // Give two 20px lines plus the 10px gap exactly 50px cross space; this
    // isolates line formation from unrelated align-content distribution.
    Fixture f(html, axis.css("body {flex-wrap:wrap;gap:10px;} #a {flex-basis:0px;}").c_str());
    check(f, axis, 250, 100, 200, "wrap uses decorated hypothetical not zero base", 0, 0, 30, 50);
    check(f, axis, 310, 100, 200, "exact decorated wrap boundary", 0, 110, 0, 50);
    check(f, axis, 250, 100, 200, "wrap relayout resets lines", 0, 0, 30, 50);
}

static void percentage_relayout(Axis axis) {
    Fixture f(html, axis.css("body>div {box-sizing:content-box;flex-basis:50%;} #a {" +
        axis.decoration("10%") + "}").c_str());
    // Percent basis uses the container main size. Both horizontal and vertical
    // percentage padding use its WIDTH, even when column main size is height.
    for (float available : {300.f, 400.f, 250.f, 300.f}) {
        const float decoration = (axis.row ? available : 400.f) * .2f + 20.f;
        char label[96];
        std::snprintf(label, sizeof(label), "percentage basis/padding viewport %g", available);
        check(f, axis, available, (available + decoration) / 2,
              (available - decoration) / 2, label);
    }
}

static void indefinite_percentage_column() {
    const Axis axis{false};
    // Empty content isolates percentage fallback from intrinsic block sizing:
    // the indefinite 50% basis must not resolve against viewport height 300.
    Fixture f(html, axis.css("body {height:auto;} body>div {flex-basis:50%;}").c_str());
    ++layouts;
    const auto result = f.layout(400, 300);
    const auto c = f.rect(result, "container");
    const auto a = f.rect(result, "a");
    const auto b = f.rect(result, "b");
    expect(c.height, 100, "indefinite column percentage", "content-sized container");
    expect(a.height, 100, "indefinite column percentage", "A content plus decoration");
    expect(b.height, 0, "indefinite column percentage", "B content fallback");
    expect(a.y - c.y, 0, "indefinite column percentage", "A main offset");
    expect(b.y - c.y, 100, "indefinite column percentage", "B main offset");
    aether_layout_result_destroy(result);
}

int main() {
    require(AETHER_ABI_VERSION_GET_MAJOR(aether_abi_version()) == AETHER_ABI_VERSION_MAJOR,
            "compatible ABI major version");
    const auto capabilities = AETHER_CAPABILITY_DOM | AETHER_CAPABILITY_STYLE_RUNTIME | AETHER_CAPABILITY_LAYOUT;
    require((aether_abi_capabilities() & capabilities) == capabilities, "required ABI capabilities");
    for (bool row : {true, false}) {
        const Axis axis{row};
        weighted_shrink(axis);
        grow_floor_relayout(axis);
        additional_boundaries(axis);
        constraints(axis);
        tiny_violation_redistribution(axis);
        margins_and_gap(axis);
        decorated_wrap(axis);
        percentage_relayout(axis);
    }
    indefinite_percentage_column();
    std::printf("flex decoration coverage: %d geometric layouts, %d numeric assertions\n", layouts, checks);
    if (failures) {
        std::fprintf(stderr, "%d flex decoration ABI checks failed\n", failures);
        return 1;
    }
    std::puts("flex decoration ABI tests passed");
    return 0;
}
