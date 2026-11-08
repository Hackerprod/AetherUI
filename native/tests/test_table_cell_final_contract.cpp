// Finite public-API tests for the bounded native unspanned-cell contract.
// All checks remain active under NDEBUG. This is not a browser conformance suite.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int checks = 0, failures = 0, scenarios = 0;
static const char* case_name = "setup";
static const char* mode_name = "setup";
#define SETUP(x) do { if (!(x)) { std::fprintf(stderr, "SETUP FAIL case=%s mode=%s line=%d: %s\n", case_name, mode_name, __LINE__, #x); std::exit(2); } } while (false)
static void check(bool pass, const char* label) {
    ++checks;
    if (!pass) {
        ++failures;
        std::fprintf(stderr, "FAIL case=%s mode=%s %s\n", case_name, mode_name, label);
    }
}
static void number(const char* label, float actual, float expected) {
    const bool pass = std::isfinite(actual) && std::isfinite(expected) && std::fabs(actual - expected) < .01f;
    if (!pass) std::fprintf(stderr, "  %s actual=%g expected=%g\n", label, actual, expected);
    check(pass, label);
}
static void rect(const char* label, aether_rectf actual, aether_rectf expected) {
    std::printf("case=%s mode=%s %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n",
        case_name, mode_name, label, actual.x, actual.y, actual.width, actual.height,
        expected.x, expected.y, expected.width, expected.height);
    number(label, actual.x, expected.x); number(label, actual.y, expected.y);
    number(label, actual.width, expected.width); number(label, actual.height, expected.height);
}
static aether_text_metrics_t measure(const char*, const aether_computed_style_t*, float, void*) {
    return {10,10,8};
}
struct OffsetState {
    aether_node_t cell = nullptr;
    int total = 0, cell_calls = 0;
    float requested = 100.f;
};
static aether_scroll_offset_t offsets(aether_node_t node, void* data) {
    auto& state = *static_cast<OffsetState*>(data);
    ++state.total;
    if (node == state.cell) { ++state.cell_calls; return {0,state.requested}; }
    return {0,0};
}
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t result = nullptr;
    Fixture(const char* name, int mode, const std::string& html, const std::string& rules,
            float viewport_height = 300, OffsetState* offset_state = nullptr) {
        case_name = name;
        mode_name = mode == 0 ? "legacy" : mode == 1 ? "prepared" : mode == 2 ? "contextual" : "contextual-prepared";
        ++scenarios;
        SETUP(aether_doc_create(&doc) == AETHER_OK);
        const std::string document = "<html><body>" + html + "</body></html>";
        SETUP(aether_doc_parse_html(doc, document.c_str()) == AETHER_OK);
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{400,viewport_height,1,16,0,0,0};
        SETUP(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime) == AETHER_OK);
        const std::string css = "*{margin:0;padding:0;border:0;box-sizing:content-box}"
            "html,body,div{display:block}table{display:table;width:200px}"
            "tr{display:table-row}td{display:table-cell}"
            "#cell{position:relative;background:#ff0000}#flow{width:20px;height:20px}"
            "#large{width:20px;height:80px}" + rules;
        int64_t sheet = 0;
        SETUP(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"table-final-contract",&sheet,nullptr) == AETHER_OK);
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        SETUP(count >= 0);
        aether_style_updates_free(updates,count);
        if (mode >= 2) {
            aether_style_revision_t revision = nullptr;
            SETUP(aether_style_runtime_retain_revision(runtime,&revision) == AETHER_OK);
            SETUP(aether_style_revision_create_map(revision,&map) == AETHER_OK);
            aether_style_revision_destroy(revision);
        } else SETUP(aether_style_runtime_create_resolved_style_map(runtime,doc,&map) == AETHER_OK);
        SETUP(aether_layout_create(measure,nullptr,&engine) == AETHER_OK);
        if (mode == 1 || mode == 3) {
            aether_bool_t changed = 0;
            SETUP(aether_layout_prepare_transition_context(engine,nullptr,map,nullptr,&changed) == AETHER_OK);
        }
        if (offset_state) offset_state->cell = node("cell");
        SETUP(aether_layout_layout(engine,doc,map,400,viewport_height,
            offset_state ? offsets : nullptr,offset_state,&result) == AETHER_OK);
        SETUP(result);
    }
    ~Fixture() {
        aether_layout_result_destroy(result); aether_layout_destroy(engine);
        aether_style_map_destroy(map); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) const {
        auto n = aether_doc_get_element_by_id(doc,id); SETUP(n); return n;
    }
    aether_layout_box_t box(const char* id) const {
        auto b = aether_layout_result_find_primary_box(result,node(id)); SETUP(b); return b;
    }
    aether_rectf border(const char* id) const { return aether_layout_box_get_border_box(box(id)); }
    void height(const char* id, float expected) const { number(id,border(id).height,expected); }
    void range(const char* id, float x, float y) const {
        auto scroll = aether_layout_box_get_scroll_metrics(box(id));
        number(id,scroll.scrollable_overflow_x,x); number(id,scroll.scrollable_overflow_y,y);
    }
    void hit(float x, float y, const char* id) const {
        const auto actual = aether_layout_result_hit_test(result,x,y);
        if (actual != node(id)) std::fprintf(stderr,"  hit(%g,%g) expected=%s\n",x,y,id);
        check(actual == node(id),"hit owner");
    }
    void paint(aether_rectf red_expected, bool require_clip = false, OffsetState* state = nullptr,
               bool require_borders = false) const {
        aether_display_list_builder_t builder = nullptr; aether_display_list_t list = nullptr;
        SETUP(aether_display_list_builder_create(measure,nullptr,&builder) == AETHER_OK);
        SETUP(aether_display_list_build(builder,result,nullptr,&list) == AETHER_OK);
        int fills = 0, clips = 0, scopes = 0, borders = 0;
        const auto expected_clip = aether_layout_box_get_padding_box(box("cell"));
        for (int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t command{};
            SETUP(aether_display_list_get_command(list,i,&command) == AETHER_OK);
            if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
                const auto color = command.data.fill_rounded_rect.color;
                if (color.r == 1.f && color.g == 0.f && color.b == 0.f) {
                    ++fills; rect("red paint",command.data.fill_rounded_rect.rect,red_expected);
                }
            }
            if (command.tag == AETHER_PAINT_PUSH_CLIP) {
                const auto actual = command.data.push_clip.rect;
                if (std::isfinite(actual.x) && std::isfinite(actual.y) &&
                    std::isfinite(actual.width) && std::isfinite(actual.height) &&
                    std::fabs(actual.x - expected_clip.x) < .01f &&
                    std::fabs(actual.y - expected_clip.y) < .01f &&
                    std::fabs(actual.width - expected_clip.width) < .01f &&
                    std::fabs(actual.height - expected_clip.height) < .01f) ++clips;
            }
            if (require_borders && command.tag == AETHER_PAINT_FILL_BORDER_SIDE) {
                ++borders;
                rect("border paint outer",command.data.fill_border_side.outer_rect,red_expected);
                rect("border paint inner",command.data.fill_border_side.inner_rect,expected_clip);
            }
            if (command.tag == AETHER_PAINT_PUSH_SCROLL_OFFSET &&
                command.data.push_scroll_offset.scroll_container_id == aether_node_get_style_node_id(node("cell"))) ++scopes;
            aether_paint_command_free(&command);
        }
        check(fills == 1,"one red fill");
        if (require_clip) {
            std::printf("case=%s mode=%s matching final padding clips=%d expected=(%g,%g,%g,%g)\n",
                case_name,mode_name,clips,expected_clip.x,expected_clip.y,expected_clip.width,expected_clip.height);
            check(clips > 0,"paint clip matches final cell padding rectangle");
        }
        if (require_borders) check(borders == 2,"two decorated cell border-side commands");
        if (state) {
            check(scopes > 0,"dynamic scroll scope present");
            check(state->total == 0,"paint does not sample offset callback");
        }
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
    }
};
static const std::string ordinary =
    "<table id='table'><tr id='row'><td id='cell'><div id='flow'></div></td>"
    "<td id='tall'><div id='large'></div></td></tr>"
    "<tr id='next'><td id='nextcell'><div style='height:10px'></div></td></tr></table>";
