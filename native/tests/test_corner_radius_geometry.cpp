#include <aether/aether.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <limits>

namespace {
int checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "FAIL corner geometry: %s\n", message);
        std::exit(1);
    }
}

bool same(float a, float b) { return std::isfinite(a) && std::isfinite(b) && std::fabs(a - b) < .001f; }
bool same_rect(aether_rectf a, aether_rectf b) {
    return same(a.x, b.x) && same(a.y, b.y) &&
           same(a.width, b.width) && same(a.height, b.height);
}

void expect_radii(aether_corner_radii actual, aether_corner_radii expected, const char* message) {
    // Name the scalar fields explicitly rather than indexing across struct members.
    const float a[] = {actual.top_left_x, actual.top_left_y, actual.top_right_x, actual.top_right_y,
                       actual.bottom_right_x, actual.bottom_right_y, actual.bottom_left_x, actual.bottom_left_y};
    const float e[] = {expected.top_left_x, expected.top_left_y, expected.top_right_x, expected.top_right_y,
                       expected.bottom_right_x, expected.bottom_right_y, expected.bottom_left_x, expected.bottom_left_y};
    for (int i = 0; i < 8; ++i) {
        const bool matches = std::isfinite(a[i]) && std::isfinite(e[i]) &&
            (e[i] == 0 ? a[i] == 0 : a[i] > 0 &&
             std::fabs(double(a[i]) - e[i]) <= 4 * std::numeric_limits<float>::epsilon() * std::fabs(double(e[i])));
        if (!matches)
            std::fprintf(stderr, "%s axis %d: actual %g, expected %g\n", message, i, a[i], e[i]);
        require(matches, message);
    }
}

aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

struct Frame {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;
    aether_rectf border_box{};

    ~Frame() {
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout);
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(document);
    }
};

void build(Frame& f, const char* declaration, bool focused = false) {
    require(aether_doc_create(&f.document) == AETHER_OK, "create document");
    require(aether_doc_parse_html(f.document,
        "<html><body><div id='panel'></div></body></html>") == AETHER_OK, "parse document");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{300, 200, 1, 16, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &f.runtime) == AETHER_OK,
            "create runtime");
    // The child is not the canvas background source. Margins place it away from
    // the canvas origin so command matching includes its actual measured box.
    const std::string css = std::string("html,body{display:block;margin:0;background:transparent;}"
        "#panel{display:block;margin-left:13px;margin-top:17px;padding:0;background:#123456;") + declaration + "}";
    int64_t sheet{};
    int32_t rules{};
    require(aether_style_runtime_add_stylesheet(f.runtime, css.c_str(), 0, "corner-geometry.css",
        &sheet, &rules) == AETHER_OK, "add stylesheet");
    aether_style_update_t* updates = nullptr;
    const int count = aether_style_runtime_resolve_subtree(f.runtime,
        aether_doc_get_document_element(f.document), &updates);
    require(count > 0, "resolve styles");
    aether_style_updates_free(updates, count);
    require(aether_style_runtime_create_resolved_style_map(f.runtime, f.document, &f.styles) == AETHER_OK,
            "create resolved map");
    require(aether_layout_create(measure, nullptr, &f.engine) == AETHER_OK, "create layout engine");
    require(aether_layout_layout(f.engine, f.document, f.styles, 300, 200, nullptr, nullptr,
        &f.layout) == AETHER_OK, "layout");
    const auto panel = aether_doc_get_element_by_id(f.document, "panel");
    require(panel != nullptr, "panel exists");
    const auto box = aether_layout_result_find_primary_box(f.layout, panel);
    require(box != nullptr, "panel has primary box");
    f.border_box = aether_layout_box_get_border_box(box);
    require(aether_display_list_builder_create(measure, nullptr, &f.builder) == AETHER_OK, "create display builder");
    require(aether_display_list_build(f.builder, f.layout, focused ? panel : nullptr, &f.list) == AETHER_OK,
            "build display list");
}

