// Public-ABI regression coverage for the transition property pipeline.
// Expectations preserve the characterized engine behavior, not browser CSS conformance.
#include <aether/aether.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
const char* current_case = "setup";
void require(bool condition, const char* message) {
    // Deliberately not assert(): these checks must run in Release / NDEBUG.
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", current_case, message);
        std::exit(1);
    }
}
void near(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::fabs(actual - expected) < 0.000001f, message);
}
struct transition_s {
    int properties;
    float duration;
    float delay;
    int timing;
};
constexpr int opacity = AETHER_TRANSITION_PROPERTY_OPACITY;
constexpr int transform = AETHER_TRANSITION_PROPERTY_TRANSFORM;
constexpr int all = opacity | transform;
constexpr int linear = AETHER_TRANSITION_TIMING_LINEAR;
constexpr int ease = AETHER_TRANSITION_TIMING_EASE;
constexpr int ease_in = AETHER_TRANSITION_TIMING_EASE_IN;
constexpr int ease_out = AETHER_TRANSITION_TIMING_EASE_OUT;
constexpr transition_s previous{opacity, .7f, .2f, ease_in};
constexpr transition_s initial{0, 0, 0, linear};

struct fixture_s {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t root = nullptr;
    aether_node_t box = nullptr;
    aether_computed_style_t style{};
    bool have_style = false;
    bool box_updated = false;
    uint32_t impact = 0;

