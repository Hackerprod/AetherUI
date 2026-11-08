#include "../src/style_runtime/style_property_registry_internal.h"
#include "../src/css/background_layers.h"
#include "../src/css/linear_gradient.h"
#include <cmath>
#include <cstdio>
#include <cstring>

// Private registration entry point; this executable compiles its real definition.
void builtin_properties_register(style_property_registry_s*);
static int checks = 0, failures = 0;
static void check(bool ok, const char* what, const char* source = "") {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s [%s]\n", what, source); }
}
static bool close(float a, float b) { return std::fabs(a-b) < .0001f; }
static std::string item(const style_shorthand_writer_s& writer, const char* name) {
    for (const auto& value : writer.items) if (value.name == name) return value.value;
    return {};
}
int main() {
    style_property_registry_s registry;
    builtin_properties_register(&registry);
    registry.seal();
    const auto* image = registry.try_get("background-image");
    const auto* background = registry.try_get_shorthand("background");
    check(image && image->parse && image->apply_checked, "real image callbacks registered");
    check(background && background->expand, "real shorthand callback registered");
    if (!image || !image->parse || !image->apply_checked || !background || !background->expand) return 1;
    const char* corners[] = {
        "linear-gradient(to top right, red, blue)",
        "/* leading ) , */linear-gradient(to top right, red, blue)",
        "\flinear-gradient(to top right, red, blue)",
        "/* leading */\fLiNeAr-GrAdIeNt(to top right, red, blue)/* trailing ) */"
    };
    for (const char* source : corners) {
        std::string parsed = "unchanged";
        check(image->parse(source, parsed), "longhand parse accepts trivia and complete name", source);
        aether_computed_style_t style{};
        const auto result = image->apply_checked(&style, source);
        check(result.success, "direct registered applicator accepts gradient", source);
        check(result.diagnostic_code && std::strcmp(result.diagnostic_code, "AETHER-CSS-GRADIENT-APPROXIMATION") == 0,
              "corner approximation warning preserved", source);
        check(style.background_layers.count == 1 && style.background_layers.items,
              "corner has exactly one owned layer", source);
        if (style.background_layers.count == 1 && style.background_layers.items) {
            const auto& layer = style.background_layers.items[0];
            const auto& gradient = layer.gradient;
            check(layer.kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT && gradient.stop_count == 2 && gradient.stops,
                  "gradient kind and stop ownership", source);
            check(close(gradient.direction_x, .70710678f) && close(gradient.direction_y, -.70710678f), "corner direction", source);
            if (gradient.stop_count == 2 && gradient.stops) {
                check(close(gradient.stops[0].color.r, 1) && close(gradient.stops[0].color.b, 0) &&
                      close(gradient.stops[1].color.r, 0) && close(gradient.stops[1].color.b, 1), "stop colors preserved", source);
            }
        }
        aether_background_layer_list_free(&style.background_layers);
    }
    const char* invalid[] = {
        "linear-gradientx(red,blue)", "linear-gradient", "linear-gradient-bogus(red,blue)",
        "linear-gradient (red,blue)", "linear-gradient/**/(red,blue)",
        "linear-gradient(red,blue) junk", "linear-gradient(red,blue)/**/none",
        "linear-gradient(red,blue)rgb(1,2,3)", "linear-gradient(red,blue))",
        "linear-gradient(red,blue) center / cover repeat no-repeat round",
        "linear-gradient(red,blue) repeat center no-repeat",
        "linear-gradient(red,blue) center / cover repeat red no-repeat",
        "linear-gradient(red,blue) repeat center / cover no-repeat",
        "linear-gradient(red,blue)[]", "linear-gradient(red,blue);", "linear-gradient(red,blue) !important"
    };
    for (const char* source : invalid) {
        std::string parsed = "unchanged";
        check(!image->parse(source, parsed), "longhand rejects invalid boundary", source);
        check(parsed == "unchanged", "parse failure preserves caller string", source);
        aether_computed_style_t style{};
        const auto seeded = image->apply_checked(&style, "linear-gradient(red,blue)");
        check(seeded.success && style.background_layers.count == 1, "seed existing ownership");
        auto* original = style.background_layers.items;
        auto* stops = original ? original[0].gradient.stops : nullptr;
        const auto result = image->apply_checked(&style, source);
        check(!result.success, "direct applicator rejects invalid boundary", source);
        check(style.background_layers.items == original && style.background_layers.count == 1,
              "apply failure preserves destination allocation", source);
        if (style.background_layers.items == original && original) {
            check(original[0].gradient.stops == stops && original[0].gradient.stop_count == 2 &&
                  close(stops[0].color.r, 1) && close(stops[1].color.b, 1), "apply failure preserves stops", source);
        }
        aether_background_layer_list_free(&style.background_layers);
        // A following rgb() is a valid shorthand color, but not a longhand image.
        if (std::strstr(source, ")rgb(")) continue;
        aether_background_shorthand_s primary{};
        check(!aether_background_shorthand_try_parse(source, primary), "invalid fixture reaches fallback", source);
        style_shorthand_writer_s writer;
        writer.set("sentinel", "preserved");
        check(!background->expand(source, writer), "fallback rejects prefix-only and suffix garbage", source);
        check(writer.items.size() == 1 && item(writer, "sentinel") == "preserved", "failed expansion publishes no siblings", source);
    }
    // This is intentionally a private expansion test, not a claim that the later
    // longhand list validator supports EOF comments. The primary splitter rejects
    // this value, but the standalone gradient parser accepts its trailing trivia.
    const char* fallback = "linear-gradient(red,blue)/* EOF comment";
    aether_background_shorthand_s primary{};
    check(!aether_background_shorthand_try_parse(fallback, primary), "valid fallback bypasses failed primary splitter");
    aether_background_layer_list layers{};
    check(!aether_background_image_list_try_parse(fallback, &layers), "list validator limitation remains explicit");
    aether_background_layer_list_free(&layers);
    aether_linear_gradient gradient{};
    check(aether_linear_gradient_try_parse(fallback, &gradient), "fallback is a real valid standalone gradient");
    aether_linear_gradient_free(&gradient);
    style_shorthand_writer_s writer;
    check(background->expand(fallback, writer), "registered expansion really executes valid fallback");
    check(writer.items.size() == 5 && item(writer, "background-image") == fallback &&
          item(writer, "background-position") == "0% 0%" && item(writer, "background-size") == "auto auto" &&
          item(writer, "background-repeat") == "repeat repeat" && item(writer, "background-color") == "rgba(255, 0, 0, 1)",
          "valid fallback publishes full values and first-stop color");
    std::fprintf(stderr, "Gradient boundary dispatch: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
