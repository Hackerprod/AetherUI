#include <aether/aether.h>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
// Independent normative oracle: CSS Color 4, CRD 3 November 2026, section 6.1.
// https://www.w3.org/TR/2026/CRD-css-color-4-20261103/#named-colors
// Transcribed from the specification's name/hex columns, not production data.
// transparent is specified separately and deliberately excluded from this array.
struct named_color { const char* name; uint32_t rgb; };
constexpr named_color colors[] = {
    {"aliceblue", 0xf0f8ffu},
    {"antiquewhite", 0xfaebd7u},
    {"aqua", 0x00ffffu},
    {"aquamarine", 0x7fffd4u},
    {"azure", 0xf0ffffu},
    {"beige", 0xf5f5dcu},
    {"bisque", 0xffe4c4u},
    {"black", 0x000000u},
    {"blanchedalmond", 0xffebcdu},
    {"blue", 0x0000ffu},
    {"blueviolet", 0x8a2be2u},
    {"brown", 0xa52a2au},
    {"burlywood", 0xdeb887u},
    {"cadetblue", 0x5f9ea0u},
    {"chartreuse", 0x7fff00u},
    {"chocolate", 0xd2691eu},
    {"coral", 0xff7f50u},
    {"cornflowerblue", 0x6495edu},
    {"cornsilk", 0xfff8dcu},
    {"crimson", 0xdc143cu},
    {"cyan", 0x00ffffu},
    {"darkblue", 0x00008bu},
    {"darkcyan", 0x008b8bu},
    {"darkgoldenrod", 0xb8860bu},
    {"darkgray", 0xa9a9a9u},
    {"darkgreen", 0x006400u},
    {"darkgrey", 0xa9a9a9u},
    {"darkkhaki", 0xbdb76bu},
    {"darkmagenta", 0x8b008bu},
    {"darkolivegreen", 0x556b2fu},
    {"darkorange", 0xff8c00u},
    {"darkorchid", 0x9932ccu},
    {"darkred", 0x8b0000u},
    {"darksalmon", 0xe9967au},
    {"darkseagreen", 0x8fbc8fu},
    {"darkslateblue", 0x483d8bu},
    {"darkslategray", 0x2f4f4fu},
    {"darkslategrey", 0x2f4f4fu},
    {"darkturquoise", 0x00ced1u},
    {"darkviolet", 0x9400d3u},
    {"deeppink", 0xff1493u},
    {"deepskyblue", 0x00bfffu},
    {"dimgray", 0x696969u},
    {"dimgrey", 0x696969u},
    {"dodgerblue", 0x1e90ffu},
    {"firebrick", 0xb22222u},
    {"floralwhite", 0xfffaf0u},
    {"forestgreen", 0x228b22u},
    {"fuchsia", 0xff00ffu},
    {"gainsboro", 0xdcdcdcu},
    {"ghostwhite", 0xf8f8ffu},
    {"gold", 0xffd700u},
    {"goldenrod", 0xdaa520u},
    {"gray", 0x808080u},
    {"green", 0x008000u},
    {"greenyellow", 0xadff2fu},
    {"grey", 0x808080u},
    {"honeydew", 0xf0fff0u},
    {"hotpink", 0xff69b4u},
    {"indianred", 0xcd5c5cu},
    {"indigo", 0x4b0082u},
    {"ivory", 0xfffff0u},
    {"khaki", 0xf0e68cu},
    {"lavender", 0xe6e6fau},
    {"lavenderblush", 0xfff0f5u},
    {"lawngreen", 0x7cfc00u},
    {"lemonchiffon", 0xfffacdu},
    {"lightblue", 0xadd8e6u},
    {"lightcoral", 0xf08080u},
    {"lightcyan", 0xe0ffffu},
    {"lightgoldenrodyellow", 0xfafad2u},
    {"lightgray", 0xd3d3d3u},
    {"lightgreen", 0x90ee90u},
    {"lightgrey", 0xd3d3d3u},
    {"lightpink", 0xffb6c1u},
    {"lightsalmon", 0xffa07au},
    {"lightseagreen", 0x20b2aau},
    {"lightskyblue", 0x87cefau},
    {"lightslategray", 0x778899u},
    {"lightslategrey", 0x778899u},
    {"lightsteelblue", 0xb0c4deu},
    {"lightyellow", 0xffffe0u},
    {"lime", 0x00ff00u},
    {"limegreen", 0x32cd32u},
    {"linen", 0xfaf0e6u},
    {"magenta", 0xff00ffu},
    {"maroon", 0x800000u},
    {"mediumaquamarine", 0x66cdaau},
    {"mediumblue", 0x0000cdu},
    {"mediumorchid", 0xba55d3u},
    {"mediumpurple", 0x9370dbu},
    {"mediumseagreen", 0x3cb371u},
    {"mediumslateblue", 0x7b68eeu},
    {"mediumspringgreen", 0x00fa9au},
    {"mediumturquoise", 0x48d1ccu},
    {"mediumvioletred", 0xc71585u},
    {"midnightblue", 0x191970u},
    {"mintcream", 0xf5fffau},
    {"mistyrose", 0xffe4e1u},
    {"moccasin", 0xffe4b5u},
    {"navajowhite", 0xffdeadu},
    {"navy", 0x000080u},
    {"oldlace", 0xfdf5e6u},
    {"olive", 0x808000u},
    {"olivedrab", 0x6b8e23u},
    {"orange", 0xffa500u},
    {"orangered", 0xff4500u},
    {"orchid", 0xda70d6u},
    {"palegoldenrod", 0xeee8aau},
    {"palegreen", 0x98fb98u},
    {"paleturquoise", 0xafeeeeu},
    {"palevioletred", 0xdb7093u},
    {"papayawhip", 0xffefd5u},
    {"peachpuff", 0xffdab9u},
    {"peru", 0xcd853fu},
    {"pink", 0xffc0cbu},
    {"plum", 0xdda0ddu},
    {"powderblue", 0xb0e0e6u},
    {"purple", 0x800080u},
    {"rebeccapurple", 0x663399u},
    {"red", 0xff0000u},
    {"rosybrown", 0xbc8f8fu},
    {"royalblue", 0x4169e1u},
    {"saddlebrown", 0x8b4513u},
    {"salmon", 0xfa8072u},
    {"sandybrown", 0xf4a460u},
    {"seagreen", 0x2e8b57u},
    {"seashell", 0xfff5eeu},
    {"sienna", 0xa0522du},
    {"silver", 0xc0c0c0u},
    {"skyblue", 0x87ceebu},
    {"slateblue", 0x6a5acdu},
    {"slategray", 0x708090u},
    {"slategrey", 0x708090u},
    {"snow", 0xfffafau},
    {"springgreen", 0x00ff7fu},
    {"steelblue", 0x4682b4u},
    {"tan", 0xd2b48cu},
    {"teal", 0x008080u},
    {"thistle", 0xd8bfd8u},
    {"tomato", 0xff6347u},
    {"turquoise", 0x40e0d0u},
    {"violet", 0xee82eeu},
    {"wheat", 0xf5deb3u},
    {"white", 0xffffffu},
    {"whitesmoke", 0xf5f5f5u},
    {"yellow", 0xffff00u},
    {"yellowgreen", 0x9acd32u},
};
constexpr size_t color_count = sizeof(colors) / sizeof(colors[0]);
static_assert(color_count == 148);

