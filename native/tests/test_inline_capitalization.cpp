// Public ABI regression: no private headers or helper substitutions.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_style.h>
#include <algorithm>
#include <cmath>
#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
static unsigned checks = 0, failures = 0, fixtures = 0;
static std::string scenario;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(x)) {                                                                                \
            ++failures;                                                                            \
            std::fprintf(stderr, "FAIL [%s] line %d: %s\n", scenario.c_str(), __LINE__, #x);       \
        }                                                                                          \
    } while (0)
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        const bool prerequisite = bool(x);                                                         \
        CHECK(prerequisite);                                                                       \
        if (!prerequisite)                                                                         \
            std::exit(2);                                                                          \
    } while (0)
static bool near(float a, float b) { return std::fabs(a - b) < .01f; }
struct StyledText {
    std::string text, family;
    int weight;
    aether_color_rgba color;
};
struct Meter {
    std::vector<StyledText> styled;
    std::function<void()> nested;
    bool entered = false;
    bool scale = false;
    unsigned contextual_calls = 0;
    std::vector<std::string> calls;
    static aether_text_metrics_t measure(const char *text, const aether_computed_style_t *style,
                                         float, void *ctx) {
        auto &m = *static_cast<Meter *>(ctx);
        m.calls.emplace_back(text);
        m.styled.push_back({text, style->font_family, style->font_weight, style->color});
        if (m.nested && !m.entered) {
            m.entered = true;
            m.nested();
        }
        float width = 0;
        for (unsigned char c : std::string(text))
            width += (c >= 'A' && c <= 'Z') ? 13 : (c >= 'a' && c <= 'z') ? 7 : c == ' ' ? 3 : 5;
        return {width * (m.scale && style->font_weight == 700 ? 2 : 1), 20, 15};
    }
    static aether_text_metrics_t contextual(const char *text, const aether_computed_style_t *style,
                                            float, float width, void *ctx) {
        ++static_cast<Meter *>(ctx)->contextual_calls;
        return measure(text, style, width, ctx);
    }
};
struct Run {
    std::string text;
    aether_node_t owner;
    aether_rectf rect;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_layout_result_t result{};
    Meter meter;
    bool revision_mode = false;
    std::vector<std::pair<aether_node_t, std::string>> source_nodes;
    void snapshot(aether_node_t n) {
        if (aether_node_get_type(n) == AETHER_NODE_TEXT)
            source_nodes.push_back({n, dom(n)});
        for (int i = 0; i < aether_node_get_child_count(n); ++i)
            snapshot(aether_node_get_child(n, i));
    }
    void verify_sources() {
        for (auto &entry : source_nodes)
            CHECK(dom(entry.first) == entry.second);
    }
    Fixture(std::string content, std::string css = "", bool revision = false)
        : revision_mode(revision) {
        ++fixtures;
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(
            aether_doc_parse_html(
                doc, ("<html><body><div id='host'>" + content + "</div></body></html>").c_str()) ==
            AETHER_OK);
        snapshot(aether_doc_get_document_element(doc));
        aether_style_environment_t env{640, 480, 1, 16, 0, 0, 0};
        REQUIRE(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) ==
                AETHER_OK);
        int64_t id{};
        std::string rules = "html,body,div{display:block;margin:0;padding:0;border:0}#host{width:"
                            "200px;font-size:16px;line-height:20px;text-transform:capitalize}" +
                            css;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, rules.c_str(), 0, "capitalization",
                                                    &id, nullptr) == AETHER_OK);
        resolve();
        REQUIRE(aether_layout_create(Meter::measure, &meter, &engine) == AETHER_OK);
        if (revision_mode)
            aether_layout_set_contextual_text_measurer(engine, Meter::contextual, &meter);
        layout();
    }
    ~Fixture() {
        verify_sources();
        aether_layout_result_destroy(result);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    aether_node_t node(const char *id = "host") { return aether_doc_get_element_by_id(doc, id); }
    void resolve() {
        aether_layout_result_destroy(result);
        result = nullptr;
        aether_style_map_destroy(map);
        map = nullptr;
        if (revision_mode) {
            aether_style_transaction_t tx{};
            aether_style_revision_t rev{};
            REQUIRE(aether_style_runtime_prepare_revision(
                        runtime, aether_doc_get_document_element(doc), nullptr, &tx) == AETHER_OK);
            REQUIRE(aether_style_runtime_commit_revision(runtime, tx, &rev) == AETHER_OK);
            aether_style_transaction_destroy(tx);
            REQUIRE(aether_style_revision_create_map(rev, &map) == AETHER_OK);
            aether_style_revision_destroy(rev);
        } else {
            aether_style_update_t *updates{};
            int n = aether_style_runtime_resolve_subtree(
                runtime, aether_doc_get_document_element(doc), &updates);
            REQUIRE(n >= 0);
            aether_style_updates_free(updates, n);
            REQUIRE(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) ==
                    AETHER_OK);
        }
    }

    void layout() {
        aether_layout_result_destroy(result);
        result = nullptr;
        meter.calls.clear();
        meter.styled.clear();
        REQUIRE(aether_layout_layout(engine, doc, map, 640, 480, nullptr, nullptr, &result) ==
                AETHER_OK);
        verify_sources();
    }
    static void collect(aether_layout_box_t b, std::vector<Run> &runs) {
        char *t = aether_layout_box_get_text(b);
        if (t && *t)
            runs.push_back(
                {t, aether_layout_box_get_element(b), aether_layout_box_get_border_box(b)});
        aether_string_free(t);
        for (int i = 0; i < aether_layout_box_get_child_count(b); ++i)
            collect(aether_layout_box_get_child(b, i), runs);
    }
    std::vector<Run> runs() {
        std::vector<Run> r;
        auto b = aether_layout_result_find_primary_box(result, node());
        REQUIRE(b);
        collect(b, r);
        return r;
    }
    std::vector<StyledText> paints() {
        aether_display_list_builder_t builder{};
        aether_display_list_t list{};
        REQUIRE(aether_display_list_builder_create(Meter::measure, &meter, &builder) == AETHER_OK);
        if (revision_mode)
            aether_display_list_builder_set_contextual_text_measurer(builder, Meter::contextual,
                                                                     &meter);
        REQUIRE(aether_display_list_build(builder, result, nullptr, &list) == AETHER_OK);
        std::vector<StyledText> out;
        for (int i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t c{};
            REQUIRE(aether_display_list_get_command(list, i, &c) == AETHER_OK);
            if (c.tag == AETHER_PAINT_DRAW_TEXT) {
                auto &t = c.data.draw_text;
                out.push_back({t.text, t.font_family, t.font_weight, t.color});
            }
            aether_paint_command_free(&c);
        }
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        return out;
    }
    std::string paint() {
        std::string s;
        for (auto &p : paints())
            s += p.text;
        return s;
    }

    static std::string dom(aether_node_t n) {
        std::string s;
        if (aether_node_get_type(n) == AETHER_NODE_TEXT) {
            char *t = aether_node_get_text_content(n);
            if (t)
                s = t;
            aether_string_free(t);
        }
        for (int i = 0; i < aether_node_get_child_count(n); ++i)
            s += dom(aether_node_get_child(n, i));
        return s;
    }
};
static void test(std::string name, std::string html, std::string expected, float width,
                 std::string css = "") {
    scenario = name;
    Fixture f(html, css);
    auto runs = f.runs();
    std::string stored;
    float w = 0;
    for (auto &r : runs) {
        stored += r.text;
        w += r.rect.width;
        std::printf("%s run=%s width=%.0f owner=%p x=%.0f y=%.0f\n", name.c_str(), r.text.c_str(),
                    r.rect.width, (void *)r.owner, r.rect.x, r.rect.y);
    }
    auto painted = f.paint();
    auto source = Fixture::dom(f.node());
    for (auto &c : f.meter.calls)
        std::printf("%s measured=%s\n", name.c_str(), c.c_str());
    std::printf("%s DOM=%s stored=%s painted=%s width=%.0f\n", name.c_str(), source.c_str(),
                stored.c_str(), painted.c_str(), w);
    CHECK(stored == expected);
    CHECK(painted == expected);
    CHECK(near(w, width));
    CHECK(source == "hello");
    if (name == "split") {
        CHECK(runs.size() == 2);
        if (runs.size() == 2) {
            CHECK(runs[0].owner == f.node());
            CHECK(runs[1].owner == f.node("suffix"));
            CHECK(near(runs[0].rect.x + runs[0].rect.width, runs[1].rect.x));
        }
    }
}
static std::string joined(const std::vector<Run> &runs) {
    std::string s;
    for (auto &r : runs)
        s += r.text;
    return s;
}
static std::string owned(Fixture &f, const char *id) {
    std::string s;
    for (auto &r : f.runs())
        if (r.owner == f.node(id))
            s += r.text;
    return s;
}
static bool seen(const Meter &m, const std::string &text) {
    return std::find(m.calls.begin(), m.calls.end(), text) != m.calls.end();
}
static void expect(Fixture &f, const std::string &text, const std::string &paint) {
    const auto source = Fixture::dom(f.node());
    CHECK(joined(f.runs()) == text);
    CHECK(f.paint() == paint);
    CHECK(Fixture::dom(f.node()) == source);
    for (auto &run : f.runs())
        if (run.text.find_first_of(" \t\r\n") == std::string::npos)
            CHECK(seen(f.meter, run.text));
}
static void simple(const char *name, const std::string &html, const std::string &expected,
                   const std::string &css = "") {
    scenario = name;
    Fixture f(html, css);
    expect(f, expected, expected);
}
static void continuity() {
    simple("comment", "hel<!-- ignored --><span>lo</span>", "Hello");
    // Public mutation exposes append/remove only; parsing omits empty text nodes.
    // Empty element coverage below does not claim genuine empty text-node coverage.
    simple("nested and empty",
           "<span></span>h<span>e<span></span><b>l</b></span><span>lo</span><span></span>",
           "Hello");
    simple("none prefix", "<span style='text-transform:none'>hel</span><span>lo</span>", "hello");
    simple("uppercase prefix", "<span style='text-transform:uppercase'>hel</span><span>lo</span>",
           "HELlo");
    simple("lowercase prefix", "<span style='text-transform:lowercase'>HEL</span><span>lo</span>",
           "hello");
    simple("none suffix", "hel<span style='text-transform:none'>lo</span>", "Hello");
    simple("uppercase suffix", "hel<span style='text-transform:uppercase'>lo</span>", "HelLO");
    simple("lowercase suffix", "hel<span style='text-transform:lowercase'>LO</span>", "Hello");
    simple("repeated modes",
           "<span style='text-transform:none'>he</span><span "
           "style='text-transform:uppercase'>l</span><span>lo</span><span "
           "style='text-transform:lowercase'>WORLD</span><span>end</span>",
           "heLloworldend");
    simple("collected space", "<span>hel </span><span>lo</span>", "Hel Lo");
    simple("direct separator retained", "<span>hel</span> <span>lo</span>",
           "Hel Lo"); // The admitted stream retains whitespace-only source nodes.
    simple("collected VT", "<span>hel\v</span><span>lo</span>", "Hel Lo");
    simple("collected FF", "<span>hel\f</span><span>lo</span>", "Hel Lo");
    simple("omitted VT", "<span>hel</span>\v<span>lo</span>", "HelLo");
    simple("omitted FF", "<span>hel</span>\f<span>lo</span>", "HelLo");
    simple("high byte continuation", "<span>hel\xC3\x89</span><span>lo</span>", "Hel\xC3\x89lo");
    simple("none subtree", "hel<span style='display:none'>ignored </span><span>lo</span>", "Hello");
    simple("relative continuity", "hel<span style='position:relative'>lo</span>", "Hello");
    for (const char *ws : {"normal", "nowrap", "pre", "pre-wrap", "pre-line"}) {
        scenario = std::string("whitespace ") + ws;
        Fixture f("<span>hel\t</span><span id='suffix'>lo</span>",
                  std::string("#host{white-space:") + ws + "}");
        CHECK(owned(f, "suffix") == "Lo");
        CHECK(seen(f.meter, "Lo"));
    }
    for (const char *newline : {"\n", "\r\n", "\r"}) {
        scenario = "preserved newline";
        Fixture f(std::string("<span>hel") + newline + "</span><span id='suffix'>lo</span>",
                  "#host{white-space:pre}");
        auto r = f.runs();
        CHECK(owned(f, "suffix") == "Lo");
        REQUIRE(r.size() >= 2);
        CHECK(r.back().rect.y > r.front().rect.y);
    }
    scenario = "forced break";
    Fixture br("hel<br><span>lo</span>");
    auto b = br.runs();
    expect(br, "HelLo", "HelLo");
    REQUIRE(b.size() == 2);
    CHECK(b[1].rect.y > b[0].rect.y);
    scenario = "indivisible overflow word retains lowercase";
    Fixture wrap("hel<span>lo</span>", "#host{width:30px}");
    auto w = wrap.runs();
    expect(wrap, "Hello", "Hello");
    REQUIRE(w.size() == 2);
    CHECK(near(w[1].rect.y, w[0].rect.y));
    CHECK(near(w[1].rect.x - w[0].rect.x, 27));
    CHECK(near(w[0].rect.width + w[1].rect.width, 41));
    scenario = "44px corrected height";
    Fixture fit("hel<span>lo</span>", "#host{width:44px}");
    auto r = fit.runs();
    REQUIRE(r.size() == 2);
    CHECK(near(r[0].rect.y, r[1].rect.y));
    CHECK(near(
        aether_layout_box_get_height(aether_layout_result_find_primary_box(fit.result, fit.node())),
        20));
    CHECK(seen(fit.meter, "lo"));
    CHECK(!seen(fit.meter, "Lo"));
}
static void provenance(bool revision) {
    scenario = revision ? "revision styled provenance" : "resolved styled provenance";
    Fixture f("hel<span id='suffix'>lo</span>",
              "#host{font-family:Prefix;font-weight:400;color:#112233}#suffix{font-family:Suffix;"
              "font-weight:700;color:#aabbcc}",
              revision);
    auto before = Fixture::dom(f.node());
    auto r = f.runs();
    REQUIRE(r.size() == 2);
    CHECK(r[0].owner == f.node());
    CHECK(r[1].owner == f.node("suffix"));
    expect(f, "Hello", "Hello");
    CHECK(near(r[0].rect.width, 27));
    CHECK(near(r[1].rect.width, 14));
    auto style = [&](aether_node_t n) {
        auto h = aether_style_map_get(f.map, n);
        REQUIRE(h);
        aether_computed_style_t s{};
        REQUIRE(aether_computed_style_copy(h, &s) == AETHER_OK);
        aether_computed_style_handle_destroy(h);
        return s;
    };
    auto a = style(f.node()), b = style(f.node("suffix"));
    auto verify = [&](const StyledText &t) {
        const auto &s = t.text == "Hel" ? a : b;
        CHECK(t.text == "Hel" || t.text == "lo");
        CHECK(t.family == s.font_family);
        CHECK(t.weight == s.font_weight);
        CHECK(near(t.color.r, s.color.r));
        CHECK(near(t.color.g, s.color.g));
        CHECK(near(t.color.b, s.color.b));
    };
    for (auto &t : f.meter.styled)
        verify(t);
    CHECK(seen(f.meter, "Hel"));
    CHECK(seen(f.meter, "lo"));
    auto paint = f.paints();
    REQUIRE(paint.size() == 2);
    for (auto &t : paint)
        verify(t);
    f.meter.scale = true;
    f.layout();
    r = f.runs();
    REQUIRE(r.size() == 2);
    CHECK(near(r[0].rect.width, 27));
    CHECK(near(r[1].rect.width, 28));
    CHECK(Fixture::dom(f.node()) == before);
    if (revision)
        CHECK(f.meter.contextual_calls > 0);
}
// Family expectations are independent semantic bytes, never values copied from
// another consumer. In particular, the emitted backslash is not a second escape.
template <size_t N>
static void family_bytes(const char (&actual)[N], const std::string &expected) {
    CHECK(N == 64);
    REQUIRE(expected.size() < N);
    CHECK(std::memcmp(actual, expected.data(), expected.size()) == 0);
    CHECK(actual[expected.size()] == '\0');
    CHECK(actual[N - 1] == '\0');
}
static void escaped_family_consumers(bool revision) {
    struct Case { const char *css; std::string bytes; };
    const Case cases[] = {
        {R"('\5c 42')", std::string("\x5c" "42", 3)},
        {R"('\e9')", std::string("\xc3\xa9", 2)},
        {R"('\1f642')", std::string("\xf0\x9f\x99\x82", 4)},
    };
    for (const auto &c : cases)
        for (bool gradient : {false, true}) {
            scenario = std::string(revision ? "revision" : "resolved") +
                       " escaped family " + c.css + (gradient ? " gradient" : " text");
            Fixture f("<span id='suffix'>hello</span>",
                      std::string("#host{font-family:") + c.css +
                          "}#suffix{text-shadow:2px 3px 1px #112233;" +
                          (gradient ? "background:linear-gradient(red,blue);background-clip:text;"
                                    : "") + "}", revision);
            // The leaf inherits the already-decoded value from the parent.
            for (const char *id : {"host", "suffix"}) {
                auto handle = aether_style_map_get(f.map, f.node(id));
                REQUIRE(handle);
                aether_computed_style_t copy{};
                REQUIRE(aether_computed_style_copy(handle, &copy) == AETHER_OK);
                family_bytes(copy.font_family, c.bytes);
                aether_computed_style_free(&copy);
                aether_computed_style_handle_destroy(handle);
            }
            bool measured = false;
            for (const auto &t : f.meter.styled)
                if (t.text == "Hello") {
                    measured = true;
                    CHECK(t.family == c.bytes);
                }
            CHECK(measured);
            CHECK(owned(f, "suffix") == "Hello");
            CHECK(Fixture::dom(f.node()) == "hello");
            if (revision)
                CHECK(f.meter.contextual_calls > 0);

            aether_display_list_builder_t builder{};
            aether_display_list_t list{};
            REQUIRE(aether_display_list_builder_create(Meter::measure, &f.meter, &builder) ==
                    AETHER_OK);
            if (revision)
                aether_display_list_builder_set_contextual_text_measurer(
                    builder, Meter::contextual, &f.meter);
            REQUIRE(aether_display_list_build(builder, f.result, nullptr, &list) == AETHER_OK);
            unsigned text_count = 0, gradient_count = 0, shadow_count = 0;
            for (int i = 0; i < aether_display_list_get_count(list); ++i) {
                aether_paint_command_t command{};
                REQUIRE(aether_display_list_get_command(list, i, &command) == AETHER_OK);
                if (command.tag == AETHER_PAINT_DRAW_TEXT) {
                    const auto &t = command.data.draw_text;
                    REQUIRE(t.text);
                    CHECK(std::string(t.text) == "Hello");
                    family_bytes(t.font_family, c.bytes);
                    ++text_count;
                } else if (command.tag == AETHER_PAINT_DRAW_TEXT_GRADIENT) {
                    const auto &t = command.data.draw_text_gradient;
                    REQUIRE(t.text);
                    CHECK(std::string(t.text) == "Hello");
                    family_bytes(t.font_family, c.bytes);
                    ++gradient_count;
                } else if (command.tag == AETHER_PAINT_DRAW_TEXT_SHADOW) {
                    const auto &t = command.data.draw_text_shadow;
                    REQUIRE(t.text);
                    CHECK(std::string(t.text) == "Hello");
                    family_bytes(t.font_family, c.bytes);
                    ++shadow_count;
                }
                aether_paint_command_free(&command);
            }
            CHECK(gradient ? gradient_count > 0 : text_count > 0);
            CHECK(gradient ? text_count == 0 : gradient_count == 0);
            CHECK(shadow_count > 0);
            aether_display_list_destroy(list);
            aether_display_list_builder_destroy(builder);
        }
}
static void escaped_family_snapshots(bool revision) {
    scenario = revision ? "revision escaped family snapshots" : "resolved escaped family snapshots";
    const std::string old_bytes("A\x5c" "42", 4);
    const std::string new_bytes("\xc3\xa9\xf0\x9f\x99\x82", 6);
    aether_computed_style_handle_t old_handle{}, new_handle{}, cloned_handle{};
    aether_computed_style_t old_copy{}, new_copy{};
    {
        Fixture f("<span id='suffix'>hello</span>",
                  R"(#host{--family:'A\5c 42';font-family:var(--family)})", revision);
        old_handle = aether_style_map_get(f.map, f.node("suffix"));
        REQUIRE(old_handle);
        REQUIRE(aether_computed_style_copy(old_handle, &old_copy) == AETHER_OK);
        REQUIRE(aether_computed_style_handle_clone(old_handle, &cloned_handle) == AETHER_OK);
        REQUIRE(cloned_handle);
        family_bytes(old_copy.font_family, old_bytes);
        REQUIRE(aether_element_set_attribute(f.node(), "style",
                                             R"(--family:'\e9\1f642')") == AETHER_OK);
        f.resolve(); // Destroys the old map; retained public handles own their data.
        f.layout();
        new_handle = aether_style_map_get(f.map, f.node("suffix"));
        REQUIRE(new_handle);
        REQUIRE(aether_computed_style_copy(new_handle, &new_copy) == AETHER_OK);
        family_bytes(new_copy.font_family, new_bytes);
        family_bytes(old_copy.font_family, old_bytes);
        CHECK(owned(f, "suffix") == "Hello");
    } // Runtime, stylesheet, maps and document are gone before inspecting exports.
    const auto verify_handle = [&](aether_computed_style_handle_t handle,
                                   const std::string &bytes) {
        aether_computed_style_t copy{};
        REQUIRE(aether_computed_style_copy(handle, &copy) == AETHER_OK);
        family_bytes(copy.font_family, bytes);
        aether_computed_style_free(&copy);
    };
    verify_handle(old_handle, old_bytes);
    verify_handle(cloned_handle, old_bytes);
    verify_handle(new_handle, new_bytes);
    family_bytes(old_copy.font_family, old_bytes);
    family_bytes(new_copy.font_family, new_bytes);
    // Mutating an owned ABI copy cannot change either retained snapshot.
    old_copy.font_family[0] = 'X';
    verify_handle(old_handle, old_bytes);
    verify_handle(cloned_handle, old_bytes);
    verify_handle(new_handle, new_bytes);
    aether_computed_style_free(&old_copy);
    aether_computed_style_free(&new_copy);
    aether_computed_style_handle_destroy(old_handle);
    aether_computed_style_handle_destroy(cloned_handle);
    aether_computed_style_handle_destroy(new_handle);
}
static void boundaries() {
    for (const char *display : {"block", "flex", "table", "inline-block"})
        for (const char *content : {"", "end", "end "}) {
            scenario = std::string("boundary ") + display + " [" + content + "]";
            const std::string inside =
                std::string(display) == "table" && *content
                    ? std::string(
                          "<span style='display:table-row'><span style='display:table-cell'>") +
                          content + "</span></span>"
                    : content;
            Fixture f(std::string("hel<span id='boundary'>") + inside +
                          "</span><span id='suffix'>lo</span>",
                      std::string("#boundary{display:") + display + "}");
            CHECK(owned(f, "suffix") == "Lo");
            CHECK(seen(f.meter, "Lo"));
            if (*content)
                CHECK(seen(f.meter, "End"));
        }
    for (const char *position : {"absolute", "fixed"})
        for (bool nested : {false, true}) {
            scenario = std::string(nested ? "nested " : "direct ") + position;
            std::string html = "hel<span id='positioned'>other </span><span id='suffix'>lo</span>";
            if (nested)
                html = "<span>" + html + "</span>";
            Fixture f(html, std::string("#positioned{position:") + position + "}");
            CHECK(owned(f, "suffix") == "lo");
            CHECK(owned(f, "positioned").find("Other") != std::string::npos);
            CHECK(f.paint().find("Other") != std::string::npos);
            CHECK(seen(f.meter, "Other"));
        }
    scenario = "nested positioned break isolation";
    Fixture iso("<span>hel<span style='position:absolute'>other<br><i>end</i></span><span "
                "id='suffix'>lo</span></span>");
    CHECK(owned(iso, "suffix") == "lo");
    CHECK(iso.paint().find("Other") != std::string::npos);
    CHECK(iso.paint().find("End") != std::string::npos);
    scenario = "nested block bounded capitalization";
    Fixture nested(
        "<span>hel<span style='display:block'>other</span><span id='suffix'>lo</span></span>");
    CHECK(owned(nested, "suffix") == "Lo");
    CHECK(nested.paint().find("Other") != std::string::npos);
    for (const char *visibility : {"hidden", "collapse"}) {
        scenario = std::string("visibility ") + visibility;
        Fixture f("<span id='prefix'>hel</span><span id='suffix'>lo</span>",
                  std::string("#prefix{visibility:") + visibility + "}");
        expect(f, "Hello", "lo");
        CHECK(seen(f.meter, "Hel"));
        CHECK(owned(f, "suffix") == "lo");
    }
    for (const char *attr : {"value", "placeholder"}) {
        scenario = std::string("opaque input ") + attr;
        Fixture f(std::string("hel<input id='control' ") + attr +
                  "='inner word'><span id='suffix'>lo</span>");
        CHECK(owned(f, "suffix") == "Lo");
        CHECK(f.paint().find("Inner Word") != std::string::npos);
        CHECK(seen(f.meter, "Inner Word"));
    }
    scenario = "nonopaque inline button";
    Fixture button("hel<button id='control'>lo</button><span id='suffix'>end</span>",
                   "button{display:inline;padding:0;border:0}");
    CHECK(owned(button, "control") == "lo");
    CHECK(owned(button, "suffix") == "end");
    scenario = "independent real flex items";
    Fixture flex("<span id='one'>hel<b>lo</b></span><span id='two'>world</span>",
                 "#host{display:flex}");
    expect(flex, "HelloWorld", "HelloWorld");
    CHECK(owned(flex, "two") == "World");
    CHECK(seen(flex.meter, "lo"));
}
static void public_sizing_and_isolation() {
    scenario = "nested display none opaque control is transparent";
    Fixture hidden_control("<span>hel<input id='hidden-control' value='ignored '"
                           " style='display:none'><span id='suffix'>lo</span></span>");
    expect(hidden_control, "Hello", "Hello");
    CHECK(owned(hidden_control, "hidden-control").empty());
    CHECK(owned(hidden_control, "suffix") == "lo");
    CHECK(!seen(hidden_control.meter, "Ignored"));
    CHECK(!seen(hidden_control.meter, "Ignored "));

    for (const char *kind : {"inline-block", "block", "flex"}) {
        scenario = std::string("positioned nested boundary ") + kind;
        Fixture f("<span>hel<span style='position:absolute'>other<span "
                  "id='inner'>end</span></span><span id='suffix'>lo</span></span>",
                  std::string("#inner{display:") + kind + "}");
        CHECK(owned(f, "suffix") == "lo");
        CHECK(owned(f, "inner") == "End");
        CHECK(f.paint().find("Other") != std::string::npos);
        CHECK(f.paint().find("End") != std::string::npos);
    }
    scenario = "nested outflow noncapitalize output retained";
    Fixture plain(
        "<span>hel<span style='position:absolute'>other<span "
        "style='display:inline-block'>end</span></span><span id='suffix'>lo</span></span>",
        "#host{text-transform:none}");
    expect(plain, "helotherendlo", "helotherendlo");
    scenario = "auto inline block max-content";
    Fixture atomic("<span id='sized'>hel<b>lo</b></span>", "#sized{display:inline-block}");
    auto box = aether_layout_result_find_primary_box(atomic.result, atomic.node("sized"));
    REQUIRE(box);
    CHECK(near(aether_layout_box_get_width(box), 41));
    CHECK(seen(atomic.meter, "Hel"));
    CHECK(seen(atomic.meter, "lo"));
    scenario = "flex auto basis max-content";
    Fixture flex("<span id='sized'>hel<b>lo</b></span>",
                 "#host{display:flex}#sized{flex:0 0 auto}");
    box = aether_layout_result_find_primary_box(flex.result, flex.node("sized"));
    REQUIRE(box);
    CHECK(near(aether_layout_box_get_width(box), 41));
    CHECK(seen(flex.meter, "Hel"));
    CHECK(seen(flex.meter, "lo"));
    scenario = "flex constrained minimum contribution";
    Fixture minimum("<span id='sized'>hel<b>lo</b></span>",
                    "#host{display:flex;width:20px}#sized{flex:0 1 auto}");
    box = aether_layout_result_find_primary_box(minimum.result, minimum.node("sized"));
    REQUIRE(box);
    CHECK(near(aether_layout_box_get_width(box), 41));
    CHECK(seen(minimum.meter, "Hel"));
    CHECK(seen(minimum.meter, "lo"));
}

