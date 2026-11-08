#include "../src/css/background_layers.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

// Expected identities are literal bytes, never produced by the decoder under test.
static void expect_url(const std::string& css, const char* identity) {
    aether_background_layer_list result{};
    require(aether_background_image_list_try_parse(css, &result), css.c_str());
    require(result.count == 1 && result.items &&
        result.items[0].kind == AETHER_BACKGROUND_LAYER_IMAGE_URL && result.items[0].image_source,
        "URL result must contain an owned image source");
    require(std::strcmp(result.items[0].image_source, identity) == 0, css.c_str());
    aether_background_layer_list_free(&result);
}

static void escaped_urls() {
    struct example { const char* css; const char* identity; };
    const example cases[] = {
        {"url(hero.png)", "hero.png"}, {"url(caf\xc3\xa9.png)", "caf\xc3\xa9.png"},
        {"url(hero%2Epng)", "hero%2Epng"}, {"URL('Hero/../A%2Epng')", "Hero/../A%2Epng"},
        {R"(url(hero\2e png))", "hero.png"}, {R"(url('hero\2e png'))", "hero.png"},
        {R"(url(hero\.png))", "hero.png"}, {R"(url("hero\.png"))", "hero.png"},
        {R"(url(\7x))", "\x07x"},
        {R"(url(\2e))", "."}, {R"(url(\00002e))", "."},
        {R"(url(\00002e7))", ".7"}, {R"(url(\2E))", "."},
        {R"(url(\e9.png))", "\xc3\xa9.png"},
        {R"(url(\1f642.png))", "\xf0\x9f\x99\x82.png"},
        {R"(url(\0.png))", "\xef\xbf\xbd.png"},
        {R"(url(\d800.png))", "\xef\xbf\xbd.png"},
        {R"(url(\dfff.png))", "\xef\xbf\xbd.png"},
        {R"(url(\110000.png))", "\xef\xbf\xbd.png"},
        {R"(url(\10ffff.png))", "\xf4\x8f\xbf\xbf.png"},
        {R"(url(a\)\(\,\/\'\"b))", "a)(,/'\"b"},
        {R"(url('a\'\"\)\(\,\/b'))", "a'\")(,/b"},
        {R"(url(a\29\28\2c\2f\22 b))", "a)(,/\"b"},
        {R"(url(a\ ))", "a "}, {R"(url('a\20 '))", "a "},
        {R"(url(\5c 2e))", "\\2e"}, {R"(url('\\2e'))", "\\2e"},
        {R"(url('\2f*literal*\2f'))", "/*literal*/"},
        {R"(url('/*literal*/'))", "/*literal*/"},
        {R"(url('a\2e  b'))", "a. b"},
        {"url(  hero.png \t)", "hero.png"},
    };
    for (const auto& item : cases) expect_url(item.css, item.identity);
    for (const std::string separator : {" ", "\t", "\n", "\r", "\r\n", "\f"}) {
        expect_url("url(hero\\2e" + separator + "png)", "hero.png");
        expect_url("url('hero\\2e" + separator + "png')", "hero.png");
    }
    for (const std::string line : {"\n", "\r", "\r\n", "\f"})
        expect_url("url('he\\" + line + "ro.png')", "hero.png");

    const char* malformed[] = {
        "url()", "url('')", "url(\"\")", "url('", "url(a", "url('a')junk",
        "url(a b)", "url(a(b)", "url('a\nb')", "url('a\rb')", "url('a\fb')",
        "url('a' 'b')", "url(a\x01" "b)", "url(a\x7f" "b)",
        R"(url('a\'))", R"(url(a\))", R"(url(a\2e  b))",
        "url(he\\\nro.png)", "url(he\\\rro.png)", "url(he\\\r\nro.png)", "url(he\\\fro.png)",
        "url(\xc0\xaf)", "url('\xed\xa0\x80')", "url('\xf4\x90\x80\x80')",
    };
    for (const auto* css : malformed) {
        aether_background_layer_list result{};
        require(!aether_background_image_list_try_parse(css, &result), css);
        require(result.count == 0 && !result.items, "malformed URL must not publish layers");
    }
    for (const std::string css : {std::string("url(a\0b)", 8), std::string("url('a\0b')", 10)}) {
        aether_background_layer_list result{};
        require(!aether_background_image_list_try_parse(css, &result) && !result.items && !result.count,
            "literal embedded NUL must not enter a C-string resource identity");
    }
    aether_background_shorthand_s shorthand{};
    require(aether_background_shorthand_try_parse(R"(url('hero\2e png') center / cover no-repeat)", shorthand),
        "escaped shorthand parses");
    require(shorthand.image == R"(url('hero\2e png'))", "shorthand retains CSS spelling, not decoded identity");
    expect_url(shorthand.image, "hero.png");
    aether_background_layer_list mixed{};
    require(aether_background_image_list_try_parse(
        R"(url(hero\2e png), linear-gradient(90deg, red, blue), url(other\.png))", &mixed),
        "escaped URL and gradient list parses");
    require(mixed.count == 3 && mixed.items && mixed.items[0].image_source && mixed.items[2].image_source &&
        mixed.items[1].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT &&
        std::strcmp(mixed.items[0].image_source, "hero.png") == 0 &&
        std::strcmp(mixed.items[2].image_source, "other.png") == 0, "mixed list preserves order and identities");
    const auto* old_items = mixed.items;
    require(!aether_background_image_list_try_parse("url(new.png), url('')", &mixed) &&
        mixed.items == old_items && mixed.count == 3 && mixed.items[0].image_source &&
        std::strcmp(mixed.items[0].image_source, "hero.png") == 0,
        "failed replacement preserves the previously owned complete list");
    aether_background_layer_list_free(&mixed);
    require(!aether_background_image_list_try_parse("url(hero.png), url('')", &mixed) && !mixed.items && !mixed.count,
        "a malformed later layer cannot publish an earlier valid layer");
}

