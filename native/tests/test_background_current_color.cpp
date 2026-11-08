// Public ABI regression: no private headers, no assert/NDEBUG dependence, no
// clear_cache. --baseline-red runs only the two original contextual defects.
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
unsigned checks=0, failures=0;
void need(bool ok,const char* label) {
    if(!ok){std::fprintf(stderr,"HARNESS FAIL: %s\n",label);std::exit(2);}
}
void check(bool ok,const std::string& label) {
    ++checks;if(!ok){++failures;if(failures<=40)std::fprintf(stderr,"FAIL: %s\n",label.c_str());}
}
bool near(float a,float b){return std::fabs(a-b)<0.00001f;}
bool equal(aether_color_rgba a,aether_color_rgba b){return near(a.r,b.r)&&near(a.g,b.g)&&near(a.b,b.b)&&near(a.a,b.a);}
const aether_color_rgba purple{102/255.f,51/255.f,153/255.f,1}, red{1,0,0,1}, blue{0,0,1,1}, lime{0,1,0,1}, clear{0,0,0,0};
struct Flat {
    aether_computed_style_t value{};
    explicit Flat(aether_computed_style_handle_t h){need(aether_computed_style_copy(h,&value)==AETHER_OK,"copy");}
    Flat(const Flat&)=delete;
    ~Flat(){aether_computed_style_free(&value);}
};
struct Fixture {
    aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};
    explicit Fixture(const std::string& html="<html><body><div id='x'></div></body></html>") {
        need(aether_doc_create(&doc)==AETHER_OK,"doc");need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"HTML");
        aether_style_environment_t env{800,600,1,16,0,0,0};
        aether_style_runtime_options_t opt{};opt.max_variable_depth=32;opt.max_variable_expansion_length=65536;
        need(aether_style_runtime_create(&opt,&env,nullptr,nullptr,&runtime)==AETHER_OK,"runtime");
    }
    ~Fixture(){aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
    aether_node_t node(const char* id){auto n=aether_doc_get_element_by_id(doc,id);need(n!=nullptr,"node");return n;}
    int64_t add(const std::string& css,int layer=2){int64_t s{};need(aether_style_runtime_add_stylesheet(runtime,css.c_str(),layer,"background-current-color",&s,nullptr)==AETHER_OK,"sheet");return s;}
    void style(const char* id,const std::string& css){need(aether_element_set_attribute(node(id),"style",css.c_str())==AETHER_OK,"inline style mutation");}
    void resolve(){
        aether_style_transaction_t tx{};aether_style_revision_t next{};
        need(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),nullptr,&tx)==AETHER_OK,"prepare");
        need(aether_style_runtime_commit_revision(runtime,tx,&next)==AETHER_OK,"commit");
        aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);revision=next;
    }
    aether_computed_style_handle_t handle(const char* id,uint32_t* impact=nullptr){
        const auto target=node(id);
        for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t n{};aether_computed_style_handle_t h{};uint32_t bits{};
            need(aether_style_revision_get_style(revision,i,&n,&h,&bits)==AETHER_OK,"entry");
            if(n==target){if(impact)*impact=bits;return h;}aether_computed_style_handle_destroy(h);
        }need(false,"missing revision node");return nullptr;
    }
    uint32_t expect(const char* id,aether_color_rgba expected,const std::string& label){uint32_t impact{};auto h=handle(id,&impact);{Flat f(h);check(equal(f.value.background_color,expected),label);}aether_computed_style_handle_destroy(h);return impact;}
};
struct Case {const char* label;std::string css;aether_color_rgba expected;};
void basics(bool baseline){
    std::vector<Case> cases={
        {"longhand independent RGBA","background-color:CuRrEnTcOlOr;color:rebeccapurple;",purple},
        {"standalone independent RGBA","color:rebeccapurple;background:currentColor;",purple}
    };
    if(baseline) cases={{"longhand independent RGBA","color:red;background-color:currentColor;",red},
                        {"standalone independent RGBA","background:currentColor;color:red;",red}};
    if(!baseline){const std::vector<Case> more={
        {"alpha retained","color:rgba(20,40,60,.25);opacity:.5;background-color:currentColor;",{20/255.f,40/255.f,60/255.f,.25f}},
        {"inherited producer","background-color:currentColor;",purple},
        {"color currentColor producer","color:currentColor;background-color:currentColor;",purple},
        {"fill producer","text-fill-color:blue;background-color:currentColor;",blue},
        {"webkit producer","-webkit-text-fill-color:lime;background-color:currentColor;",lime},
        // Engine aliases share a producer; registry order, not CSS alias equivalence.
        {"alias registry precedence","text-fill-color:blue;-webkit-text-fill-color:lime;color:red;background-color:currentColor;",blue},
        {"initial clears symbol","background-color:currentColor;background-color:initial;",clear},
        {"unset clears symbol","background-color:currentColor;background-color:unset;",clear},
        {"revert existing initial contract","background-color:currentColor;background-color:revert;",clear},
        {"literal invalid retains fallback","background-color:blue;background-color:nonsense;",blue},
        {"var symbol","--b:currentColor;background-color:var(--b);",purple},
        {"var concrete","--b:blue;background-color:var(--b);",blue},
        {"var initial","background-color:currentColor;background-color:var(--missing,initial);",clear},
        {"var unset","background-color:currentColor;background-color:var(--missing,unset);",clear},
        {"var revert","background-color:currentColor;background-color:var(--missing,revert);",clear},
        {"custom inherit absent stays missing","--b:inherit;background-color:currentColor;background-color:var(--b);",clear},
        {"var fallback","background-color:var(--missing,currentColor);",purple},
        {"var missing no resurrection","background-color:blue;background-color:var(--missing);",clear},
        {"var invalid no resurrection","--b:nonsense;background-color:blue;background-color:var(--b,lime);",clear},
        {"deferred shorthand","--b:currentColor;background:var(--b);",purple},
        {"deferred shorthand invalid","--b:nonsense;background:blue;background:var(--b);",clear},
        {"deferred shorthand missing","background:blue;background:var(--missing);",clear},
        {"deferred shorthand fallback","background:var(--missing,currentColor);",purple},
        {"later literal wins deferred shorthand","--b:currentColor;background:var(--b);background-color:blue;",blue},
        {"later deferred shorthand wins literal","--b:currentColor;background-color:blue;background:var(--b);",purple},
        {"important longhand wins shorthand","background-color:blue!important;--b:currentColor;background:var(--b);",blue},
        {"important deferred longhand wins literal","--b:currentColor;background-color:var(--b)!important;background-color:blue;",purple},
        {"none resets reference","background-color:currentColor;background:none;",clear},
        {"none external reference","background:none currentColor;",purple},
        {"standalone resets image size repeat","background:url(asset://x) center / 10px 20px no-repeat blue;background:currentColor;",purple}
    };cases.insert(cases.end(),more.begin(),more.end());}
    std::string html="<html><body id='parent'>",css="#parent{color:rebeccapurple;}";
    for(size_t i=0;i<cases.size();++i){const auto id="x"+std::to_string(i);html+="<div id='"+id+"'></div>";css+="#"+id+"{"+cases[i].css+"}";}
    Fixture f(html+"</body></html>");f.add(css);f.resolve();
    for(size_t i=0;i<cases.size();++i){const auto id="x"+std::to_string(i);f.expect(id.c_str(),cases[i].expected,cases[i].label);}
    if(!baseline){auto h=f.handle(("x"+std::to_string(cases.size()-1)).c_str());{Flat flat(h);check(flat.value.background_layers.count==0,"standalone clears image layers");}aether_computed_style_handle_destroy(h);}
}
void compound(){
    const std::string g="linear-gradient(red 0%,blue 100%)";
    const std::vector<std::string> valid={"url(asset://x) currentColor",g+" currentColor",g+",url(asset://x) currentColor",
        g+" currentColor center / 10px 20px no-repeat",g+" center / cover currentColor",g+" center / 10px currentColor",
        g+" center / 10px 20px currentColor",g+" center / 10px no-repeat currentColor"};
    for(size_t i=0;i<valid.size();++i){std::string concrete=valid[i];auto p=concrete.find("currentColor");concrete.replace(p,12,"#663399");
        Fixture f("<html><body><div id='x'></div><div id='control'></div></body></html>");
        f.add("#x{color:#663399;background:"+valid[i]+"}#control{background:"+concrete+"}");f.resolve();
        f.expect("x",purple,"compound "+std::to_string(i));auto a=f.handle("x"),b=f.handle("control");{
            Flat x(a),control(b);check(aether_background_layer_list_equal(&x.value.background_layers,&control.value.background_layers),"all compound layer fields "+std::to_string(i));
            check(x.value.background_layers.count==(i==2?2:1),"nonempty compound layer count");
            if(i!=0 && x.value.background_layers.count>0){const auto& layer=x.value.background_layers.items[0];check(layer.gradient.stop_count==2,"two concrete stops");if(layer.gradient.stop_count==2){check(equal(layer.gradient.stops[0].color,red)&&equal(layer.gradient.stops[1].color,blue),"independent gradient stop RGBA");}}
            if((i==5||i==6)&&x.value.background_layers.count>0){const auto& layer=x.value.background_layers.items[0];check(layer.size_kind==AETHER_BACKGROUND_SIZE_EXPLICIT&&near(layer.size_x.value,10),"one/two size width");check(i==5?layer.size_y.unit==AETHER_CSS_UNIT_AUTO:near(layer.size_y.value,20),"one/two size height");}
        }aether_computed_style_handle_destroy(a);aether_computed_style_handle_destroy(b);
    }
    const std::vector<std::string> invalid={g+" currentColor,"+g,"currentColor red","red currentColor","currentColor currentColor",
        "none none currentColor","none url(asset://x) currentColor","url(asset://x) none currentColor","url(asset://x) url(asset://y) currentColor",
        g+" center / 10px nonsense currentColor","linear-gradient(red,currentColor)"};
    for(const auto& value:invalid){Fixture f;f.add("#x{color:red;background:blue;background:"+value+";}");f.resolve();f.expect("x",blue,"rejected compound "+value);}
    for(const auto& value:std::vector<std::string>{"red","transparent"}){Fixture f;f.add("#x{background:"+g+" center / 10px "+value+";}");f.resolve();f.expect("x",value=="red"?red:clear,"concrete color size lookahead");auto h=f.handle("x");{Flat x(h);check(x.value.background_layers.count==1,"concrete lookahead keeps image");}aether_computed_style_handle_destroy(h);}
    Fixture image;image.add("#x{background-image:"+g+";background-image:currentColor;}");image.resolve();auto h=image.handle("x");{Flat x(h);check(x.value.background_layers.count==1&&x.value.background_layers.items[0].gradient.stop_count==2,"currentColor not accepted as image");}aether_computed_style_handle_destroy(h);
}
void mutations_and_inheritance(){
    Fixture f("<html id='root'><body><div id='parent'><div id='x'><div id='grand'></div></div><div id='plain'></div><div id='short'></div><div id='variable'></div></div><div id='fixed'></div></body></html>");
    f.add("#parent{color:red;background-color:red;}#x{color:blue;background-color:inherit;}#grand{color:lime;background-color:inherit;}#plain{color:blue;}#short{color:blue;background:inherit;}#variable{color:blue;background-color:var(--missing,inherit);}#fixed{color:red;background-color:blue;}#root{background-color:inherit;}");
    f.resolve();f.expect("root",clear,"inherit with no parent");f.expect("x",red,"concrete inherited background");f.expect("grand",red,"concrete chain");f.expect("plain",clear,"no implicit background inheritance");
    // Full traversal must recalculate descendants even when the parent's visible
    // RGBA is unchanged. Public style reuse must not reuse old symbolic metadata.
    // Mutate DOM declarations, not stylesheets: adding a sheet clears runtime
    // states and would test new-node ALL_BITS rather than incremental probes.
    f.style("parent","background-color:currentColor;");f.resolve();
    check(f.expect("parent",red,"parent unchanged RGBA forward")==AETHER_STYLE_IMPACT_NONE,"parent metadata-only no visual impact");
    auto impact=f.expect("x",blue,"symbolic inherit uses child producer");check((impact&AETHER_STYLE_IMPACT_PAINT)!=0,"child symbolic change paints");
    check((impact&(AETHER_STYLE_IMPACT_LAYOUT|AETHER_STYLE_IMPACT_TEXT_LAYOUT))==0,"isolated child background has no layout impact");
    f.expect("grand",lime,"symbolic inheritance chain");f.expect("short",blue,"shorthand explicit inheritance");f.expect("variable",blue,"var explicit inheritance");f.expect("plain",clear,"symbol not inherited by default");
    f.resolve();check(f.expect("x",blue,"no-op retains symbolic child")==AETHER_STYLE_IMPACT_NONE,"no-op child no visible update");
    f.style("parent","background-color:red;");f.resolve();check(f.expect("parent",red,"parent unchanged RGBA reverse")==AETHER_STYLE_IMPACT_NONE,"reverse parent metadata-only");
    check((f.expect("x",red,"reverse symbolic to concrete child")&AETHER_STYLE_IMPACT_PAINT)!=0,"reverse child paints");f.expect("grand",red,"reverse concrete chain");
    f.style("x","color:blue;background-color:currentColor;");f.resolve();f.expect("x",blue,"restore local reference");
    f.style("x","color:lime;background-color:currentColor;");f.style("fixed","color:lime;");f.resolve();
    check((f.expect("x",lime,"same runtime producer changes reference")&AETHER_STYLE_IMPACT_PAINT)!=0,"producer paints contextual background");
    // Do not forbid legitimate color-producer impacts here.
    f.expect("fixed",blue,"explicit background unaffected by producer");
    for(const auto& reset:std::vector<std::string>{"blue","transparent","initial","unset","revert","var(--missing)"}){
        f.style("x","color:red;background-color:currentColor;");f.resolve();f.expect("x",red,"seed reset reference");
        f.style("x","color:red;background-color:"+reset+";");f.resolve();const auto expected=reset=="blue"?blue:clear;f.expect("x",expected,"reset "+reset);
        f.style("x","color:lime;background-color:"+reset+";");f.resolve();f.expect("x",expected,"reset has no sticky flag "+reset);
    }
    f.style("x","color:lime;background-color:currentColor;");f.resolve();f.expect("x",lime,"removable symbol");
    // The public DOM ABI has no remove_attribute; empty style removes all inline
    // declarations while keeping the same node and cached previous revision.
    f.style("x","");f.resolve();f.expect("x",red,"removal restores concrete inherited background");
    f.style("x","color:lime;");f.resolve();f.expect("x",red,"removed reference does not revive on producer change");
}
void retained_and_stale(){
    aether_computed_style_handle_t old{},latest{},from_update{};aether_style_revision_t retained{};
    aether_style_update_t* updates{};int count{};aether_computed_style_t retained_copy{};
    {
        Fixture f;f.add("#x{color:red;background-color:currentColor;}");
        count=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&updates);need(count>0&&updates,"legacy updates");
        for(int i=0;i<count;++i){aether_node_t n{};aether_computed_style_handle_t h{};need(aether_style_runtime_get_update(updates[i],&n,&h,nullptr)==AETHER_OK,"read retained update");if(n==f.node("x"))from_update=h;else aether_computed_style_handle_destroy(h);}
        need(from_update!=nullptr,"target update retained");
        f.resolve();old=f.handle("x");need(aether_style_revision_clone(f.revision,&retained)==AETHER_OK,"retain revision");
        Flat copy(old);check(equal(copy.value.background_color,red),"old public copy red");
        need(aether_computed_style_copy(old,&retained_copy)==AETHER_OK,"copy retained beyond owners");
        aether_style_transaction_t tx{};need(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&tx)==AETHER_OK,"prepare stale");
        f.style("x","color:lime;");aether_style_revision_t rejected{};
        check(aether_style_runtime_commit_revision(f.runtime,tx,&rejected)==AETHER_ERROR_INVALID_STATE&&rejected==nullptr,"stale transaction rejected atomically");
        aether_style_transaction_destroy(tx);aether_style_revision_destroy(rejected);
        aether_style_revision_t current{};need(aether_style_runtime_retain_revision(f.runtime,&current)==AETHER_OK,"retain after stale");
        check(aether_style_revision_generation(current)==aether_style_revision_generation(retained),"stale did not publish revision");aether_style_revision_destroy(current);
        f.resolve();latest=f.handle("x");f.expect("x",lime,"fresh producer committed");check(equal(copy.value.background_color,red),"old copy immutable after resolve");
        {Flat prior(old),update(from_update);check(equal(prior.value.background_color,red)&&equal(update.value.background_color,red),"old handle and update remain red");}
    }
    // Owners retain the revision beyond runtime and DOM lifetime. No borrowed
    // builder pointers or dereferencing the now-detached node token are used.
    {Flat a(old),b(latest),c(from_update);check(equal(a.value.background_color,red)&&equal(c.value.background_color,red)&&equal(b.value.background_color,lime),"handles readable after runtime and DOM destruction");}
    for(int i=0;i<count;++i){aether_computed_style_handle_t h{};need(aether_style_runtime_get_update(updates[i],nullptr,&h,nullptr)==AETHER_OK,"retained update survives owners");aether_computed_style_handle_destroy(h);}
    check(equal(retained_copy.background_color,red),"public copy readable after owners destroyed");aether_computed_style_free(&retained_copy);
    aether_style_updates_free(updates,count);aether_computed_style_handle_destroy(old);aether_computed_style_handle_destroy(latest);aether_computed_style_handle_destroy(from_update);aether_style_revision_destroy(retained);
}
} // namespace
int main(int argc,char** argv){
    const bool baseline=argc==2&&std::string(argv[1])=="--baseline-red";
    if(argc!=1&&!baseline){std::fprintf(stderr,"usage: test_background_current_color [--baseline-red]\n");return 2;}
    basics(baseline);if(!baseline){compound();mutations_and_inheritance();retained_and_stale();}
    std::printf("background_current_color: %u checks, %u failures%s\n",checks,failures,baseline?" (focal baseline)":"");return failures?1:0;
}
