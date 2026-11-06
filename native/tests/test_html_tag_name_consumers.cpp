#include <aether/aether.h>
#include "form_controls.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool same(float a, float b) { return std::fabs(a - b) < .001f; }
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? 8.f * static_cast<float>(std::strlen(text)) : 0.f, 16.f, 12.f};
}
struct Frame {
    std::map<aether_node_t, aether_computed_style_handle_t> snapshots;
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;
    Frame(const std::string& html, const char* css) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse fixture");
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{200.f, 100.f, 1.f, 16.f, 0, 0, 0};
        require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
                "create style runtime");
        int64_t sheet = 0; int32_t rules = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css, 0, "tag-consumers.css", &sheet, &rules) == AETHER_OK,
                "add stylesheet");
        aether_style_update_t* updates = nullptr;
        auto count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_document_element(doc), &updates);
        require(count > 0, "resolve styles");
        for (int32_t i = 0; i < count; ++i) {
            aether_node_t node = nullptr; aether_computed_style_handle_t handle = nullptr;
            require(aether_style_runtime_get_update(updates[i], &node, &handle, nullptr) == AETHER_OK, "snapshot style update");
            snapshots.emplace(node, handle);
        }
        aether_style_updates_free(updates, count);
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &styles) == AETHER_OK, "resolved map");
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "create layout");
        require(aether_layout_layout(engine, doc, styles, 200.f, 100.f, nullptr, nullptr, &layout) == AETHER_OK,
                "layout fixture");
        require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK, "create painter");
        require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK, "paint fixture");
    }
    ~Frame() {
        for (const auto& entry : snapshots) aether_computed_style_handle_destroy(entry.second);
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); aether_layout_destroy(engine);
        aether_style_map_destroy(styles); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;
    aether_node_t node(const char* id) {
        auto result = aether_doc_get_element_by_id(doc, id); require(result != nullptr, "fixture node exists"); return result;
    }
    aether_rectf rect(const char* id) {
        auto box = aether_layout_result_find_primary_box(layout, node(id)); if (!box) std::fprintf(stderr, "Missing primary box: %s\n", id); require(box != nullptr, "primary box exists");
        return aether_layout_box_get_border_box(box);
    }
    aether_computed_style_t style(const char* id) {
        auto handle = snapshots.at(node(id)); aether_computed_style_t result{};
        require(handle && aether_computed_style_copy(handle, &result) == AETHER_OK, "copy computed style");
        return result;
    }
    int display(const char* id) {
        auto value = style(id); const int display = value.display;
        aether_computed_style_free(&value); return display;
    }
    std::string control_text() {
        auto value = style("control");
        auto text = aether_opaque_control_display_text(node("control"), value);
        aether_computed_style_free(&value); return text;
    }
    aether_rectf text_rect(const char* text) {
        aether_rectf result{}; int matches = 0;
        for (int32_t i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read text geometry command");
            if (command.tag == AETHER_PAINT_DRAW_TEXT && command.data.draw_text.text &&
                std::strcmp(command.data.draw_text.text, text) == 0) {
                result = command.data.draw_text.rect; ++matches;
            }
            aether_paint_command_free(&command);
        }
        require(matches == 1, "unique text geometry exists"); return result;
    }
    int painted_text(const char* text) {
        int matches = 0;
        for (int32_t i = 0; i < aether_display_list_get_count(list); ++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(list, i, &command) == AETHER_OK, "read paint command");
            if (command.tag == AETHER_PAINT_DRAW_TEXT && command.data.draw_text.text &&
                std::strcmp(command.data.draw_text.text, text) == 0) ++matches;
            aether_paint_command_free(&command);
        }
        return matches;
    }
};
void equal_rect(aether_rectf a, aether_rectf b) {
    require(same(a.x, b.x) && same(a.y, b.y) && same(a.width, b.width) && same(a.height, b.height),
            "case variants must have identical real layout geometry");
}
void controls() {
    const char* tags[][3] = {{"input", "INPUT", "InPuT"}, {"button", "BUTTON", "BuTtOn"},
                            {"textarea", "TEXTAREA", "TeXtArEa"}, {"select", "SELECT", "SeLeCt"}};
    const aether_form_control_content_model_e models[] = {
        AETHER_FORM_CONTROL_OPAQUE_SINGLE_LINE, AETHER_FORM_CONTROL_FLOW_CONTENT,
        AETHER_FORM_CONTROL_MULTILINE_EDITABLE, AETHER_FORM_CONTROL_ORDINARY_CONTENT};
    const char* css = "#root,#body{display:block;margin:0;padding:0}"
                      "#control{display:block;width:80px;height:24px;box-sizing:content-box;"
                      "padding:0;margin:0;border-width:0;font-size:16px;line-height:1}";
    for (int kind = 0; kind < 4; ++kind) {
        aether_rectf lower{};
        for (int variant = 0; variant < 3; ++variant) {
            std::string html = "<html id='root'><body id='body'><";
            html += tags[kind][variant]; html += " id='control' value='ValueText' placeholder='HintText'>";
            if (kind != 0) { html += "ChildText</"; html += tags[kind][0]; html += ">"; }
            html += "</body></html>";
            Frame frame(html, css);
            auto node = frame.node("control");
            require(aether_form_control_content_model(node) == models[kind], "literal native control classification");
            require(aether_form_control_is_opaque_single_line(node) == (kind == 0), "only INPUT is opaque single-line");
            auto rect = frame.rect("control");
            require(same(rect.width, 80.f) && same(rect.height, 24.f), "literal control border-box dimensions");
            if (variant == 0) lower = rect; else equal_rect(lower, rect);
            if (kind == 0) {
                require(frame.control_text() == "ValueText", "INPUT value precedence");
                require(frame.painted_text("ValueText") == 1 && frame.painted_text("HintText") == 0,
                        "INPUT value must paint exactly once, not placeholder");
            } else {
                require(frame.control_text().empty(), "non-input must not use input value");
                require(frame.painted_text("ChildText") > 0, "non-input content must survive actual painting");
            }
        }
    }
    for (const char* tag : tags[0]) {
        for (const char* value : {"", " value=''"}) {
            Frame frame(std::string("<html id='root'><body id='body'><") + tag +
                        " id='control' placeholder='HintText'" + value + "></body></html>", css);
            require(frame.control_text() == "HintText",
                    "missing or empty INPUT value must select placeholder");
            require(frame.painted_text("HintText") == 1, "INPUT placeholder must reach actual painter");
        }
    }
}
void rcdata_controls() {
    const char* css = "#root,#body{display:block;margin:0;padding:0}"
                      "#control{display:block;width:160px;height:24px;white-space:pre;"
                      "padding:0;margin:0;border-width:0;font-size:16px;line-height:1}";
    for (const char* tag : {"textarea", "TEXTAREA", "TeXtArEa"}) {
        for (const char* terminator : {">", "/>", " />"}) {
            Frame frame(std::string("<html id='root'><body id='body'><") + tag +
                        " id='control' value='ValueText' placeholder='HintText'" + terminator +
                        "\r\n&lt;b&gt;&amp;</TeXtArEa><p id='after'>Tail</p></body></html>", css);
            const auto control = frame.node("control");
            require(aether_form_control_content_model(control) == AETHER_FORM_CONTROL_MULTILINE_EDITABLE,
                    "RCDATA textarea retains multiline control model");
            require(!aether_form_control_is_opaque_single_line(control) && frame.control_text().empty(),
                    "textarea does not adopt INPUT value or placeholder");
            require(aether_node_get_child_count(control) == 1, "RCDATA textarea has one text child");
            const auto text = aether_node_get_child(control, 0);
            require(aether_node_get_type(text) == AETHER_NODE_TEXT && aether_node_get_parent(text) == control,
                    "decoded markup remains a direct text child");
            char* content = aether_node_get_text_content(text);
            require(content && std::strcmp(content, "<b>&") == 0, "textarea normalized and decoded content");
            aether_string_free(content);
            require(frame.painted_text("<b>&") == 1, "RCDATA text paints exactly once");
            require(frame.painted_text("ValueText") == 0 && frame.painted_text("HintText") == 0,
                    "textarea value and placeholder do not replace text");
            require(aether_node_get_parent(frame.node("after")) == frame.node("body"),
                    "post-RCDATA content is a body sibling");
        }
    }
    for (const char* tag : {"button", "select", "div"}) {
        Frame frame(std::string("<html id='root'><body id='body'><") + tag +
                    " id='control'><b id='child'>ChildText</b></" + tag + "></body></html>", css);
        require(aether_node_get_parent(frame.node("child")) == frame.node("control"),
                "ordinary flow controls retain their element children");
        require(frame.painted_text("ChildText") == 1, "ordinary flow children still paint");
    }
}
void breaks() {
    const char* css = "#root,#body{display:block;margin:0;padding:0;font-size:16px;line-height:1}"
                      "#hidden{display:none}#block{display:block;width:9px;height:7px}";
    for (const char* tag : {"br", "BR", "bR"}) {
        std::string html = "<html id='root'><body id='body'><span id='before'>A</span><";
        html += tag; html += " id='natural'><span id='after'>B</span><";
        html += tag; html += " id='hidden'><"; html += tag; html += " id='block'></body></html>";
        Frame frame(html, css);
        require(frame.display("natural") == AETHER_DISPLAY_LINE_BREAK, "BR without display must become LINE_BREAK");
        require(frame.text_rect("B").y > frame.text_rect("A").y, "natural BR must move following text to next line");
        require(frame.display("hidden") == AETHER_DISPLAY_NONE, "explicit BR display:none wins");
        require(!aether_layout_result_find_primary_box(frame.layout, frame.node("hidden")), "hidden BR has no layout box");
        require(frame.display("block") == AETHER_DISPLAY_BLOCK, "explicit BR display:block wins");
        auto rect = frame.rect("block");
        require(same(rect.width, 9.f) && same(rect.height, 7.f), "overridden BR uses block geometry");
    }
}
bool canvas_rect(aether_rectf rect) {
    return same(rect.x, 0.f) && same(rect.y, 0.f) && same(rect.width, 200.f) && same(rect.height, 100.f);
}
void canvas() {
    for (const char* body_tag : {"body", "BODY"}) {
      for (const char* tag : {"html", "HTML", "HtMl"}) {
        // IDs deliberately avoid relying on tag selector case-insensitivity.
        Frame frame(std::string("<") + tag + " id='root'><" + body_tag + " id='body'><div id='content'></div></body></html>",
            "#root{display:block;background-color:#010203}"
            "#body{display:block;margin:8px;background-color:#aabbcc}"
            "#content{display:block;width:20px;height:10px}");
        require(aether_layout_box_get_element(aether_layout_result_get_root(frame.layout)) == frame.node("body"),
                "canvas layout root must be the real body, not document-element fallback");
        int roots = 0, bodies = 0;
        for (int32_t i = 0; i < aether_display_list_get_count(frame.list); ++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list, i, &command) == AETHER_OK, "read canvas command");
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
                auto color = command.data.fill_rounded_rect.color;
                auto rect = command.data.fill_rounded_rect.rect;
                if (same(color.r, 1.f/255.f) && same(color.g, 2.f/255.f) && same(color.b, 3.f/255.f)) {
                    ++roots; require(canvas_rect(rect), "HTML background must cover literal viewport canvas");
                }
                if (same(color.r, 170.f/255.f) && same(color.g, 187.f/255.f) && same(color.b, 204.f/255.f)) {
                    ++bodies; require(!canvas_rect(rect), "BODY retains distinct ordinary box background");
                    equal_rect(rect, frame.rect("body"));
                }
            }
            aether_paint_command_free(&command);
        }
        require(roots == 1 && bodies == 1, "root canvas and body background must each paint exactly once");
      }
    }
}
}
int main(int argc, char** argv) {
    // Optional sections make independent pre-patch failures reproducible.
    const std::string section = argc > 1 ? argv[1] : "all";
    require(argc <= 2 && (section == "all" || section == "controls" || section == "rcdata" ||
                         section == "breaks" || section == "canvas"),
            "unknown test section");
    if (section == "all" || section == "controls") controls();
    if (section == "all" || section == "rcdata") rcdata_controls();
    if (section == "all" || section == "breaks") breaks();
    if (section == "all" || section == "canvas") canvas();
    return 0;
}
