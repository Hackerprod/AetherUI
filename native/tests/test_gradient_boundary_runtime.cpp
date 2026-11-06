// Public-ABI boundary/cascade regression. All checks remain active with NDEBUG.
#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
size_t checks = 0;
size_t cases = 0;
std::string context;
void require(bool condition, const char* detail) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s (check %zu)\n", context.c_str(), detail, checks);
        std::exit(1);
    }
}
void scalar(float a, float b, const char* field) {
    require(std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) < .0001f, field);
}
void color(const aether_color_rgba& a, const aether_color_rgba& b) {
    scalar(a.r, b.r, "color.r"); scalar(a.g, b.g, "color.g");
    scalar(a.b, b.b, "color.b"); scalar(a.a, b.a, "color.a");
}
void length(const aether_css_length& a, const aether_css_length& b) {
    require(a.unit == b.unit, "length.unit");
    scalar(a.value, b.value, "length.value");
    scalar(a.linear_percent, b.linear_percent, "length.linear_percent");
    scalar(a.linear_em, b.linear_em, "length.linear_em");
    scalar(a.linear_rem, b.linear_rem, "length.linear_rem");
    scalar(a.linear_vw, b.linear_vw, "length.linear_vw");
    scalar(a.linear_vh, b.linear_vh, "length.linear_vh");
}
void gradient(const aether_linear_gradient& a, const aether_linear_gradient& b) {
    scalar(a.direction_x, b.direction_x, "gradient.direction_x");
    scalar(a.direction_y, b.direction_y, "gradient.direction_y");
    require(a.stop_count == b.stop_count, "gradient.stop_count");
    require((a.stops != nullptr) == (b.stops != nullptr), "gradient.stops presence");
    for (int32_t i = 0; i < a.stop_count; ++i) {
        color(a.stops[i].color, b.stops[i].color);
        length(a.stops[i].position, b.stops[i].position);
        scalar(a.stops[i].offset, b.stops[i].offset, "stop.offset");
        require(a.stops[i].has_position == b.stops[i].has_position, "stop.has_position");
    }
}
void snapshot(const aether_computed_style_t& a, const aether_computed_style_t& b) {
    color(a.background_color, b.background_color);
    require(a.background_layers.count == b.background_layers.count, "background layer count");
    require((a.background_layers.items != nullptr) == (b.background_layers.items != nullptr), "layer storage presence");
    for (int32_t i = 0; i < a.background_layers.count; ++i) {
        const auto& x = a.background_layers.items[i];
        const auto& y = b.background_layers.items[i];
        require(x.kind == y.kind, "layer.kind");
        require((x.image_source != nullptr) == (y.image_source != nullptr), "URL presence");
        if (x.image_source && y.image_source)
            require(std::strcmp(x.image_source, y.image_source) == 0, "URL contents");
        gradient(x.gradient, y.gradient);
        length(x.position_x, y.position_x); length(x.position_y, y.position_y);
        require(x.size_kind == y.size_kind, "layer.size_kind");
        length(x.size_x, y.size_x); length(x.size_y, y.size_y);
        require(x.repeat_x == y.repeat_x, "layer.repeat_x");
        require(x.repeat_y == y.repeat_y, "layer.repeat_y");
    }
    require(a.has_text_gradient == b.has_text_gradient, "has_text_gradient");
    gradient(a.text_gradient, b.text_gradient);
}
struct style {
    aether_computed_style_t value{};
    style() = default;
    style(const style&) = delete;
    style& operator=(const style&) = delete;
    ~style() { aether_computed_style_free(&value); }
};
struct fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_node_t body = nullptr;
    int64_t sheet = 0;
    aether_computed_style_handle_t current = nullptr;
    fixture() {
        require(aether_doc_create(&doc) == AETHER_OK, "doc create");
        require(aether_doc_parse_html(doc, "<html><body></body></html>") == AETHER_OK, "doc parse");
        body = aether_doc_get_body(doc);
        require(body != nullptr, "body lookup");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{800, 600, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "runtime create");
    }
    ~fixture() {
        aether_computed_style_handle_destroy(current);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    void set(const std::string& declarations, bool inlined) {
        if (sheet) {
            require(aether_style_runtime_remove_stylesheet(runtime, sheet), "remove old sheet");
            sheet = 0;
        }
        require(aether_element_set_attribute(body, "style", inlined ? declarations.c_str() : "") == AETHER_OK,
                "inline mutation");
        if (!inlined) {
            const std::string source = "body {" + declarations + "}";
            require(aether_style_runtime_add_stylesheet(runtime, source.c_str(), 0,
                "gradient-boundary.css", &sheet, nullptr) == AETHER_OK, "add sheet");
        }
    }
    void copy(style& destination) {
        aether_style_update_t* updates = nullptr;
        const int32_t count = aether_style_runtime_resolve_subtree(runtime,
            aether_doc_get_document_element(doc), &updates);
        require(count >= 0, "resolve subtree");
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, nullptr, nullptr) == AETHER_OK,
                    "inspect update node");
            if (node == body) {
                aether_computed_style_handle_t next = nullptr;
                require(aether_style_runtime_get_update(updates[i], nullptr, &next, nullptr) == AETHER_OK,
                        "get body style handle");
                aether_computed_style_handle_destroy(current);
                current = next;
            }
        }
        aether_style_updates_free(updates, count);
        require(current != nullptr, "computed handle");
        auto handle = current;
        style twin;
        require(aether_computed_style_copy(handle, &destination.value) == AETHER_OK, "copy computed style");
        require(aether_computed_style_copy(handle, &twin.value) == AETHER_OK, "second independent copy");
        snapshot(destination.value, twin.value);
        if (destination.value.background_layers.count) {
            require(destination.value.background_layers.items != twin.value.background_layers.items,
                    "layer copies must not alias");
            for (int32_t i = 0; i < destination.value.background_layers.count; ++i) {
                const auto& x = destination.value.background_layers.items[i];
                const auto& y = twin.value.background_layers.items[i];
                if (x.gradient.stop_count) require(x.gradient.stops != y.gradient.stops, "stop copies must not alias");
                if (x.image_source) require(x.image_source != y.image_source, "URL copies must not alias");
            }
        }
        // destination survives update destruction and, for reference styles,
        // handle/runtime/document destruction. No private structures are used.
        // On an unchanged resolve there need not be a new update: current owns
        // the most recent public-ABI handle, independently of the runtime.
    }
};
const std::string lower_image = "linear-gradient(90deg,#e13c72 13%,#46218b calc(80% + 3px)),url(lower.png)";
const std::string lower =
    "background-color:#192b4d; background-image:" + lower_image + ";"
    "background-position:17% 23px,31px 47%;"
    "background-size:61px 73%,contain; background-repeat:space round,no-repeat repeat;";
