#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/measure_state.h"
#include "frozen_measure_test_support.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
static void require(bool ok, const char* message) { if (!ok) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); } }
static bool near(float a, float b) { return std::abs(a-b) < .001f; }
struct Fixture {
    aether_doc_t doc{}; aether_style_runtime_t runtime{};
    std::map<aether_node_t,aether_computed_style_handle_t> styles;
    Fixture(const char* css, float initial = 16) {
        require(aether_doc_create(&doc)==AETHER_OK,"doc");
        require(aether_doc_parse_html(doc,"<html><body><div id='p'><div id='c'></div></div></body></html>")==AETHER_OK,"html");
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{800,600,1,initial,0,0,0};
        require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"runtime");
        int64_t id{}; require(aether_style_runtime_add_stylesheet(runtime,css,0,"measures",&id,nullptr)==AETHER_OK,"css");
        resolve();
    }
    void resolve() {
        aether_style_update_t* updates{};
        int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);
        require(n>=0,"resolve");
        for (int i=0;i<n;++i) { aether_node_t node{}; aether_computed_style_handle_t h{}; uint32_t impact{};
            require(aether_style_runtime_get_update(updates[i],&node,&h,&impact)==AETHER_OK,"update");
            if(styles.count(node)) aether_computed_style_handle_destroy(styles[node]); styles[node]=h;
        } aether_style_updates_free(updates,n);
    }
    aether_computed_style_handle_t at(const char* id) { auto n=aether_doc_get_element_by_id(doc,id); require(styles.count(n),"node style"); return styles[n]; }
    aether_computed_style_handle_t contextual(const char* id) {
        const auto wanted = aether_doc_get_element_by_id(doc,id);
        aether_style_revision_t revision{};
        require(aether_style_runtime_retain_revision(runtime,&revision)==AETHER_OK,"retain revision");
        aether_computed_style_handle_t result{};
        for(int i=0;i<aether_style_revision_style_count(revision);++i) {
            aether_node_t node{}; aether_computed_style_handle_t h{}; uint32_t impact{};
            require(aether_style_revision_get_style(revision,i,&node,&h,&impact)==AETHER_OK,"revision style");
            if(node==wanted) result=h; else aether_computed_style_handle_destroy(h);
        }
        aether_style_revision_destroy(revision); require(result!=nullptr,"contextual style found"); return result;
    }
    ~Fixture() { for(auto p:styles) aether_computed_style_handle_destroy(p.second); aether_style_runtime_destroy(runtime);aether_doc_destroy(doc); }
};
int main() {
    {
        Fixture f("html{font-size:32px}body{font-size:20px}#p{font-size:40px}#c{gap:1em 2em;font-size:2em;width:1rem;height:2vw}");
        auto h=f.at("c"); require(near(h->style.font_size,80),"font em parent");
        require(near(h->style.width.value,32),"rem html not body"); require(near(h->style.height.value,16),"viewport");
        require(h->measures && near(h->measures->scalars().row_gap.value,80) && near(h->measures->scalars().column_gap.value,160),"two gap axes own font");
    }
    for (const auto* line : {"2", "calc(2)", "2em", "200%"}) {
        Fixture f((std::string("#p{font-size:20px;width:2em;line-height:")+line+"}#c{font-size:10px;width:inherit}").c_str());
        auto h=f.at("c"); require(near(h->style.width.value,40),"inherit computed length");
        require(near(frozen_measure_test::used_line_height(h),(std::string(line)=="2" || std::string(line)=="calc(2)")?20:40),"typed inherited line height");
    }
    {
        Fixture f("#p{width:50%;font-size:40px}#c{width:inherit;font-size:50%}");
        auto h=f.at("c"); require(h->style.width.unit==AETHER_CSS_UNIT_PERCENT && near(h->style.width.value,50),"inherit percentage"); require(near(h->style.font_size,20),"font percent");
    }
    {
        Fixture f("#c{width:80px;width:-1px;width:12;padding-left:20px;padding-left:-1px;border-left-width:3px;border-left-width:10%}");
        auto h=f.at("c"); require(near(h->style.width.value,80),"strict width fallback"); require(near(h->style.padding.left.value,20),"strict padding fallback"); require(near(h->style.border_width.left.value,3),"border rejects percentage");
        aether_css_length legacy{}; require(aether_css_length_try_parse("12",&legacy) && near(legacy.value,12),"legacy parser unchanged");
    }
    {
        Fixture f("html{font-size:0}#c{font-size:0;width:1rem;height:1em}"); auto h=f.at("c"); require(near(h->style.width.value,0)&&near(h->style.height.value,0),"zero font is valid");
    }
    {
        Fixture f("html{font-size:2rem}#c{width:1rem;font-size:initial}",10); auto h=f.at("c"); require(near(h->style.width.value,20)&&near(h->style.font_size,10),"initial bootstrap and root rem");
    }
    {
        Fixture f("html{font-size:32px}#p{font-size:20px;transform-origin:2em 1rem;text-shadow:1em 1rem red}#c{font-size:10px;transform:translate(10vw,1rem);transform-origin:inherit;border-radius:1rem;background:linear-gradient(red 1rem,blue 10vw)}");
        auto h=f.at("c"); require(h->style.transforms.count==1,"computed transform exists");
        require(h->style.transforms.items[0].x.unit==AETHER_CSS_UNIT_PX && near(h->style.transforms.items[0].x.value,80),"transform vw computed");
        require(h->style.transforms.items[0].y.unit==AETHER_CSS_UNIT_PX && near(h->style.transforms.items[0].y.value,32),"transform rem computed");
        require(near(h->style.transform_origin.x.value,40) && near(h->style.transform_origin.y.value,32),"origin inherits computed parent");
        require(near(h->style.border_radii.top_left.horizontal.value,32),"radius computed");
        require(h->style.text_shadows.count==1 && near(h->style.text_shadows.items[0].offset_x,20) && near(h->style.text_shadows.items[0].offset_y,32),"shadow computed inheritance");
    }
    for (const char* expression : {"0%", "calc(0% + 30px)", "calc(50% - 50% + 30px)"}) {
        Fixture f((std::string("#p{height:") + expression + "}#c{height:inherit}").c_str());
        auto h = f.at("c");
        const bool percent = frozen_measure_test::has_percentage(h->measures.get(), "height", h->style.height);
        require(percent, "zero percentage category inherited");
        require(measure_resolve_used_length(h->style.height, 0, false, percent).kind == measure_used_kind_e::deferred_percentage, "zero percentage indefinite deferred");
        require(measure_resolve_used_length(h->style.height, 0, true, percent).kind == measure_used_kind_e::resolved, "defined zero is definite");
    }
    {
        Fixture f("#c{width:16px}");
        require(aether_element_set_attribute(aether_doc_get_element_by_id(f.doc,"c"),"style","width:1rem")==AETHER_OK,"metadata edit");
        f.resolve();
        auto retained=f.contextual("c");
        require((retained->measures->dependency("width") & MEASURE_ROOT_FONT) != 0,"metadata-only dependency published");
        require(near(retained->style.width.value,16),"metadata-only same value");
        require(aether_element_set_attribute(aether_doc_get_document_element(f.doc),"style","font-size:20px")==AETHER_OK,"root edit");
        f.resolve(); auto current=f.contextual("c");
        require(near(current->style.width.value,20),"metadata root invalidates");
        require(near(retained->style.width.value,16) && near(retained->measures->document().values().root_font_size,16),"old context remains immutable");
        aether_computed_style_handle_destroy(current); aether_computed_style_handle_destroy(retained);
    }
    {
        Fixture f("html{font-size:0}#c{width:10vw;height:1rem}@media(min-width:75em){#c{padding-left:7px}}",10);
        auto first=f.contextual("c");
        require(near(first->style.width.value,80) && near(first->style.height.value,0),"zero root viewport initial");
        require(near(first->style.padding.left.value,7),"media queries use initial font10");
        aether_style_environment_t env{1000,600,1,10,0,0,0};
        aether_style_transaction_t transaction{}; aether_style_revision_t revision{};
        require(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),&env,&transaction)==AETHER_OK,"resize prepare");
        require(aether_style_runtime_commit_revision(f.runtime,transaction,&revision)==AETHER_OK,"resize commit");
        aether_style_transaction_destroy(transaction); aether_style_revision_destroy(revision);
        auto after=f.contextual("c");
        require(near(after->style.width.value,100)&&near(after->style.height.value,0),"resize1000 rootfont0");
        require(near(first->style.width.value,80)&&near(first->measures->document().values().viewport_width,800),"resize retains old context");
        require(aether_element_set_attribute(aether_doc_get_document_element(f.doc),"style","font-size:100px")==AETHER_OK,"root font100");
        f.resolve(); auto large=f.contextual("c");
        require(near(large->style.padding.left.value,7)&&near(large->style.height.value,100),"MQ threshold unaffected by root CSS");
        aether_computed_style_handle_destroy(first); aether_computed_style_handle_destroy(after); aether_computed_style_handle_destroy(large);
    }
    for (const bool explicit_child : {false,true}) {
        const std::string child = explicit_child ? "background-position:10px 11px;background-size:15px 16px;background-repeat:space;background-image:inherit" : "background-image:inherit";
        Fixture f(("#p{background-image:url(a),url(b);background-position:30px 31px;background-size:40px 41px;background-repeat:no-repeat}#c{"+child+"}").c_str());
        auto h=f.at("c"); require(h->style.background_layers.count==2,"image payload inherits all layers");
        for(int i=0;i<2;++i) {
            const auto& l=h->style.background_layers.items[i];
            require(near(l.position_x.value,explicit_child?10:0)&&near(l.position_y.value,explicit_child?11:0),"image inherit does not inherit parent position");
            require(explicit_child ? (near(l.size_x.value,15)&&near(l.size_y.value,16)) : (l.size_x.unit==AETHER_CSS_UNIT_AUTO&&l.size_y.unit==AETHER_CSS_UNIT_AUTO),"image inherit keeps child size initial or declared");
            require(l.repeat_x==(explicit_child?AETHER_BACKGROUND_SPACE:AETHER_BACKGROUND_REPEAT),"image inherit does not inherit repeat");
        }
    }
    for (const bool image_first : {false,true}) {
        const std::string image="background-image:url(a),url(b),url(c);";
        const std::string values="font-size:10px;background-position:inherit;background-size:inherit;";
        Fixture f(("#p{font-size:20px;background-position:30px 31px,40px 41px;background-size:2em 3em}#c{"+(image_first?image+values:values+image)+"}").c_str());
        auto h=f.at("c"); require(h->style.background_layers.count==3,"child can have images when parent has none");
        for(int i=0;i<3;++i) {
            const auto& l=h->style.background_layers.items[i];
            require(near(l.position_x.value,i%2?40:30)&&near(l.position_y.value,i%2?41:31),"position inherits unpainted parent list cyclically independent of declaration order");
            require(near(l.size_x.value,40)&&near(l.size_y.value,60),"size inherits computed parent list not child em");
        }
    }
    {
        Fixture f("#p{background-position:30px 31px;background-size:40px 41px}#c{background-position:inherit;background-size:inherit}");
        auto empty=f.contextual("c"); require(empty->style.background_layers.count==0,"longhands do not create painted images");
        require(near(empty->measures->background_positions()[0].x.value,30),"position retained with no painted layer");
        require(aether_element_set_attribute(aether_doc_get_element_by_id(f.doc,"c"),"style","background-image:url(later)")==AETHER_OK,"add later image");
        f.resolve(); auto current=f.contextual("c");
        require(current->style.background_layers.count==1&&near(current->style.background_layers.items[0].position_x.value,30)&&near(current->style.background_layers.items[0].size_x.value,40),"inherited lists applied when image appears later");
        require(empty->style.background_layers.count==0,"old unpainted snapshot stays unchanged");
        aether_computed_style_handle_destroy(empty);aether_computed_style_handle_destroy(current);
    }
    for(const char* declaration : {"font-size:3e38%", "font-size:calc(3e38% + 1px)",
                                  "font-size:2000px;line-height:3e38%", "font-size:2000px;line-height:calc(3e38% + 1px)",
                                  "font-size:2000px;line-height:calc(3e38)"}) {
        Fixture f("#p{font-size:2000px}#c{font-size:20px;line-height:2}");
        auto before=f.contextual("c");
        aether_style_revision_t retained{};require(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK,"retain before numeric overflow");
        const auto generation=aether_style_revision_generation(retained);
        require(aether_element_set_attribute(aether_doc_get_element_by_id(f.doc,"c"),"style",declaration)==AETHER_OK,"set finite authored value with computed overflow");
        aether_style_transaction_t transaction{};
        require(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&transaction)==AETHER_ERROR_INVALID_STATE,"numeric computed overflow rejects candidate");
        require(transaction==nullptr,"numeric overflow has no candidate");
        aether_style_revision_t current{};require(aether_style_runtime_retain_revision(f.runtime,&current)==AETHER_OK,"retain after numeric overflow");
        require(aether_style_revision_generation(current)==generation,"numeric overflow preserves generation");
        auto after=f.contextual("c");
        require(near(after->style.font_size,20)&&near(frozen_measure_test::used_line_height(after),40),"numeric overflow cannot fall back or replace previous style");
        require(near(before->style.font_size,20),"old numeric snapshot intact");
        aether_computed_style_handle_destroy(before);aether_computed_style_handle_destroy(after);
        aether_style_revision_destroy(retained);aether_style_revision_destroy(current);
    }
    {
        aether_css_length percent{100,AETHER_CSS_UNIT_PERCENT,0,0,0,0,0};
        const float basis=1e38f;
        require(aether_css_length_resolve(percent,basis,16,16,800,600)==basis,"finite percentage result cannot overflow intermediate multiplication");
        aether_css_length cancelled{0,AETHER_CSS_UNIT_LINEAR,0,3e38f,-3e38f,0,0};
        require(aether_css_length_resolve(cancelled,0,2000,2000,800,600)==0,"affine cancellation does not overflow float intermediates");
        require(std::isnan(aether_css_length_resolve(aether_css_length_auto(),0,16,16,800,600)),"legacy AUTO remains NaN");
    }
    std::puts("computed measures passed");
}