unsigned checks = 0, failures = 0;
constexpr unsigned diagnostic_limit = 24;
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        ++failures;
        if (failures <= diagnostic_limit)
            std::fprintf(stderr, "FAIL: %.180s\n", message.c_str());
    }
}
bool same(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}
bool same_color(const aether_color_rgba& a, const aether_color_rgba& b) {
    return same(a.r, b.r) && same(a.g, b.g) &&
           same(a.b, b.b) && same(a.a, b.a);
}
aether_color_rgba unpack(uint32_t rgb) {
    return {((rgb >> 16) & 255u) / 255.f,
            ((rgb >> 8) & 255u) / 255.f,
            (rgb & 255u) / 255.f, 1.f};
}
void expect(const std::string& input, bool valid, aether_color_rgba expected = {}) {
    aether_color_rgba actual{7, 8, 9, 10};
    const bool parsed = aether_color_rgba_try_parse(input.c_str(), &actual);
    check(parsed == valid, input + " accepted/rejected");
    // Separate channel checks catch all four sentinels and negative zero on failure.
    check(same(actual.r, expected.r), input + " exact red");
    check(same(actual.g, expected.g), input + " exact green");
    check(same(actual.b, expected.b), input + " exact blue");
    check(same(actual.a, expected.a), input + " exact alpha");
}
std::string uppercase(std::string input, bool alternating) {
    for (size_t i = 0; i < input.size(); ++i)
        if ((!alternating || i % 2 == 0) && input[i] >= 'a' && input[i] <= 'z')
            input[i] = static_cast<char>(input[i] - 'a' + 'A');
    return input;
}
} // namespace