void asymmetric_consumers(bool huge = false) {
    Frame f;
    // Content-box 192x92 plus four 4px borders gives the required 200x100 box.
    // Different side colors intentionally select border ring commands rather
    // than the uniform-border stroke optimization.
    const std::string declaration = std::string("width:192px;height:92px;border-width:4px;border-style:solid;"
        "border-top-color:red;border-right-color:green;border-bottom-color:blue;border-left-color:black;"
        ) + (huge ? "border-radius:3e38px;" :
        "border-top-left-radius:25% 50%;border-top-right-radius:12px 18px;"
        "border-bottom-right-radius:16px 22px;border-bottom-left-radius:20px 24px;") +
        "overflow:hidden;box-shadow:3px 5px 2px black;backdrop-filter:blur(2px);";
    build(f, declaration.c_str(), true);
    const auto box = f.border_box;
    require(same(box.width, 200) && same(box.height, 100), "measured asymmetric border box is 200x100");
    const aether_rectf inner{box.x + 4, box.y + 4, 192, 92};
    const aether_rectf focus{box.x - 2, box.y - 2, 204, 104};
    // Independent literal expectations, not calls to the production resolver.
    const aether_corner_radii outer = huge ? aether_corner_radii{50,50,50,50,50,50,50,50} :
        aether_corner_radii{50, 50, 12, 18, 16, 22, 20, 24};
    const aether_corner_radii inset = huge ? aether_corner_radii{46,46,46,46,46,46,46,46} :
        aether_corner_radii{46, 46, 8, 14, 12, 18, 16, 20};
    const aether_corner_radii inflated = huge ? aether_corner_radii{52,52,52,52,52,52,52,52} :
        aether_corner_radii{52, 52, 14, 20, 18, 24, 22, 26};
    int backgrounds = 0, clips = 0, focuses = 0, shadows = 0, backdrops = 0;
    int sides[4]{};
    for (int32_t i = 0; i < aether_display_list_get_count(f.list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(f.list, i, &command) == AETHER_OK, "read asymmetric command");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT && same_rect(command.data.fill_rounded_rect.rect, box)) {
            ++backgrounds;
            expect_radii(command.data.fill_rounded_rect.radii, outer, "background independent axes");
        } else if (command.tag == AETHER_PAINT_FILL_BORDER_SIDE && same_rect(command.data.fill_border_side.outer_rect, box)) {
            const auto& border = command.data.fill_border_side;
            require(border.side >= 0 && border.side < 4, "border side identity");
            ++sides[border.side];
            require(same_rect(border.inner_rect, inner), "border inner rectangle");
            expect_radii(border.outer_radii, outer, "border outer axes");
            expect_radii(border.inner_radii, inset, "border inset axes");
        } else if (command.tag == AETHER_PAINT_PUSH_CLIP && same_rect(command.data.push_clip.rect, inner)) {
            ++clips;
            expect_radii(command.data.push_clip.radii, inset, "overflow padding clip axes");
        } else if (command.tag == AETHER_PAINT_STROKE_ROUNDED_RECT && same_rect(command.data.stroke_rounded_rect.rect, focus)) {
            ++focuses;
            require(same(command.data.stroke_rounded_rect.stroke_width, 1), "focus stroke width");
            expect_radii(command.data.stroke_rounded_rect.radii, inflated, "focus inflated axes");
        } else if (command.tag == AETHER_PAINT_DRAW_BOX_SHADOW && same_rect(command.data.draw_box_shadow.rect, box)) {
            ++shadows;
            expect_radii(command.data.draw_box_shadow.radii, outer, "outer shadow axes");
            require(command.data.draw_box_shadow.shadow.inset == AETHER_FALSE, "outer shadow identity");
        } else if (command.tag == AETHER_PAINT_APPLY_BACKDROP_FILTER && same_rect(command.data.apply_backdrop_filter.rect, box)) {
            ++backdrops;
            expect_radii(command.data.apply_backdrop_filter.radii, outer, "backdrop axes");
            require(command.data.apply_backdrop_filter.filters.count == 1, "one backdrop blur");
        }
        aether_paint_command_free(&command);
    }
    require(backgrounds == 1, "one matching child background");
    for (int side : sides) require(side == 1, "one matching border command for each side");
    require(clips == 1, "one matching rounded overflow clip");
    require(focuses == 1, "one matching focus ring");
    require(shadows == 1, "one matching outer shadow");
    require(backdrops == 1, "one matching backdrop command");
}

