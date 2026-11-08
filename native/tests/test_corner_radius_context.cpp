#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL corner context: %s\n", message); std::exit(1); }
}
bool near(float a, float b) { return std::fabs(a - b) < .001f; }
struct Style {
    aether_computed_style_t value{};
    explicit Style(aether_computed_style_handle_t handle) {
        require(aether_computed_style_copy(handle, &value) == AETHER_OK, "copy style");
    }
    ~Style() { aether_computed_style_free(&value); }
    Style(const Style&) = delete;
    Style& operator=(const Style&) = delete;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_environment_t env{300, 200, 1, 16, 0, 0, 0};
    std::map<aether_node_t, aether_computed_style_handle_t> handles;
    uint32_t child_impact{};
    explicit Fixture(const std::string& css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc,
            "<html id='root'><body><div id='p'><div id='c'></div></div></body></html>") == AETHER_OK, "parse document");
        aether_style_runtime_options_t options{};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
        add(css);
        resolve();
    }
    ~Fixture() {
        for (const auto& item : handles) aether_computed_style_handle_destroy(item.second);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) { return aether_doc_get_element_by_id(doc, id); }
    aether_computed_style_handle_t at(const char* id) {
        auto it = handles.find(node(id));
        require(it != handles.end(), "find owning style handle");
        return it->second;
    }
    aether_computed_style_handle_t take(const char* id) {
        // Transfer actual ownership out of the update-handle cache. The caller
        // keeps this handle across later resolutions and destroys it itself.
        const auto handle = at(id);
        handles.erase(node(id));
        return handle;
    }
    int64_t add(const std::string& css) {
        int64_t sheet{};
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "corner-context.css",
            &sheet, nullptr) == AETHER_OK, "add stylesheet");
        return sheet;
    }
    void consume(aether_style_update_t* updates, int count) {
        require(count >= 0, "resolve result");
        child_impact = 0;
        for (int i = 0; i < count; ++i) {
            aether_node_t n{};
            aether_computed_style_handle_t h{};
            uint32_t impact{};
            require(aether_style_runtime_get_update(updates[i], &n, &h, &impact) == AETHER_OK, "read update");
            if (n == node("c")) child_impact |= impact;
            auto old = handles.find(n);
            if (old != handles.end()) aether_computed_style_handle_destroy(old->second);
            handles[n] = h;
        }
        aether_style_updates_free(updates, count);
    }
    void resolve() {
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_resolve_subtree(runtime,
            aether_doc_get_document_element(doc), &updates);
        consume(updates, count);
    }
    void set(const char* id, const std::string& style) {
        require(aether_element_set_attribute(node(id), "style", style.c_str()) == AETHER_OK, "set inline style");
        resolve();
    }
    void resize(float width, float height) {
        env.viewport_width = width;
        env.viewport_height = height;
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_set_environment(runtime, &env,
            aether_doc_get_document_element(doc), &updates);
        consume(updates, count);
    }
};

void length(aether_css_length actual, float value, aether_css_unit_t unit = AETHER_CSS_UNIT_PX) {
    require(actual.unit == unit, "length unit");
    if (!near(actual.value, value)) std::fprintf(stderr, "actual length %g, expected %g\n", actual.value, value);
    require(near(actual.value, value), "length value");
}
void pair(aether_corner_radius actual, float x, float y) {
    length(actual.horizontal, x);
    length(actual.vertical, y);
}
void linear(aether_css_length actual, float px, float percent) {
    length(actual, px, AETHER_CSS_UNIT_LINEAR);
    require(near(actual.linear_percent, percent), "retained percentage coefficient");
    // Other contextual coefficients must have frozen into CSS pixels.
    require(near(aether_css_length_resolve(actual, 100, 99, 88, 777, 666), px + percent),
            "computed calc does not re-evaluate consuming context");
}
void corners(const aether_border_radii& actual, const float (&expected)[8]) {
    pair(actual.top_left, expected[0], expected[1]);
    pair(actual.top_right, expected[2], expected[3]);
    pair(actual.bottom_right, expected[4], expected[5]);
    pair(actual.bottom_left, expected[6], expected[7]);
}
void child(Fixture& f, const float (&expected)[8]) {
    Style s(f.at("c"));
    corners(s.value.border_radii, expected);
}

void context_and_inheritance() {
    Fixture f("html{font-size:10px}#p{font-size:20px;--tokens:2em 3em;"
        "border-top-left-radius:2em 3em;border-top-right-radius:1rem 2rem;"
        "border-bottom-right-radius:10vw 20vh;"
        "border-bottom-left-radius:calc(1em + 10%) calc(2rem + 20%)}"
        "#c{font-size:7px;border-top-left-radius:inherit;border-bottom-left-radius:inherit;"
        "border-top-right-radius:5px 9px;border-bottom-right-radius:var(--tokens)}");
    {
        Style p(f.at("p"));
        pair(p.value.border_radii.top_left, 40, 60);
        pair(p.value.border_radii.top_right, 10, 20);
        pair(p.value.border_radii.bottom_right, 30, 40);
        linear(p.value.border_radii.bottom_left.horizontal, 20, 10);
        linear(p.value.border_radii.bottom_left.vertical, 20, 20);
        Style c(f.at("c"));
        pair(c.value.border_radii.top_left, 40, 60); // Already computed parent lengths.
        pair(c.value.border_radii.top_right, 5, 9); // Independent sibling corner.
        pair(c.value.border_radii.bottom_right, 14, 21); // Inherited tokens use child font.
        linear(c.value.border_radii.bottom_left.horizontal, 20, 10);
        linear(c.value.border_radii.bottom_left.vertical, 20, 20);
    }
    f.set("root", "font-size:12px");
    f.set("p", "font-size:25px");
    f.resize(400, 250);
    {
        Style p(f.at("p"));
        pair(p.value.border_radii.top_left, 50, 75);
        pair(p.value.border_radii.top_right, 12, 24);
        pair(p.value.border_radii.bottom_right, 40, 50);
        linear(p.value.border_radii.bottom_left.horizontal, 25, 10);
        linear(p.value.border_radii.bottom_left.vertical, 24, 20);
        Style c(f.at("c"));
        pair(c.value.border_radii.top_left, 50, 75);
        pair(c.value.border_radii.top_right, 5, 9);
        pair(c.value.border_radii.bottom_right, 14, 21);
        linear(c.value.border_radii.bottom_left.horizontal, 25, 10);
        linear(c.value.border_radii.bottom_left.vertical, 24, 20);
    }
}

