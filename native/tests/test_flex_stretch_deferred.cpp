// Public runtime regression for deferred row/nowrap stretch layout.
// All checks remain active under NDEBUG. Callback counts are deterministic;
// high-depth probes are gated by a small successful probe, never wall time.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_style.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
int failures = 0, checks = 0, scenarios = 0;
unsigned long long callbacks = 0;
std::string active_case = "setup";

void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL infrastructure %s: %s\n", active_case.c_str(), message);
        std::exit(2);
    }
}
void check(bool value, const std::string& name, const char* field) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), field);
    }
}
void number(float actual, float expected, const std::string& name, const char* field) {
    const bool pass = std::isfinite(actual) && std::fabs(actual - expected) < .001f;
    if (!pass) std::fprintf(stderr, "  actual=%g expected=%g\n", actual, expected);
    check(pass, name, field);
}
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    ++callbacks;
    return {10.f * static_cast<float>(std::strlen(text)), 20.f, 15.f};
}

struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    aether_layout_engine_t engine = nullptr;
    bool contextual;
    float viewport_width = 640, viewport_height = 480;

    Fixture(const std::string& html, const std::string& css, bool context = false)
        : contextual(context) {
        require(aether_doc_create(&doc) == AETHER_OK, "create document");
        require(aether_doc_parse_html(doc, html.c_str()) == AETHER_OK, "parse HTML");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{640, 480, 1, 16, 0, 0, 0};
        require(aether_style_runtime_create(&options, &env, nullptr, nullptr, &runtime) == AETHER_OK,
                "create runtime");
        add_css(css);
        require(aether_layout_create(measure, nullptr, &engine) == AETHER_OK, "create engine");
        ++scenarios;
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    ~Fixture() {
        aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        aether_style_runtime_destroy(runtime);
        aether_doc_destroy(doc);
    }
    void add_css(const std::string& css) {
        int64_t sheet = 0;
        require(aether_style_runtime_add_stylesheet(runtime, css.c_str(), 0,
                    "flex-stretch-deferred.css", &sheet, nullptr) == AETHER_OK, "add stylesheet");
        refresh_map();
    }
    void refresh_map() {
        aether_style_update_t* updates = nullptr;
        const int count = aether_style_runtime_resolve_subtree(runtime,
            aether_doc_get_document_element(doc), &updates);
        require(count >= 0, "resolve runtime styles");
        aether_style_updates_free(updates, count);
        aether_style_map_t next = nullptr;
        if (contextual) {
            aether_style_revision_t revision = nullptr;
            require(aether_style_runtime_retain_revision(runtime, &revision) == AETHER_OK, "retain revision");
            require(aether_style_revision_create_map(revision, &next) == AETHER_OK, "contextual map");
            aether_style_revision_destroy(revision);
        } else {
            require(aether_style_runtime_create_resolved_style_map(runtime, doc, &next) == AETHER_OK,
                    "resolved map");
        }
        aether_style_map_destroy(map);
        map = next;
    }
    aether_node_t node(const char* id) const {
        const auto n = aether_doc_get_element_by_id(doc, id);
        require(n != nullptr, "find element");
        return n;
    }
    aether_layout_result_t layout(float width = 640, float height = 480) {
        if (contextual && (width != viewport_width || height != viewport_height)) {
            aether_style_environment_t env{width,height,1,16,0,0,0};
            aether_style_update_t* updates = nullptr;
            const auto count = aether_style_runtime_set_environment(runtime,&env,
                aether_doc_get_document_element(doc),&updates);
            require(count >= 0,"resize style environment");
            aether_style_updates_free(updates,count);
            viewport_width = width; viewport_height = height;
            refresh_map();
        }
        aether_layout_result_t result = nullptr;
        const auto status = aether_layout_layout(engine, doc, map, width, height, nullptr, nullptr, &result);
        if (status != AETHER_OK) std::fprintf(stderr, "  layout status=%d\n", int(status));
        require(status == AETHER_OK && result, "layout");
        return result;
    }
    aether_layout_box_t box(aether_layout_result_t result, const char* id) const {
        const auto b = aether_layout_result_find_primary_box(result, node(id));
        require(b != nullptr, "primary box");
        return b;
    }
    aether_rectf rect(aether_layout_result_t result, const char* id) const {
        return aether_layout_box_get_border_box(box(result, id));
    }
};

