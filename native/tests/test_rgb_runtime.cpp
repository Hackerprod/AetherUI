// RGB regression through the public stylesheet/runtime ABI. No assert(): these
// checks must also run in release builds. No locale mutation in this executable.
#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "HARNESS FAIL: %s\n", message); std::exit(2); }
}
struct Styles {
    aether_computed_style_t actual{}, expected{};
    ~Styles() { aether_computed_style_free(&actual); aether_computed_style_free(&expected); }
};
void append(std::vector<float>& out, aether_color_rgba c) {
    out.insert(out.end(), {c.r, c.g, c.b, c.a});
}
std::vector<float> signature(const aether_computed_style_t& s, const std::string& property) {
    std::vector<float> out;
    if (property == "color") append(out, s.color);
    else if (property == "border" || property == "border-color") {
        for (auto c : {s.border_colors.top, s.border_colors.right, s.border_colors.bottom, s.border_colors.left}) append(out, c);
        if (property == "border") {
            for (auto length : {s.border_width.top, s.border_width.right, s.border_width.bottom, s.border_width.left}) {
                out.push_back(float(length.unit)); out.push_back(length.value);
            }
            out.insert(out.end(), {float(s.border_styles.top), float(s.border_styles.right),
                                  float(s.border_styles.bottom), float(s.border_styles.left)});
        }
    } else if (property == "box-shadow" || property == "text-shadow") {
        const auto& list = property == "box-shadow" ? s.box_shadows : s.text_shadows;
        out.push_back(float(list.count));
        for (int i = 0; i < list.count; ++i) {
            const auto& shadow = list.items[i];
            append(out, shadow.color);
            out.insert(out.end(), {shadow.offset_x, shadow.offset_y, shadow.blur_radius, shadow.spread_radius, float(shadow.inset)});
        }
    } else {
        append(out, s.background_color);
        out.push_back(float(s.background_layers.count));
        for (int i = 0; i < s.background_layers.count; ++i) {
            const auto& layer = s.background_layers.items[i];
            out.push_back(float(layer.kind));
            out.push_back(float(layer.gradient.stop_count));
            for (int j = 0; j < layer.gradient.stop_count; ++j) append(out, layer.gradient.stops[j].color);
        }
    }
    return out;
}
void compare(const std::string& property, const std::string& actual,
             const std::string& expected, const std::string& label, bool inlined = false) {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    require(aether_doc_create(&doc) == AETHER_OK, "create document");
    const std::string html = "<html><body><div id='parent'><div id='actual' style='" +
        (inlined ? actual : "") + "'></div><div id='expected'></div></div></body></html>";
    require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
    aether_style_runtime_options_t options{};
    options.max_variable_depth = 32;
    options.max_variable_expansion_length = 65536;
    aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK, "create runtime");
    // Non-black inherited color and a distinctive inherited text shadow make
    // computed-invalid inheritance distinguishable from initial and lower values.
    const std::string css = "#parent{color:#2468ac;text-shadow:7px 8px #2468ac;}#actual{" +
        (inlined ? "" : actual) + "}#expected{" + expected + "}";
    int64_t sheet{};
    require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 2, "rgb-runtime.css", &sheet, nullptr) == AETHER_OK, "compile stylesheet");
    aether_style_update_t* updates{};
    const int count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
    require(count > 0 && updates, "resolve styles");
    Styles styles;
    bool found_actual = false, found_expected = false;
    const auto actual_node = aether_doc_get_element_by_id(doc, "actual");
    const auto expected_node = aether_doc_get_element_by_id(doc, "expected");
    for (int i = 0; i < count; ++i) {
        aether_node_t node{};
        aether_computed_style_handle_t handle{};
        require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "read update");
        if (node == actual_node) {
            require(aether_computed_style_copy(handle, &styles.actual) == AETHER_OK, "copy actual"); found_actual = true;
        }
        if (node == expected_node) {
            require(aether_computed_style_copy(handle, &styles.expected) == AETHER_OK, "copy expected"); found_expected = true;
        }
        aether_computed_style_handle_destroy(handle);
    }
    require(found_actual && found_expected, "both styles present");
    const auto a = signature(styles.actual, property), e = signature(styles.expected, property);
    bool equal = a.size() == e.size();
    for (size_t i = 0; i < a.size() && i < e.size(); ++i)
        equal = std::isfinite(a[i]) && std::isfinite(e[i]) && std::fabs(a[i] - e[i]) < .0001f && equal;
    ++checks;
    if (!equal) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s [%s, %s]\n  actual CSS: %s\n", label.c_str(), property.c_str(), inlined ? "inline" : "sheet", actual.c_str());
        std::fprintf(stderr, "  actual:"); for (float v : a) std::fprintf(stderr, " %g", v);
        std::fprintf(stderr, "\n  expected:"); for (float v : e) std::fprintf(stderr, " %g", v);
        std::fprintf(stderr, "\n");
    }
    aether_style_updates_free(updates, count);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(doc);
}
struct Consumer { const char* property; const char* prefix; const char* suffix; };
const Consumer consumers[] = {
    {"color", "", ""}, {"background-color", "", ""}, {"background", "", ""},
    {"border-color", "", ""}, {"border", "3px solid ", ""},
    {"box-shadow", "1px 2px 3px ", ""}, {"text-shadow", "1px 2px 3px ", ""},
    {"background-image", "linear-gradient(", ", #abcdef)"},
    {"background", "linear-gradient(", ", #abcdef)"}
};
std::string declaration(const Consumer& c, const std::string& color) {
    return std::string(c.property) + ":" + c.prefix + color + c.suffix + ";";
}
void invalid(const std::string& value, bool inlined) {
    for (const auto& c : consumers) {
        const auto lower = declaration(c, "#e13c72");
        compare(c.property, lower + declaration(c, value), lower,
                "invalid literal preserves lower declaration", inlined);
        compare(c.property, "--bad:" + value + ";" + lower + declaration(c, "var(--bad, #00ff00)"),
                std::string(c.property) + ":unset;",
                "defined invalid var does not use fallback or revive lower declaration", inlined);
    }
}
} // namespace