void lifecycle() {
    Fixture f("#c{--r:10px 20px;border-top-left-radius:var(--r);"
        "border-top-right-radius:3px 4px;border-bottom-right-radius:5px 6px;border-bottom-left-radius:7px 8px}");
    const auto retained = f.take("c");
    f.set("c", "--r:10px 30px");
    child(f, {10, 30, 3, 4, 5, 6, 7, 8});
    require((f.child_impact & AETHER_STYLE_IMPACT_PAINT) != 0, "vertical-only radius change repaints");
    require((f.child_impact & AETHER_STYLE_IMPACT_LAYOUT) == 0, "ordinary-box radius-only update does not relayout");
    {
        // Read the retained owning handle only AFTER the runtime has advanced.
        Style old(retained);
        corners(old.value.border_radii, {10, 20, 3, 4, 5, 6, 7, 8});
    }
    f.resolve();
    child(f, {10, 30, 3, 4, 5, 6, 7, 8});
    aether_style_runtime_clear_cache(f.runtime);
    f.resolve();
    child(f, {10, 30, 3, 4, 5, 6, 7, 8});
    f.set("c", "--r:10px / 30px");
    child(f, {0, 0, 3, 4, 5, 6, 7, 8});
    f.set("c", "--r:10px 40px");
    child(f, {10, 40, 3, 4, 5, 6, 7, 8});
    { Style old(retained); corners(old.value.border_radii, {10, 20, 3, 4, 5, 6, 7, 8}); }
    aether_computed_style_handle_destroy(retained);

    const auto sheet = f.add("#c{border-top-left-radius:11px 21px}");
    f.resolve();
    child(f, {11, 21, 3, 4, 5, 6, 7, 8});
    require(aether_style_runtime_remove_stylesheet(f.runtime, sheet) == AETHER_TRUE, "remove replacement sheet");
    f.resolve();
    child(f, {10, 40, 3, 4, 5, 6, 7, 8});
    f.add("#c{border-top-left-radius:12px 22px}");
    f.resolve();
    child(f, {12, 22, 3, 4, 5, 6, 7, 8});
}

void cascade_and_aggregate_controls() {
    {
        Fixture f("#c{border-top-left-radius:7px;border-top-left-radius:10px / 20px !important}");
        child(f, {7, 7, 0, 0, 0, 0, 0, 0});
    }
    {
        Fixture f("#c{border-top-left-radius:11px 22px !important;border-top-left-radius:31px 41px}");
        child(f, {11, 22, 0, 0, 0, 0, 0, 0});
        f.set("c", "border-top-left-radius:51px 61px");
        child(f, {11, 22, 0, 0, 0, 0, 0, 0});
    }
    {
        Fixture f("#c{border-top-left-radius:7px 9px}");
        f.set("c", "border-top-left-radius:10px / 20px !important");
        child(f, {7, 9, 0, 0, 0, 0, 0, 0});
        f.set("c", "border-top-left-radius:13px 23px");
        child(f, {13, 23, 0, 0, 0, 0, 0, 0});
    }
    struct Aggregate { const char* value; float expected[8]; };
    const Aggregate cases[] = {
        {"10px", {10, 10, 10, 10, 10, 10, 10, 10}},
        {"10px 20px", {10, 10, 20, 20, 10, 10, 20, 20}},
        {"10px 20px 30px", {10, 10, 20, 20, 30, 30, 20, 20}},
        {"10px 20px 30px 40px", {10, 10, 20, 20, 30, 30, 40, 40}},
        {"10px 20px 30px 40px / 11px 21px 31px 41px", {10, 11, 20, 21, 30, 31, 40, 41}}
    };
    for (const auto& item : cases) {
        Fixture f(std::string("#c{border-radius:") + item.value + "}");
        child(f, item.expected);
    }
    // Native compatibility, not browser shorthand expansion: aggregate and
    // corner properties are independent winners applied in registry order.
    for (const char* declaration : {
        "border-radius:10px 20px / 30px 40px;border-top-left-radius:7px 9px;",
        "border-top-left-radius:7px 9px;border-radius:10px 20px / 30px 40px;"}) {
        Fixture f(std::string("#c{") + declaration + "}");
        child(f, {7, 9, 20, 40, 10, 30, 20, 40});
    }
}
} // namespace

int main() {
    context_and_inheritance();
    lifecycle();
    cascade_and_aggregate_controls();
    std::printf("corner context: %d active NDEBUG checks passed\n", checks);
    return 0;
}