const char* reset = "*{margin:0;padding:0;border:0;box-sizing:border-box;}"
    "html,body,div{display:block;}body{width:400px;height:200px;}";
const char* pair = "<body><div id='p'><div id='a'><div id='d'></div></div>"
    "<div id='b'></div></div></body>";
const char* pair_css = "#p{display:flex;width:200px;height:100px;align-items:stretch;}"
    "#a,#b{min-width:0;flex:0 0 80px;}#b,#d{height:20px;}";
struct Expected { const char* id; float x, y, width, height; };

void rectangle(aether_rectf actual, aether_rectf expected, const std::string& name) {
    number(actual.x, expected.x, name, "x"); number(actual.y, expected.y, name, "y");
    number(actual.width, expected.width, name, "width"); number(actual.height, expected.height, name, "height");
}
void geometry(const Fixture& f, aether_layout_result_t r, const std::string& name,
              std::initializer_list<Expected> expected) {
    for (const auto& e : expected)
        rectangle(f.rect(r, e.id), {e.x,e.y,e.width,e.height}, name + "/" + e.id);
}
void scenario(const char* label, const char* css, std::initializer_list<Expected> expected,
              const char* html = pair, std::initializer_list<const char*> absent = {}) {
    for (bool contextual : {false, true}) {
        const std::string name = std::string(label) + (contextual ? "/contextual" : "/resolved");
        active_case = name;
        Fixture f(html, std::string(reset) + pair_css + css, contextual);
        const int dom_children = aether_node_get_child_count(f.node("p"));
        auto first = f.layout(), second = f.layout(800, 600);
        aether_layout_result_t clone = nullptr;
        require(aether_layout_clone_snapshot(first, &clone) == AETHER_OK, "clone frame");
        for (auto frame : {first, second, clone}) {
            geometry(f, frame, name, expected);
            for (const auto* id : absent)
                check(!aether_layout_result_find_primary_box(frame, f.node(id)), name,
                      "standalone table-row descendants remain unmaterialized");
            for (const auto& e : expected) {
                // Controls may have additional text boxes for the same element.
                // The primary box itself must appear exactly once.
                int occurrences = 0;
                const auto primary = f.box(frame, e.id);
                for (int i = 0; i < aether_layout_result_get_paint_order_count(frame); ++i) {
                    const auto b = aether_layout_result_get_paint_order_box(frame, i);
                    if (b == primary) {
                        ++occurrences;
                        check(aether_layout_box_get_element(b) == f.node(e.id), name,
                              "primary paint entry belongs to expected element");
                    }
                }
                number(static_cast<float>(occurrences), 1, name + "/" + e.id, "one primary paint entry");
            }
        }
        number(static_cast<float>(aether_node_get_child_count(f.node("p"))),
               static_cast<float>(dom_children), name, "layout preserves DOM children");
        aether_layout_result_destroy(first);
        geometry(f, clone, name + "/clone after source destroyed", expected);
        aether_layout_result_destroy(second); aether_layout_result_destroy(clone);
    }
}