static std::string positioned(const std::string& child) {
    return "<table id='table'><tr id='row'><td id='cell'><div id='flow'></div>" + child +
        "</td><td id='tall'><div id='large'></div></td></tr></table>";
}
static void simple_cells(int mode) {
    for (int count : {2,3,4}) {
        for (int reverse=0;reverse<2;++reverse) {
            std::string html = "<table id='table'><tr id='row'>";
            for (int i=0;i<count;++i) {
                const bool tall = i == (reverse ? 0 : count-1);
                html += "<td id='c" + std::to_string(i) + "'><div id='f" + std::to_string(i) +
                    "' style='width:20px;height:" + (tall ? "80" : "20") + "px'></div></td>";
            }
            html += "</tr><tr id='next'><td id='nextcell'><div style='height:10px'></div></td></tr></table>";
            Fixture f("columns-order-multiple",mode,html,"");
            const float width = 200.f / count;
            for (int i=0;i<count;++i) {
                const std::string id = "c"+std::to_string(i), child = "f"+std::to_string(i);
                rect(id.c_str(),f.border(id.c_str()),{i*width,0,width,80});
                number("cell margin allocation",aether_layout_box_get_margin_box(f.box(id.c_str())).height,80);
                f.height(child.c_str(),i == (reverse ? 0 : count-1) ? 80 : 20);
            }
            rect("following fewer cells",f.border("nextcell"),{0,80,width,10});
            rect("table",f.border("table"),{0,0,200,90});
        }
    }
    {
        Fixture f("equal",mode,ordinary,"#large{height:20px}");
        f.height("cell",20); f.height("tall",20); number("next",f.border("next").y,20);
    }
    {
        Fixture f("empty",mode,positioned(""),"#flow{display:none}");
        rect("empty cell",f.border("cell"),{0,0,90,80});
        f.hit(50,50,"cell"); f.paint({0,0,90,80});
    }
    {
        Fixture f("multiple-unequal-rows",mode,
            "<table><tr id='r1'><td id='a'><div style='height:20px'></div></td><td id='b'><div style='height:80px'></div></td></tr>"
            "<tr id='r2'><td id='c'><div style='height:30px'></div></td><td id='d'><div style='height:10px'></div></td></tr></table>","");
        f.height("a",80); f.height("b",80); f.height("c",30); f.height("d",30);
        number("second row y",f.border("r2").y,80);
    }
}
static void decorations(int mode) {
    for (int border_box=0;border_box<2;++border_box) {
        const std::string sizing = border_box ? "border-box" : "content-box";
        Fixture f("decorated",mode,positioned("<div id='anchor'></div>"),
            "#cell{margin-top:3px;margin-bottom:7px;padding-top:4px;padding-bottom:6px;border-top:2px solid black;border-bottom:2px solid black;box-sizing:"+sizing+"}"
            "#anchor{position:absolute;left:0;bottom:0;width:5px;height:5px}");
        rect("border",f.border("cell"),{0,3,100,70});
        rect("padding",aether_layout_box_get_padding_box(f.box("cell")),{0,5,100,66});
        rect("content",aether_layout_box_get_content_box(f.box("cell")),{0,9,100,56});
        number("margin height",aether_layout_box_get_margin_box(f.box("cell")).height,80);
        rect("bottom anchor",f.border("anchor"),{0,66,5,5});
        f.height("flow",20);
        f.hit(50,3,"cell"); f.hit(50,72,"cell");
        f.hit(50,2,"row"); f.hit(50,74,"row");
        f.paint({0,3,100,70},false,nullptr,true);
    }
    {
        Fixture f("negative-margins",mode,ordinary,"#cell{margin-top:-3px;margin-bottom:-7px}");
        rect("negative margin cell",f.border("cell"),{0,-3,100,90});
        number("negative margin allocation",aether_layout_box_get_margin_box(f.box("cell")).height,80);
        number("row advance",f.border("next").y,80);
    }
    {
        Fixture f("auto-margins-relative",mode,ordinary,"#cell{margin:auto;left:4px;top:6px}");
        rect("relative cell",f.border("cell"),{4,6,100,80});
        rect("relative child",f.border("flow"),{4,6,20,20});
        number("relative row advance",f.border("next").y,80);
    }
}
static void constraints(int mode) {
    struct Constraint { const char* css; float row; float outside; };
    const Constraint cases[] = {
        {"height:20px;max-height:20px",80,20},
        {"height:20px;min-height:100px",100,100},
        {"height:20px;min-height:100px;max-height:10px",100,100},
        {"height:20px;max-height:10px",80,10}
    };
    for (const auto& c : cases) {
        Fixture f("authored-clamps",mode,ordinary+"<div id='outside'><div style='height:20px'></div></div>",
            std::string("#cell,#outside{")+c.css+"}");
        f.height("cell",c.row); f.height("tall",c.row); f.height("row",c.row);
        f.height("flow",20); f.height("outside",c.outside);
    }
    struct Percent { const char* rules; float flow; };
    const Percent percents[] = {
        {"#cell{height:40px}#flow{height:50%}",20},
        {"#cell{min-height:40px}#flow{height:50%}",0},
        {"#cell{height:50%}#flow{height:50%}",0},
        {"table{height:100px}#cell{height:50%}#flow{height:50%}",25},
        {"#cell{height:calc(20px + 20px)}#flow{height:calc(50% + 0px)}",20},
        {"table{height:100px}#cell{height:calc(25% + 25px)}#flow{height:50%}",25},
        {"#cell{height:calc(50% + 10px)}#flow{height:50%}",0}
    };
    for (const auto& p : percents) {
        Fixture f("native-percent-reference",mode,ordinary,p.rules);
        f.height("cell",80); f.height("flow",p.flow); f.height("row",80);
    }
}
static void positioned_cases(int mode) {
    {
        Fixture f("positioned-percent-insets",mode,
            positioned("<div id='automatic'></div><div id='percent'></div><div id='opposing'></div>"),
            "#percent{position:absolute;left:30px;top:0;width:5px;height:50%;background:#00ff00}"
            "#opposing{position:absolute;left:40px;top:10px;bottom:15px;width:5px}"
            "#automatic{position:absolute;width:5px;height:5px}");
        rect("percent",f.border("percent"),{30,0,5,40});
        rect("opposing",f.border("opposing"),{40,10,5,55});
        rect("automatic",f.border("automatic"),{20,0,5,5});
        f.hit(32,35,"percent"); f.hit(42,60,"opposing"); f.paint({0,0,100,80});
    }
    {
        Fixture f("positioned-decorations",mode,positioned("<div id='anchor'></div>"),
            "#cell{padding-top:4px;padding-bottom:6px;border-top:2px solid black;border-bottom:2px solid black}"
            "#anchor{position:absolute;left:0;bottom:0;height:50%;width:5px;margin-top:2px;margin-bottom:3px}");
        // Final padding height76, height50%=38; bottom margin3 ends at y75.
        rect("decorated percentage anchor",f.border("anchor"),{0,37,5,38});
        f.height("flow",20);
    }
    {
        Fixture f("positioned-nested-table",mode,positioned(
            "<div id='absolute'><table id='inner'><tr><td id='innercell'><div style='height:5px'></div></td>"
            "<td id='innertall'><div style='height:15px'></div></td></tr></table></div>"),
            "#absolute{position:absolute;left:0;bottom:0;width:40px}#inner{width:40px}");
        rect("absolute nested table",f.border("absolute"),{0,65,40,15});
        f.height("innercell",15); f.height("innertall",15); f.height("flow",20);
    }
}
static void scrolling(int mode) {
    for (const char* overflow : {"hidden","auto","scroll","clip"}) {
        Fixture f(overflow,mode,positioned("<div id='spill'></div>"),
            std::string("#cell{overflow:")+overflow+"}#spill{position:absolute;left:110px;top:70px;width:30px;height:30px}");
        const bool clips_only = std::strcmp(overflow,"clip") == 0;
        f.range("cell",clips_only ? 0 : 40,clips_only ? 0 : 20);
        f.height("cell",80); f.height("flow",20); f.paint({0,0,100,80},true);
        check(aether_layout_result_hit_test(f.result,115,85) != f.node("spill"),"spill clipped outside final cell");
    }
    {
        Fixture f("capped-flow-scroll",mode,ordinary,"#cell{height:20px;max-height:20px;overflow:hidden}#flow{height:120px}");
        f.height("cell",80); f.height("flow",120); f.range("cell",0,40);
    }
}
static void nested(int mode) {
    Fixture f("nested-flow",mode,
        "<table id='table'><tr><td id='cell'><table id='inner'><tr id='innerrow'>"
        "<td id='innercell'><div id='flow'></div></td><td id='innertall'><div style='width:20px;height:40px'></div></td>"
        "</tr></table></td><td id='tall'><div id='large'></div></td></tr></table>","#inner{width:60px}");
    f.height("cell",80); f.height("inner",40); f.height("innerrow",40);
    f.height("innercell",40); f.height("innertall",40); f.height("flow",20);
}
static const std::string fixed_html =
    "<div id='outer'><table id='table'><tr id='row'><td id='cell'><div id='wrapper'><div id='fixed'></div></div></td>"
    "<td id='tall'><div id='large'></div></td></tr></table></div>";
