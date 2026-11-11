// Public ABI regression: parser LF suppression reaches layout and paint unchanged.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks = 0;
int pairs = 0;
std::string scenario;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL [%s]: %s\n", scenario.c_str(), message);
        throw std::runtime_error(message);
    }
}
void near(float actual, float expected, const char* message) {
    if (!(std::fabs(actual - expected) < .01f)) {
        std::fprintf(stderr, "  actual=%g expected=%g\n", actual, expected);
    }
    require(std::fabs(actual - expected) < .01f, message);
}
void same_rect(aether_rectf actual, aether_rectf expected) {
    near(actual.x, expected.x, "rectangle x");
    near(actual.y, expected.y, "rectangle y");
    near(actual.width, expected.width, "rectangle width");
    near(actual.height, expected.height, "rectangle height");
}
std::string owned(char* value) {
    require(value != nullptr, "public owned string exists");
    const std::string result(value);
    aether_string_free(value);
    return result;
}
struct Meter {
    int legacy_calls = 0;
    int contextual_calls = 0;
    static aether_text_metrics_t measure(const char* text,
        const aether_computed_style_t*, float, void* user) {
        ++static_cast<Meter*>(user)->legacy_calls;
        return {float(std::strlen(text)) * 10, 20, 15};
    }
    static aether_text_metrics_t contextual(const char* text,
        const aether_computed_style_t*, float, float, void* user) {
        ++static_cast<Meter*>(user)->contextual_calls;
        return {float(std::strlen(text)) * 10, 20, 15};
    }
};
struct Snapshot {
    aether_rectf target{};
    std::vector<aether_paint_command_tag_t> commands;
    std::vector<std::string> text;
    std::vector<aether_rectf> text_rects;
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_style_map_t map{};
    aether_layout_engine_t engine{};
    aether_layout_result_t layout{};
    aether_display_list_builder_t builder{};
    aether_display_list_t list{};
    aether_node_t target{};
    aether_node_t text_node{};
    Meter meter;
    bool contextual = false;