static void generated() {
    for (const char *ending : {"", "\xC3\x89"})
    for (const char *pseudo : {"before", "after"})
        for (const char *kind : {"inline", "none", "inline-block", "block", "absolute", "fixed"}) {
            // Two representative high-byte pseudo cases, not another kind matrix.
            if (*ending && std::strcmp(kind, "inline") != 0) continue;
            scenario = std::string("generated ") + pseudo + " " + kind + ending;
            const bool before = std::string(pseudo) == "before",
                       ignored = std::string(kind) == "none",
                       isolated = std::string(kind) == "absolute" || std::string(kind) == "fixed",
                       boundary =
                           std::string(kind) == "inline-block" || std::string(kind) == "block";
            const std::string rule =
                std::string("#generated::") + pseudo +
                "{content:'mid" + ending + "';font-family:Pseudo;font-weight:700;color:#ab1234;" +
                (isolated ? "position:" : "display:") + kind + "}";
            Fixture f("hel<span id='generated'>lo</span><span id='suffix'>end</span>", rule);
            const auto source = Fixture::dom(f.node());
            auto runs = f.runs();
            aether_node_t owner{};
            std::string generated_text;
            for (auto &r : runs)
                if (r.owner != f.node() && r.owner != f.node("generated") &&
                    r.owner != f.node("suffix")) {
                    owner = r.owner;
                    generated_text += r.text;
                }
            if (ignored) {
                CHECK(generated_text.empty());
                CHECK(owned(f, "generated") == "lo");
                CHECK(owned(f, "suffix") == "end");
            } else {
                REQUIRE(owner);
                CHECK(owner != f.node("generated"));
                CHECK(generated_text == (std::string((isolated || boundary) ? "Mid" : "mid") + ending));
                CHECK(owned(f, "generated") == ((boundary && before) ? "Lo" : "lo"));
                CHECK(owned(f, "suffix") == ((boundary && !before) ? "End" : "end"));
                auto handle = aether_style_map_get(f.map, owner);
                REQUIRE(handle);
                aether_computed_style_t pseudo_style{};
                REQUIRE(aether_computed_style_copy(handle, &pseudo_style) == AETHER_OK);
                aether_computed_style_handle_destroy(handle);
                CHECK(std::string(pseudo_style.font_family) == "Pseudo");
                CHECK(pseudo_style.font_weight == 700);
                CHECK(near(pseudo_style.color.r, 171.0f / 255));
                CHECK(near(pseudo_style.color.g, 18.0f / 255));
                CHECK(near(pseudo_style.color.b, 52.0f / 255));
                auto verify_pseudo = [&](const StyledText &t) {
                    CHECK(t.text == generated_text);
                    CHECK(t.family == pseudo_style.font_family);
                    CHECK(t.weight == pseudo_style.font_weight);
                    CHECK(near(t.color.r, pseudo_style.color.r));
                    CHECK(near(t.color.g, pseudo_style.color.g));
                    CHECK(near(t.color.b, pseudo_style.color.b));
                    CHECK(near(t.color.a, pseudo_style.color.a));
                };
                bool measured = false;
                for (const auto &t : f.meter.styled)
                    if (t.family == "Pseudo") {
                        measured = true;
                        verify_pseudo(t);
                    }
                CHECK(measured);
                bool drawn = false;
                for (const auto &p : f.paints())
                    if (p.text == generated_text) {
                        drawn = true;
                        verify_pseudo(p);
                    }
                CHECK(drawn);
            }
            CHECK(Fixture::dom(f.node()) == source);
            f.resolve();
            f.layout();
            CHECK(owned(f, "suffix") ==
                  ((boundary && !before)
                       ? "End"
                       : "end")); // Reacquire all generated handles after resolution.
        }
}
static void lifecycle(bool revision) {
    scenario = revision ? "revision lifecycle" : "resolved lifecycle";
    Fixture f("<span id='prefix'>hel</span><span id='suffix'>lo</span><span id='bank' "
              "style='display:none'>hel </span><span id='empty' style='display:none'></span>",
              "", revision);
    auto original = f.result;
    f.result = nullptr;
    auto oldmap = f.map;
    f.map = nullptr;
    auto prefix = aether_node_get_child(f.node("prefix"), 0),
         space = aether_node_get_child(f.node("bank"), 0);
    REQUIRE(prefix);
    REQUIRE(space);
    REQUIRE(aether_node_append_child(f.node("bank"), prefix) == AETHER_OK);
    REQUIRE(aether_node_append_child(f.node("prefix"), space) == AETHER_OK);
    f.resolve();
    f.layout();
    CHECK(owned(f, "suffix") == "Lo");
    CHECK(Fixture::dom(f.node("prefix")) == "hel ");
    REQUIRE(aether_node_append_child(f.node("bank"), space) == AETHER_OK);
    REQUIRE(aether_node_append_child(f.node("prefix"), prefix) == AETHER_OK);
    f.resolve();
    f.layout();
    CHECK(owned(f, "suffix") == "lo");
    std::vector<Run> retained;
    Fixture::collect(aether_layout_result_find_primary_box(original, f.node()), retained);
    CHECK(joined(retained) == "Hello");
    aether_layout_result_destroy(original);
    aether_style_map_destroy(oldmap);
    // Public append moves parsed text into genuine adjacent DOM text nodes.
    auto suffix = aether_node_get_child(f.node("suffix"), 0);
    REQUIRE(suffix);
    REQUIRE(aether_node_append_child(f.node("prefix"), suffix) == AETHER_OK);
    f.resolve();
    f.layout();
    CHECK(owned(f, "prefix") == "Hello");
    CHECK(aether_node_get_child_count(f.node("prefix")) == 2);
    REQUIRE(aether_node_append_child(f.node("suffix"), suffix) == AETHER_OK);
    f.resolve();
    f.layout();
    CHECK(owned(f, "suffix") == "lo");
    for (const char *mode : {"none", "uppercase", "lowercase", "capitalize"}) {
        REQUIRE(aether_element_set_attribute(f.node("prefix"), "style",
                                             (std::string("text-transform:") + mode).c_str()) ==
                AETHER_OK);
        f.resolve();
        f.layout();
        CHECK(owned(f, "suffix") == "lo");
        const auto text = joined(f.runs());
        f.resolve();
        f.layout();
        CHECK(joined(f.runs()) == text);
    }
    for (const char *width : {"30px", "44px", "200px"}) {
        REQUIRE(aether_element_set_attribute(f.node(), "style",
                                             (std::string("width:") + width).c_str()) == AETHER_OK);
        f.resolve();
        f.layout();
        CHECK(owned(f, "suffix") == "lo");
        CHECK(Fixture::dom(f.node("prefix")) == "hel");
    }
    // An existing empty parsed element between runs cannot reset context.
    REQUIRE(aether_node_append_child(f.node("prefix"), f.node("empty")) == AETHER_OK);
    f.resolve();
    f.layout();
    CHECK(owned(f, "suffix") == "lo");
}
static void reentry(bool revision) {
    scenario = revision ? "revision separate-engine reentry" : "resolved separate-engine reentry";
    Fixture inner("<span>hel </span><span id='suffix'>lo</span>", "", revision),
        outer("hel<span id='suffix'>lo</span>", "", revision);
    for (int i = 0; i < 3; ++i) {
        outer.layout();
        inner.layout();
        CHECK(owned(outer, "suffix") == "lo");
        CHECK(owned(inner, "suffix") == "Lo");
    }
    outer.meter.nested = [&] {
        inner.layout();
        CHECK(owned(inner, "suffix") == "Lo");
    };
    outer.layout();
    CHECK(outer.meter.entered);
    CHECK(owned(outer, "suffix") == "lo");
    CHECK(owned(inner, "suffix") == "Lo");
    CHECK(seen(outer.meter, "lo"));
    CHECK(seen(inner.meter, "Lo"));
    outer.meter.nested = {};
    expect(outer, "Hello", "Hello");
}
static void noncapitalize_preservation() {
    for (const char *mode : {"none", "uppercase", "lowercase"}) {
        scenario = std::string("noncapitalize preservation ") + mode;
        Fixture f("<span id='outer'>HeL<span id='positioned' style='position:absolute'>OtHeR<br><i "
                  "id='atomic' style='display:inline-block'>EnD</i><b id='block' "
                  "style='display:block'>MoRe</b></span><span id='suffix'>Lo</span></span>",
                  std::string("#host{text-transform:") + mode + "}");
        auto runs = f.runs();
        for (const auto &r : runs) {
            char *id = aether_element_get_attribute(r.owner, "id");
            std::printf("%s run=%s owner=%s rect=%.2f,%.2f,%.2f,%.2f\n", mode, r.text.c_str(),
                        id ? id : "", r.rect.x, r.rect.y, r.rect.width, r.rect.height);
            aether_string_free(id);
        }
        std::printf("%s paint=%s\n", mode, f.paint().c_str());
        for (const auto &call : f.meter.calls)
            std::printf("%s measured=%s\n", mode, call.c_str());
        CHECK(owned(f, "suffix") == (std::string(mode) == "uppercase"   ? "LO"
                                     : std::string(mode) == "lowercase" ? "lo"
                                                                        : "Lo"));
    }
}

