#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <string>

#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); std::abort(); } } while (false)
static void equal(float actual, float expected) {
    if (std::fabs(actual - expected) > .01f) {
        std::fprintf(stderr,"expected %.3f, got %.3f\n", expected, actual); std::abort();
    }
}
struct fixture {
    aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_style_map_t map{};
    aether_layout_engine_t engine{}; aether_layout_result_t layout{};
    fixture(const char* css, const char* children = "<div id='c'></div>", bool layout_succeeds = true) {
        REQUIRE(aether_doc_create(&doc) == AETHER_OK);
        const auto html = std::string("<!doctype html><html><body><div id='p'>") + children + "</div></body></html>";
        aether_doc_parse_html(doc, html.c_str());
        aether_style_runtime_options_t options{};
        aether_style_environment_t environment{800,600,1,16,0,0,0};
        REQUIRE(aether_style_runtime_create(&options,&environment,nullptr,nullptr,&runtime) == AETHER_OK);
        int64_t sheet;
        const auto source = std::string("*{display:block;margin:0;padding:0}") + css;
        aether_style_runtime_add_stylesheet(runtime,source.c_str(),0,"used-geometry",&sheet,nullptr);
        aether_style_update_t* updates{};
        const int count = aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        REQUIRE(count >= 0); aether_style_updates_free(updates,count);
        aether_style_revision_t revision{};
        REQUIRE(aether_style_runtime_retain_revision(runtime,&revision)==AETHER_OK);
        REQUIRE(aether_style_revision_create_map(revision,&map)==AETHER_OK);
        aether_style_revision_destroy(revision);
        REQUIRE(aether_layout_create(nullptr,nullptr,&engine) == AETHER_OK);
        REQUIRE(aether_layout_layout(engine,doc,map,800,600,nullptr,nullptr,&layout) == (layout_succeeds ? AETHER_OK : AETHER_ERROR_INVALID_STATE));
    }
    aether_rectf rect(const char* id) {
        return aether_layout_box_get_border_box(aether_layout_result_find_primary_box(layout,aether_doc_get_element_by_id(doc,id)));
    }
    ~fixture() { aether_layout_result_destroy(layout); aether_layout_destroy(engine); aether_style_map_destroy(map); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc); }
};
struct callback_context { aether_node_t node; int calls = 0; bool mutate = false; };
static aether_text_metrics_t contextual_measure(const char*, const aether_computed_style_t*, float line, float, void* data) {
    auto& context = *static_cast<callback_context*>(data); ++context.calls;
    equal(line,40);
    if (context.mutate) aether_element_set_attribute(context.node,"title","mutated");
    return {10,line,line*.8f};
}
int main() {
    { fixture f("html{font-size:32px}body{font-size:20px}#c{font-size:10px;width:1rem;height:1rem}"); equal(f.rect("c").width,32); equal(f.rect("c").height,32); }
    { fixture f("#p{height:auto}#c{height:50%}"); equal(f.rect("c").height,0); }
    { fixture f("#p{height:auto;min-height:200px}#c{height:50%;min-height:50%;max-height:20%}"); equal(f.rect("c").height,0); }
    { fixture f("#p{height:200px}#c{height:50%}"); equal(f.rect("c").height,100); }
    for (const char* mode : {"", "position:absolute;", "display:inline-block;"}) {
        std::string css = "#c{"; css += mode; css += "width:20px;height:20px;min-width:100px;max-width:50px;min-height:100px;max-height:50px}";
        fixture f(css.c_str()); equal(f.rect("c").width,100); equal(f.rect("c").height,100);
    }
    { fixture f("#p{display:flex;width:50px;flex-wrap:wrap;row-gap:30px;column-gap:10px;align-items:start}#p>div{width:20px;height:10px;flex-shrink:0}","<div id='a'></div><div id='b'></div><div id='c'></div>"); equal(f.rect("b").x-f.rect("a").x,30); equal(f.rect("c").y-f.rect("a").y,40); }
    { fixture f("html{font-size:0}#c{width:1rem;height:2em}"); equal(f.rect("c").width,0); equal(f.rect("c").height,0); }
    { fixture f("#p{font-size:20px;line-height:2}#c{font-size:10px}","<div id='c'>text</div>"); equal(f.rect("c").height,20); }
    { fixture f("#p{font-size:20px;line-height:2em}#c{font-size:10px}","<div id='c'>text</div>"); equal(f.rect("c").height,40); }
    { fixture f("#p{font-size:20px;line-height:2em;display:flex}","text"); equal(f.rect("p").height,40); }
    {
        fixture f("html{font-size:32px}#c{width:1rem;height:20px;border:1vw solid red}");
        aether_display_list_builder_t builder{};
        REQUIRE(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
        aether_display_list_t old_list{};
        REQUIRE(aether_display_list_build(builder,f.layout,nullptr,&old_list)==AETHER_OK);
        int64_t sheet; aether_style_runtime_add_stylesheet(f.runtime,"html{font-size:64px}#c{width:2rem}",0,"next",&sheet,nullptr);
        aether_style_update_t* updates{};
        int count=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&updates);
        REQUIRE(count>=0); aether_style_updates_free(updates,count);
        aether_style_map_t next{};
        aether_style_revision_t revision{};
        REQUIRE(aether_style_runtime_retain_revision(f.runtime,&revision)==AETHER_OK);
        REQUIRE(aether_style_revision_create_map(revision,&next)==AETHER_OK);
        aether_style_revision_destroy(revision);
        auto old_layout=f.layout;
        REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&old_layout)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(old_layout==f.layout);
        aether_display_list_t untouched=old_list;
        REQUIRE(aether_display_list_build_with_styles(builder,f.layout,next,nullptr,&untouched)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(untouched==old_list);
        aether_style_map_destroy(next);
        aether_style_map_destroy(f.map); f.map=nullptr;
        aether_style_runtime_destroy(f.runtime); f.runtime=nullptr;
        aether_display_list_t retained{};
        REQUIRE(aether_display_list_build(builder,f.layout,nullptr,&retained)==AETHER_OK);
        REQUIRE(aether_display_list_get_count(retained)==aether_display_list_get_count(old_list));
        equal(f.rect("c").width,48);
        aether_display_list_destroy(retained); aether_display_list_destroy(old_list);
        aether_display_list_builder_destroy(builder);
    }
    {
        fixture f("#p{font-size:20px;line-height:2em}#c{font-size:10px}","<div id='c'>text</div>");
        callback_context context{aether_doc_get_element_by_id(f.doc,"c")};
        aether_layout_set_contextual_text_measurer(f.engine,contextual_measure,&context);
        aether_layout_result_t next{};
        REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&next)==AETHER_OK);
        REQUIRE(context.calls>0); aether_layout_result_destroy(next);
        context.mutate=true; next=f.layout;
        REQUIRE(aether_layout_layout(f.engine,f.doc,f.map,800,600,nullptr,nullptr,&next)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(next==f.layout);
    }
    {
        fixture f("#p{display:flex;font-size:20px;line-height:2em}","text");
        aether_layout_result_t clone{};
        REQUIRE(aether_layout_clone_snapshot(f.layout,&clone)==AETHER_OK);
        aether_layout_result_destroy(f.layout); f.layout=nullptr;
        for (int i=0; i<1000; ++i) {
            aether_layout_result_t next{};
            REQUIRE(aether_layout_clone_snapshot(clone,&next)==AETHER_OK);
            aether_layout_result_destroy(clone); clone=next;
        }
        aether_display_list_builder_t builder{}; aether_display_list_t list{};
        REQUIRE(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
        REQUIRE(aether_display_list_build(builder,clone,nullptr,&list)==AETHER_OK);
        equal(aether_layout_box_get_border_box(aether_layout_result_find_primary_box(clone,aether_doc_get_element_by_id(f.doc,"p"))).height,40);
        aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
        aether_layout_result_destroy(clone);
    }
    for (bool placeholder : {false,true}) {
        fixture f("input{width:100px;height:20px}", placeholder ? "<input id='c' placeholder='before'>" : "<input id='c' value='before'>");
        auto* node=aether_doc_get_element_by_id(f.doc,"c");
        aether_element_set_attribute(node,"value","after");
        aether_element_set_attribute(node,"placeholder","new placeholder");
        aether_display_list_builder_t builder{}; aether_display_list_t list{};
        REQUIRE(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
        REQUIRE(aether_display_list_build(builder,f.layout,nullptr,&list)==AETHER_OK);
        bool found=false;
        for (int i=0; i<aether_display_list_get_count(list); ++i) {
            aether_paint_command_t command{};
            REQUIRE(aether_display_list_get_command(list,i,&command)==AETHER_OK);
            if(command.tag==AETHER_PAINT_DRAW_TEXT) {
                REQUIRE(std::string(command.data.draw_text.text)=="before"); found=true;
            }
            aether_paint_command_free(&command);
        }
        REQUIRE(found); aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
    }
    {
        fixture f("#p{width:100px;padding:10px;transform:translateX(0)}#inner{height:200px}#c{position:fixed;left:10%;top:50%;width:50%;height:50%}","<div id='inner'><div id='c'></div></div>");
        const auto r=f.rect("c"); equal(r.x,12); equal(r.y,110); equal(r.width,60); equal(r.height,110);
    }
    {
        fixture a("#c{width:10px}"), b("#c{width:20px}");
        auto unchanged=b.layout;
        REQUIRE(aether_layout_layout(b.engine,b.doc,a.map,800,600,nullptr,nullptr,&unchanged)==AETHER_ERROR_INVALID_STATE);
        REQUIRE(unchanged==b.layout);
    }
    for (const char* property : {"height", "flex-basis", "max-height", "min-height"}) {
        for (const char* value : {"calc(10px + 0%)", "calc(10px + 50% - 50%)"}) {
            const auto css=std::string("#p{display:flex;flex-direction:column;height:auto}#c{flex-shrink:0;")+property+":"+value+"}#g{height:30px}";
            fixture f(css.c_str(),"<div id='c'><div id='g'></div></div>"); equal(f.rect("c").height,30);
        }
    }
    { fixture f("#p{display:flex;flex-direction:column;height:0px}#c{height:calc(10px + 0%);flex-shrink:0}#g{height:30px}","<div id='c'><div id='g'></div></div>"); equal(f.rect("c").height,10); }
    for (const char* property : {"width","height","min-width","max-width","padding-left"}) {
        const auto css=std::string("#p{width:1000px;height:1000px}#c{")+property+":3e38%}";
        fixture f(css.c_str(),"<div id='c'></div>",false);
    }
    { fixture f("#p{display:flex;flex-direction:column;height:1000px;row-gap:3e38%}#p>div{height:1px}","<div></div><div></div>",false); }
    { fixture f("#c{width:3e38px;padding-left:3e38px}","<div id='c'></div>",false); }
    for (bool definite : {false,true}) {
        const auto css=std::string("#p{display:flex;flex-direction:column;row-gap:calc(10px + 10%);height:")+(definite ? "100px" : "auto")+"}#p>div{height:10px;flex-shrink:0}";
        fixture f(css.c_str(),"<div id='a'></div><div id='c'></div>");
        equal(f.rect("c").y-f.rect("a").y-f.rect("a").height,definite ? 20 : 10);
    }
    std::puts("contextual used geometry PASS");
    return 0;
}
