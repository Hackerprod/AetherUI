#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

aether_scroll_offset_t requested_scroll(aether_node_t element, void* data) {
    return element == data ? aether_scroll_offset_t{20.f, 30.f} : aether_scroll_offset_t{};
}

aether_layout_box_t box_for(aether_doc_t document, aether_layout_result_t layout, const char* id) {
    const auto element = aether_doc_get_element_by_id(document, id);
    require(element != nullptr, "fixture element lookup failed");
    const auto box = aether_layout_result_find_primary_box(layout, element);
    require(box != nullptr, "fixture layout box lookup failed");
    return box;
}

} // namespace


// Relative visual overflow must still feed scroll metrics; normal-flow placement
// must not enlarge the parent's reserved dimensions or the child's local range.
static void verify_relative_scroll() {
    int checked = 0;
    for (const auto* inner_overflow : {"overflow:visible;", "overflow:hidden;", "overflow:clip;",
             "overflow-x:visible;overflow-y:clip;", "overflow-x:clip;overflow-y:visible;"}) {
        for (const int extent : {80,160}) {
            for (const int delta : {0,60,-60}) {
                aether_doc_t doc = nullptr;
                require(aether_doc_create(&doc) == AETHER_OK && aether_doc_parse_html(doc,
                    "<body><div id='outer'><div id='child'><div id='extent'></div></div></div></body>") == AETHER_OK,"relative scroll document");
                const int extent_height = extent == 160 ? 150:80;
                const std::string css = std::string("html,body,div{display:block;margin:0;}#outer{width:100px;height:100px;overflow:auto;}") +
                    "#child{position:relative;width:80px;height:80px;left:" + std::to_string(delta) + "px;top:" + std::to_string(delta) +
                    "px;" + inner_overflow + "}#extent{width:" + std::to_string(extent) + "px;height:" + std::to_string(extent_height) + "px;}";
                aether_style_runtime_options_t options{};aether_style_environment_t env{360,340,1,16,0,0,0};
                aether_style_runtime_t runtime = nullptr;
                require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK,"relative scroll runtime");
                int64_t sheet = 0;int32_t rules = 0;
                require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"relative-scroll.css",&sheet,&rules) == AETHER_OK,"relative scroll CSS");
                aether_style_update_t* updates = nullptr;
                const auto count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
                require(count>0,"relative scroll cascade");aether_style_updates_free(updates,count);
                aether_style_map_t styles = nullptr;
                require(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles) == AETHER_OK,"relative scroll map");
                aether_layout_engine_t engine = nullptr;
                require(aether_layout_create(measure,nullptr,&engine) == AETHER_OK,"relative scroll engine");
                aether_layout_result_t layout = nullptr;
                require(aether_layout_layout(engine,doc,styles,360,340,nullptr,nullptr,&layout) == AETHER_OK,"relative scroll layout");
                const auto outer = box_for(doc,layout,"outer"), child = box_for(doc,layout,"child");
                const auto ob = aether_layout_box_get_border_box(outer), cb = aether_layout_box_get_border_box(child);
                require(same(ob.width,100) && same(ob.height,100) && same(cb.width,80) && same(cb.height,80),"relative scroll dimensions stable");
                require(same(cb.x-ob.x,static_cast<float>(delta)) && same(cb.y-ob.y,static_cast<float>(delta)),"relative scroll child displacement");
                const bool visible_x = std::strstr(inner_overflow,"overflow:visible") || std::strstr(inner_overflow,"overflow-x:visible");
                const bool visible_y = std::strstr(inner_overflow,"overflow:visible") || std::strstr(inner_overflow,"overflow-y:visible");
                const float expected_x = std::max(0.f,static_cast<float>(delta+(visible_x?extent:80)-100));
                const float expected_y = std::max(0.f,static_cast<float>(delta+(visible_y?extent_height:80)-100));
                const auto sm = aether_layout_box_get_scroll_metrics(outer), cm = aether_layout_box_get_scroll_metrics(child);
                require(same(sm.scrollable_overflow_x,expected_x) && same(sm.scrollable_overflow_y,expected_y),"visual relative overflow and per-axis nested propagation");
                const bool hidden = std::strstr(inner_overflow,"overflow:hidden") != nullptr;
                require(same(cm.scrollable_overflow_x,hidden?static_cast<float>(extent-80):0.f) &&
                        same(cm.scrollable_overflow_y,hidden?static_cast<float>(extent_height-80):0.f),"translation preserves child-local scroll range");
                aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);++checked;
            }
        }
    }
    std::printf("relative scroll: %d layouts passed (visual extents, zero/negative ranges, per-axis nested propagation)\n",checked);
}

