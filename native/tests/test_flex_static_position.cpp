// Public contract regression: final flex content anchors are not containing blocks.
// All checks remain active in Release/NDEBUG. No private layout headers are used.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static unsigned checks = 0, scenarios = 0;

static std::string label;
#define REQUIRE(x) \
    do { \
        ++checks; \
        if (!(x)) { \
            std::fprintf(stderr, "FAIL %s line %d: %s\n", label.c_str(), __LINE__, #x); \
            std::abort(); \
        } \
    } while (false)

static void rect(aether_rectf a, aether_rectf e) {
    std::printf("%s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n", label.c_str(), a.x, a.y, a.width, a.height,
        e.x, e.y, e.width, e.height);
    REQUIRE(std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.width) && std::isfinite(a.height));
    REQUIRE(std::fabs(a.x - e.x) < .01f);
    REQUIRE(std::fabs(a.y - e.y) < .01f);
    REQUIRE(std::fabs(a.width - e.width) < .01f);
    REQUIRE(std::fabs(a.height - e.height) < .01f);
}

static aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) {
    return { 10, 20, 15 };
}

static const char* reset = "html,body,div{display:block;margin:0;padding:0;border:0;box-sizing:content-box}";

static const char* host =
    "#host{position:relative;display:flex;width:300px;height:200px;"
    "padding:10px 40px 30px 20px;border:5px solid black;}";

static const char* item = "#item{position:absolute;width:40px;height:20px;}";

struct Fixture {
    bool prepared;
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_layout_result_t result{};
    int64_t sheet{};

    Fixture(bool p, const std::string& html = "<div id='host'><div id='item'></div></div>") : prepared(p) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        REQUIRE(aether_doc_parse_html(doc, ("<html><body>" + html + "</body></html>").c_str()) == AETHER_OK);
        aether_style_runtime_options_t opts{};
        aether_style_environment_t env { 800, 600, 1, 16, 0, 0, 0 };
        REQUIRE(aether_style_runtime_create(&opts, &env, nullptr, nullptr, &runtime) == AETHER_OK);
        REQUIRE(aether_layout_create(measure, nullptr, &engine) == AETHER_OK);
    }

    ~Fixture() {
        aether_layout_result_destroy(result);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }

    void css(const std::string& extra) {
        if (sheet) {
            REQUIRE(aether_style_runtime_remove_stylesheet(runtime, sheet));
        }
        const std::string source = std::string(reset) + host + item + extra;
        REQUIRE(aether_style_runtime_add_stylesheet(runtime, source.c_str(), 0, "flex-static.css", &sheet,
            nullptr) == AETHER_OK);
        aether_style_update_t* updates{};
        const int n = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        REQUIRE(n >= 0);
        aether_style_updates_free(updates, n);
        aether_style_map_destroy(map);
        map = nullptr;
        REQUIRE(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK);
    }

    void layout(float w = 800, float h = 600) {
        ++scenarios;
        aether_layout_result_destroy(result);
        result = nullptr;
        if (prepared) {
            aether_bool_t changed{};
            REQUIRE(aether_layout_prepare_transition_context(engine, nullptr, map, nullptr, &changed) == AETHER_OK);
        }
        REQUIRE(aether_layout_layout(engine, doc, map, w, h, nullptr, nullptr, &result) == AETHER_OK);
        REQUIRE(result);
    }

    aether_node_t node(const char* id) {
        auto n = aether_doc_get_element_by_id(doc, id);
        REQUIRE(n);
        return n;
    }

    aether_layout_box_t box(const char* id) {
        auto b = aether_layout_result_find_primary_box(result, node(id));
        REQUIRE(b);
        return b;
    }

    void border(const char* id, aether_rectf e) {
        rect(aether_layout_box_get_border_box(box(id)), e);
    }
};

static void start(bool p, const std::string& s) {
    label = (p ? "prepared/" : "legacy/") + s;
}