void sizes_and_controls() {
    scenario("px root", "", {{"p",0,0,200,100},{"a",0,0,80,100},{"b",80,0,80,20}});
    for (const char* height : {"50%", "calc(25% + 50px)", "calc(0% + 100px)", "var(--h)"}) {
        const auto css = std::string("#p{--h:calc(25% + 50px);height:") + height + "}";
        scenario(height, css.c_str(), {{"p",0,0,200,100},{"a",0,0,80,100},{"b",80,0,80,20}});
    }
    scenario("zero definite cross", "#p{height:0}", {{"p",0,0,200,0},{"a",0,0,80,0},{"d",0,0,80,20}});
    scenario("root min clamp", "#p{height:50px;min-height:100px}", {{"p",0,0,200,100},{"a",0,0,80,100}});
    scenario("root max clamp", "#p{height:150px;max-height:100px}", {{"p",0,0,200,100},{"a",0,0,80,100}});
    scenario("root min wins max", "#p{height:50px;min-height:100px;max-height:60px}", {{"p",0,0,200,100},{"a",0,0,80,100}});
    scenario("auto root", "#p{height:auto}", {{"p",0,0,200,20},{"a",0,0,80,20}});
    scenario("auto root minimum", "#p{height:auto;min-height:100px}", {{"p",0,0,200,100},{"a",0,0,80,100}});
    scenario("auto root maximum", "#p{height:auto;max-height:10px}", {{"p",0,0,200,10},{"a",0,0,80,10},{"b",80,0,80,20}});
    for (const char* align : {"flex-start", "center", "flex-end"}) {
        const float y = std::strcmp(align,"center") == 0 ? 40.f : std::strcmp(align,"flex-end") == 0 ? 80.f : 0.f;
        const auto css = std::string("#p{align-items:") + align + "}";
        scenario(align, css.c_str(), {{"a",0,y,80,20},{"b",80,y,80,20}});
    }
    scenario("align self excludes", "#a{align-self:center}", {{"a",0,40,80,20},{"b",80,0,80,20}});
    scenario("align self includes", "#p{align-items:flex-start}#a{align-self:stretch}", {{"a",0,0,80,100},{"b",80,0,80,20}});
    scenario("cross margins both auto", "#a{margin-top:auto;margin-bottom:auto}", {{"a",0,40,80,20}});
    scenario("cross before auto", "#a{margin-top:auto}", {{"a",0,80,80,20}});
    scenario("cross after auto", "#a{margin-bottom:auto}", {{"a",0,0,80,20}});
    scenario("explicit percentage not auto", "#a{height:50%}", {{"a",0,0,80,50}});
    scenario("one wrapped line control", "#p{flex-wrap:wrap}", {{"a",0,0,80,100},{"b",80,0,80,20}});
    scenario("multiple wrapped lines control", "#p{width:100px;flex-wrap:wrap;gap:10px}#b{height:auto;min-height:20px}",
             {{"p",0,0,100,100},{"a",0,0,80,45},{"b",0,55,80,45}});
    scenario("column control", "#p{flex-direction:column;height:200px}#a,#b{width:auto}",
             {{"p",0,0,200,200},{"a",0,0,200,80},{"b",0,80,200,80}});
    scenario("forced by stretch ancestor", "#outer{display:flex;width:200px;height:100px}#p{height:auto;min-width:0;flex:0 0 200px}",
             {{"p",0,0,200,100},{"a",0,0,80,100},{"b",80,0,80,20}},
             "<body><div id='outer'><div id='p'><div id='a'><div id='d'></div></div><div id='b'></div></div></div></body>");
}