// Dedicated environmental ABI path. No ordinary Fixture, Meter, CHECK or REQUIRE
// is reachable here: every failure unwinds before the explicit locale restoration.
namespace text_locale_case {
struct Failure {};
struct State {
    unsigned checks = 0, completed = 0;
    void need(bool yes, const char* label) {
        ++checks;
        if (!yes) { std::fprintf(stderr, "text-locale failure: %s\n", label); throw Failure{}; }
    }
    void api(aether_status_t status) { need(status == AETHER_OK, "public API status"); }
};
struct Capture {
    char observations[64][128]{};
    unsigned count = 0;
    bool error = false;
    static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*,
                                         float, void* opaque) noexcept {
        auto& c = *static_cast<Capture*>(opaque);
        if (!text || c.count == 64) { c.error = true; return {1, 20, 15}; }
        unsigned n = 0;
        while (n < 128 && text[n]) ++n;
        if (n == 128) { c.error = true; return {1, 20, 15}; }
        float width = 0;
        for (unsigned i = 0; i < n; ++i) {
            const unsigned char b = static_cast<unsigned char>(text[i]);
            c.observations[c.count][i] = text[i];
            width += b >= 'A' && b <= 'Z' ? 13 : b >= 'a' && b <= 'z' ? 7 : b == ' ' ? 3 : 5;
        }
        c.observations[c.count++][n] = 0;
        return {width, 20, 15};
    }
    static aether_text_metrics_t contextual(const char* text, const aether_computed_style_t* style,
                                            float, float width, void* opaque) noexcept {
        return measure(text, style, width, opaque);
    }
    bool saw(const char* text) const noexcept {
        for (unsigned i = 0; i < count; ++i)
            if (std::strcmp(observations[i], text) == 0) return true;
        return false;
    }
};
struct Owners {
    Capture capture;
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_transaction_t transaction{};
    aether_style_revision_t revision{};
    aether_style_map_t map{}, legacy_map{};
    aether_layout_engine_t engine{};
    aether_layout_result_t result{}, legacy_result{};
    aether_display_list_builder_t builder{};
    ~Owners() noexcept {
        if (builder) aether_display_list_builder_destroy(builder);
        if (legacy_result) aether_layout_result_destroy(legacy_result);
        if (result) aether_layout_result_destroy(result);
        if (engine) aether_layout_destroy(engine);
        if (legacy_map) aether_style_map_destroy(legacy_map);
        if (map) aether_style_map_destroy(map);
        if (revision) aether_style_revision_destroy(revision);
        if (transaction) aether_style_transaction_destroy(transaction);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
};
struct Updates {
    aether_style_update_t* value{};
    int count = 0;
    ~Updates() noexcept { if (value) aether_style_updates_free(value, count < 0 ? 0 : count); }
};
struct String {
    char* value{};
    ~String() noexcept { if (value) aether_string_free(value); }
};
struct List {
    aether_display_list_t value{};
    ~List() noexcept { if (value) aether_display_list_destroy(value); }
};
struct Command {
    aether_paint_command_t value{};
    ~Command() noexcept { aether_paint_command_free(&value); }
};
struct Restore {
    const std::string& ctype;
    const std::string& numeric;
    bool& failed;
    bool done = false;
    bool restore() noexcept {
        const char* p = std::setlocale(LC_CTYPE, ctype.c_str());
        bool ok = p && std::strcmp(p, ctype.c_str()) == 0;
        p = std::setlocale(LC_CTYPE, nullptr);
        ok = p && std::strcmp(p, ctype.c_str()) == 0 && ok;
        p = std::setlocale(LC_NUMERIC, nullptr);
        ok = p && std::strcmp(p, numeric.c_str()) == 0 && ok;
        done = true;
        if (!ok) failed = true;
        return ok;
    }
    ~Restore() noexcept { if (!done) restore(); }
};
static std::string select(State& s, const char* requested) {
    const char* p = std::setlocale(LC_CTYPE, requested);
    s.need(p != nullptr, "select required locale");
    return std::string(p); // Copy before any subsequent setlocale call.
}
static void unchanged(State& s, const std::string& selected) {
    const char* p = std::setlocale(LC_CTYPE, nullptr);
    s.need(p && selected == p, "LC_CTYPE changed during product operation");
}
static void legacy_map(State& s, Owners& o, aether_style_map_t* destination) {
    Updates updates;
    updates.count = aether_style_runtime_resolve_subtree(o.runtime,
        aether_doc_get_document_element(o.doc), &updates.value);
    s.need(updates.count >= 0, "legacy resolve");
    s.api(aether_style_runtime_create_resolved_style_map(o.runtime, o.doc, destination));
}
static void initialize(State& s, Owners& o, const char* html, const char* css, bool contextual) {
    s.api(aether_doc_create(&o.doc));
    s.api(aether_doc_parse_html(o.doc, html));
    aether_style_environment_t env{400, 240, 1, 16, 0, 0, 0};
    s.api(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &o.runtime));
    int64_t sheet{};
    s.api(aether_style_runtime_add_stylesheet(o.runtime, css, 0, "text-locale", &sheet, nullptr));
    if (contextual) {
        s.api(aether_style_runtime_prepare_revision(o.runtime,
            aether_doc_get_document_element(o.doc), &env, &o.transaction));
        s.api(aether_style_runtime_commit_revision(o.runtime, o.transaction, &o.revision));
        s.api(aether_style_revision_create_map(o.revision, &o.map));
    } else legacy_map(s, o, &o.map);
    s.api(aether_layout_create(Capture::measure, &o.capture, &o.engine));
    s.api(aether_display_list_builder_create(Capture::measure, &o.capture, &o.builder));
    if (contextual) {
        aether_layout_set_contextual_text_measurer(o.engine, Capture::contextual, &o.capture);
        aether_display_list_builder_set_contextual_text_measurer(o.builder, Capture::contextual, &o.capture);
    }
    s.api(aether_layout_layout(o.engine, o.doc, o.map, 400, 240, nullptr, nullptr, &o.result));
    s.need(!o.capture.error, "capture after layout");
    s.need(o.result != nullptr, "layout exists");
}
static void attribute(State& s, aether_node_t node, const char* name, const char* expected) {
    String actual;
    actual.value = aether_element_get_attribute(node, name);
    s.need(actual.value && std::strcmp(actual.value, expected) == 0, "authored attribute unchanged");
}
static void verify_paint(State& s, Owners& o, aether_layout_result_t result,
                          const char* expected, int text_count, int commands = -1) {
    List list;
    s.api(aether_display_list_build(o.builder, result, nullptr, &list.value));
    s.need(!o.capture.error, "capture after paint");
    s.need(list.value != nullptr, "display list exists");
    const int count = aether_display_list_get_count(list.value);
    s.need(count >= 0 && (commands < 0 || count == commands), "paint command count");
    std::string joined;
    int texts = 0;
    for (int i = 0; i < count; ++i) {
        Command cmd;
        s.api(aether_display_list_get_command(list.value, i, &cmd.value));
        if (cmd.value.tag == AETHER_PAINT_DRAW_TEXT) {
            s.need(cmd.value.data.draw_text.text != nullptr, "paint text exists");
            joined += cmd.value.data.draw_text.text;
            ++texts;
        }
    }
    s.need(texts == text_count && joined == expected, "literal paint bytes and text count");
}
static void verify_span_boxes(State& s, aether_layout_box_t box, std::string& joined,
                              unsigned& fragments, float& width) {
    s.need(box != nullptr, "span layout box");
    String text;
    text.value = aether_layout_box_get_text(box);
    if (text.value && *text.value) {
        joined += text.value;
        width += aether_layout_box_get_border_box(box).width;
        ++fragments;
    }
    for (int i = 0; i < aether_layout_box_get_child_count(box); ++i)
        verify_span_boxes(s, aether_layout_box_get_child(box, i), joined, fragments, width);
}
constexpr const char* input_html = "<html><body><input id='probe' value='A\xC3\x89'></body></html>";
constexpr const char* input_css = "html,body{display:block;margin:0;padding:0}input{display:inline-block;"
    "width:120px;height:30px;border:0;padding:0;text-transform:lowercase;font-size:16px;line-height:20px}";
