#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void require(const bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool same(const float actual, const float expected) {
    return std::fabs(actual - expected) < .001f;
}

aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) * 8.f : 0.f, 16.f, 12.f};
}

aether_scroll_offset_t no_scroll(aether_node_t, void*) {
    return aether_scroll_offset_t{};
}

/* Build a document, resolve a stylesheet, lay out at the given viewport and
   return the result. The caller owns every handle until layout is destroyed. */
struct fixture_s {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;

    ~fixture_s() {
        if (list) aether_display_list_destroy(list);
        if (builder) aether_display_list_builder_destroy(builder);
        if (layout) aether_layout_result_destroy(layout);
        if (engine) aether_layout_destroy(engine);
        if (styles) aether_style_map_destroy(styles);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (document) aether_doc_destroy(document);
    }
};

fixture_s build_fixture(const char* html, const char* css, float viewport_width,
                        float viewport_height) {
    fixture_s fixture;
    require(aether_doc_create(&fixture.document) == AETHER_OK &&
                aether_doc_parse_html(fixture.document, html) == AETHER_OK,
            "scroll model fixture document creation failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{viewport_width, viewport_height, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr,
                                        &fixture.runtime) == AETHER_OK,
            "scroll model fixture runtime creation failed");
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(fixture.runtime, css, 0, "scroll-model.css",
                                                &sheet, &rules) == AETHER_OK,
            "scroll model fixture stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(
        fixture.runtime, aether_doc_get_document_element(fixture.document), &updates);
    require(update_count > 0, "scroll model fixture style resolution failed");
    aether_style_updates_free(updates, update_count);

    require(aether_style_runtime_create_resolved_style_map(fixture.runtime, fixture.document,
                                                           &fixture.styles) == AETHER_OK,
            "scroll model fixture style map creation failed");
    require(aether_layout_create(measure, nullptr, &fixture.engine) == AETHER_OK,
            "scroll model fixture layout engine creation failed");
    require(aether_layout_layout(fixture.engine, fixture.document, fixture.styles,
                                 viewport_width, viewport_height, no_scroll, nullptr,
                                 &fixture.layout) == AETHER_OK,
            "scroll model fixture layout failed");

    require(aether_display_list_builder_create(measure, nullptr, &fixture.builder) == AETHER_OK,
            "scroll model fixture builder creation failed");
    require(aether_display_list_build(fixture.builder, fixture.layout, nullptr,
                                      &fixture.list) == AETHER_OK,
            "scroll model fixture display-list creation failed");
    return fixture;
}

aether_layout_box_t box_for(aether_doc_t document, aether_layout_result_t layout, const char* id) {
    const auto element = aether_doc_get_element_by_id(document, id);
    require(element != nullptr, "scroll model fixture element lookup failed");
    const auto box = aether_layout_result_find_primary_box(layout, element);
    require(box != nullptr, "scroll model fixture layout box lookup failed");
    return box;
}

struct command_counts_s {
    int push_clip = 0;
    int pop_clip = 0;
    int push_scroll = 0;
    int pop_scroll = 0;
    int chrome = 0;
};

command_counts_s count_commands(aether_display_list_t list) {
    command_counts_s counts;
    for (int32_t index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, index, &command) == AETHER_OK,
                "scroll model fixture command read failed");
        switch (command.tag) {
            case AETHER_PAINT_PUSH_CLIP: ++counts.push_clip; break;
            case AETHER_PAINT_POP_CLIP: ++counts.pop_clip; break;
            case AETHER_PAINT_PUSH_SCROLL_OFFSET: ++counts.push_scroll; break;
            case AETHER_PAINT_POP_SCROLL_OFFSET: ++counts.pop_scroll; break;
            case AETHER_PAINT_DRAW_SCROLLBAR_CHROME: ++counts.chrome; break;
            default: break;
        }
        aether_paint_command_free(&command);
    }
    return counts;
}

} // namespace

