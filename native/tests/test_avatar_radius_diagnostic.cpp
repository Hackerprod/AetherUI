#include <aether/aether.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

bool same(float actual, float expected) { return std::fabs(actual - expected) < .001f; }

aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    // This probe deliberately measures only fixed-size avatar geometry. The host-level probe
    // covers DirectWrite and the document's real text layout separately.
    return {text ? static_cast<float>(std::char_traits<char>::length(text)) * 7.f : 0.f, 16.f, 12.f};
}

std::string read_file(const char* path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "could not open the requested Kanban page");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string extract_embedded_style(const std::string& html) {
    const auto start_tag = html.find("<style>");
    const auto end_tag = html.find("</style>", start_tag == std::string::npos ? 0 : start_tag + 7);
    require(start_tag != std::string::npos && end_tag != std::string::npos,
            "Kanban page does not contain its expected embedded stylesheet");
    return html.substr(start_tag + 7, end_tag - (start_tag + 7));
}

void collect_avatar_elements(aether_node_t node, std::vector<aether_node_t>& output) {
    if (!node) return;
    if (aether_node_get_type(node) == AETHER_NODE_ELEMENT && aether_element_has_class(node, "mini-avatar")) {
        output.push_back(node);
    }
    const auto count = aether_node_get_child_count(node);
    for (int32_t index = 0; index < count; ++index) {
        collect_avatar_elements(aether_node_get_child(node, index), output);
    }
}

bool matches_color(const aether_color_rgba& actual, const std::array<float, 3>& expected) {
    return same(actual.r, expected[0]) && same(actual.g, expected[1]) && same(actual.b, expected[2]);
}

bool matches_rect(const aether_rectf& actual, const aether_rectf& expected) {
    return same(actual.x, expected.x) && same(actual.y, expected.y) &&
           same(actual.width, expected.width) && same(actual.height, expected.height);
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 2,
            "usage: aether_avatar_radius_diagnostic <absolute-path-to-kanban_board.html>");
    const auto html = read_file(argv[1]);
    const auto css = extract_embedded_style(html);

    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;

    require(aether_doc_create(&document) == AETHER_OK && aether_doc_parse_html(document, html.c_str()) == AETHER_OK,
            "Kanban document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{770.f, 870.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &runtime) == AETHER_OK,
            "Kanban style runtime creation failed");
    int64_t sheet = 0;
    int32_t rules = 0;
    require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0, "kanban_board.html", &sheet, &rules) == AETHER_OK,
            "Kanban stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(runtime, aether_doc_get_body(document), &updates);
    require(update_count > 0, "Kanban style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(runtime, document, &styles) == AETHER_OK,
            "Kanban style map creation failed");
    require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK &&
                aether_layout_layout(engine, document, styles, 770.f, 870.f, nullptr, nullptr, &layout) == AETHER_OK,
            "Kanban layout failed");

    std::vector<aether_node_t> avatars;
    collect_avatar_elements(aether_doc_get_document_element(document), avatars);
    require(avatars.size() == 4, "Kanban page must contain its four authored mini-avatar elements");

    std::vector<aether_rectf> avatar_boxes;
    avatar_boxes.reserve(avatars.size());
    bool all_boxes_are_square = true;
    for (size_t index = 0; index < avatars.size(); ++index) {
        const auto box = aether_layout_result_find_primary_box(layout, avatars[index]);
        require(box != nullptr, "Kanban avatar did not produce a primary layout box");
        const auto border_box = aether_layout_box_get_border_box(box);
        std::printf("avatar[%zu] border-box: x=%.3f y=%.3f width=%.3f height=%.3f\n",
                    index, border_box.x, border_box.y, border_box.width, border_box.height);
        all_boxes_are_square &= same(border_box.width, 28.f) && same(border_box.height, 28.f);
        avatar_boxes.push_back(border_box);
    }
    require(all_boxes_are_square,
            "every fixed 24px Kanban avatar plus a 2px border must remain a 28px square");

    require(aether_display_list_builder_create(measure, nullptr, &builder) == AETHER_OK &&
                aether_display_list_build(builder, layout, nullptr, &list) == AETHER_OK,
            "Kanban display-list construction failed");

    const std::array<std::array<float, 3>, 4> expected_colors{{
        {{0x3b / 255.f, 0x82 / 255.f, 0xf6 / 255.f}},
        {{0xec / 255.f, 0x48 / 255.f, 0x99 / 255.f}},
        {{0x8b / 255.f, 0x5c / 255.f, 0xf6 / 255.f}},
        {{0x10 / 255.f, 0xb9 / 255.f, 0x81 / 255.f}},
    }};
    std::array<bool, 4> found_fill{};
    std::array<bool, 4> found_stroke{};
    for (int32_t index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list, index, &command) == AETHER_OK,
                "Kanban display-list command read failed");
        for (size_t avatar_index = 0; avatar_index < avatar_boxes.size(); ++avatar_index) {
            const auto& box = avatar_boxes[avatar_index];
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT &&
                matches_color(command.data.fill_rounded_rect.color, expected_colors[avatar_index]) &&
                matches_rect(command.data.fill_rounded_rect.rect, box)) {
                const auto& fill = command.data.fill_rounded_rect;
                std::printf("avatar[%zu] fill: width=%.3f height=%.3f radii=(%.3f, %.3f)\n",
                            avatar_index, fill.rect.width, fill.rect.height,
                            fill.radii.top_left_x, fill.radii.top_left_y);
                found_fill[avatar_index] = same(fill.radii.top_left_x, 14.f) && same(fill.radii.top_left_y, 14.f);
            }
            const aether_rectf expected_stroke{box.x + 1.f, box.y + 1.f, box.width - 2.f, box.height - 2.f};
            if (command.tag == AETHER_PAINT_STROKE_ROUNDED_RECT &&
                matches_color(command.data.stroke_rounded_rect.color, {{1.f, 1.f, 1.f}}) &&
                matches_rect(command.data.stroke_rounded_rect.rect, expected_stroke)) {
                const auto& stroke = command.data.stroke_rounded_rect;
                std::printf("avatar[%zu] stroke: width=%.3f height=%.3f radii=(%.3f, %.3f) stroke=%.3f\n",
                            avatar_index, stroke.rect.width, stroke.rect.height,
                            stroke.radii.top_left_x, stroke.radii.top_left_y, stroke.stroke_width);
                found_stroke[avatar_index] = same(stroke.radii.top_left_x, 13.f) &&
                                            same(stroke.radii.top_left_y, 13.f) && same(stroke.stroke_width, 2.f);
            }
        }
        aether_paint_command_free(&command);
    }
    for (size_t index = 0; index < avatars.size(); ++index) {
        require(found_fill[index], "Kanban avatar fill must preserve a 28px square with 14px circular radii");
        require(found_stroke[index], "Kanban avatar stroke must preserve matching 26px inner-circle geometry");
    }

    aether_display_list_destroy(list);
    aether_display_list_builder_destroy(builder);
    aether_layout_result_destroy(layout);
    aether_layout_destroy(engine);
    aether_style_map_destroy(styles);
    aether_style_runtime_destroy(runtime);
    aether_doc_destroy(document);
    return 0;
}