int main() {
    verify_relative_scroll();
    aether_doc_t document = nullptr;
    require(aether_doc_create(&document) == AETHER_OK &&
                aether_doc_parse_html(document,
                    "<html><body><div id='both'><div id='wide-tall'></div></div>"
                    "<div id='x-only'><div></div></div><div id='hidden'><div></div></div></body></html>") == AETHER_OK,
            "scroll fixture document creation failed");

    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300.f, 300.f, 1.f, 16.f, 0, 0, 0};
    aether_style_runtime_t runtime = nullptr;
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "scroll fixture runtime creation failed");
    const char* css =
        "html,body { display:block; margin:0; }"
        "#both { display:block; width:100px; height:100px; overflow:scroll; background:#ffffff; }"
        "#wide-tall { display:block; width:200px; height:200px; background:#ff0000; }"
        "#x-only { display:block; width:100px; height:30px; overflow-x:auto; }"
        "#x-only > div { display:block; width:220px; height:20px; }"
        "#hidden { display:block; width:100px; height:30px; overflow:hidden; }"
        "#hidden > div { display:block; width:220px; height:60px; }";
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css, 0, "scroll-metrics.css", &sheet, &rules) == AETHER_OK,
            "scroll fixture stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(
        runtime, aether_doc_get_document_element(document), &updates);
    require(update_count > 0, "scroll fixture style resolution failed");
    aether_style_updates_free(updates, update_count);

    aether_style_map_t styles = nullptr;
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
            "scroll fixture style map creation failed");
    aether_layout_engine_t engine = nullptr;
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK,
            "scroll fixture layout engine creation failed");
    const auto both_element = aether_doc_get_element_by_id(document, "both");
    aether_layout_result_t layout = nullptr;
    require(aether_layout_layout(engine, document, styles, 300.f, 300.f,
                requested_scroll, both_element, &layout) == AETHER_OK,
            "scroll fixture layout failed");

    const auto both = box_for(document, layout, "both");
    const auto content = box_for(document, layout, "wide-tall");
    const auto both_metrics = aether_layout_box_get_scroll_metrics(both);
    require(same(both_metrics.scrollable_overflow_x, 100.f) &&
                same(both_metrics.scrollable_overflow_y, 100.f),
            "a two-axis scroll container must report independent horizontal and vertical ranges");
    require(same(both_metrics.scroll_offset_x, 0.f) && same(both_metrics.scroll_offset_y, 0.f),
            "retained scroll must keep layout boxes in static coordinates");
    const auto both_content = aether_layout_box_get_content_box(both);
    const auto child_border = aether_layout_box_get_border_box(content);
    require(same(child_border.x, both_content.x) && same(child_border.y, both_content.y),
            "retained scroll must not translate the content subtree during layout");
    const auto geometry = aether_layout_box_get_scrollbar_geometry(both);
    const auto shifted_geometry = aether_layout_box_get_scrollbar_geometry_at_offset(both, {20.f, 30.f});
    require(geometry.horizontal_visible && geometry.vertical_visible,
            "two-axis scroll containers must publish both scrollbar geometries");
    require(geometry.horizontal_thumb.width > 0.f && geometry.vertical_thumb.height > 0.f &&
                geometry.corner.width == 4.f && geometry.corner.height == 4.f,
            "published scrollbar geometry must include both thumbs and their shared corner");
    require(shifted_geometry.horizontal_thumb.x > geometry.horizontal_thumb.x &&
                shifted_geometry.vertical_thumb.y > geometry.vertical_thumb.y,
            "scrollbar geometry must resolve live thumb positions from a supplied retained offset");

    const auto x_only = box_for(document, layout, "x-only");
    const auto x_metrics = aether_layout_box_get_scroll_metrics(x_only);
    require(same(x_metrics.scrollable_overflow_x, 120.f) && same(x_metrics.scrollable_overflow_y, 0.f),
            "overflow-x:auto must expose only horizontal scroll range when vertical content fits");
    const auto hidden = box_for(document, layout, "hidden");
    const auto hidden_metrics = aether_layout_box_get_scroll_metrics(hidden);
    require(same(hidden_metrics.scrollable_overflow_x, 120.f) &&
                same(hidden_metrics.scrollable_overflow_y, 30.f),
            "overflow:hidden is a scroll container: it must accumulate scrollable overflow for programmatic offsets");
    const auto hidden_geometry = aether_layout_box_get_scrollbar_geometry(hidden);
    require(!hidden_geometry.horizontal_visible && !hidden_geometry.vertical_visible,
            "overflow:hidden must not publish user scrollbar chrome");

    aether_display_list_builder_t builder = nullptr;
    require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK,
            "scroll fixture display-list builder creation failed");
    aether_display_list_t list = nullptr;
    require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK,
            "scroll fixture display-list creation failed");
    int push_scopes = 0;
    int pop_scopes = 0;
    int chrome = 0;
    for (int32_t index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, index, &command) == AETHER_OK,
                "scroll fixture command read failed");
        if (command.tag == AETHER_PAINT_PUSH_SCROLL_OFFSET) ++push_scopes;
        if (command.tag == AETHER_PAINT_POP_SCROLL_OFFSET) ++pop_scopes;
        if (command.tag == AETHER_PAINT_DRAW_SCROLLBAR_CHROME) ++chrome;
        aether_paint_command_free(&command);
    }
    require(push_scopes == pop_scopes && push_scopes >= 1,
            "retained scroll scopes must be balanced in the display list");
    require(chrome >= 1,
            "scrollbar chrome must be emitted as live retained commands instead of frozen fill commands");

    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