static std::string fixed_rules(const char* cell_overflow, const char* outer_overflow, const char* wrapper_overflow) {
    return std::string("#outer{width:200px;height:100px;overflow-y:")+outer_overflow+"}"
        "#cell{transform:translate(0px,0px);overflow-y:"+cell_overflow+"}"
        "#wrapper{width:20px;height:20px;overflow-y:"+wrapper_overflow+"}"
        "#fixed{position:fixed;left:0;top:100%;width:5px;height:30px}";
}
static void fixed_repair() {
    struct Control { const char* cell; const char* outer; const char* wrapper; float cell_range, wrapper_range, outer_range, root_range; };
    const Control cases[] = {
        {"hidden","auto","visible",30,0,0,40},
        {"visible","auto","visible",0,0,10,40},
        {"visible","visible","visible",0,0,0,50},
        {"hidden","auto","hidden",0,90,0,40}
    };
    for (int index=0;index<4;++index) {
        const auto& c = cases[index];
        OffsetState state;
        Fixture f("prepared-fixed-repair",1,fixed_html,fixed_rules(c.cell,c.outer,c.wrapper),60,&state);
        check(state.total == 0,"layout and repair do not sample offsets");
        rect("fixed final owner",f.border("fixed"),{0,80,5,30});
        f.range("cell",0,c.cell_range); f.range("wrapper",0,c.wrapper_range); f.range("outer",0,c.outer_range);
        number("root repaired range",aether_layout_box_get_scrollable_overflow_y(aether_layout_result_get_root(f.result)),c.root_range);
        if (index == 0) {
            f.paint({0,0,100,80},true,&state);
            const auto before = aether_layout_box_get_scroll_metrics(f.box("cell"));
            for (float request : {100.f,0.f,10.f}) {
                state.requested = request; state.total = 0; state.cell_calls = 0;
                auto hit = aether_layout_result_hit_test_with_scroll_offsets(f.result,2,55,offsets,&state);
                check(hit == f.node(request == 100 ? "fixed" : "cell"),"requested offset clamped hit owner");
                check(state.cell_calls == 1,"short-cell offset sampled once by hit");
                const auto after = aether_layout_box_get_scroll_metrics(f.box("cell"));
                check(std::memcmp(&before.scroll_offset_x,&after.scroll_offset_x,sizeof(float)) == 0 &&
                    std::memcmp(&before.scroll_offset_y,&after.scroll_offset_y,sizeof(float)) == 0,
                    "hit preserves stored offset bytes");
            }
        }
    }
    {
        Fixture f("legacy-viewport-fixed",0,fixed_html,fixed_rules("visible","visible","visible"),60);
        rect("viewport fixed",f.border("fixed"),{0,60,5,30});
    }
    {
        Fixture f("owner-above-table",1,fixed_html,
            fixed_rules("visible","visible","visible")+
            "#cell{transform:none}#outer{transform:translate(0px,0px)}#fixed{top:10px;bottom:15px;height:auto}",60);
        rect("above-table opposing fixed",f.border("fixed"),{0,10,5,75});
    }
    {
        Fixture f("fixed-relative-owner",1,fixed_html,
            fixed_rules("visible","visible","visible")+"#cell{left:4px;top:6px}#fixed{top:10px;bottom:15px;height:auto}",60);
        rect("relative owner fixed",f.border("fixed"),{4,16,5,55});
    }
    {
        Fixture f("fixed-auto-axes",1,fixed_html,
            fixed_rules("visible","visible","visible")+"#fixed{top:auto;left:auto;height:50%}",60);
        rect("auto axes fixed",f.border("fixed"),{0,0,5,40});
    }
}
static void contextual() {
    for (int mode : {2,3}) {
        Fixture f("contextual-transformed-clip",mode,
            positioned("<div id='spill'></div>"),
            "#cell{overflow:hidden}#spill{position:absolute;top:70px;left:0;width:5px;height:30px;transform:translateY(10px)}");
        f.height("cell",80);
        rect("contextual untransformed used box",f.border("spill"),{0,70,5,30});
        f.range("cell",0,30); f.paint({0,0,100,80},true);
        check(aether_layout_result_hit_test(f.result,2,85) != f.node("spill"),"contextual final clip hit");
    }
    {
        Fixture f("contextual-fixed-transformed-owner",3,fixed_html,
            fixed_rules("visible","visible","visible")+"#cell{transform:translateY(10px)}",60);
        rect("contextual fixed used rectangle",f.border("fixed"),{0,80,5,30});
        number("contextual root transformed spill",aether_layout_box_get_scrollable_overflow_y(aether_layout_result_get_root(f.result)),60);
    }
}
int main() {
    for (int mode : {0,1}) {
        simple_cells(mode); decorations(mode); constraints(mode); positioned_cases(mode);
        scrolling(mode); nested(mode);
    }
    fixed_repair(); contextual();
    std::printf("table-cell public contract: scenarios=%d active_checks=%d failures=%d (NDEBUG-safe)\n",scenarios,checks,failures);
    return failures ? 1 : 0;
}
