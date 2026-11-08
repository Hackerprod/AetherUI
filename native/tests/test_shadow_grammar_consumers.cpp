// Public runtime -> fresh style map -> layout -> display-list regression.
// All checks remain active under NDEBUG; no renderer or pixel oracle is used.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
void require(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
bool same(float a, float b) { return std::fabs(a-b) < .001f; }
bool color(aether_color_rgba a, aether_color_rgba b) {
    return same(a.r,b.r) && same(a.g,b.g) && same(a.b,b.b) && same(a.a,b.a);
}
bool shadow(aether_shadow a, aether_shadow b) {
    return same(a.offset_x,b.offset_x) && same(a.offset_y,b.offset_y) &&
        same(a.blur_radius,b.blur_radius) && same(a.spread_radius,b.spread_radius) &&
        color(a.color,b.color) && a.inset==b.inset;
}
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? 6.f*static_cast<float>(std::strlen(text)) : 0.f, 10.f, 8.f};
}
const aether_shadow text_expected[] = {
    {1,2,3,0,{1,0,0,1},AETHER_FALSE}, {7,8,9,0,{0,0,1,1},AETHER_FALSE}
};
const aether_shadow box_expected[] = {
    {2,3,4,5,{1,0,0,1},AETHER_FALSE}, {8,9,10,-2,{0,0,1,1},AETHER_FALSE}
};
const char* fallback = "text-shadow:1px 2px 3px red,7px 8px 9px blue;"
    "box-shadow:2px 3px 4px 5px red,8px 9px 10px -2px blue;";