void clamps_margins_and_bases() {
    scenario("main min greater than max", "#a{min-width:100px;max-width:50px}", {{"a",0,0,100,100},{"b",100,0,80,20}});
    scenario("cross min greater than max", "#a{min-height:140px;max-height:50px}#b{height:auto}",
             {{"p",0,0,200,100},{"a",0,0,80,140},{"b",80,0,80,100}});
    scenario("cross max clamp", "#a{max-height:50px}", {{"a",0,0,80,50},{"b",80,0,80,20}});
    scenario("percentage min and max", "#a{min-width:50%;max-width:25%;max-height:50%}", {{"a",0,0,100,50},{"b",100,0,80,20}});
    scenario("signed cross margins", "#a{margin-top:-10px;margin-bottom:20px}", {{"a",0,-10,80,90},{"d",0,-10,80,20}});
    scenario("negative both cross margins", "#a{margin-top:-10px;margin-bottom:-20px}", {{"a",0,-10,80,130}});
    scenario("percent and calc cross margins", "#a{margin-top:5%;margin-bottom:calc(5% + 5px)}", {{"a",0,10,80,75}});
    scenario("negative calc cross margins", "#a{margin-top:calc(5% - 20px);margin-bottom:calc(10% - 30px)}", {{"a",0,-10,80,120}});
    scenario("cross margins exceed line", "#a{margin-top:70px;margin-bottom:50px}", {{"a",0,70,80,0},{"p",0,0,200,100}});
    scenario("signed main margins", "#a{margin-left:-10px;margin-right:20px}", {{"a",-10,0,80,100},{"b",90,0,80,20}});
    scenario("auto main margins", "#a{margin-left:auto;margin-right:auto}", {{"a",20,0,80,100},{"b",120,0,80,20}});
    scenario("main percent calc margins", "#a{margin-left:5%;margin-right:calc(5% + 5px)}", {{"a",10,0,80,100},{"b",105,0,80,20}});
    scenario("border box decoration", "#a{padding:5px;border:5px solid}", {{"a",0,0,80,100},{"d",10,10,60,20},{"b",80,0,80,20}});
    scenario("content box decoration", "#a{box-sizing:content-box;padding:5px;border:5px solid}", {{"a",0,0,100,100},{"d",10,10,80,20},{"b",100,0,80,20}});
    scenario("decoration floors dimensions", "#a{flex-basis:10px;padding:50px;border:5px solid}", {{"a",0,0,110,110},{"d",55,55,0,20},{"b",110,0,80,20}});
    scenario("percent padding and calc clamp", "#a{padding:5%;border:5px solid;min-width:calc(50% + 10px);max-width:40px}",
             {{"a",0,0,110,100},{"d",15,15,80,20},{"b",110,0,80,20}});
    scenario("root content decoration", "#p{box-sizing:content-box;padding:5px;border:5px solid}",
             {{"p",0,0,220,120},{"a",10,10,80,100},{"b",90,10,80,20}});
    scenario("root border decoration", "#p{padding:5px;border:5px solid}",
             {{"p",0,0,200,100},{"a",10,10,80,80},{"b",90,10,80,20}});
    for (const char* basis : {"80px", "40%", "calc(20% + 40px)", "var(--basis)"}) {
        const auto css = std::string("#p{--basis:80px}#a{flex-basis:") + basis + "}";
        scenario(basis, css.c_str(), {{"a",0,0,80,100},{"b",80,0,80,20}});
    }
    scenario("auto basis specified width", "#a{flex-basis:auto;width:80px}", {{"a",0,0,80,100},{"b",80,0,80,20}});
    scenario("content basis retained", "#a{flex-basis:content}", {{"a",0,0,60,100},{"b",60,0,80,20}},
             "<body><div id='p'><div id='a'>abcdef</div><div id='b'></div></div></body>");
    scenario("horizontal overflow control", "#a{overflow-x:auto;overflow-y:hidden}#d{width:120px;height:160px}",
             {{"a",0,0,80,100},{"d",0,0,120,160}});
    scenario("vertical overflow control", "#a{overflow-x:hidden;overflow-y:auto}#d{width:120px;height:160px}",
             {{"a",0,0,80,100},{"d",0,0,120,160}});
    scenario("fractional grow and gap", "#p{gap:10px}#a,#b{flex-grow:.25}", {{"a",0,0,87.5f,100},{"b",97.5f,0,87.5f,20}});
    scenario("fractional shrink", "#p{width:120px}#a,#b{flex-shrink:.25}", {{"a",0,0,70,100},{"b",70,0,70,20}});
}