static void fixed_input(State& s, const std::string& selected) {
    const auto c = select(s, "C");
    Owners o;
    initialize(s, o, input_html, input_css, false);
    auto node = aether_doc_get_element_by_id(o.doc, "probe");
    s.need(node != nullptr, "fixed input node");
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    attribute(s, node, "value", "A\xC3\x89"); unchanged(s, c);
    select(s, selected.c_str());
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    attribute(s, node, "value", "A\xC3\x89"); unchanged(s, selected);
    select(s, "C");
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    attribute(s, node, "value", "A\xC3\x89"); unchanged(s, c);
    select(s, selected.c_str());
    s.api(aether_element_set_attribute(node, "value", "i I"));
    verify_paint(s, o, o.result, "i i", 1, 5);
    attribute(s, node, "value", "i I"); unchanged(s, selected);
    ++s.completed;
}
static void measured_span(State& s, const std::string& selected) {
    // A..Z=13, a..z=7, SP=3, other bytes=5: a+C3+89 + SP + i+i = 34.
    for (const char* phase : {"C", selected.c_str()}) {
        const auto active = select(s, phase);
        Owners o;
        initialize(s, o, "<html><body><div id='probe'><span>A\xC3\x89\viI</span></div></body></html>",
            "*{margin:0;padding:0;border:0}html,body,div{display:block}"
            "#probe{width:200px;font-size:16px;line-height:20px;text-transform:lowercase}", false);
        s.need(o.capture.count > 0 && o.capture.saw("a\xC3\x89") && o.capture.saw("ii") &&
            !o.capture.saw("a\xE3\x89"), "literal measured bytes, not corrupted variant");
        for (unsigned i = 0; i < o.capture.count; ++i)
            s.need(std::strcmp(o.capture.observations[i], "a\xC3\x89") == 0 ||
                   std::strcmp(o.capture.observations[i], "ii") == 0 ||
                   std::strcmp(o.capture.observations[i], " ") == 0,
                   "every measured span observation matches a literal token");
        auto node = aether_doc_get_element_by_id(o.doc, "probe");
        s.need(node != nullptr, "span host");
        auto box = aether_layout_result_find_primary_box(o.result, node);
        std::string joined;
        unsigned fragments = 0;
        float width = 0;
        verify_span_boxes(s, box, joined, fragments, width);
        s.need(joined == "a\xC3\x89 ii" && fragments == 3 && std::fabs(width - 34.f) < .01f,
            "span literal layout bytes and metric width");
        s.need(std::fabs(aether_layout_box_get_height(box) - 20.f) < .01f, "span line height");
        verify_paint(s, o, o.result, "a\xC3\x89 ii", 3);
        String source;
        auto span = aether_node_get_child(node, 0);
        s.need(span != nullptr, "span DOM element");
        auto text_node = aether_node_get_child(span, 0);
        s.need(text_node != nullptr && aether_node_get_type(text_node) == AETHER_NODE_TEXT, "span DOM text node");
        source.value = aether_node_get_text_content(text_node);
        s.need(source.value && std::strcmp(source.value, "A\xC3\x89\viI") == 0, "span DOM original");
        unchanged(s, active);
    }
    ++s.completed;
}
static void retained_context(State& s, const std::string& selected) {
    const auto c = select(s, "C");
    Owners o;
    initialize(s, o, input_html, input_css, true);
    legacy_map(s, o, &o.legacy_map);
    s.api(aether_layout_layout(o.engine, o.doc, o.legacy_map, 400, 240, nullptr, nullptr, &o.legacy_result));
    s.need(!o.capture.error, "capture after legacy counterpart layout");
    auto node = aether_doc_get_element_by_id(o.doc, "probe");
    s.need(node != nullptr, "snapshot input");
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    verify_paint(s, o, o.legacy_result, "a\xC3\x89", 1, 5); unchanged(s, c);
    select(s, selected.c_str());
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    s.api(aether_element_set_attribute(node, "value", ""));
    s.api(aether_element_set_attribute(node, "placeholder", "B\xC3\x89 I"));
    verify_paint(s, o, o.result, "a\xC3\x89", 1, 5);
    verify_paint(s, o, o.legacy_result, "b\xC3\x89 i", 1, 5);
    attribute(s, node, "value", "");
    attribute(s, node, "placeholder", "B\xC3\x89 I"); unchanged(s, selected);
    ++s.completed;
}
} // namespace text_locale_case