static void contextual_shorthand() {
    aether_background_shorthand_s result{};
    for (const char* color : {"currentColor", "ReD", "transparent"}) {
        const std::string value = color;
        require(aether_background_shorthand_try_parse("url(asset://x) center / 10px " + value, result),
            "single size followed by final color");
        require(result.size == "10px" && result.has_color && result.color == value,
            "single size must not consume the color");
        require(aether_background_shorthand_try_parse("url(asset://x) center / 10px 20px " + value, result),
            "two sizes followed by final color");
        require(result.size == "10px 20px" && result.color == value, "two sizes are retained");
        require(aether_background_shorthand_try_parse("none " + value, result) &&
            result.image == "none" && result.color == value, "explicit none and final color");
    }
    require(aether_background_shorthand_try_parse("none", result) && !result.has_color && result.image == "none",
        "none resets omitted color");
    require(aether_background_shorthand_try_parse("CuRrEnTcOlOr", result) && result.color == "CuRrEnTcOlOr",
        "standalone symbol preserved for longhand");
    require(aether_background_shorthand_try_parse(
        "url(asset://x), linear-gradient(red,blue) center / cover no-repeat currentColor", result) &&
        result.has_color && result.color == "currentColor" && result.size == "auto auto, cover",
        "final contextual color with two image layers");
    for (const char* css : {"none none", "none url(asset://x)", "url(asset://x) none",
            "url(asset://x) url(asset://y)", "currentColor red", "red currentColor",
            "url(asset://x) currentColor,url(asset://y)", "none center / 10px nonsense",
            "none center / 10px 20px 30px currentColor", "none center / currentColor",
            "linear-gradient(red,currentColor)"})
        require(!aether_background_shorthand_try_parse(css, result), css);
    aether_background_layer_list layers{};
    require(!aether_background_image_list_try_parse("currentColor", &layers), "symbol is not an image");
}