void percentage_semantics() {
    const char* html = "<body><div id='p'><div id='a'><div id='d'>aa</div><div id='s'></div></div></div></body>";
    // The outside box is 100 in both passes. An equality shortcut must not
    // preserve the natural 20px percentage child in the definitive final tree.
    for (const char* align : {"flex-start", "stretch"}) {
        const float inner = std::strcmp(align,"stretch") == 0 ? 50.f : 20.f;
        const auto css = std::string("#p{align-items:") + align + "}#a{flex-basis:200px}#d{height:50%}#s{height:80px}";
        scenario(align, css.c_str(), {{"a",0,0,200,100},{"d",0,0,200,inner},{"s",0,inner,200,80}}, html);
    }
    scenario("auto min parent not definite for nonstretch child",
             "#p{height:auto;min-height:100px;align-items:flex-start}#a{height:50%;min-height:100px}#d{height:50%}",
             {{"p",0,0,200,100},{"a",0,0,80,100},{"d",0,0,80,20}},
             "<body><div id='p'><div id='a'><div id='d'>aa</div></div></div></body>");
    scenario("unresolved percentage item not auto stretch", "body{height:auto}#p{height:auto;min-height:100px}#a{height:50%}",
             {{"p",0,0,200,100},{"a",0,0,80,20},{"b",80,0,80,20}});
    scenario("percentage cycle resolves after stretch", "#p{height:auto}#a{min-height:60px}#d{height:50%}",
             {{"p",0,0,200,60},{"a",0,0,80,60},{"d",0,0,80,30}});
    scenario("zero percent calc descendant", "#d{height:calc(0% + 15px)}", {{"a",0,0,80,100},{"d",0,0,80,15}});
    scenario("contextual variable descendant", "#p{--size:calc(25% + 5px)}#d{height:var(--size)}", {{"a",0,0,80,100},{"d",0,0,80,30}});
}

void special_descendants() {
    scenario("relative stretched item", "#a{position:relative;left:10px;top:5px}", {{"a",10,5,80,100},{"d",10,5,80,20},{"b",80,0,80,20}});
    scenario("absolute descendant percentages", "#a{position:relative}#d{position:absolute;left:10px;top:50%;width:20px;height:25%}",
             {{"a",0,0,80,100},{"d",10,50,20,25},{"b",80,0,80,20}});
    scenario("static absolute anchor", "#a{position:relative}#d{position:absolute;width:20px;height:10px}",
             {{"a",0,0,80,100},{"d",0,0,20,10}});
    scenario("fixed descendant", "#d{position:fixed;left:10px;top:30px;width:20px;height:10px}",
             {{"a",0,0,80,100},{"d",10,30,20,10}});
    scenario("anonymous text sibling", "", {{"a",20,0,80,100},{"b",100,0,80,20}},
             "<body><div id='p'>ab<div id='a'><div id='d'></div></div><div id='b'></div></div></body>");
    scenario("input and button", "input{display:block;width:30px;height:15px}button{display:block;width:40px;height:20px}",
             {{"a",0,0,80,100},{"input",0,0,30,15},{"button",0,15,40,20}},
             "<body><div id='p'><div id='a'><input id='input' value='x'><button id='button'>ok</button></div><div id='b'></div></div></body>");
    const char* pseudo = "<body><div id='p'><div id='a'><div id='row'><div id='cell'><div id='d'></div></div></div></div><div id='b'></div></div></body>";
    scenario("pseudo table item retained", "#a{display:table}#row{display:table-row}#cell{display:table-cell}",
             {{"a",0,0,80,100},{"row",0,0,80,20},{"cell",0,0,80,20},{"d",0,0,80,20}}, pseudo);
    scenario("pseudo table row item retained", "#a{display:table-row}#row{display:table-cell}",
             {{"a",0,0,80,100}}, pseudo, {"row","cell","d"});
    scenario("pseudo table cell item retained", "#a{display:table-cell}",
             {{"a",0,0,80,100},{"row",0,0,80,20},{"d",0,0,80,20}}, pseudo);
    scenario("native table inside deferred item", "table{display:table;width:80px}tr{display:table-row}td{display:table-cell}",
             {{"a",0,0,80,100},{"t",0,0,80,20},{"cell",0,0,80,20},{"d",0,0,80,20}},
             "<body><div id='p'><div id='a'><table id='t'><tr><td id='cell'><div id='d'></div></td></tr></table></div><div id='b'></div></div></body>");
}