int main() {
    /* ── overflow: clip is never a scroll container, but still clips ── */
    {
        auto fixture = build_fixture(
            "<body><div id='clip'><div></div></div></body>",
            "body { display:block; margin:0; }"
            "#clip { display:block; width:100px; height:50px; overflow:clip; }"
            "#clip > div { display:block; width:200px; height:100px; }",
            300.f, 300.f);
        const auto box = box_for(fixture.document, fixture.layout, "clip");
        const auto metrics = aether_layout_box_get_scroll_metrics(box);
        require(same(metrics.scrollable_overflow_x, 0.f) &&
                    same(metrics.scrollable_overflow_y, 0.f),
                "overflow:clip must not become a scroll container even when content overflows");
        const auto geometry = aether_layout_box_get_scrollbar_geometry(box);
        require(!geometry.horizontal_visible && !geometry.vertical_visible,
                "overflow:clip must never publish scrollbar chrome");
        const auto counts = count_commands(fixture.list);
        require(counts.push_clip == counts.pop_clip && counts.push_clip >= 1,
                "overflow:clip must still clip painted content to the padding box");
        require(counts.push_scroll == 0,
                "overflow:clip must not push a scroll offset scope");
        require(counts.chrome == 0,
                "overflow:clip must not emit scrollbar chrome");
    }

    /* ── one-axis visible → auto (computed value normalization) ── */
    {
        auto fixture = build_fixture(
            "<body><div id='one'><div></div></div></body>",
            "body { display:block; margin:0; }"
            "#one { display:block; width:100px; height:50px; overflow-x:auto; }"
            "#one > div { display:block; height:120px; }",
            300.f, 300.f);
        const auto box = box_for(fixture.document, fixture.layout, "one");
        const auto metrics = aether_layout_box_get_scroll_metrics(box);
        require(same(metrics.scrollable_overflow_y, 70.f),
                "overflow-x:auto + overflow-y:visible must normalize y to auto and scroll it");
        require(same(metrics.scrollable_overflow_x, 0.f),
                "the auto axis must not invent horizontal overflow");
    }

    /* ── one-axis clip → hidden (computed value normalization) ── */
    {
        auto fixture = build_fixture(
            "<body><div id='two'><div></div></div></body>",
            "body { display:block; margin:0; }"
            "#two { display:block; width:100px; height:50px; overflow-x:scroll; overflow-y:clip; }"
            "#two > div { display:block; height:120px; }",
            300.f, 300.f);
        const auto box = box_for(fixture.document, fixture.layout, "two");
        const auto metrics = aether_layout_box_get_scroll_metrics(box);
        require(same(metrics.scrollable_overflow_y, 70.f),
                "overflow-y:clip next to overflow-x:scroll must normalize to hidden and scroll");
        const auto geometry = aether_layout_box_get_scrollbar_geometry(box);
        require(geometry.horizontal_visible && !geometry.vertical_visible,
                "scroll always shows a bar; hidden (from clip) must not");
    }

    /* ── overflow:hidden is a programmatic scroll container, never user-scrollable ── */
    {
        auto fixture = build_fixture(
            "<body><div id='hid'><div></div></div></body>",
            "body { display:block; margin:0; }"
            "#hid { display:block; width:100px; height:50px; overflow:hidden; }"
            "#hid > div { display:block; height:120px; }",
            300.f, 300.f);
        const auto box = box_for(fixture.document, fixture.layout, "hid");
        const auto metrics = aether_layout_box_get_scroll_metrics(box);
        require(same(metrics.scrollable_overflow_y, 70.f),
                "overflow:hidden must accumulate scrollable overflow for programmatic offsets");
        const auto geometry = aether_layout_box_get_scrollbar_geometry(box);
        require(!geometry.horizontal_visible && !geometry.vertical_visible,
                "overflow:hidden must not show user scrollbar chrome");
        const auto counts = count_commands(fixture.list);
        require(counts.push_scroll >= 1,
                "overflow:hidden must still push its programmatic scroll offset while painting");
        require(counts.chrome == 0,
                "overflow:hidden must not emit user scrollbar chrome");
    }

    /* ── overflow:scroll is always a scroll container with bars, even with no overflow ── */
    {
        auto fixture = build_fixture(
            "<body><div id='always'><div></div></div></body>",
            "body { display:block; margin:0; }"
            "#always { display:block; width:100px; height:50px; overflow:scroll; }"
            "#always > div { display:block; height:20px; }",
            300.f, 300.f);
        const auto box = box_for(fixture.document, fixture.layout, "always");
        const auto metrics = aether_layout_box_get_scroll_metrics(box);
        require(same(metrics.scrollable_overflow_y, 0.f) &&
                    same(metrics.scrollable_overflow_x, 0.f),
                "overflow:scroll with fitting content has no scrollable overflow");
        const auto geometry = aether_layout_box_get_scrollbar_geometry(box);
        require(geometry.horizontal_visible && geometry.vertical_visible,
                "overflow:scroll must always publish both scrollbar geometries");
        const auto counts = count_commands(fixture.list);
        require(counts.chrome >= 1,
                "overflow:scroll must always emit scrollbar chrome");
    }

    /* ── viewport propagation: a page with no overflow rule scrolls by default ── */
    {
        auto fixture = build_fixture(
            "<body><div style='display:block; height:1000px;'></div></body>",
            "body { display:block; margin:0; }",
            300.f, 300.f);
        require(aether_layout_result_get_viewport_overflow_x(fixture.layout) == AETHER_OVERFLOW_AUTO &&
                    aether_layout_result_get_viewport_overflow_y(fixture.layout) == AETHER_OVERFLOW_AUTO,
                "visible propagated to the viewport must be interpreted as auto");
        const auto root = aether_layout_result_get_root(fixture.layout);
        const auto root_metrics = aether_layout_box_get_scroll_metrics(root);
        require(root_metrics.scrollable_overflow_y > 0.f,
                "a page without an overflow rule must scroll its root by default");
        const auto counts = count_commands(fixture.list);
        require(counts.push_scroll >= 1,
                "the scrollable root must translate its content in the display list");
    }

    /* ── viewport propagation: overflow:hidden on the propagated element keeps the page
          programmatically scrollable but hides chrome and disables user scroll ── */
    {
        auto fixture = build_fixture(
            "<body><div style='display:block; height:1000px;'></div></body>",
            "body { display:block; margin:0; overflow:hidden; }",
            300.f, 300.f);
        require(aether_layout_result_get_viewport_overflow_y(fixture.layout) == AETHER_OVERFLOW_HIDDEN,
                "overflow:hidden on the body must propagate to the viewport");
        const auto root = aether_layout_result_get_root(fixture.layout);
        const auto root_metrics = aether_layout_box_get_scroll_metrics(root);
        require(root_metrics.scrollable_overflow_y > 0.f,
                "overflow:hidden must keep the root as a programmatic scroll container");
        const auto root_geometry = aether_layout_box_get_scrollbar_geometry(root);
        require(!root_geometry.horizontal_visible && !root_geometry.vertical_visible,
                "overflow:hidden must not show viewport scrollbar chrome");
    }

    /* ── viewport propagation: overflow on html wins over a visible body ── */
    {
        auto fixture = build_fixture(
            "<html><body><div style='display:block; height:1000px;'></div></body></html>",
            "html { overflow:hidden; } body { display:block; margin:0; }",
            300.f, 300.f);
        require(aether_layout_result_get_viewport_overflow_y(fixture.layout) == AETHER_OVERFLOW_HIDDEN,
                "non-visible html overflow must propagate to the viewport instead of the body");
        const auto root = aether_layout_result_get_root(fixture.layout);
        const auto root_metrics = aether_layout_box_get_scroll_metrics(root);
        require(root_metrics.scrollable_overflow_y > 0.f,
                "html overflow:hidden must keep the root as a programmatic scroll container");
        const auto root_geometry = aether_layout_box_get_scrollbar_geometry(root);
        require(!root_geometry.horizontal_visible && !root_geometry.vertical_visible,
                "html overflow:hidden must not show viewport scrollbar chrome");
    }

    return 0;
}