struct Scene {
    aether_style_map_t styles{};
    aether_layout_engine_t engine{};
    aether_layout_result_t layout{};
    aether_display_list_builder_t builder{};
    aether_display_list_t list{};
    ~Scene() {
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(layout); aether_layout_destroy(engine);
        aether_style_map_destroy(styles);
    }
};
struct Fixture {
    aether_doc_t doc{};
    aether_style_runtime_t runtime{};
    aether_node_t grid{};
    Fixture(const std::string& declarations) {
        require(aether_doc_create(&doc)==AETHER_OK,"create document");
        require(aether_doc_parse_html(doc,"<html><body><div id='parent'><div id='grid'>Shadow</div></div></body></html>")==AETHER_OK,"parse real text");
        grid=aether_doc_get_element_by_id(doc,"grid"); require(grid!=nullptr,"find grid");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{200,120,1,16,0,0,0};
        require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"create runtime");
        const std::string css="html,body,#parent{display:block;margin:0;background:transparent;}"
            "#parent{text-shadow:11px 12px 13px currentColor;}"
            "#grid{display:block;width:100px;height:40px;color:#00ff00;font-size:10px;"+declarations+"}";
        int64_t sheet{};
        require(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"shadow-consumers.css",&sheet,nullptr)==AETHER_OK,"add sheet");
    }
    ~Fixture() { aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
    void build(Scene& scene) {
        require(!scene.styles && !scene.layout && !scene.list,"new scene for each resolution");
        aether_style_update_t* updates{};
        const auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(count>=0,"resolve subtree"); aether_style_updates_free(updates,count);
        require(aether_style_runtime_create_resolved_style_map(runtime,doc,&scene.styles)==AETHER_OK,"fresh style map");
        require(aether_layout_create(measure,nullptr,&scene.engine)==AETHER_OK,"layout engine");
        require(aether_layout_layout(scene.engine,doc,scene.styles,200,120,nullptr,nullptr,&scene.layout)==AETHER_OK,"fresh layout");
        require(aether_display_list_builder_create(measure,nullptr,&scene.builder)==AETHER_OK,"list builder");
        require(aether_display_list_build(scene.builder,scene.layout,nullptr,&scene.list)==AETHER_OK,"fresh display list");
    }
};
struct Copy {
    aether_computed_style_t value{};
    Copy(Scene& s, aether_node_t node) {
        auto h=aether_style_map_get(s.styles,node); require(h!=nullptr,"snapshot handle");
        require(aether_computed_style_copy(h,&value)==AETHER_OK,"copy computed snapshot");
        aether_computed_style_handle_destroy(h);
    }
    ~Copy() { aether_computed_style_free(&value); }
};
struct Command {
    bool text;
    aether_rectf rect;
    aether_shadow value;
    std::string content;
};
std::vector<Command> collect(Scene& scene, aether_node_t node, int text_count, int box_count) {
    auto box=aether_layout_result_find_primary_box(scene.layout,node);
    require(box!=nullptr,"primary layout box");
    const auto origin=aether_layout_box_get_border_box(box);
    require(same(origin.width,100) && same(origin.height,40),"deterministic box geometry");
    std::vector<Command> result;
    int texts=0, boxes=0, glyphs=0;
    for (int i=0;i<aether_display_list_get_count(scene.list);++i) {
        aether_paint_command_t c{};
        require(aether_display_list_get_command(scene.list,i,&c)==AETHER_OK,"read paint command");
        if (c.tag==AETHER_PAINT_DRAW_TEXT) {
            require(c.data.draw_text.text && std::strcmp(c.data.draw_text.text,"Shadow")==0,"actual glyph text");
            ++glyphs;
        }
        if (c.tag==AETHER_PAINT_DRAW_TEXT_SHADOW || c.tag==AETHER_PAINT_DRAW_BOX_SHADOW) {
            const bool text=c.tag==AETHER_PAINT_DRAW_TEXT_SHADOW;
            auto rect=text ? c.data.draw_text_shadow.rect : c.data.draw_box_shadow.rect;
            const auto value=text ? c.data.draw_text_shadow.shadow : c.data.draw_box_shadow.shadow;
            std::string content;
            if (text) {
                ++texts; require(c.data.draw_text_shadow.text!=nullptr,"shadow text pointer");
                content=c.data.draw_text_shadow.text; require(content=="Shadow","shadow has real text");
                require(same(rect.width,36) && rect.height>0,"measured text geometry");
            } else {
                ++boxes; require(same(rect.width,100) && same(rect.height,40),"outer box dimensions");
                require(same(rect.x,origin.x) && same(rect.y,origin.y),"outer box origin");
            }
            rect.x-=origin.x; rect.y-=origin.y;
            result.push_back({text,rect,value,content});
        }
        aether_paint_command_free(&c);
    }
    require(texts==text_count && boxes==box_count,"exact shadow command counts");
    require(glyphs==1,"one real glyph run, never an empty passing scene");
    return result;
}
void equivalent(const std::vector<Command>& a, const std::vector<Command>& b) {
    require(a.size()==b.size(),"twin command count");
    for (size_t i=0;i<a.size();++i)
        require(a[i].text==b[i].text && shadow(a[i].value,b[i].value) &&
            same(a[i].rect.x,b[i].rect.x) && same(a[i].rect.y,b[i].rect.y) &&
            same(a[i].rect.width,b[i].rect.width) && same(a[i].rect.height,b[i].rect.height) &&
            a[i].content==b[i].content,"twin geometry, shadow fields, text and order");
}
void order(const std::vector<Command>& commands) {
    int texts=0,boxes=0;
    for (const auto& c:commands) {
        int& n=c.text ? texts : boxes;
        require(n<2 && shadow(c.value,(c.text ? text_expected : box_expected)[1-n]),"distinct shadows in reverse CSS order"); ++n;
    }
    require(texts==2 && boxes==2,"both two-shadow controls observed");
}
void snapshot(const aether_computed_style_t& s) {
    require(s.text_shadows.count==2 && s.text_shadows.items && s.box_shadows.count==2 && s.box_shadows.items,"snapshot list sizes");
    for (int i=0;i<2;++i)
        require(shadow(s.text_shadows.items[i],text_expected[i]) && shadow(s.box_shadows.items[i],box_expected[i]),"snapshot values in declaration order");
}
} // namespace
int main() {
    Fixture expected(fallback);
    Fixture actual(std::string(fallback)+"text-shadow:1px 2px red 3px;box-shadow:1px inset 2px;");
    Scene expected_scene,old_scene;
    expected.build(expected_scene); actual.build(old_scene);
    auto baseline=collect(expected_scene,expected.grid,2,2); order(baseline);
    equivalent(collect(old_scene,actual.grid,2,2),baseline);
    Copy retained(old_scene,actual.grid); snapshot(retained.value);

    require(aether_element_set_attribute(actual.grid,"style",
        "--bad:1px red 2px;text-shadow:var(--bad,20px 21px blue);box-shadow:var(--bad,20px 21px blue);")==AETHER_OK,"mutate defined invalid variable");
    Scene changed; actual.build(changed);
    Copy computed(changed,actual.grid);
    require(computed.value.box_shadows.count==0,"computed invalid box becomes none");
    require(computed.value.text_shadows.count==1 && computed.value.text_shadows.items,"computed invalid text inherits");
    const aether_shadow inherited{11,12,13,0,{0,1,0,1},AETHER_FALSE};
    require(shadow(computed.value.text_shadows.items[0],inherited),"inherited symbolic shadow uses child color");
    auto commands=collect(changed,actual.grid,1,0);
    require(commands.size()==1 && commands[0].text && shadow(commands[0].value,inherited),"new display uses inherited text, not lower or var fallback");
    // Inspect the old map, layout and list independently after rebuilding all three.
    Copy old_again(old_scene,actual.grid); snapshot(old_again.value); snapshot(retained.value);
    require(old_again.value.text_shadows.items!=retained.value.text_shadows.items,"independent snapshot storage");
    equivalent(collect(old_scene,actual.grid,2,2),baseline);

    require(aether_element_set_attribute(actual.grid,"style",
        "text-shadow:none;box-shadow:red inset 4px 5px 6px 7px;")==AETHER_OK,"mutate valid inset");
    Scene inset; actual.build(inset); Copy inset_style(inset,actual.grid);
    const aether_shadow inset_expected{4,5,6,7,{1,0,0,1},AETHER_TRUE};
    require(inset_style.value.box_shadows.count==1 && inset_style.value.box_shadows.items &&
        shadow(inset_style.value.box_shadows.items[0],inset_expected),"valid inset remains computed");
    require(collect(inset,actual.grid,0,0).empty(),"inset deliberately emits no outer shadow");
    equivalent(collect(old_scene,actual.grid,2,2),baseline); snapshot(retained.value);
    return 0;
}