    fixture_s(const std::string& declaration, bool in_line) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc,
            "<html><body><div id='parent'><div id='box'></div></div></body></html>")
            == AETHER_OK, "parse document");
        root = aether_doc_get_document_element(doc);
        box = aether_doc_get_element_by_id(doc, "box");
        require(root && box, "find nodes");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime)
            == AETHER_OK, "create runtime");
        // A real non-null parent with a distinct transition is essential to the
        // inherit regression. Every declaration also overrides a non-default base.
        const std::string css = "#parent { transition: transform 0.8s ease; } "
            "#box { transition: opacity 0.7s ease-in 0.2s; "
            + (in_line ? std::string() : declaration) + "}";
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2,
            "transition-pipeline.css", &sheet, nullptr) == AETHER_OK, "add stylesheet");
        if (in_line) require(aether_element_set_attribute(box, "style", declaration.c_str())
            == AETHER_OK, "set inline declaration");
        require(resolve() > 0 && box_updated, "initial box update");
    }
    fixture_s(const fixture_s&) = delete;
    fixture_s& operator=(const fixture_s&) = delete;
    ~fixture_s() {
        if (have_style) aether_computed_style_free(&style);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    int resolve() {
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime, root, &updates);
        require(count >= 0, "resolve subtree");
        box_updated = false;
        impact = 0;
        for (int i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            aether_computed_style_handle_t handle = nullptr;
            uint32_t change = 0;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, &change)
                == AETHER_OK, "get public update");
            if (node == box) {
                box_updated = true;
                impact |= change;
                if (have_style) aether_computed_style_free(&style);
                require(aether_computed_style_copy(handle, &style) == AETHER_OK, "copy public style");
                have_style = true;
            }
            aether_computed_style_handle_destroy(handle);
        }
        aether_style_updates_free(updates, count);
        require(have_style, "retain last computed style when there is no update");
        return count;
    }
    void expect(transition_s value) const {
        if (style.transition_properties != value.properties)
            std::fprintf(stderr, "actual mask %d, expected %d\n", style.transition_properties, value.properties);
        require(style.transition_properties == value.properties, "transition property mask");
        near(style.transition_duration, value.duration, "transition duration");
        near(style.transition_delay, value.delay, "transition delay");
        require(style.transition_timing == value.timing, "transition timing");
    }
    void unchanged() {
        require(resolve() == 0 && !box_updated && impact == 0, "repeat resolution must emit no updates");
    }
    void mutate_variables(bool expect_change = true) {
        require(aether_element_set_attribute(root, "style",
            "--transition: transform 250ms ease-out 25ms; --time: 250ms; "
            "--delay: 25ms; --prop: transform; --timing: ease-out;")
            == AETHER_OK, "mutate inherited custom properties");
        const int count = resolve();
        if (!expect_change) {
            require(count == 0 && !box_updated && impact == 0, "ignored inline shorthand stays unchanged");
            return;
        }
        require(count == 1 && box_updated, "variable mutation updates only box");
        require(impact == AETHER_STYLE_IMPACT_PAINT,
            "transition mutation must be PAINT only, without LAYOUT/HITTEST/COMPOSITE");
    }
};
void check(const std::string& declaration, transition_s expected, bool in_line) {
    current_case = declaration.c_str();
    fixture_s f(declaration, in_line);
    f.expect(expected);
    f.unchanged();
    f.expect(expected);
}
void check_variable(const std::string& declaration, transition_s before,
                    transition_s after, bool in_line) {
    current_case = declaration.c_str();
    // Inline deferred shorthands are ignored by the characterized baseline;
    // deferred longhands do resolve. Keep this distinction explicit.
    const bool ignored = in_line && declaration.find("transition:") != std::string::npos;
    if (ignored) {
        before = previous;
        if (declaration.find("transition-duration: 50ms") != std::string::npos) before.duration = .05f;
        after = before;
    }
    fixture_s f(declaration, in_line);
    f.expect(before);
    f.unchanged();
    f.mutate_variables(!ignored);
    f.expect(after);
    f.unchanged();
    f.expect(after);
}
float pixels(aether_css_length length) {
    return aether_css_length_resolve(length, 100, 16, 16, 800, 600);
}
void edges(aether_css_edges value, float top, float right, float bottom, float left) {
    near(pixels(value.top), top, "top edge");
    near(pixels(value.right), right, "right edge");
    near(pixels(value.bottom), bottom, "bottom edge");
    near(pixels(value.left), left, "left edge");
}
void cross_family(bool in_line) {
    current_case = "shared whitespace tokenizer: numeric public results";
    fixture_s f("margin: 1px 2px 3px 4px; padding: calc(2px + 3px) 5px; "
        "inset: 6px 7px; border: 1px solid rgb(1, 2, 3); "
        "flex: 1 2 calc(10px + 2px); gap: 8px 9px; overflow: hidden auto;", in_line);
    edges(f.style.margin, 1, 2, 3, 4);
    edges(f.style.padding, 5, 5, 5, 5);
    edges(f.style.inset, 6, 7, 6, 7);
    edges(f.style.border_width, 1, 1, 1, 1);
    require(f.style.border_styles.top == AETHER_BORDER_STYLE_SOLID, "border style");
    near(f.style.border_color.r, 1.f / 255.f, "border red");
    near(f.style.border_color.g, 2.f / 255.f, "border green");
    near(f.style.border_color.b, 3.f / 255.f, "border blue");
    near(f.style.flex_grow, 1, "flex grow");
    near(f.style.flex_shrink, 2, "flex shrink");
    require(f.style.flex_basis.kind == AETHER_FLEX_BASIS_LENGTH, "flex basis kind");
    near(pixels(f.style.flex_basis.length), 12, "flex calc basis");
    // The current ABI has one gap scalar; the last column-gap application wins.
    near(f.style.gap, 9, "two-value gap");
    require(f.style.overflow_x == AETHER_OVERFLOW_HIDDEN, "overflow x");
    require(f.style.overflow_y == AETHER_OVERFLOW_AUTO, "overflow y");
    f.expect(previous);
    f.unchanged();
}
void nonfinite(bool in_line) {
    // These are legacy strtof inputs, not a claim that CSS permits them. NaN
    // compares unequal to itself in the current diff path and repeats PAINT;
    // positive infinity is stable. Never use an epsilon comparison for either.
    for (const char* property : {"transition", "transition-duration", "transition-delay"}) {
        const bool shorthand = std::string(property) == "transition";
        const bool delay = std::string(property) == "transition-delay";
        for (const char* token : {"nans", "infs", "1e40s"}) {
            const std::string declaration = std::string(property) + ": " +
                (shorthand ? "all " : "") + token + ";";
            current_case = declaration.c_str();
            fixture_s f(declaration, in_line);
            const bool nan = token[0] == 'n';
            for (int phase = 0; phase < 3; ++phase) {
                if (phase) {
                    const int count = f.resolve();
                    require(count == (nan ? 1 : 0) && f.box_updated == nan,
                        "nonfinite baseline repeat update behavior");
                    require(f.impact == (nan ? AETHER_STYLE_IMPACT_PAINT : 0),
                        "nonfinite baseline repeat impact");
                }
                const float value = delay ? f.style.transition_delay : f.style.transition_duration;
                require(nan ? std::isnan(value) : (std::isinf(value) && value > 0),
                    "nonfinite computed classification");
                require(f.style.transition_properties == (shorthand ? all : opacity), "nonfinite mask");
                require(f.style.transition_timing == (shorthand ? linear : ease_in), "nonfinite timing");
                if (delay) near(f.style.transition_duration, .7f, "nonfinite leaves duration alone");
                else near(f.style.transition_delay, shorthand ? 0 : .2f, "nonfinite leaves delay alone");
            }
        }
    }
}
} // namespace