void background_control(const char* declaration, aether_corner_radii expected, const char* label, float computed_px = 0) {
    Frame f;
    const std::string css = std::string("width:100px;height:50px;") + declaration;
    build(f, css.c_str());
    require(same(f.border_box.width, 100) && same(f.border_box.height, 50), "control measured border box");
    if (computed_px > 0) {
        const auto panel = aether_doc_get_element_by_id(f.document, "panel");
        const auto handle = aether_style_map_get(f.styles, panel);
        require(handle != nullptr, "computed radius handle");
        aether_computed_style_t style{};
        require(aether_computed_style_copy(handle, &style) == AETHER_OK, "computed radius copy");
        const auto& r = style.border_radii;
        for (const auto* v : {&r.top_left.horizontal,&r.top_left.vertical,&r.top_right.horizontal,&r.top_right.vertical,
                             &r.bottom_right.horizontal,&r.bottom_right.vertical,&r.bottom_left.horizontal,&r.bottom_left.vertical}) {
            require(std::isfinite(v->value) && v->value == computed_px && v->unit == AETHER_CSS_UNIT_PX &&
                v->linear_percent == 0 && v->linear_em == 0 && v->linear_rem == 0 && v->linear_vw == 0 && v->linear_vh == 0,
                "all seven computed PX fields survive before normalization");
        }
        aether_computed_style_free(&style);
        aether_computed_style_handle_destroy(handle);
    }
    int found = 0;
    for (int32_t i = 0; i < aether_display_list_get_count(f.list); ++i) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(f.list, i, &command) == AETHER_OK, "read control command");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT && same_rect(command.data.fill_rounded_rect.rect, f.border_box)) {
            ++found;
            expect_radii(command.data.fill_rounded_rect.radii, expected, label);
        }
        aether_paint_command_free(&command);
    }
    require(found == 1, "one matching control background");
}

void uniform_border() {
    Frame f;
    build(f,"width:92px;height:42px;border:4px solid red;border-radius:3e38px;");
    const auto b = f.border_box;
    require(same(b.width,100) && same(b.height,50),"uniform border box");
    const aether_rectf stroke{b.x+2,b.y+2,96,46};
    int strokes = 0, backgrounds = 0;
    for (int i=0; i<aether_display_list_get_count(f.list); ++i) {
        aether_paint_command_t c{};
        require(aether_display_list_get_command(f.list,i,&c)==AETHER_OK,"uniform command");
        if (c.tag == AETHER_PAINT_STROKE_ROUNDED_RECT && same_rect(c.data.stroke_rounded_rect.rect,stroke)) {
            ++strokes;
            require(same(c.data.stroke_rounded_rect.stroke_width,4),"uniform width");
            expect_radii(c.data.stroke_rounded_rect.radii,{23,23,23,23,23,23,23,23},"uniform half-width inset");
        }
        if (c.tag == AETHER_PAINT_FILL_ROUNDED_RECT && same_rect(c.data.fill_rounded_rect.rect,b)) {
            ++backgrounds;
            expect_radii(c.data.fill_rounded_rect.radii,{25,25,25,25,25,25,25,25},"uniform outer radii");
        }
        aether_paint_command_free(&c);
    }
    require(strokes==1 && backgrounds==1,"one uniform stroke and background");
}
} // namespace

int main() {
    asymmetric_consumers();
    asymmetric_consumers(true);
    uniform_border();
    background_control("border-radius:9999px;",{25,25,25,25,25,25,25,25},"ordinary large",9999);
    background_control("border-radius:3e38px;",{25,25,25,25,25,25,25,25},"finite huge",3e38f);
    background_control("border-radius:3e38px / 10px 20px 30px 40px;",
        {50,1.66666666e-36f,50,3.33333332e-36f,50,4.99999998e-36f,50,6.66666664e-36f},"huge asymmetric positive axes");
    // min(100/160, 50/80) = .625: one scale applies to all eight axes.
    background_control("border-top-left-radius:80px 40px;border-top-right-radius:80px 40px;"
        "border-bottom-right-radius:80px 40px;border-bottom-left-radius:80px 40px;",
        {50, 25, 50, 25, 50, 25, 50, 25}, "common overlap scale .625");
    background_control("border-top-left-radius:0;border-top-right-radius:0;"
        "border-bottom-right-radius:0;border-bottom-left-radius:0;", {}, "zero rectangular control");
    background_control("border-top-left-radius:calc(0px - 5px) 20px;",
        {0, 20, 0, 0, 0, 0, 0, 0}, "negative typed calc clamps only at used geometry");
    std::printf("corner geometry: %d active NDEBUG checks passed\n", checks);
    return 0;
}