// Literal spellings and enum expectations are independent of the shorthand.
static void contiguous_repeat_units() {
    const std::string g = "linear-gradient(red,blue)";
    struct keyword { const char* text; int x, y; };
    const keyword basic[] = {
        {"repeat", AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_REPEAT},
        {"no-repeat", AETHER_BACKGROUND_NO_REPEAT, AETHER_BACKGROUND_NO_REPEAT},
        {"space", AETHER_BACKGROUND_SPACE, AETHER_BACKGROUND_SPACE},
        {"round", AETHER_BACKGROUND_ROUND, AETHER_BACKGROUND_ROUND}
    };
    const auto accepted = [&](const std::string& source, const std::string& spelling, int x, int y,
                              const char* position, const char* size) {
        aether_background_shorthand_s out{};
        require(aether_background_shorthand_try_parse(source, out), source.c_str());
        require(out.image == g && out.repeat == spelling && out.position == position && out.size == size,
                "repeat shorthand retains complete component spellings");
        std::vector<aether_background_repeat_s> repeats;
        require(aether_background_repeat_list_try_parse(out.repeat, repeats) && repeats.size() == 1,
                "repeat unit decodes into one pair");
        require(repeats[0].x == x && repeats[0].y == y, "independent literal repeat axes");
    };
    for (const auto& x : basic) for (const auto& y : basic) {
        const auto pair = std::string(x.text) + " " + y.text;
        accepted(g + " " + pair + " center / cover", pair, x.x, y.x, "center", "cover");
        accepted(g + " center / cover " + pair, pair, x.x, y.x, "center", "cover");
        std::vector<aether_background_repeat_s> longhand;
        require(aether_background_repeat_list_try_parse(pair, longhand) && longhand.size() == 1 &&
                longhand[0].x == x.x && longhand[0].y == y.x, "all sixteen longhand pairs remain valid");
    }
    const keyword singles[] = {
        basic[0], basic[1], basic[2], basic[3],
        {"repeat-x", AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT},
        {"repeat-y", AETHER_BACKGROUND_NO_REPEAT, AETHER_BACKGROUND_REPEAT}
    };
    for (const auto& k : singles) {
        accepted(g + " " + k.text + " left top / 20px 30px", k.text, k.x, k.y, "left top", "20px 30px");
        accepted(g + " left top / 20px 30px " + k.text, k.text, k.x, k.y, "left top", "20px 30px");
    }
    for (const auto& source : {g + " repeat no-repeat center", g + " center repeat no-repeat"})
        accepted(source, "repeat no-repeat", AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT,
                 "center", "auto auto");
    for (const auto& source : {g + " space round left top / 20px 30px", g + " left top / 20px 30px space round"})
        accepted(source, "space round", AETHER_BACKGROUND_SPACE, AETHER_BACKGROUND_ROUND, "left top", "20px 30px");
    struct size_case { const char* text; int kind; aether_css_unit_t xunit, yunit; float x, y; };
    const size_case sizes[] = {
        {"cover", AETHER_BACKGROUND_SIZE_COVER, AETHER_CSS_UNIT_AUTO, AETHER_CSS_UNIT_AUTO, 0, 0},
        {"contain", AETHER_BACKGROUND_SIZE_CONTAIN, AETHER_CSS_UNIT_AUTO, AETHER_CSS_UNIT_AUTO, 0, 0},
        {"20px", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_PX, AETHER_CSS_UNIT_AUTO, 20, 0},
        {"auto", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_AUTO, AETHER_CSS_UNIT_AUTO, 0, 0},
        {"auto 30px", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_AUTO, AETHER_CSS_UNIT_PX, 0, 30},
        {"20px auto", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_PX, AETHER_CSS_UNIT_AUTO, 20, 0},
        {"20px 30px", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_PX, AETHER_CSS_UNIT_PX, 20, 30},
        {"calc(10px + 2px) 30px", AETHER_BACKGROUND_SIZE_EXPLICIT, AETHER_CSS_UNIT_LINEAR, AETHER_CSS_UNIT_PX, 12, 30}
    };
    for (const auto& s : sizes) {
        accepted(g + " center / " + s.text + " repeat no-repeat", "repeat no-repeat",
                 AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT, "center", s.text);
        std::vector<aether_background_size_s> parsed;
        require(aether_background_size_list_try_parse(s.text, parsed) && parsed.size() == 1, "size independently decodes");
        require(parsed[0].kind == s.kind && parsed[0].x.unit == s.xunit && parsed[0].y.unit == s.yunit &&
                parsed[0].x.value == s.x && parsed[0].y.value == s.y, "size values remain literal and auto is not swallowed");
        if (s.xunit == AETHER_CSS_UNIT_LINEAR)
            require(parsed[0].x.linear_percent == 0 && parsed[0].x.linear_em == 0 && parsed[0].x.linear_rem == 0 &&
                    parsed[0].x.linear_vw == 0 && parsed[0].x.linear_vh == 0, "calc has only its twelve pixel coefficient");
    }
    for (const char* trivia : {"/**/", "\t\n", "/* , / */"}) {
        accepted(g + " center / cover RePeAt" + trivia + "No-RePeAt", "RePeAt No-RePeAt",
                 AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT, "center", "cover");
        accepted(g + " RePeAt" + trivia + "No-RePeAt center / cover", "RePeAt No-RePeAt",
                 AETHER_BACKGROUND_REPEAT, AETHER_BACKGROUND_NO_REPEAT, "center", "cover");
    }
    for (const char* suffix : {"red repeat no-repeat", "repeat no-repeat currentColor"}) {
        aether_background_shorthand_s out{};
        require(aether_background_shorthand_try_parse(g + " center / 20px " + suffix, out), "color adjacent to repeat pair");
        require(out.size == "20px" && out.repeat == "repeat no-repeat" && out.has_color &&
                out.color == (suffix[0] == 'r' && suffix[1] == 'e' && suffix[2] == 'd' ? "red" : "currentColor"),
                "color is not consumed as size or repeat");
    }
    std::vector<std::string> bad;
    for (const char* repeat : {"repeat no-repeat round", "repeat-x no-repeat", "repeat no-repeat-x",
                              "repeat repeat-x", "repeat-x repeat-y", "repeat-x repeat-x", "repeat no-repeat repeat"}) {
        bad.push_back(g + " " + repeat + " center / cover");
        bad.push_back(g + " center / cover " + repeat);
    }
    for (const auto& source : {
            g + " repeat center no-repeat", "repeat " + g + " no-repeat center",
            g + " repeat red no-repeat center", g + " repeat center / cover no-repeat",
            g + " center / cover repeat red no-repeat", g + " repeat no-repeat center / cover round",
            g + " center /", g + " center / 10px 20px 30px repeat no-repeat",
            g + " center / cover / repeat no-repeat", g + " center / cover repeat no-repeat garbage",
            g + " red, " + g, g + " " + g, g + " red blue",
            g + " center / cover repeat no-repeat red, " + g}) bad.push_back(source);
    for (const auto& source : bad) {
        aether_background_shorthand_s out{};
        require(!aether_background_shorthand_try_parse(source, out), source.c_str());
        require(out.image.empty() && out.position.empty() && out.size.empty() && out.repeat.empty() &&
                out.color.empty() && !out.has_color, "failed shorthand leaves canonical empty output, including staged color");
    }
}

