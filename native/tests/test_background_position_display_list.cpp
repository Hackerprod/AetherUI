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

aether_text_metrics_t measure_text(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? static_cast<float>(std::strlen(text)) : 0.f, 1.f, .8f};
}

struct frame_s {
    aether_doc_t document = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t styles = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;

    ~frame_s() {
        aether_display_list_destroy(list);
        aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout);
        aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(document);
    }
};

void build(frame_s& frame, const char* css) {
    require(aether_doc_create(&frame.document) == AETHER_OK &&
                aether_doc_parse_html(frame.document, "<html><body><div id='grid'></div></body></html>") == AETHER_OK,
            "gradient tile fixture document creation failed");
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{200.f, 120.f, 1.f, 16.f, 0, 0, 0};
    require(aether_style_runtime_create(&options, &environment, nullptr, nullptr, &frame.runtime) == AETHER_OK,
            "gradient tile fixture runtime creation failed");
    int64_t sheet = 0;
    int32_t rule_count = 0;
    require(aether_style_runtime_add_stylesheet(frame.runtime, css, 0, "gradient-tiles.css", &sheet, &rule_count) == AETHER_OK,
            "gradient tile fixture stylesheet creation failed");
    aether_style_update_t* updates = nullptr;
    const auto update_count = aether_style_runtime_resolve_subtree(
        frame.runtime, aether_doc_get_document_element(frame.document), &updates);
    require(update_count > 0, "gradient tile fixture style resolution failed");
    aether_style_updates_free(updates, update_count);
    require(aether_style_runtime_create_resolved_style_map(frame.runtime, frame.document, &frame.styles) == AETHER_OK,
            "gradient tile fixture style map creation failed");
    require(aether_layout_create(measure_text, nullptr, &frame.engine) == AETHER_OK &&
                aether_layout_layout(frame.engine, frame.document, frame.styles, 200.f, 120.f, nullptr, nullptr, &frame.layout) == AETHER_OK,
            "gradient tile fixture layout failed");
    require(aether_display_list_builder_create(measure_text, nullptr, &frame.builder) == AETHER_OK &&
                aether_display_list_build(frame.builder, frame.layout, nullptr, &frame.list) == AETHER_OK,
            "gradient tile fixture display-list creation failed");
}

} // namespace

int main() {
    struct test_case { const char* position; float x, y; };
    const test_case cases[] = {
        {"center left",0,20}, {"center right",60,20}, {"top center",30,0},
        {"center top",30,0}, {"10px top",10,0}, {"center 10px",30,10},
        {"top 10px",12,12}, {"var(--bad,90% 90%)",0,0},
        {"var(--missing,90% 90%)",54,36}, {"calc(25% + 2px) center",17,20}
    };
    for (int shorthand=0; shorthand<2; ++shorthand) for (const auto& item:cases) {
        frame_s frame{};
        std::string css = "html, body { display:block; margin:0; background:transparent; }"
            "#grid { display:block; width:100px; height:80px; --bad:top 10px;"
            "background-image:linear-gradient(red,blue);background-size:40px 40px;"
            "background-repeat:no-repeat;background-position:20% 30%;";
        // Deferred shorthand has separate invalid-winner behavior, covered by context tests.
        if (shorthand && std::strstr(item.position,"var(")) continue;
        css += shorthand ? "background:linear-gradient(red,blue) " : "background-position:";
        css += item.position;
        css += shorthand ? " / 40px 40px no-repeat;}" : ";}";
        build(frame, css.c_str());
        auto box = aether_layout_result_find_primary_box(frame.layout,
            aether_doc_get_element_by_id(frame.document,"grid"));
        require(box != nullptr,"position fixture layout box");
        const auto origin = aether_layout_box_get_border_box(box);
        int gradients=0;
        for (int32_t index=0; index<aether_display_list_get_count(frame.list); ++index) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list,index,&command)==AETHER_OK,"read command");
            if(command.tag==AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT) {
                ++gradients; const auto& tile=command.data.fill_rounded_rect_gradient;
                if (!same(tile.rect.x,origin.x+item.x) || !same(tile.rect.y,origin.y+item.y))
                    std::fprintf(stderr,"position %s shorthand=%d actual=(%g,%g) expected=(%g,%g)\n",
                        item.position,shorthand,tile.rect.x-origin.x,tile.rect.y-origin.y,item.x,item.y);
                require(same(tile.rect.x,origin.x+item.x) && same(tile.rect.y,origin.y+item.y),"positioned tile offset");
                require(same(tile.rect.width,40) && same(tile.rect.height,40),"visible tile size");
                require(same(tile.gradient_rect.x,tile.rect.x) && same(tile.gradient_rect.y,tile.rect.y) &&
                    same(tile.gradient_rect.width,40) && same(tile.gradient_rect.height,40),"full gradient geometry");
            }
            aether_paint_command_free(&command);
        }
        require(gradients==1,"exactly one positioned gradient");
    }
    {
        frame_s frame{};
        build(frame,"html,body{display:block;margin:0;background:transparent;}"
            "#grid{display:block;width:100px;height:80px;"
            "background-image:linear-gradient(0deg,red,blue),linear-gradient(90deg,red,blue),linear-gradient(180deg,red,blue);"
            "background-position:center left,center right;background-size:40px 40px;background-repeat:no-repeat;}");
        auto box=aether_layout_result_find_primary_box(frame.layout,aether_doc_get_element_by_id(frame.document,"grid"));
        require(box!=nullptr,"multilayer box");
        const auto origin=aether_layout_box_get_border_box(box);
        int seen[3]{};
        for(int32_t i=0;i<aether_display_list_get_count(frame.list);++i) {
            aether_paint_command_t command{};
            require(aether_display_list_get_command(frame.list,i,&command)==AETHER_OK,"multilayer command");
            if(command.tag==AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT) {
                const auto& tile=command.data.fill_rounded_rect_gradient;
                const int identity=same(tile.gradient.direction_x,1.f) ? 1 :
                    same(tile.gradient.direction_y,-1.f) ? 0 :
                    same(tile.gradient.direction_y,1.f) ? 2 : -1;
                require(identity>=0 && identity<3,"distinct gradient identity");
                ++seen[identity];
                require(same(tile.rect.x-origin.x,identity==1?60.f:0.f) && same(tile.rect.y-origin.y,20.f),"multilayer A B A offset");
                require(same(tile.gradient_rect.x,tile.rect.x) && same(tile.gradient_rect.y,tile.rect.y),"multilayer full tile origin");
            }
            aether_paint_command_free(&command);
        }
        require(seen[0]==1 && seen[1]==1 && seen[2]==1,"three distinct layers independent of order");
    }
    return 0;
}