static void run(bool p, const std::string& name, const std::string& css,
                aether_rectf e, aether_rectf content = {25, 15, 300, 200},
                aether_rectf padding = {5, 5, 360, 240}) {
    start(p, name);
    Fixture f(p);
    f.css(css);
    f.layout();
    rect(aether_layout_box_get_content_box(f.box("host")), content);
    rect(aether_layout_box_get_padding_box(f.box("host")), padding);
    f.border("item", e);
}

static void research(bool p) {
    const auto before = scenarios;
    run(p, "A/start", "#host{justify-content:start;align-items:start}", { 25, 15, 40, 20 });
    run(p, "A/center", "#host{justify-content:center;align-items:center}", { 155, 105, 40, 20 });
    run(p, "A/end", "#host{justify-content:end;align-items:end}", { 285, 195, 40, 20 });
    run(p, "A/column", "#host{flex-direction:column;justify-content:end;align-items:center}", { 155, 195, 40,
        20 });
    const std::string c = "#host{justify-content:center;align-items:center}";
    run(p, "A/left", c + "#item{left:7px}", { 12, 105, 40, 20 });
    run(p, "A/explicit", c + "#item{left:7px;top:9px}", { 12, 14, 40, 20 });
    run(p, "A/percentage", c + "#item{width:50%}", { 85, 105, 180, 20 });
    run(p, "A/fixed", c + "#item{position:fixed}", { 155, 105, 40, 20 });
    run(p, "A/unpadded", c + "#host{padding:0}", { 135, 95, 40, 20 }, { 5, 5, 300, 200 }, { 5, 5, 300, 200 });
    REQUIRE(scenarios - before == 9);
}

static void alignments(bool p) {
    const auto before = scenarios;
    const char* mains[] = { "start", "center", "end", "space-between" };
    const char* crosses[] = { "start", "center", "end", "stretch" };
    const float fraction[] = { 0, .5f, 1, 0 };
    for (int column = 0; column < 2; ++column) {
        for (int m = 0; m < 4; ++m) {
            for (int c = 0; c < 4; ++c) {
                const std::string s = std::string("#host{flex-direction:") + (column ? "column" : "row") + ";justify-content:" +
                    mains[m] + ";align-items:" + crosses[c] + "}";
                run(p, "B/" + std::to_string(column) + "/" + mains[m] + "/" + crosses[c], s, { 25 + 260 * fraction[column ?
                    c : m], 15 + 180 * fraction[column ? m : c], 40, 20 });
            }
        }
    }
    const char* selves[] = { "start", "center", "stretch", "auto" };
    const float sf[] = { 0, .5f, 0, 1 };
    for (int column = 0; column < 2; ++column) {
        for (int s = 0; s < 4; ++s) {
            run(p, "B/self/" + std::to_string(column) + "/" + selves[s], std::string("#host{flex-direction:") +
                (column ? "column" : "row") + ";align-items:end}#item{align-self:" + selves[s] + "}", { 25 + (column ?
                260 * sf[s] : 0), 15 + (column ? 0 : 180 * sf[s]), 40, 20 });
        }
    }
    REQUIRE(scenarios - before == 40);
}

static void paint(aether_display_list_t list, aether_rectf expected, unsigned rgb = 0x123456);