const std::string valid = "linear-gradient(to right,#126b35 calc(10% + 2px),#284fd1 83%)";
const std::string fallback = "linear-gradient(180deg,#00aa00 7px,#003300 91%)";

void reference(const std::string& declarations, style& out) {
    fixture f;
    f.set(declarations, false);
    f.copy(out);
}
struct test_case { std::string name, actual, expected; };
void run_matrix(bool inlined, const std::vector<test_case>& matrix) {
    fixture f;
    for (int round = 0; round < 3; ++round) {
        for (const auto& c : matrix) {
            context = std::string(inlined ? "inline/" : "sheet/") + c.name + "/round-" + std::to_string(round);
            style expected, actual;
            reference(c.expected, expected);
            f.set(c.actual, inlined);
            f.copy(actual);
            snapshot(actual.value, expected.value);
            // Exercise a second resolution, retaining the first owned snapshot.
            style again;
            f.copy(again);
            snapshot(again.value, expected.value);
            snapshot(actual.value, expected.value);
            ++cases;
        }
    }
}
} // namespace

int main() {
    context = "lower-longhand sentinel";
    style sentinel;
    reference(lower, sentinel);
    require(sentinel.value.background_layers.count == 2, "lower fixture has two layers");
    require(sentinel.value.background_color.a != 0, "lower color is noninitial");
    const auto& first = sentinel.value.background_layers.items[0];
    require(first.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT && first.gradient.stop_count == 2,
            "lower gradient survives");
    require(first.position_x.unit == AETHER_CSS_UNIT_PERCENT && first.position_x.value == 17,
            "distinct lower x position");
    require(first.position_y.unit == AETHER_CSS_UNIT_PX && first.position_y.value == 23,
            "distinct lower y position");
    require(first.size_kind == AETHER_BACKGROUND_SIZE_EXPLICIT && first.size_x.value == 61 && first.size_y.value == 73,
            "distinct lower size");
    require(first.repeat_x == AETHER_BACKGROUND_SPACE && first.repeat_y == AETHER_BACKGROUND_ROUND,
            "distinct lower repeats");
    const auto& second = sentinel.value.background_layers.items[1];
    require(second.kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && second.image_source &&
            std::strcmp(second.image_source, "lower.png") == 0, "lower URL sentinel");
    require(second.position_x.value == 31 && second.position_y.value == 47 &&
            second.size_kind == AETHER_BACKGROUND_SIZE_CONTAIN && second.repeat_x == AETHER_BACKGROUND_NO_REPEAT &&
            second.repeat_y == AETHER_BACKGROUND_REPEAT, "distinct second-layer metadata");

    context = "valid-image sentinel";
    style valid_sentinel;
    reference("background-image:" + valid + ";", valid_sentinel);
    require(valid_sentinel.value.background_layers.count == 1, "valid fixture has one layer");
    const auto& vg = valid_sentinel.value.background_layers.items[0].gradient;
    require(vg.stop_count == 2 && vg.stops != nullptr, "valid fixture has two stops");
    scalar(vg.direction_x, 1, "right direction x");
    scalar(vg.direction_y, 0, "right direction y");
    color(vg.stops[0].color, {18.f / 255.f, 107.f / 255.f, 53.f / 255.f, 1});
    color(vg.stops[1].color, {40.f / 255.f, 79.f / 255.f, 209.f / 255.f, 1});
    require(vg.stops[0].has_position && vg.stops[1].has_position, "authored stop flags");
    require(vg.stops[0].position.unit == AETHER_CSS_UNIT_LINEAR, "symbolic calc stop unit");
    scalar(vg.stops[0].position.value, 2, "calc absolute term");
    scalar(vg.stops[0].position.linear_percent, 10, "calc percent term");
    require(vg.stops[1].position.unit == AETHER_CSS_UNIT_PERCENT, "percent stop unit");
    scalar(vg.stops[1].position.value, 83, "authored percentage");

    std::vector<test_case> matrix;
    // Invalid literal declarations are discarded, whereas deferred winners
    // become initial. The var fallback must not rescue a defined invalid value.
    const std::vector<std::string> bad = {
        "linear-gradient(red,blue) junk", "linear-gradient(red,blue) none",
        "linear-gradient (red,blue)", "linear-gradient/**/(red,blue)",
        "linear-gradientx(red,blue)", "linear-gradient(red,blue)/**/junk",
        "linear-gradient(red,blue)rgb(1,2,3)", "linear-gradient(red,blue)[]",
        "linear-gradient(red,blue) \"tail\""
    };
    for (const std::string property : {"background-image", "background"}) {
        const std::string initial = property == "background" ? "background:initial;" : lower + "background-image:none;";
        for (size_t i = 0; i < bad.size(); ++i) {
            // Adjacent rgb() is a color component, valid in the shorthand.
            if (property == "background" && i == 6) continue;
            const auto prefix = property + "/bad-" + std::to_string(i);
            matrix.push_back({prefix + "/literal", lower + property + ":" + bad[i] + ";", lower});
            matrix.push_back({prefix + "/literal-important", lower + property + ":" + bad[i] + " !important;", lower});
            matrix.push_back({prefix + "/defined-invalid", lower + "--bad:" + bad[i] + ";" + property +
                ":var(--bad," + fallback + ");", initial});
            matrix.push_back({prefix + "/defined-invalid-important", lower + "--bad:" + bad[i] + ";" + property +
                ":var(--bad," + fallback + ") !important;", initial});
        }
        matrix.push_back({property + "/missing-fallback", lower + property + ":var(--missing," + fallback + ");",
            lower + property + ":" + fallback + ";"});
        matrix.push_back({property + "/defined-valid", lower + "--good:" + valid + ";" + property +
            ":var(--good," + fallback + ");", lower + property + ":" + valid + ";"});
        matrix.push_back({property + "/ident-is-not-function", lower + "--name:linear-gradient;" + property +
            ":var(--name)(red,blue);", initial});
        matrix.push_back({property + "/comments-and-case", lower + property +
            ":/*(*/ LiNeAr-GrAdIeNt(to right,#126b35 calc(10% + 2px),#284fd1 83%) /*)*/;",
            lower + property + ":" + valid + ";"});
    }
    // A shorthand legitimately consumes components beyond its image function.
    matrix.push_back({"shorthand/adjacent-color-component", lower +
        "background:linear-gradient(red,blue)rgb(1,2,3);",
        "background:linear-gradient(red,blue) rgb(1,2,3);"});
    matrix.push_back({"shorthand/valid-trailing-components", lower + "background:" + valid +
        " no-repeat round 12% 19px / 43px 57% #415263;",
        "background-image:" + valid + ";background-position:12% 19px;background-size:43px 57%;"
        "background-repeat:no-repeat round;background-color:#415263;"});
    const std::string invalid_var = "--bad:linear-gradient(red,blue) junk;background:var(--bad," + fallback + ");";
    // A surviving image exposes every geometry/repetition sibling's reset, even
    // though an ordinary invalid shorthand has no layers in which to inspect it.
    matrix.push_back({"shorthand/important-image-survives", lower + "background-image:" + lower_image + " !important;" + invalid_var,
        "background:initial;background-image:" + lower_image + ";"});
    matrix.push_back({"shorthand/later-image-survives", lower + invalid_var + "background-image:" + lower_image + ";",
        "background:initial;background-image:" + lower_image + ";"});
    matrix.push_back({"shorthand/important-geometry-survives", lower + "background-position:37px 41%;"
        "background-position:37px 41% !important;background-size:79px 83px !important;"
        "background-repeat:round space !important;background-color:#a18723 !important;" + invalid_var +
        "background-image:" + valid + ";",
        "background-image:" + valid + ";background-position:37px 41%;background-size:79px 83px;"
        "background-repeat:round space;background-color:#a18723;"});
    matrix.push_back({"shorthand/important-invalid-beats-normal", lower +
        "--bad:linear-gradient(red,blue) junk;background:var(--bad) !important;background-image:" + valid + ";",
        "background:initial;"});
    matrix.push_back({"image/invalid-middle-layer-is-atomic", lower +
        "background-image:" + valid + ",linear-gradient(red,blue) junk,url(other.png);", lower});
    run_matrix(false, matrix);
    run_matrix(true, matrix);
    std::printf("PASS gradient boundary public ABI: %zu cases, %zu checks (NDEBUG-safe)\n", cases, checks);
    return 0;
}