std::string public_order(const Fixture& f, aether_layout_result_t r,
                         std::initializer_list<const char*> ids) {
    std::string order;
    for (int i = 0; i < aether_layout_result_get_paint_order_count(r); ++i) {
        const auto b = aether_layout_result_get_paint_order_box(r, i);
        for (const auto* id : ids) if (aether_layout_box_get_element(b) == f.node(id)) {
            if (!order.empty()) order += ' ';
            order += id;
        }
    }
    return order;
}
void paint_and_hit(const Fixture& f, aether_layout_result_t r, const std::string& label) {
    check(public_order(f,r,{"p","a","ad","b","bd","c","cd"}) == "p b bd a ad c cd", label,
          "public mixed pending/final paint chronology");
    const auto p = f.box(r,"p");
    number(static_cast<float>(aether_layout_box_get_child_count(p)), 3, label, "three final children");
    int i = 0;
    for (const auto* id : {"a","b","c"}) {
        check(aether_layout_box_get_child(p,i++) == f.box(r,id), label, "final child order is DOM order");
        check(aether_layout_result_find_primary_box(r,f.node(id)) == f.box(r,id), label, "primary lookup final tree");
    }
    // A, B and C overlap. Paint traversal must stay DOM order a,b,c even
    // though the exposed allocation chronology is p,b,bd,a,ad,c,cd.
    check(aether_layout_result_hit_test(r,5,5) == f.node("cd"), label, "overlap hits last child descendant");
    check(aether_layout_result_hit_test(r,5,50) == f.node("c"), label, "overlap hits stretched C");
    aether_display_list_builder_t builder = nullptr;
    aether_display_list_t list = nullptr;
    require(aether_display_list_builder_create(measure,nullptr,&builder) == AETHER_OK, "create painter");
    require(aether_display_list_build(builder,r,nullptr,&list) == AETHER_OK, "build display list");
    std::string painted;
    for (int index = 0; index < aether_display_list_get_count(list); ++index) {
        aether_paint_command_t command{};
        require(aether_display_list_get_command(list,index,&command) == AETHER_OK, "paint command");
        if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT) {
            const auto& fill = command.data.fill_rounded_rect;
            const char* id = fill.color.r == 1 && fill.color.g == 0 && fill.color.b == 0 ? "a"
                : fill.color.r == 0 && fill.color.g == 1 && fill.color.b == 0 ? "b"
                : fill.color.r == 0 && fill.color.g == 0 && fill.color.b == 1 ? "c" : nullptr;
            if (id) { painted += id; rectangle(fill.rect,f.rect(r,id),label + "/paint " + id); }
        }
        aether_paint_command_free(&command);
    }
    check(painted == "abc",label,"display list order and one fill per final item");
    aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
}
void order_and_lifetime() {
    for (bool contextual : {false,true}) {
        const std::string label = contextual ? "order/contextual" : "order/resolved";
        active_case = label;
        Fixture simple("<body><div id='p'><div id='a'></div><div id='b'></div><div id='c'></div></div></body>",
            std::string(reset) + "#p{display:flex;width:100px;height:100px}#a,#b,#c{min-width:0;flex:0 0 20px}#b{height:20px}",contextual);
        auto plain = simple.layout();
        check(public_order(simple,plain,{"p","a","b","c"}) == "p b a c",label,"original p b a c public index");
        aether_layout_result_destroy(plain);
        Fixture f("<body><div id='p'><div id='a'><div id='ad'></div></div><div id='b'><div id='bd'></div></div>"
                  "<div id='c'><div id='cd'></div></div></div></body>",std::string(reset) +
                  "#p{display:flex;width:100px;height:100px}#a,#b,#c{min-width:0;flex:0 0 40px}"
                  "#a{background:#ff0000}#b{height:20px;margin-left:-40px;background:#00ff00}"
                  "#c{margin-left:-40px;background:#0000ff}#ad,#bd,#cd{height:10px}",contextual);
        auto first = f.layout();
        aether_layout_result_t clone = nullptr;
        require(aether_layout_clone_snapshot(first,&clone) == AETHER_OK,"clone mixed tree");
        f.add_css("#p{height:150px}");
        auto resized = f.layout(800,600);
        for (auto frame : {first,clone,resized}) paint_and_hit(f,frame,label);
        number(f.rect(first,"a").height,100,label,"retained source height");
        number(f.rect(clone,"a").height,100,label,"retained clone height");
        number(f.rect(resized,"a").height,150,label,"new styles relayout height");
        aether_layout_result_destroy(first); aether_layout_result_destroy(resized);
        paint_and_hit(f,clone,label + "/after other frames destroyed");
        aether_layout_result_destroy(clone);
    }
}