int main() {
    require(std::setlocale(LC_ALL, "C") != nullptr, "C locale");
    for (bool in_line : {false, true}) {
        check("transition-property: transform;", {transform, .7f, .2f, ease_in}, in_line);
        check("transition: ;", previous, in_line);
        check("transition-duration: 1 s;", {opacity, 1, .2f, ease_in}, in_line);
        check("transition-duration: 0x1p2s;", {opacity, 4, .2f, ease_in}, in_line);
        check("transition-duration: 150ms;", {opacity, .15f, .2f, ease_in}, in_line);
        check("transition-delay: 50ms;", {opacity, .7f, .05f, ease_in}, in_line);
        check("transition-timing-function: ease-out;", {opacity, .7f, .2f, ease_out}, in_line);
        check("transition: all 0.3s ease;", {all, .3f, 0, ease}, in_line);
        check("transition: ease 50ms transform 20ms;", {transform, .05f, .02f, ease}, in_line);
        check("transition: 0.3s;", {0, .3f, 0, linear}, in_line);
        check("transition: none;", initial, in_line);
        check("transition: linear;", initial, in_line);
        check("transition-property: width;", {0, .7f, .2f, ease_in}, in_line);
        check("transition-property: opacity, transform;", {0, .7f, .2f, ease_in}, in_line);
        for (const char* invalid : {"width 1s", "opacity 1s, transform 2s", "all -1s",
            "all 1s -1s", "all 1s 2s 3s", "opacity transform 1s", "ease linear 1s",
            "all calc(1s + 2s)", "revert-layer"})
            check(std::string("transition: ") + invalid + ";", previous, in_line);
        check("transition-duration: -1s; transition-delay: -1ms; "
            "transition-timing-function: steps(2);", previous, in_line);
        // inherit with a real parent currently resets this non-inherited family.
        // Preserve that engine quirk rather than asserting browser semantics.
        for (const char* global : {"initial", "inherit", "unset", "revert"}) {
            check(std::string("transition: ") + global + ";", initial, in_line);
            check(std::string("transition-duration: ") + global + ";",
                {opacity, 0, .2f, ease_in}, in_line);
            check(std::string("transition-property: ") + global + ";",
                {0, .7f, .2f, ease_in}, in_line);
            check(std::string("transition-delay: ") + global + ";",
                {opacity, .7f, 0, ease_in}, in_line);
            check(std::string("transition-timing-function: ") + global + ";",
                {opacity, .7f, .2f, linear}, in_line);
        }
        check("transition: all 300ms ease; transition-duration: 50ms;", {all, .05f, 0, ease}, in_line);
        check("transition-duration: 50ms; transition: all 300ms ease;", {all, .3f, 0, ease}, in_line);
        check("transition-duration: 50ms !important; transition: all 300ms ease;", {all, .05f, 0, ease}, in_line);
        check("transition: all 300ms ease !important; transition-duration: 50ms;", {all, .3f, 0, ease}, in_line);
        check_variable("transition: var(--transition, all 300ms ease);",
            {all, .3f, 0, ease}, {transform, .25f, .025f, ease_out}, in_line);
        check_variable("transition-property: var(--prop, opacity); transition-duration: var(--time, 300ms); "
            "transition-delay: var(--delay, 50ms); transition-timing-function: var(--timing, ease);",
            {opacity, .3f, .05f, ease}, {transform, .25f, .025f, ease_out}, in_line);
        check_variable("transition-duration: 50ms !important; transition: var(--transition, all 300ms ease);",
            {all, .05f, 0, ease}, {transform, .05f, .025f, ease_out}, in_line);
        check_variable("transition: var(--transition, all 300ms ease); transition-duration: 50ms;",
            {all, .05f, 0, ease}, {transform, .05f, .025f, ease_out}, in_line);
        check_variable("transition: all var(--time, 300ms) ease;",
            {all, .3f, 0, ease}, {all, .25f, 0, ease}, in_line);
        cross_family(in_line);
        nonfinite(in_line);
    }
    std::puts("transition property public pipeline: PASS");
}