static int run_text_locale_case(const char* requested) {
    using namespace text_locale_case;
    State state;
    bool failed = false, absent = false, restored = false;
    unsigned effective = 0;
    std::string original, numeric, selected;
    try {
        const char* p = std::setlocale(LC_CTYPE, nullptr);
        state.need(p != nullptr, "capture original LC_CTYPE"); original = p;
        p = std::setlocale(LC_NUMERIC, nullptr);
        state.need(p != nullptr, "capture original LC_NUMERIC"); numeric = p;
        Restore guard{original, numeric, failed};
        try {
            p = std::setlocale(LC_CTYPE, requested);
            if (!p) {
                state.need(std::strcmp(requested, "C") != 0, "mandatory C locale unavailable");
                absent = true;
            } else {
                selected = p;
                fixed_input(state, selected);
                measured_span(state, selected);
                retained_context(state, selected);
                state.need(state.completed == 3, "all three dedicated fixtures completed");
                effective = 1;
            }
        } catch (...) { failed = true; }
        // All product owners have unwound before this explicit, checked restore.
        restored = guard.restore();
    } catch (...) { failed = true; }
    std::printf("text-locale requested=%s resolved=%s effective=%u fixtures=%u checks=%u restored=%d status=%s\n",
        requested, selected.empty() ? "unavailable" : selected.c_str(), effective, state.completed,
        state.checks, int(restored), failed || !restored ? "FAIL" : absent ? "SKIP" : "PASS");
    return failed || !restored ? 1 : absent ? 77 : 0;
}
int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "--text-locale") == 0 &&
        (std::strcmp(argv[2], "C") == 0 || std::strcmp(argv[2], "en_US") == 0 ||
         std::strcmp(argv[2], "tr_TR") == 0))
        return run_text_locale_case(argv[2]);
    if (argc == 2 && std::string(argv[1]) == "--noncapitalize-preservation") {
        noncapitalize_preservation();
        std::printf("fixtures=%u checks=%u failures=%u\n", fixtures, checks, failures);
        return failures ? 1 : 0;
    }

    if (argc != 1) { std::fprintf(stderr, "invalid arguments\n"); return 2; }

    test("unsplit", "hello", "Hello", 41);
    test("split", "hel<span id='suffix'>lo</span>", "Hello", 41);
    continuity();
    provenance(false);
    provenance(true);
    escaped_family_consumers(false);
    escaped_family_consumers(true);
    escaped_family_snapshots(false);
    escaped_family_snapshots(true);
    boundaries();
    public_sizing_and_isolation();
    generated();
    lifecycle(false);
    lifecycle(true);
    reentry(false);
    reentry(true);
    std::printf("fixtures=%u checks=%u failures=%u\n", fixtures, checks, failures);
    return failures ? 1 : 0;
}