static void siblings(bool p) {
    const auto before = scenarios;
    struct Sole {
        const char* style;
        aether_rectf expected;
    } sole[] = {
        { "width:40px;height:20px", { 155, 105, 40, 20 } },
        { "width:80px;height:30px", { 135, 100, 80, 30 } },
        { "position:fixed;width:20px;height:60px", { 165, 85, 20, 60 } }
    };
    for (unsigned i = 0; i < 3; ++i) {
        start(p, "C/sole/" + std::to_string(i));
        Fixture f(p);
        f.css(std::string("#host{justify-content:center;align-items:center}#item{") + sole[i].style + "}");
        f.layout();
        f.border("host", { 0, 0, 370, 250 });
        f.border("item", sole[i].expected);
        rect(aether_layout_box_get_border_box(aether_layout_result_get_root(f.result)), { 0, 0, 800, 250 });
    }
    for (int changed = 0; changed < 2; ++changed) {
        start(p, "C/flow-only/" + std::to_string(changed));
        Fixture f(p, "<div id='host'><div id='flow'></div><div id='flow2'></div></div>");
        f.css(std::string("#host{justify-content:center;align-items:center}#flow,#flow2{width:") + (changed ?
            "70" : "10") + "px;height:30px;flex-shrink:0}");
        f.layout();
        f.border("host", { 0, 0, 370, 250 });
        rect(aether_layout_box_get_border_box(aether_layout_result_get_root(f.result)), { 0, 0, 800, 250 });
    }
    for (int changed = 0; changed < 2; ++changed) {
        for (int reorder = 0; reorder < 2; ++reorder) {
            start(p, "C/siblings/" + std::to_string(changed) + "/" + std::to_string(reorder));
            Fixture f(p, reorder ? "<div id='host'><div id='third'></div><div id='flow2'></div><div id='item'></div>"
                "<div id='flow'></div><div id='second'></div></div>"
                : "<div id='host'><div id='flow'></div><div id='item'></div>"
                  "<div id='second'></div>"
                "<div id='flow2'></div><div id='third'></div></div>");
            f.css(std::string("#host{justify-content:center;align-items:center}#second{position:absolute;"
                "width:80px;height:30px}#third{position:fixed;width:20px;height:60px}"
                "#flow,#flow2{width:") + (changed ? "70" : "10") + "px;height:30px;flex-shrink:0}");
            f.layout();
            f.border("host", { 0, 0, 370, 250 });
            f.border("item", { 155, 105, 40, 20 });
            f.border("second", { 135, 100, 80, 30 });
            f.border("third", { 165, 85, 20, 60 });
            rect(aether_layout_box_get_border_box(aether_layout_result_get_root(f.result)), { 0, 0, 800, 250 });
        }
    }
    for (int height : { 30, 80 }) {
        start(p, "C/auto/" + std::to_string(height));
        Fixture f(p, "<div id='host'><div id='flow'></div><div id='item'></div></div>");
        f.css("#host{height:auto;justify-content:center;align-items:center}#flow{width:10px;"
            "height:" + std::to_string(height) + "px;flex-shrink:0}");
        f.layout();
        f.border("host", { 0, 0, 370, float(height + 50) });
        f.border("item", { 155, float(5 + height / 2), 40, 20 });
        rect(aether_layout_box_get_border_box(aether_layout_result_get_root(f.result)), { 0, 0, 800, float(height +
            50) });
    }
    for (int height : { 30, 80 }) {
        start(p, "C/auto-flow-only/" + std::to_string(height));
        Fixture f(p, "<div id='host'><div id='flow'></div></div>");
        f.css("#host{height:auto;justify-content:center;align-items:center}#flow{width:10px;"
            "height:" + std::to_string(height) + "px;flex-shrink:0}");
        f.layout();
        f.border("host", { 0, 0, 370, float(height + 50) });
        rect(aether_layout_box_get_border_box(aether_layout_result_get_root(f.result)), { 0, 0, 800, float(height +
            50) });
    }
    REQUIRE(scenarios - before == 13);
}

static void final_sizes(bool p) {
    const auto before = scenarios;
    struct Case {
        const char* css;
        float content;
        bool flow;
    } cases[] = {
        { "height:auto", 0, false },
        { "height:auto", 60, true },
        { "height:auto;min-height:100px", 100, true },
        { "height:auto;max-height:30px", 30, true },
        { "height:10px;box-sizing:border-box", 0, false },
        { "height:200px;max-height:90px", 90, false },
        { "height:20px;min-height:80px", 80, false }
    };
    for (int column = 0; column < 2; ++column) {
        for (unsigned i = 0; i < 7; ++i) {
            start(p, "D/" + std::to_string(column) + "/" + std::to_string(i));
            Fixture f(p, cases[i].flow ? "<div id='host'><div id='flow'></div><div id='item'></div></div>" :
                "<div id='host'><div id='item'></div></div>");
            f.css(std::string("#host{") + cases[i].css + ";flex-direction:" + (column ? "column" : "row") +
                ";justify-content:center;align-items:center}#flow{width:10px;height:60px;"
                "flex-shrink:0}");
            f.layout();
            const float width = i == 4 ? 230 : 300;
            rect(aether_layout_box_get_content_box(f.box("host")), { 25, 15, width, cases[i].content });
            f.border("host", { 0, 0, width + 70, cases[i].content + 50 });
            f.border("item", { 25 + (width - 40) / 2, 5 + cases[i].content / 2, 40, 20 });
        }
    }
    REQUIRE(scenarios - before == 14);
}