int main() {
    // All checks remain live with NDEBUG; no assert-based test outcomes.
    check(color_count == 148, "fixture contains 148 opaque names");
    unsigned variants = 0;
    for (size_t i = 0; i < color_count; ++i) {
        const std::string name = colors[i].name;
        bool lowercase_ascii = !name.empty();
        for (unsigned char c : name)
            lowercase_ascii = lowercase_ascii && c >= 'a' && c <= 'z';
        check(lowercase_ascii, name + " lowercase ASCII fixture key");
        check(name != "transparent" && name != "currentcolor", name + " concrete opaque name");
        check(colors[i].rgb <= 0xffffffu, name + " 24-bit fixture RGB");
        if (i)
            check(std::strcmp(colors[i - 1].name, colors[i].name) < 0,
                  name + " strict ordering and uniqueness");
        const auto expected = unpack(colors[i].rgb);
        expect(name, true, expected);
        expect(uppercase(name, false), true, expected);
        expect(uppercase(name, true), true, expected);
        variants += 3;
    }
    check(variants == 444, "444 exhaustive named-color case variants");
    for (const char* input : {"transparent", "TRANSPARENT", "TrAnSpArEnT"})
        expect(input, true, {});

    // Aliases complement the normative byte checks above, not replace them.
    const char* aliases[][2] = {
        {"aqua", "cyan"}, {"fuchsia", "magenta"}, {"gray", "grey"},
        {"darkgray", "darkgrey"}, {"darkslategray", "darkslategrey"},
        {"dimgray", "dimgrey"}, {"lightgray", "lightgrey"},
        {"lightslategray", "lightslategrey"}, {"slategray", "slategrey"}
    };
    for (const auto& pair : aliases) {
        aether_color_rgba a{7, 8, 9, 10}, b{11, 12, 13, 14};
        check(aether_color_rgba_try_parse(pair[0], &a), std::string(pair[0]) + " alias accepted");
        check(aether_color_rgba_try_parse(pair[1], &b), std::string(pair[1]) + " alias accepted");
        check(same_color(a, b), std::string(pair[0]) + "/" + pair[1] + " alias equality");
    }
    // Explicit guards against similarly named but distinct colors.
    expect("green", true, unpack(0x008000));
    expect("lime", true, unpack(0x00ff00));
    expect("gray", true, unpack(0x808080));
    expect("silver", true, unpack(0xc0c0c0));

    for (const char* input : {
             "", " ", "\t", "madeupcolor", "rebeccapurpl", "rebeccapurple1",
             "xrebeccapurple", "redblue", "rebecca purple", "rebecca-purple",
             "'rebeccapurple'", "\"rebeccapurple\"", "\\72 ebeccapurple",
             "rebecca/**/purple", "/**/red", "red/**/", "red;", "red!",
             "lightslateblue", "violetred", "CanvasText", "ButtonText",
             "currentColor", "CURRENTCOLOR", "inherit", "initial", "unset",
             "revert", "revert-layer", "none", "transparent1",
             "\xc2\xa0red", "red\xc2\xa0", "r\xc3\xa9" "d",
             "\xc4\xb0NDIGO", "rebeccapurpl\xc3\xa9"})
        expect(input, false);

    // The direct API trims only initial space/tab, unlike runtime token trimming.
    for (const auto& item : {named_color{"red", 0xff0000},
                            named_color{"rebeccapurple", 0x663399}}) {
        const std::string name = item.name;
        for (const char* prefix : {" ", "\t", " \t\t "})
            expect(prefix + name, true, unpack(item.rgb));
        for (const char* suffix : {" ", "\t", "\n", "\r", "\f", "\v"})
            expect(name + suffix, false);
        for (const char* prefix : {"\n", "\r", "\f", "\v", " \t\n"})
            expect(prefix + name, false);
        for (unsigned byte : {0x80u, 0xffu}) {
            const std::string high(1, static_cast<char>(byte));
            expect(high + name, false);
            expect(name + high, false);
            expect(name.substr(0, 1) + high + name.substr(2), false);
        }
        for (size_t length : {size_t{127}, size_t{128}, size_t{256}})
            expect(name + std::string(length - name.size(), 'x'), false);
        // Initial trimming precedes the residual-length check.
        expect(std::string(128, ' ') + name, true, unpack(item.rgb));
    }

    const std::string rgb127 = "rgb(" + std::string(117, ' ') + "0,0,0)";
    const std::string rgb128 = "rgb(" + std::string(118, ' ') + "0,0,0)";
    check(rgb127.size() == 127 && rgb128.size() == 128, "RGB boundary fixture sizes");
    expect(rgb127, true, {0, 0, 0, 1});
    expect(rgb128, false);
    expect(" \t" + rgb127, true, {0, 0, 0, 1});
    expect(" \t" + rgb128, false);

    aether_color_rgba untouched{7, 8, 9, 10};
    check(!aether_color_rgba_try_parse(nullptr, &untouched), "null input rejected");
    check(same(untouched.r, 7), "null input preserves red");
    check(same(untouched.g, 8), "null input preserves green");
    check(same(untouched.b, 9), "null input preserves blue");
    check(same(untouched.a, 10), "null input preserves alpha");
    check(!aether_color_rgba_try_parse("red", nullptr), "old name/null output");
    check(!aether_color_rgba_try_parse("rebeccapurple", nullptr), "new name/null output");
    check(!aether_color_rgba_try_parse("transparent", nullptr), "transparent/null output");
    check(!aether_color_rgba_try_parse("invalid", nullptr), "invalid/null output");
    check(!aether_color_rgba_try_parse(nullptr, nullptr), "both null");
    if (failures > diagnostic_limit)
        std::fprintf(stderr, "%u additional failures suppressed\n", failures - diagnostic_limit);
    std::printf("named_colors: %u checks, %u failures; %u opaque case variants\n",
                checks, failures, variants);
    return failures ? 1 : 0;
}