    Fixture() = default;
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        if (list) aether_display_list_destroy(list);
        if (builder) aether_display_list_builder_destroy(builder);
        if (layout) aether_layout_result_destroy(layout);
        if (engine) aether_layout_destroy(engine);
        if (map) aether_style_map_destroy(map);
        if (runtime) aether_style_runtime_destroy(runtime);
        if (doc) aether_doc_destroy(doc);
    }
    void verify_dom(const std::string& expected) const {
        require(aether_doc_get_element_by_id(doc, "target") == target,
                "target ID still resolves to the same element");
        require(owned(aether_element_get_attribute(target, "id")) == "target",
                "literal ID attribute");
        require(aether_node_get_child_count(target) == 1, "exactly one target text child");
        require(aether_node_get_child(target, 0) == text_node, "text node identity preserved");
        require(aether_node_get_type(text_node) == AETHER_NODE_TEXT, "child is text");
        require(aether_node_get_parent(text_node) == target, "text parent preserved");
        require(owned(aether_node_get_text_content(text_node)) == expected,
                "literal DOM bytes, independent of CSS and layout");
    }
    void init(const char* tag, const std::string& source, const std::string& expected,
              const char* mode, bool use_contextual) {
        contextual = use_contextual;
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        const std::string html = std::string("<html><body><") + tag +
            " id='target'>" + source + "</" + tag + "></body></html>";
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse fixture");
        target = aether_doc_get_element_by_id(doc, "target");
        require(target != nullptr, "target exists");
        text_node = aether_node_get_child(target, 0);
        require(text_node != nullptr, "text exists");
        // In particular, establish the neutral div's literal bytes BEFORE layout.
        verify_dom(expected);
        const aether_style_environment_t env{600, 400, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(nullptr, &env, nullptr, nullptr, &runtime) ==
                AETHER_OK, "create style runtime");
        const std::string css =
            "html,body{display:block;margin:0;padding:0;border:0}"
            "#target{display:block;width:200px;margin:0;padding:0;border:0;"
            "font-family:monospace;font-size:16px;font-weight:400;line-height:20px;"
            "text-align:left;color:#000;white-space:" + std::string(mode) + "}";
        int64_t sheet{};
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                "pre-leading-lf-consumers.css", &sheet, nullptr) == AETHER_OK,
                "install identical explicit target styles");
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_resolve_subtree(
            runtime, aether_doc_get_document_element(doc), &updates);
        require(count >= 0, "resolve subtree styles");
        aether_style_updates_free(updates, count);
        require(aether_style_runtime_create_resolved_style_map(runtime, doc, &map) ==
                AETHER_OK, "create resolved style map");
        require(aether_layout_create(Meter::measure, &meter, &engine) == AETHER_OK,
                "create layout engine");
        if (contextual) {
            aether_layout_set_contextual_text_measurer(engine, Meter::contextual, &meter);
        }
        require(aether_display_list_builder_create(Meter::measure, &meter, &builder) ==
                AETHER_OK, "create display list builder");
    }
    Snapshot render(const std::string& expected) {
        // Repeat the public layout operation with the same DOM, map and viewport.
        if (list) { aether_display_list_destroy(list); list = nullptr; }
        if (layout) { aether_layout_result_destroy(layout); layout = nullptr; }
        verify_dom(expected);
        const int calls_before = contextual ? meter.contextual_calls : meter.legacy_calls;
        require(aether_layout_layout(engine, doc, map, 600, 400, nullptr, nullptr,
                &layout) == AETHER_OK && layout != nullptr, "public layout succeeds");
        require((contextual ? meter.contextual_calls : meter.legacy_calls) > calls_before,
                "selected layout measurement callback is exercised");
        const auto box = aether_layout_result_find_primary_box(layout, target);
        require(box != nullptr, "target has primary box");
        Snapshot result;
        result.target = aether_layout_box_get_border_box(box);
        require(aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK,
                "build display list");
        const int count = aether_display_list_get_count(list);
        require(count > 0, "nonempty display list");
        for (int i = 0; i < count; ++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(list, i, &command) == AETHER_OK,
                    "read display command");
            result.commands.push_back(command.tag);
            if (command.tag == AETHER_PAINT_DRAW_TEXT) {
                const bool has_text = command.data.draw_text.text != nullptr;
                if (has_text) {
                    result.text.emplace_back(command.data.draw_text.text);
                    result.text_rects.push_back(command.data.draw_text.rect);
                }
                aether_paint_command_free(&command);
                require(has_text, "draw-text command has bytes");
            } else {
                const bool extra_text = command.tag == AETHER_PAINT_DRAW_TEXT_GRADIENT ||
                                        command.tag == AETHER_PAINT_DRAW_TEXT_SHADOW;
                aether_paint_command_free(&command);
                require(!extra_text, "no other painted text commands");
            }
        }
        require(result.text == std::vector<std::string>{"A"},
                "exactly one painted text command, literally A and no other text");
        verify_dom(expected);
        return result;
    }
};
void same_snapshot(const Snapshot& actual, const Snapshot& expected) {
    same_rect(actual.target, expected.target);
    require(actual.commands == expected.commands, "display-list count and command order");
    require(actual.text == expected.text, "display-list exact text sequence");
    require(actual.text_rects.size() == expected.text_rects.size(), "text rectangle count");
    for (size_t i = 0; i < actual.text_rects.size(); ++i) {
        same_rect(actual.text_rects[i], expected.text_rects[i]);
    }
}
void literal_pre_geometry(const Snapshot& snapshot, bool double_lf) {
    near(snapshot.target.height, double_lf ? 40.f : 20.f, "literal pre target height");
    near(snapshot.text_rects[0].y - snapshot.target.y, double_lf ? 20.f : 0.f,
         "literal pre A position relative to target");
}
void run_pair(const char* tag, bool contextual, bool double_lf, const char* mode) {
    const std::string label = std::string(tag) + "/" + (contextual ? "contextual" : "legacy") +
        "/" + (double_lf ? "double-LF" : "LF") + "/" + mode;
    const std::string expected = double_lf ? "\nA" : "A";
    const std::string source = double_lf ? "\n\nA" : "\nA";
    // Never reparse expected bytes under pre/listing: that would suppress again.
    scenario = label + "/neutral-div";
    Fixture control;
    control.init("div", expected, expected, mode, contextual);
    const auto control_first = control.render(expected);
    scenario = label + "/candidate";
    Fixture actual;
    actual.init(tag, source, expected, mode, contextual);
    const auto actual_first = actual.render(expected);
    same_snapshot(actual_first, control_first);
    if (std::strcmp(mode, "pre") == 0) {
        literal_pre_geometry(control_first, double_lf);
        literal_pre_geometry(actual_first, double_lf);
    }
    scenario = label + "/neutral-div-relayout";
    const auto control_again = control.render(expected);
    same_snapshot(control_again, control_first);
    scenario = label + "/candidate-relayout";
    const auto actual_again = actual.render(expected);
    same_snapshot(actual_again, actual_first);
    same_snapshot(actual_again, control_again);
    if (std::strcmp(mode, "pre") == 0) {
        literal_pre_geometry(control_again, double_lf);
        literal_pre_geometry(actual_again, double_lf);
    }
    ++pairs;
}
} // namespace

int main() {
    try {
        for (const char* tag : {"pre", "listing"})
            for (bool contextual : {false, true})
                for (bool double_lf : {false, true})
                    for (const char* mode : {"pre", "normal", "nowrap", "pre-line", "pre-wrap"})
                        run_pair(tag, contextual, double_lf, mode);
        require(pairs == 40, "all 40 bounded geometry pairs executed");
        std::printf("html-pre-leading-lf-consumers: PASS, %d pairs, 80 initial layouts, "
                    "80 relayouts, %d active checks\n", pairs, checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "html-pre-leading-lf-consumers: FAIL after %d pairs, "
                     "%d active checks: %s\n", pairs, checks, error.what());
        return 1;
    }
}