static void separation(bool p) {
    const auto before = scenarios;
    const std::string c = "#host{justify-content:center;align-items:center}";
    struct Case {
        const char* name;
        const char* css;
        aether_rectf expected;
    } cases[] = {
        { "height-percent", "height:50%", { 155, 55, 40, 120 } },
        { "percent-insets", "left:10%;top:25%", { 41, 65, 40, 20 } },
        { "top", "top:9px", { 155, 14, 40, 20 } },
        { "right", "right:7px", { 318, 105, 40, 20 } },
        { "bottom", "bottom:9px", { 155, 216, 40, 20 } },
        { "ends", "right:7px;bottom:9px", { 318, 216, 40, 20 } },
        { "opposing", "left:10px;right:20px;top:15px;bottom:25px;width:auto;height:auto", { 15, 20, 330, 200 } },
        { "margin", "margin:2px 7px 6px 3px", { 153, 103, 40, 20 } },
        { "percentage-margin", "margin:10%", { 155, 105, 40, 20 } },
        // Both percentage margins use padding width 360: left 36, top 18.
        // Center the 76x38 margin box, then add those start margins.
        { "asymmetric-percentage-margin", "margin-left:10%;margin-top:5%", { 173, 114, 40, 20 } },
        { "auto-margin-static", "margin:auto", { 155, 105, 40, 20 } },
        { "auto-margin-explicit", "left:10px;right:10px;top:10px;bottom:10px;margin:auto", { 165, 115, 40, 20 } },
        { "oversized", "width:400px;height:300px", { -25, -35, 400, 300 } },
        { "negative-margin", "margin:-2px -7px -6px -3px", { 157, 107, 40, 20 } }
    };
    for (const auto& t : cases) {
        run(p, std::string("E/") + t.name, c + "#item{" + t.css + "}", t.expected);
    }
    run(p, "E/no-border", c + "#host{border:none}", { 150, 100, 40, 20 }, { 20, 10, 300, 200 }, { 0, 0, 360,
        240 });
    start(p, "E/percentage-padding");
    Fixture f(p, "<div id='wrapper'><div id='host'><div id='item'></div></div></div>");
    f.css(c + "#wrapper{width:400px}#host{padding:5% 10%}#item{width:50%}");
    f.layout();
    rect(aether_layout_box_get_content_box(f.box("host")), { 45, 25, 300, 200 });
    rect(aether_layout_box_get_padding_box(f.box("host")), { 5, 5, 380, 240 });
    f.border("item", { 100, 115, 190, 20 });
    REQUIRE(scenarios - before == 16);
}