void scroll_and_clipping() {
    for (const char* overflow : {"auto","hidden","scroll","clip","visible"}) {
        for (bool contextual : {false,true}) {
            active_case = std::string("scroll/") + overflow;
            const auto css = std::string(reset) + pair_css + "#a{overflow:" + overflow + "}#d{width:120px;height:160px;background:#ff0000}";
            Fixture f(pair,css,contextual);
            auto frame = f.layout();
            const std::string label = std::string("scroll/") + overflow + (contextual ? "/contextual" : "/resolved");
            geometry(f,frame,label,{{"p",0,0,200,100},{"a",0,0,80,100},{"b",80,0,80,20},{"d",0,0,120,160}});
            const bool scroller = std::strcmp(overflow,"auto") == 0 || std::strcmp(overflow,"hidden") == 0 || std::strcmp(overflow,"scroll") == 0;
            const auto metrics = aether_layout_box_get_scroll_metrics(f.box(frame,"a"));
            number(metrics.scrollable_overflow_x,scroller ? 40.f : 0.f,label,"x range");
            number(metrics.scrollable_overflow_y,scroller ? 60.f : 0.f,label,"y range");
            const bool clipped = std::strcmp(overflow,"visible") != 0;
            check((aether_layout_result_hit_test(frame,20,130) == f.node("d")) == !clipped,label,"overflow hit clipping");
            aether_display_list_builder_t builder = nullptr; aether_display_list_t list = nullptr;
            require(aether_display_list_builder_create(measure,nullptr,&builder) == AETHER_OK,"scroll painter");
            require(aether_display_list_build(builder,frame,nullptr,&list) == AETHER_OK,"scroll display list");
            int pushes = 0, pops = 0, clips = 0, fills = 0;
            for (int index = 0; index < aether_display_list_get_count(list); ++index) {
                aether_paint_command_t command{};
                require(aether_display_list_get_command(list,index,&command) == AETHER_OK,"scroll command");
                if (command.tag == AETHER_PAINT_PUSH_SCROLL_OFFSET) ++pushes;
                if (command.tag == AETHER_PAINT_POP_SCROLL_OFFSET) ++pops;
                if (command.tag == AETHER_PAINT_PUSH_CLIP) ++clips;
                if (command.tag == AETHER_PAINT_FILL_ROUNDED_RECT && command.data.fill_rounded_rect.color.r == 1.f && command.data.fill_rounded_rect.color.g == 0.f) {
                    ++fills; rectangle(command.data.fill_rounded_rect.rect,{0,0,120,160},label + "/child paint");
                }
                aether_paint_command_free(&command);
            }
            number(static_cast<float>(fills),1,label,"one child fill");
            check(pushes == pops,label,"balanced retained scroll scopes");
            check(!scroller || pushes > 0,label,"scroll scope present");
            check(!clipped || clips > 0,label,"clip present");
            aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
            aether_layout_result_destroy(frame);
        }
    }
}