int main() {
    // All six mixed RGB category combinations, including zero, in both arities.
    for (int mask = 1; mask < 7; ++mask) {
        std::string channels;
        for (int i = 0; i < 3; ++i) { if (i) channels += ','; channels += (mask & (1 << i)) ? "0%" : "0"; }
        invalid("rgb(" + channels + ")", false);
        invalid("rgba(" + channels + ",50%)", false);
    }
    for (const char* value : {
        "rgb(0x10,2,3)", "rgb(+0X1p2,2,3)", "rgb(1px,2,3)", "rgb(\"1\",2,3)",
        "rgb(1e,2,3)", "rgb(1/**/2,2,3)", "rgb(10/**/%,20%,30%)",
        "rgb(,2,3)", "rgb(1,2)", "rgb(1,2,3,4)", "rgba(1,2,3,1px)",
        "rgba(1,2,3,0x1p0)", "rgba(1,2,3,\".5\")", "rgba(1,2,3,1e)",
        "rgba(1,2,3,50/**/%)", "rgb(calc(1),2,3)"
    }) invalid(value, false);
    // Exercise the separate inline declaration path as well.
    invalid("rgb(10%,20,30%)", true);
    invalid("rgba(1,2,3,0x1p0)", true);

    for (const auto& c : consumers) {
        for (bool inlined : {false, true}) {
            compare(c.property, declaration(c, "rgb(+1e2,50,0)"), declaration(c, "#643200"), "numeric channels", inlined);
            compare(c.property, declaration(c, "rgb(100%,0%,0%)"), declaration(c, "#ff0000"), "percentage channels", inlined);
            compare(c.property, declaration(c, "rgba(100%,0%,0%,.5)"), declaration(c, "rgba(255,0,0,.5)"), "independent numeric alpha", inlined);
            compare(c.property, declaration(c, "rgba(255,0,0,50%)"), declaration(c, "rgba(255,0,0,.5)"), "independent percentage alpha", inlined);
            compare(c.property, declaration(c, "rgb(/**/100/**/, 50/**/,0/**/)"), declaration(c, "#643200"), "peripheral comments", inlined);
            compare(c.property, "--r:100%;--g:0%;--b:0%;" + declaration(c, "rgb(var(--r),var(--g),var(--b))"), declaration(c, "#ff0000"), "percentage tokens survive substitution", inlined);
            compare(c.property, declaration(c, "var(--missing, rgb(100%,0%,0%))"), declaration(c, "#ff0000"), "missing var uses valid fallback", inlined);
            compare(c.property, "--n:10;" + declaration(c, "#e13c72") + declaration(c, "rgb(var(--n)%,20%,30%)"),
                    std::string(c.property) + ":unset;", "substitution cannot manufacture percentage", inlined);
        }
    }
    std::printf("RGB runtime: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