static void fixed(bool p) {
    const auto before = scenarios;
    const char* insets[] = { "", "left:7px", "top:9px", "left:7px;top:9px" };
    for (int resize = 0; resize < 2; ++resize) {
        for (int i = 0; i < 4; ++i) {
            start(p, "F/viewport/" + std::to_string(resize) + "/" + std::to_string(i));
            Fixture f(p);
            f.css(std::string("#host{justify-content:center;align-items:center}#item{position:fixed;width:50%;") +
                insets[i] + "}");
            f.layout(resize ? 1000 : 800);
            f.border("item", { i & 1 ? 7.f : (resize ? -75.f : -25.f), i & 2 ? 9.f : 105.f, resize ? 500.f :
                400.f, 20 });
        }
    }
    run(p, "F/owner-parent", "#host{transform:translate(0);justify-content:center;align-items:center}"
        "#item{position:fixed;width:50%}", p ? aether_rectf{ 85, 105, 180, 20 } : aether_rectf{ -25, 105, 400,
        20 });
    for (int nearest = 0; nearest < 2; ++nearest) {
        start(p, "F/outer/" + std::to_string(nearest));
        Fixture f(p, "<div id='outer'><div id='host'><div id='item'><div id='grand'></div>"
            "<div id='nested'></div></div></div></div>");
        f.css(std::string("#outer{width:500px;height:400px;padding:20px;border:10px solid black;"
            "transform:translate(0)}#host{justify-content:center;align-items:center;") + (nearest ? "transform:translate(0)" :
            "") + "}#item{position:fixed;width:50%;height:50%;display:flex;align-items:start;"
            "justify-content:start;background:#123456}#grand{width:10px;height:10px;"
            "background:#654321}#nested{position:fixed;left:7px;top:9px;width:5px;height:6px}");
        f.layout();
        const float w = p ? (nearest ? 180 : 270) : 400, h = p ? (nearest ? 120 : 220) : 300;
        f.border("item", { 55 + (300 - w) / 2, 45 + (200 - h) / 2, w, h });
        f.border("grand", { 55 + (300 - w) / 2, 45 + (200 - h) / 2, 10, 10 });
        f.border("nested", { p ? (nearest ? 42.f : 17.f) : 7.f, p ? (nearest ? 44.f : 19.f) : 9.f, 5, 6 });
        const float x = 55 + (300 - w) / 2, y = 45 + (200 - h) / 2;
        REQUIRE(aether_layout_result_hit_test(f.result, x + 2, y + 2) == f.node("grand"));
        REQUIRE(aether_layout_result_hit_test(f.result, x + 20, y + 20) == f.node("item"));
        aether_display_list_builder_t builder{};
        aether_display_list_t list{};
        REQUIRE(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK);
        REQUIRE(aether_display_list_build(builder, f.result, nullptr, &list) == AETHER_OK);
        paint(list, { x, y, w, h });
        paint(list, { x, y, 10, 10 }, 0x654321);
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        REQUIRE(aether_layout_box_get_child_count(f.box("item")) == 2);
        REQUIRE(aether_layout_box_get_element(aether_layout_box_get_child(f.box("item"), 0)) == f.node("grand"));
        REQUIRE(aether_layout_box_get_element(aether_layout_box_get_child(f.box("item"), 1)) == f.node("nested"));
    }
    REQUIRE(scenarios - before == 11);
}