int main() {
    contiguous_repeat_units();
    contextual_shorthand();
    escaped_urls();
    aether_background_layer_list layers{};
    require(aether_background_image_list_try_parse(
        "url('asset://mountain'), linear-gradient(90deg, red, blue)", &layers),
        "image and gradient list should parse");
    require(layers.count == 2 && layers.items[0].kind == AETHER_BACKGROUND_LAYER_IMAGE_URL &&
                layers.items[1].kind == AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT,
        "image list must retain declaration order and kinds");
    require(std::strcmp(layers.items[0].image_source, "asset://mountain") == 0,
        "url() must retain its unquoted UTF-8 source");
    require(std::fabs(layers.items[1].gradient.direction_x - 1.f) < .001f,
        "gradient layer must preserve its parsed direction");
    aether_background_layer_list_free(&layers);

    std::vector<aether_background_position_s> positions;
    require(aether_background_position_list_try_parse("center, right bottom", positions) && positions.size() == 2,
        "position list should parse center and keyword pairs");
    require(positions[0].x.value == 50.f && positions[0].y.value == 50.f &&
                positions[1].x.value == 100.f && positions[1].y.value == 100.f,
        "position keywords must retain their percentage semantics");

    std::vector<aether_background_size_s> sizes;
    require(aether_background_size_list_try_parse("cover, 20px auto", sizes) && sizes.size() == 2,
        "size list should parse cover and explicit dimensions");
    require(sizes[0].kind == AETHER_BACKGROUND_SIZE_COVER && sizes[1].kind == AETHER_BACKGROUND_SIZE_EXPLICIT &&
                sizes[1].x.value == 20.f && sizes[1].y.unit == AETHER_CSS_UNIT_AUTO,
        "background-size must preserve cover and auto dimensions");

    std::vector<aether_background_repeat_s> repeats;
    require(aether_background_repeat_list_try_parse("no-repeat, repeat-x", repeats) && repeats.size() == 2,
        "repeat list should parse axis shorthands");
    require(repeats[0].x == AETHER_BACKGROUND_NO_REPEAT && repeats[0].y == AETHER_BACKGROUND_NO_REPEAT &&
                repeats[1].x == AETHER_BACKGROUND_REPEAT && repeats[1].y == AETHER_BACKGROUND_NO_REPEAT,
        "repeat-x must expand to its two-axis semantic values");

    aether_background_shorthand_s shorthand{};
    require(aether_background_shorthand_try_parse("url('hero.png') center / cover no-repeat", shorthand),
        "single image background shorthand should parse");
    require(aether_background_shorthand_try_parse("linear-gradient(90deg, red, blue) left top / 20px auto repeat-x red", shorthand),
        "single gradient background shorthand should parse");
    require(aether_background_shorthand_try_parse(
        "url('hero.png') center / cover no-repeat, linear-gradient(90deg, red, blue) left top / 20px auto repeat-x red",
        shorthand), "background shorthand should decompose into longhands");
    require(shorthand.image == "url('hero.png'), linear-gradient(90deg, red, blue)" &&
                shorthand.position == "center, left top" && shorthand.size == "cover, 20px auto" &&
                shorthand.repeat == "no-repeat, repeat-x" && shorthand.has_color && shorthand.color == "red",
        "background shorthand must preserve each layer's independent values");

    require(!aether_background_image_list_try_parse("url(", &layers),
        "malformed url() must be rejected instead of becoming a resource reference");
    return 0;
}
