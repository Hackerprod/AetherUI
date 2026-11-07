#include <aether/aether.h>
#include <aether/aether_contextual_style.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// All checks remain active in Release/NDEBUG. Geometry failures accumulate so
// a red run against the original DSO still exercises the entire sealed matrix.
static int failures = 0;
static int fixtures = 0;
static int checks = 0;
static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL infrastructure: %s\n", message);
        std::exit(2);
    }
}
static void expect(float actual, float expected, const std::string& name, const char* field) {
    ++checks;
    if (!std::isfinite(actual) || std::fabs(actual - expected) > .01f) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s actual=%g expected=%g\n",
                     name.c_str(), field, actual, expected);
    }
}
static aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {static_cast<float>(std::strlen(text)) * 10.f, 10.f, 8.f};
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t frame = nullptr;

    Fixture(const std::string& html, const std::string& css, bool contextual = false) {
        require(aether_doc_create(&doc) == AETHER_OK, "document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{400, 400, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK,
                "runtime");
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "cross-auto.css", &sheet, nullptr) == AETHER_OK, "stylesheet");
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(count > 0, "resolve styles");
        aether_style_updates_free(updates, count);
        if (contextual) {
            aether_style_revision_t revision = nullptr;
            require(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK, "revision");
            require(aether_style_revision_create_map(revision, &map) == AETHER_OK, "contextual map");
            aether_style_revision_destroy(revision);
        } else {
            require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) == AETHER_OK,
                    "resolved map");
        }
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "engine");
        require(aether_layout_layout(engine, doc, map, 400, 400, nullptr, nullptr, &frame) == AETHER_OK,
                "layout");
        ++fixtures;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        aether_layout_result_destroy(frame);
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) const {
        auto* n = aether_doc_get_element_by_id(doc, id);
        require(n != nullptr, "node");
        return n;
    }
    aether_layout_box_t box(const char* id) const {
        auto* b = aether_layout_result_find_primary_box(frame, node(id));
        require(b != nullptr, "primary box");
        return b;
    }
    aether_rectf border(const char* id) const { return aether_layout_box_get_border_box(box(id)); }
    aether_rectf content(const char* id) const { return aether_layout_box_get_content_box(box(id)); }
};
static const char* html = "<html><body><div id='p'><div id='c'></div></div></body></html>";
static const char* reset = "html,body,div{display:block;margin:0;padding:0;border:0;box-sizing:border-box;}";
static std::string axis(bool col) { return col ? "column" : "row"; }
static std::string dim(bool col) { return col ? "width" : "height"; }
static std::string main_dim(bool col) { return col ? "height" : "width"; }
static std::string before(bool col) { return col ? "margin-left" : "margin-top"; }
static std::string after(bool col) { return col ? "margin-right" : "margin-bottom"; }
static std::string both(bool col) { return before(col) + ":auto;" + after(col) + ":auto;"; }
static float extent(aether_rectf r, bool col) { return col ? r.width : r.height; }
static float cross(aether_rectf r, bool col) { return col ? r.x : r.y; }
static float main_pos(aether_rectf r, bool col) { return col ? r.y : r.x; }
static void geometry(const Fixture& f, const std::string& name, bool col,
                     const char* id, float offset, float size) {
    const auto b = f.border(id);
    expect(cross(b, col) - cross(f.content("p"), col), offset, name, id);
    expect(extent(b, col), size, name, "border cross size");
}
static float placement(float line, float size, int align, int margins) {
    const float free = line - size;
    if (margins) return margins == 1 ? std::max(free, 0.f) / 2.f
                                    : margins == 2 ? std::max(free, 0.f) : 0.f;
    return align == 1 ? free / 2.f : align == 2 ? free : 0.f;
}
static void matrix(bool wrap) {
    const char* aligns[] = {"flex-start", "center", "flex-end"};
    const char* margins[] = {"none", "both", "before", "after"};
    const int start = fixtures;
    for (bool col : {false, true}) {
        for (int size : {40, 100, 160}) {
            for (int align = 0; align < 3; ++align) {
                for (int margin = 0; margin < 4; ++margin) {
                    // CORE72: exact CSS and HTML from the original selection
                    // probe. Do not add explicit nowrap, decorations or resets.
                    std::string css = std::string(reset) +
                        "#p{display:flex;width:100px;height:100px;flex-direction:" + axis(col) +
                        ";align-items:" + aligns[align] + ";}#c{flex:0 0 auto;width:" +
                        std::to_string(col ? size : 20) + "px;height:" +
                        std::to_string(col ? 20 : size) + "px;";
                    if (margin == 1 || margin == 2) css += before(col) + ":auto;";
                    if (margin == 1 || margin == 3) css += after(col) + ":auto;";
                    css += "}";
                    if (wrap) css += "#p{flex-wrap:wrap;}";
                    const std::string name = std::string(wrap ? "wrap72/" : "CORE72/") +
                        axis(col) + "/" + std::to_string(size) + "/" + aligns[align] + "/" + margins[margin];
                    Fixture f(html, css);
                    const float line = wrap ? std::max(size, 100) : 100;
                    geometry(f, name, col, "c", placement(line, size, align, margin), size);
                    expect(extent(f.border("c"), !col), 20, name, "main size");
                    expect(extent(aether_layout_box_get_margin_box(f.box("c")), col),
                           margin ? std::max(line, static_cast<float>(size)) : size,
                           name, "margin cross size");
                    expect(extent(f.content("p"), col), 100, name, "host cross");
                    expect(main_pos(f.border("c"), col) - main_pos(f.content("p"), col),
                           0, name, "main offset");
                }
            }
        }
    }
    require(fixtures - start == 72, "matrix cardinality");
    std::printf("%s: 72 fixtures\n", wrap ? "wrap default/stretch" : "sealed CORE72");
}
static std::string css_for(bool col, const std::string& parent, const std::string& child) {
    return std::string(reset) + "#p{display:flex;width:100px;height:100px;flex-direction:" +
        axis(col) + ";align-items:flex-start;" + parent + "}#c{flex:0 0 auto;" +
        main_dim(col) + ":20px;" + dim(col) + ":40px;" + child + "}";
}
static void limits_and_wrap() {
    for (bool wrap : {false, true}) {
        for (bool minimum : {false, true}) {
            for (int align = 0; align < 3; ++align) {
                for (int margins = 0; margins < 4; ++margins) {
                    const char* aligns[] = {"flex-start", "center", "flex-end"};
                    std::string child = "height:" + std::to_string(minimum ? 40 : 160) + "px;";
                    if (margins == 1 || margins == 2) child += "margin-top:auto;";
                    if (margins == 1 || margins == 3) child += "margin-bottom:auto;";
                    const std::string name = std::string("limits/") + (minimum ? "min/" : "max/") +
                        (wrap ? "wrap/" : "nowrap/") + aligns[align] + "/" + std::to_string(margins);
                    Fixture f(html, css_for(false, std::string("height:auto;") +
                        (minimum ? "min-height:100px;" : "max-height:100px;") +
                        "flex-wrap:" + (wrap ? "wrap;" : "nowrap;") +
                        "align-items:" + aligns[align] + ";", child));
                    const float size = minimum ? 40 : 160;
                    geometry(f, name, false, "c", placement(wrap ? std::max(100.f, size) : 100,
                             size, align, margins), size);
                    expect(f.content("p").height, 100, name, "clamped host");
                }
            }
        }
    }
    for (bool col : {false, true}) {
        for (const char* align : {"center", "flex-end"}) {
            for (bool auto_margin : {false, true}) {
                Fixture f(html, css_for(col, std::string("flex-wrap:wrap;align-content:stretch;align-items:") +
                    align + ";", auto_margin ? both(col) : ""));
                geometry(f, "explicit stretch/" + axis(col), col, "c",
                         auto_margin || std::strcmp(align, "center") == 0 ? 30 : 60, 40);
            }
        }
        for (bool minimum : {false, true}) {
            const std::string parent = main_dim(col) + ":100px;" + dim(col) +
                (minimum ? ":auto;min-" + dim(col) + ":200px;" : ":200px;") +
                "flex-wrap:wrap;gap:" + (col ? "0px 10px;" : "10px 0px;");
            // Column auto width is already resolved by the outer block layout;
            // use min-width with an explicit width to exercise its clamp.
            const std::string fixed_parent = parent + (col && minimum ? "width:100px;" : "");
            const std::string child = main_dim(col) + ":50px;" + dim(col) + ":20px;" + both(col);
            const std::string extra = "#s,#d,#t{flex:0 0 auto;" + main_dim(col) + ":50px;}#s{" +
                dim(col) + ":40px;}#d{" + dim(col) + ":20px;" + both(col) + "}#t{" + dim(col) + ":60px;}";
            Fixture f("<html><body><div id='p'><div id='c'></div><div id='s'></div>"
                      "<div id='d'></div><div id='t'></div></div></body></html>",
                      css_for(col, fixed_parent, child) + extra);
            const std::string name = "two lines/" + axis(col) + (minimum ? "/minimum" : "/explicit");
            geometry(f, name, col, "c", 32.5f, 20);
            geometry(f, name, col, "s", 0, 40);
            geometry(f, name, col, "d", 137.5f, 20);
            geometry(f, name, col, "t", 95, 60);
            expect(extent(f.content("p"), col), 200, name, "host cross");
        }
    }
}
static void fractional_lines() {
    for (bool col : {false, true}) {
        Fixture f("<html><body><div id='p'><div id='c'></div><div id='s'></div>"
                  "<div id='d'></div><div id='u'></div><div id='t'></div><div id='v'></div>"
                  "</div></body></html>",
            css_for(col, main_dim(col) + ":100px;" + dim(col) +
                ":100.3px;flex-wrap:wrap;gap:" + (col ? "0px .1px;" : ".1px 0px;"), "") +
            "#p>div{flex:0 0 auto;" + main_dim(col) + ":50px;}#c,#d,#t{" +
                main_dim(col) + ":50px;" + dim(col) + ":2.4px;" + both(col) +
            "}#s,#u,#v{" + dim(col) + ":10.2px;}");
        const float line = (100.3f - .2f) / 3.f;
        float total = .2f;
        int index = 0;
        for (const char* id : {"c", "d", "t"}) {
            const auto margin = aether_layout_box_get_margin_box(f.box(id));
            const float origin = index++ * (line + .1f);
            geometry(f, "fractional three lines/" + axis(col), col, id,
                     origin + (line - 2.4f) / 2.f, 2.4f);
            expect(extent(margin, col), line, "fractional lines", "line extent");
            expect(cross(margin, col) - cross(f.content("p"), col), origin,
                   "fractional lines", "line origin");
            total += extent(margin, col);
        }
        expect(total, 100.3f, "fractional lines", "sum lines plus gaps");
    }
}
static void guards() {
    for (bool col : {false, true}) {
        auto one = [&](const std::string& name, const std::string& parent,
                       const std::string& child, float offset, float size) {
            Fixture f(html, css_for(col, parent, child));
            geometry(f, name + "/" + axis(col), col, "c", offset, size);
        };
        one("negative fixed rescues overflow", "", dim(col) + ":120px;" + before(col) +
            ":auto;" + after(col) + ":-40px;", 20, 120);
        one("main auto keeps cross alignment", "align-items:center;", dim(col) + ":160px;" +
            (col ? "margin-top:auto;" : "margin-left:auto;"), -30, 160);
        {
            Fixture f(html, css_for(col, "justify-content:flex-end;align-items:center;",
                main_dim(col) + ":160px;" + both(col)));
            geometry(f, "cross auto keeps justify/" + axis(col), col, "c", 30, 40);
            expect(main_pos(f.border("c"), col) - main_pos(f.content("p"), col), -60,
                   "cross auto keeps justify/" + axis(col), "main overflow");
        }
        {
            Fixture f("<html><body><div id='p'><div id='c'></div><div id='s'></div></div></body></html>",
                css_for(col, "align-items:stretch;", both(col)) + "#s{flex:0 0 auto;" +
                main_dim(col) + ":20px;" + dim(col) + ":auto;min-" + dim(col) + ":160px;}");
            geometry(f, "stretch cannot regrow fixed line/" + axis(col), col, "c", 30, 40);
            geometry(f, "stretch oversized sibling/" + axis(col), col, "s", 0, 160);
            expect(extent(f.content("p"), col), 100, "stretch fixed line", "host cross");
        }
        one("signed before", "", before(col) + ":-10px;" + after(col) + ":auto;", -10, 40);
        one("signed after", "", before(col) + ":auto;" + after(col) + ":-10px;", 70, 40);
        one("fixed before", "", before(col) + ":10px;" + after(col) + ":auto;", 10, 40);
        one("fixed after", "", before(col) + ":auto;" + after(col) + ":10px;", 50, 40);
        one("signed stretch", "align-items:stretch;", dim(col) + ":auto;" +
            before(col) + ":-10px;", -10, 110);
        one("asymmetric host decorations", "padding:1px 3px 7px 5px;border:solid;border-width:2px 4px 6px 8px;",
            both(col), col ? 20.f : 22.f, 40);
        one("min max with decorations", "min-" + dim(col) + ":120px;max-" + dim(col) +
            ":80px;padding:1px 3px 7px 5px;border:solid;border-width:2px 4px 6px 8px;",
            both(col), col ? 30.f : 32.f, 40);
        one("decoration floor", dim(col) + ":4px;padding:3px;border:2px solid red;", both(col), 0, 40);
        one("signed no auto center", "align-items:center;", before(col) + ":-10px;", 25, 40);
        one("align self overridden", "align-items:flex-end;", "align-self:center;" + both(col), 30, 40);
        one("align self overflow suppressed", "align-items:flex-end;",
            "align-self:center;" + dim(col) + ":160px;" + both(col), 0, 160);
        one("align self no auto", "", "align-self:flex-end;", 60, 40);
        one("fractional", dim(col) + ":100.5px;", dim(col) + ":40.25px;" + both(col), 30.125f, 40.25f);
        one("content box decorations", "", "box-sizing:content-box;padding:3px;border:2px solid red;" + both(col), 25, 50);
        one("border box decorations", "", "padding:3px;border:2px solid red;" + both(col), 30, 40);
        one("host content box decorations", "box-sizing:content-box;padding:3px;border:2px solid red;",
            both(col), 30, 40);
        one("host border box decorations", "padding:3px;border:2px solid red;", both(col), 25, 40);
        one("min wins max", "min-" + dim(col) + ":120px;max-" + dim(col) + ":80px;", both(col), 40, 40);
        one("calc min", "min-" + dim(col) + ":calc(80px + 40px);", both(col), 40, 40);
        one("stretch blocked by auto", "align-items:stretch;", dim(col) + ":auto;min-" +
            dim(col) + ":40px;" + both(col), 30, 40);
        one("nowrap stretch cannot grow line", "align-items:stretch;", dim(col) +
            ":auto;min-" + dim(col) + ":160px;", 0, 160);
        {
            Fixture f(html, css_for(col, main_dim(col) + ":200px;align-items:stretch;",
                main_dim(col) + ":40px;" + dim(col) + ":auto;min-" + dim(col) + ":40px;" +
                (col ? "margin-top:auto;" : "margin-left:auto;")));
            geometry(f, "main auto after relayout/" + axis(col), col, "c", 0, 100);
            expect(main_pos(f.border("c"), col) - main_pos(f.content("p"), col), 160,
                   "main auto after relayout/" + axis(col), "main offset");
        }
        for (bool wrap : {false, true}) {
            Fixture f("<html><body><div id='p'><div id='c'></div><div id='s'></div></div></body></html>",
                css_for(col, wrap ? "flex-wrap:wrap;" : "", both(col)) +
                "#s{flex:0 0 auto;" + main_dim(col) + ":20px;" + dim(col) + ":160px;}");
            geometry(f, "oversize sibling/" + axis(col) + (wrap ? "/wrap" : "/nowrap"),
                     col, "c", wrap ? 60 : 30, 40);
        }
    }
    // A numeric minimum must not become the percentage-height reference.
    for (bool wrap : {false, true}) {
        Fixture f("<html><body><div id='p'><div id='c'><div id='d'></div></div></div></body></html>",
            css_for(false, std::string("height:auto;min-height:100px;") + (wrap ? "flex-wrap:wrap;" : ""),
                "height:50%;margin-top:auto;margin-bottom:auto;") + "#d{height:20px;width:10px;}");
        geometry(f, "indefinite percent height", false, "c", 40, 20);
        expect(f.border("d").height, 20, "indefinite percent height", "descendant");
    }
}
static void definite_percentage_limits() {
    for (bool col : {false, true}) {
        for (bool wrap : {false, true}) {
            for (int scenario = 0; scenario < 5; ++scenario) {
                const bool maximum = scenario == 2 || scenario == 3;
                const bool calc = scenario == 1 || scenario == 3;
                const float used = calc || scenario == 4 ? 120.f : 100.f;
                const float size = maximum ? 160.f : 40.f;
                std::string limit;
                if (scenario == 4) {
                    limit = "min-" + dim(col) + ":60%;max-" + dim(col) + ":40%;";
                } else {
                    limit = (maximum ? "max-" : "min-") + dim(col) + ":" +
                        (calc ? "calc(50% + 20px);" : "50%;");
                }
                const std::string parent = dim(col) + ":" +
                    (maximum ? "200px;" : col ? "10px;" : "auto;") + limit +
                    (wrap ? "flex-wrap:wrap;" : "");
                Fixture f("<html><body><div id='outer'><div id='p'><div id='c'></div>"
                          "</div></div></body></html>",
                          css_for(col, parent, dim(col) + ":" + std::to_string(size) + "px;" + both(col)) +
                          "#outer{width:200px;height:200px;}");
                const std::string name = "definite percent limits/" + axis(col) +
                    (wrap ? "/wrap/" : "/nowrap/") + std::to_string(scenario);
                expect(extent(f.content("p"), col), used, name, "host content cross");
                geometry(f, name, col, "c", std::max(used - size, 0.f) / 2.f, size);
            }
        }
    }
}
static void consumers() {
    for (bool col : {false, true}) {
        Fixture f("<html><body><div id='p'><div id='c'><div id='d'></div>"
                  "<div id='fixed'></div></div></div></body></html>",
            css_for(col, "", both(col) + "position:relative;left:3px;top:7px;background:#ff0000;") +
            "#d{width:5px;height:5px;}#fixed{position:fixed;left:150px;top:120px;width:5px;height:5px;}", true);
        const auto b = f.border("c");
        geometry(f, "relative/" + axis(col), col, "c", col ? 33 : 37, 40);
        expect(f.border("d").x, b.x, "relative", "descendant x");
        expect(f.border("d").y, b.y, "relative", "descendant y");
        expect(f.border("fixed").x, 150, "fixed", "viewport x");
        expect(f.border("fixed").y, 120, "fixed", "viewport y");
        expect(aether_layout_result_hit_test(f.frame, b.x + 10, b.y + 10) == f.node("c") ? 1 : 0,
               1, "final hit test", "node");
        aether_display_list_builder_t painter = nullptr;
        aether_display_list_t list = nullptr;
        require(aether_display_list_builder_create(measure, nullptr, &painter) == AETHER_OK, "painter");
        require(aether_display_list_build(painter, f.frame, nullptr, &list) == AETHER_OK, "paint");
        int fills = 0;
        for (int i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t cmd{};
            require(aether_display_list_get_command(list, i, &cmd) == AETHER_OK, "paint command");
            if (cmd.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
                const auto r = cmd.data.fill_rounded_rect.rect;
                ++fills;
                expect(r.x, b.x, "final paint", "x");
                expect(r.y, b.y, "final paint", "y");
                expect(r.width, b.width, "final paint", "width");
                expect(r.height, b.height, "final paint", "height");
            }
            aether_paint_command_free(&cmd);
        }
        expect(static_cast<float>(fills), 1, "final paint", "background count");
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(painter);
    }
    // Fixed explicit host: this asserts final positive overflow, not the
    // preexisting auto/clamped scroll_base_height policy outside this change.
    Fixture f(html, css_for(false, "overflow-y:auto;", "height:160px;" + both(false)));
    geometry(f, "scroll final overflow", false, "c", 0, 160);
    const auto metrics = aether_layout_box_get_scroll_metrics(f.box("p"));
    expect(metrics.scrollable_overflow_y, 60, "scroll final overflow", "overflow y");
}
int main() {
    matrix(false);
    matrix(true);
    limits_and_wrap();
    guards();
    fractional_lines();
    definite_percentage_limits();
    consumers();
    std::printf("flex cross auto: fixtures=%d checks=%d failures=%d (NDEBUG-safe)\n",
                fixtures, checks, failures);
    return failures ? 1 : 0;
}