static void translations(bool p) {
    const auto before = scenarios;
    const char* insets[] = { "", "left:7px", "top:9px", "left:7px;top:9px" };
    for (int ancestor = 0; ancestor < 2; ++ancestor) {
        for (int fixed = 0; fixed < 2; ++fixed) {
            for (int i = 0; i < 4; ++i) {
                start(p, "G/relative/" + std::to_string(ancestor) + "/" + std::to_string(fixed) + "/" + std::to_string(i));
                Fixture f(p, "<div id='wrapper'><div id='host'><div id='item'><div id='grand'></div></div>"
                    "</div></div>");
                f.css(std::string("#host{left:13px;top:17px;justify-content:center;align-items:center}"
                    "#grand{width:5px;height:6px}#wrapper{") + (ancestor ? "position:relative;left:11px;top:19px" :
                    "") + "}#item{position:" + (fixed ? "fixed" : "absolute") + ";" + insets[i] + "}");
                f.layout();
                const float x = (i & 1) ? (fixed ? 7.f : 25.f + ancestor * 11) : 168.f + ancestor * 11;
                const float y = (i & 2) ? (fixed ? 9.f : 31.f + ancestor * 19) : 122.f + ancestor * 19;
                f.border("item", { x, y, 40, 20 });
                f.border("grand", { x, y, 5, 6 });
                rect(aether_layout_box_get_content_box(f.box("host")), { 38.f + ancestor * 11, 32.f + ancestor * 19,
                    300, 200 });
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        start(p, "G/flex-relocation/" + std::to_string(i));
        Fixture f(p, "<div id='wrapper'><div id='host'><div id='item'></div></div></div>");
        f.css(std::string("#wrapper{display:flex;width:600px;height:400px;justify-content:end;"
            "align-items:end}#host{flex-shrink:0;justify-content:center;align-items:center}"
            "#item{position:fixed;") + insets[i] + "}");
        f.layout();
        f.border("host", { 230, 150, 370, 250 });
        f.border("item", { i & 1 ? 7.f : 385.f, i & 2 ? 9.f : 255.f, 40, 20 });
    }
    // Outer border origin (11,19), padding origin (21,29), content (41,49).
    // Host decoration adds (25,15), and its relative offset adds (13,17):
    // the completed host content anchor is (79,81), independent of the owner.
    // Prepared width is 540 / 2 = 270; legacy viewport width is 800 / 2 = 400.
    const aether_rectf prepared_relative[] = {
        { 94, 171, 270, 20 },
        { 28, 171, 270, 20 },
        { 94, 38, 270, 20 },
        { 28, 38, 270, 20 }
    };
    const aether_rectf legacy_relative[] = {
        { 29, 171, 400, 20 },
        { 7, 171, 400, 20 },
        { 29, 9, 400, 20 },
        { 7, 9, 400, 20 }
    };
    for (int i = 0; i < 4; ++i) {
        start(p, "G/relative-effective-owner/" + std::to_string(i));
        Fixture f(p,
            "<div id='outer'><div id='host'><div id='item'>"
            "<div id='grand'></div></div></div></div>");
        f.css(std::string(
            "#outer{position:relative;left:11px;top:19px;transform:translate(0);"
            "width:500px;height:400px;padding:20px;border:10px solid black}"
            "#host{left:13px;top:17px;justify-content:center;align-items:center}"
            "#grand{width:5px;height:6px}"
            "#item{position:fixed;width:50%;height:20px;") + insets[i] + "}");
        f.layout();
        rect(aether_layout_box_get_padding_box(f.box("outer")), { 21, 29, 540, 440 });
        rect(aether_layout_box_get_content_box(f.box("host")), { 79, 81, 300, 200 });
        const auto expected = p ? prepared_relative[i] : legacy_relative[i];
        f.border("item", expected);
        f.border("grand", { expected.x, expected.y, 5, 6 });
    }
    start(p, "G/prefix-wrapper");
    Fixture f(p, "<div id='prefix'></div><div id='wrapper'><div id='host'><div id='item'>"
        "<div id='grand'></div></div></div></div>");
    f.css("#prefix{height:50px}#wrapper{margin:7px;padding:11px}#host{justify-content:end;"
        "align-items:end}#grand{width:5px;height:6px}");
    f.layout();
    f.border("host", { 18, 68, 370, 250 });
    f.border("item", { 303, 263, 40, 20 });
    f.border("grand", { 303, 263, 5, 6 });
    REQUIRE(scenarios - before == 25);
}
// Match uniquely colored solid fills, including exact duplicate detection.

static void paint(aether_display_list_t list, aether_rectf expected, unsigned rgb) {
    int matched = 0;
    const int n = aether_display_list_get_count(list);
    REQUIRE(n > 0);
    for (int i = 0; i < n; ++i) {
        aether_paint_command_t c{};
        REQUIRE(aether_display_list_get_command(list, i, &c) == AETHER_OK);
        if (c.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto color = c.data.fill_rounded_rect.color;
            if (std::fabs(color.r -((rgb >> 16) & 255) / 255.f) < .001f && std::fabs(color.g -((rgb >> 8) & 255) / 255.f) < .001f &&
                std::fabs(color.b -(rgb & 255) / 255.f) < .001f) {
                ++matched;
                rect(c.data.fill_rounded_rect.rect, expected);
            }
        }
        aether_paint_command_free(&c);
    }
    REQUIRE(matched == 1);
}

static void retention(bool p) {
    const auto before = scenarios;
    start(p, "H/retention");
    Fixture f(p);
    const std::string a = "#host{justify-content:start;align-items:start}#item{background:#123456}";
    f.css(a);
    f.layout();
    auto retained = f.result;
    f.result = nullptr;
    aether_layout_result_t snapshot{};
    REQUIRE(aether_layout_clone_snapshot(retained, &snapshot) == AETHER_OK);
    aether_display_list_builder_t builder{};
    REQUIRE(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK);
    aether_display_list_t list{};
    REQUIRE(aether_display_list_build(builder, retained, nullptr, &list) == AETHER_OK);
    paint(list, { 25, 15, 40, 20 });
    REQUIRE(aether_layout_result_hit_test(retained, 30, 20) == f.node("item"));
    REQUIRE(aether_layout_result_hit_test(retained, 10, 7) != f.node("item"));
    for (int i = 0; i < 20; ++i) {
        start(p, "H/alternating/" + std::to_string(i));
        f.css(i % 3 == 2 ? "#host{padding:0;justify-content:start;align-items:start}#item{background:#123456}" :
            i % 2 ? a : "#host{padding:5% 10%;flex-direction:column;justify-content:end;align-items:end;"
            "transform:translate(0)}#item{position:fixed;width:60px;background:#123456}");
        f.layout();
        const float hitx = i % 3 == 2 ? 10.f : i % 2 ? 30.f : 330.f;
        const float hity = i % 3 == 2 ? 10.f : i % 2 ? 20.f : 230.f;
        REQUIRE(aether_layout_result_hit_test(f.result, hitx, hity) == f.node("item"));
        f.border("item", i % 3 == 2 ? aether_rectf{ 5, 5, 40, 20 } : i % 2 ? aether_rectf{ 25, 15, 40, 20 } :
            aether_rectf{ 325, 225, 60, 20 });
        rect(aether_layout_box_get_border_box(aether_layout_result_find_primary_box(retained, f.node("item"))),
            { 25, 15, 40, 20 });
        rect(aether_layout_box_get_border_box(aether_layout_result_find_primary_box(snapshot, f.node("item"))),
            { 25, 15, 40, 20 });
        paint(list, { 25, 15, 40, 20 });
        aether_display_list_t current{};
        REQUIRE(aether_display_list_build(builder, f.result, nullptr, &current) == AETHER_OK);
        paint(current, i % 3 == 2 ? aether_rectf{ 5, 5, 40, 20 } : i % 2 ? aether_rectf{ 25, 15, 40, 20 } :
            aether_rectf{ 325, 225, 60, 20 });
        aether_display_list_destroy(current);
    }
    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(snapshot);
    aether_layout_result_destroy(retained);
    REQUIRE(scenarios - before == 21);
}

static void additional_consumers(bool p) {
    const auto before = scenarios;
    for (int fixed = 0; fixed < 2; ++fixed) {
        start(p, "H/overflow/" + std::to_string(fixed));
        Fixture f(p);
        f.css(std::string("#host{overflow:hidden;justify-content:start;align-items:start}#item{width:400px;"
            "height:300px;background:#123456;position:") + (fixed ? "fixed" : "absolute") + "}");
        f.layout();
        f.border("item", { 25, 15, 400, 300 });
        const auto scroll = aether_layout_box_get_scroll_metrics(f.box("host"));
        REQUIRE(std::isfinite(scroll.scrollable_overflow_x) && std::isfinite(scroll.scrollable_overflow_y));
        // Legacy style maps (also when prepared for fixed owners) retain the
        // existing border-bound overflow walk. Contextual visual samples have
        // their own viewport-fixed exclusion, covered by contextual suites.
        REQUIRE(std::fabs(scroll.scrollable_overflow_x - 100.f) < .01f);
        REQUIRE(std::fabs(scroll.scrollable_overflow_y - 100.f) < .01f);
        if (!fixed) {
            REQUIRE(aether_layout_result_hit_test(f.result, 30, 20) == f.node("item"));
            REQUIRE(aether_layout_result_hit_test(f.result, 400, 280) != f.node("item"));
            aether_display_list_builder_t b{};
            aether_display_list_t list{};
            REQUIRE(aether_display_list_builder_create(measure, nullptr, &b) == AETHER_OK);
            REQUIRE(aether_display_list_build(b, f.result, nullptr, &list) == AETHER_OK);
            bool clipped = false;
            for (int i = 0; i < aether_display_list_get_count(list); ++i) {
                aether_paint_command_t c{};
                REQUIRE(aether_display_list_get_command(list, i, &c) == AETHER_OK);
                if (c.tag == AETHER_PAINT_PUSH_CLIP &&
                    std::fabs(c.data.push_clip.rect.x - 5) < .01f &&
                    std::fabs(c.data.push_clip.rect.y - 5) < .01f) {
                    rect(c.data.push_clip.rect, { 5, 5, 360, 240 });
                    clipped = true;
                }
                aether_paint_command_free(&c);
            }
            REQUIRE(clipped);
            aether_display_list_destroy(list);
            aether_display_list_builder_destroy(b);
        }
    }
    for (int i = 0; i < 4; ++i) {
        start(p, "G/nested-barrier/" + std::to_string(i));
        Fixture f(p, "<div id='host'><div id='item'><div id='grand'></div><div id='nested'></div>"
            "</div></div>");
        const char* insets[] = { "", "left:7px", "top:9px", "left:7px;top:9px" };
        f.css(std::string("#host{left:13px;top:17px;justify-content:center;align-items:center}"
            "#item{position:fixed;display:flex;justify-content:end;align-items:end}"
            "#grand{width:5px;height:6px}#nested{position:fixed;width:5px;height:6px;") + insets[i] + "}");
        f.layout();
        f.border("item", { 168, 122, 40, 20 });
        f.border("grand", { 203, 136, 5, 6 });
        f.border("nested", { i & 1 ? 7.f : 203.f, i & 2 ? 9.f : 136.f, 5, 6 });
    }
    start(p, "F/outer-opposing");
    Fixture f(p, "<div id='outer'><div id='host'><div id='item'></div></div></div>");
    f.css("#outer{transform:translate(0);width:500px;height:400px;padding:20px;"
        "border:10px solid black}#item{position:fixed;left:10px;right:20px;top:15px;"
        "bottom:25px;width:auto;height:auto}");
    f.layout();
    f.border("item", p ? aether_rectf{ 20, 25, 510, 400 } : aether_rectf{ 10, 15, 770, 560 });
    start(p, "F/auto-min-owner");
    Fixture auto_owner(p, "<div id='outer'><div id='prefix'></div><div id='host'><div id='item'></div>"
        "</div></div>");
    auto_owner.css("#outer{transform:translate(0);width:500px;height:auto;min-height:500px;"
        "padding:20px;border:10px solid black}#prefix{height:60px}"
        "#host{justify-content:center;align-items:center}#item{position:fixed;width:50%;"
        "height:50%}");
    auto_owner.layout();
    auto_owner.border("outer", { 0, 0, 560, 560 });
    auto_owner.border("host", { 30, 90, 370, 250 });
    auto_owner.border("item", p ? aether_rectf{ 70, 70, 270, 270 } : aether_rectf{ 5, 55, 400, 300 });
    REQUIRE(scenarios - before == 8);
}

int main() {
    for (bool prepared : { false, true }) {
        research(prepared);
        alignments(prepared);
        siblings(prepared);
        final_sizes(prepared);
        separation(prepared);
        fixed(prepared);
        translations(prepared);
        retention(prepared);
        additional_consumers(prepared);
    }
    REQUIRE(scenarios == 314);
    REQUIRE(checks > scenarios * 10);
    std::printf("flex-static-position PASS scenarios=%u assertions=%u (legacy and prepared)\n", scenarios,
        checks);
}