unsigned long long chain(const char* mode, int depth, bool contextual) {
    active_case = std::string("chain/") + mode + "/" + std::to_string(depth);
    std::string html = "<body>";
    for (int i=0; i<depth; ++i) html += i == 0 ? "<div class='n' id='root'>" : "<div class='n'>";
    html += "<div id='leaf'>aaaa bb cc</div>";
    for (int i=0; i<depth; ++i) html += "</div>";
    html += "</body>";
    std::string css = std::string(reset) + ".n{display:flex;align-items:stretch;min-width:0;width:200px;flex:0 0 200px;}"
        "#leaf{min-width:0;flex:0 0 200px;width:200px;height:20px;}";
    const bool fixed = std::strcmp(mode,"fixed") == 0;
    const bool start = std::strcmp(mode,"start") == 0;
    const bool minimum = std::strcmp(mode,"minimum") == 0;
    const bool wrap = std::strcmp(mode,"wrap") == 0;
    const bool margins = std::strcmp(mode,"margins") == 0;
    if (fixed || wrap || margins) css += "#root{height:200px}";
    if (minimum) css += "#root{min-height:200px}";
    if (start) css += ".n{align-items:flex-start}";
    if (wrap) css += ".n{flex-wrap:wrap}";
    if (margins) css += ".n{margin-top:auto;margin-bottom:auto}";
    Fixture f(html,css,contextual);
    callbacks = 0;
    auto frame = f.layout();
    const auto measured = callbacks;
    const std::string label = std::string("callbacks/") + mode + "/depth" + std::to_string(depth) + (contextual ? "/contextual" : "/resolved");
    const auto root = f.rect(frame,"root"), leaf = f.rect(frame,"leaf");
    number(root.width,200,label,"root width");
    number(root.height,(fixed || wrap || margins || minimum) ? 200.f : 20.f,label,"root height");
    number(leaf.width,200,label,"leaf width"); number(leaf.height,20,label,"leaf height");
    std::printf("%s: %llu\n",label.c_str(),measured);
    aether_layout_result_destroy(frame);
    return measured;
}
void scaling() {
    std::vector<int> optimized_depths;
    for (int depth = 1; depth <= 16; ++depth) optimized_depths.push_back(depth);
    optimized_depths.push_back(32); optimized_depths.push_back(64);
    for (bool contextual : {false,true}) {
        for (const char* mode : {"fixed","start","margins"}) {
            for (int depth : optimized_depths) {
                const auto count = chain(mode,depth,contextual);
                check(count == 5,std::string("scaling/") + mode + "/" + std::to_string(depth),"five final text callbacks");
                // A regression still fails, but does not accidentally request
                // 2^63 natural layouts on the unoptimized implementation.
                if (count != 5) break;
            }
        }
        for (const char* mode : {"auto","minimum"}) {
            for (int depth = 1; depth <= 16; ++depth) {
                const auto expected = 5ULL * static_cast<unsigned long long>(depth);
                check(chain(mode,depth,contextual) == expected,std::string("scaling/") + mode + "/" + std::to_string(depth),
                      "one necessary natural text measurement per level");
            }
        }
        // Wrap is intentionally excluded even when it happens to form one line.
        for (int depth : {1,2,4,8,16})
            check(chain("wrap",depth,contextual) == (5ULL << (depth - 1)),"scaling/wrap","excluded wrap callback count");
    }
}
} // namespace

int main() {
    sizes_and_controls(); clamps_margins_and_bases(); percentage_semantics();
    special_descendants(); order_and_lifetime(); scroll_and_clipping(); scaling();
    std::printf("flex-stretch-deferred: %d fixtures, %d checks, %d failures\n",scenarios,checks,failures);
    return failures ? 1 : 0;
}
