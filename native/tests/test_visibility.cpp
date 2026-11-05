// Public-ABI regressions for visibility:hidden. ASCII has fixed 10px advance.
// collapse intentionally follows the documented hidden fallback, not CSS table collapse.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures = 0, assertions = 0;
std::string context;
void check(bool value, const char* message) {
    ++assertions;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL [%s] %s\n", context.c_str(), message); }
}
void ok(aether_status_t value) {
    if (value != AETHER_OK) { std::fprintf(stderr, "ABI failure %d [%s]\n", value, context.c_str()); std::exit(2); }
}
bool near(float a, float b) { return std::fabs(a-b) < .001f; }
aether_text_metrics_t measure(const char* text, const aether_computed_style_t*, float, void*) {
    return {text ? float(std::strlen(text))*10.f : 0.f, 20.f, 16.f};
}
struct Update { aether_node_t node; uint32_t impact; };
struct Paint {
    std::vector<std::string> texts;
    int tags[32]{};
    int red = 0, green = 0;
    float green_alpha = 0;
    bool balanced = true, green_scoped = false;
};
struct Fixture {
    aether_doc_t doc = nullptr;
    aether_style_runtime_t runtime = nullptr;
    aether_style_map_t map = nullptr;
    std::vector<aether_style_map_t> retired_maps;
    aether_layout_engine_t engine = nullptr;
    aether_layout_result_t layout = nullptr;
    std::vector<std::pair<aether_node_t,bool>> visibility;
    Fixture(const char* html, const std::string& css) {
        ok(aether_doc_create(&doc)); ok(aether_doc_parse_html(doc, html));
        aether_style_runtime_options_t options{};
        aether_style_environment_t env{400,300,1,16,0,0,0};
        ok(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));
        const std::string sheet = "html,body{display:block;margin:0}body{width:400px;font-size:16px;line-height:20px}div{display:block}span{display:inline}table{display:table}tr{display:table-row}td{display:table-cell} " + css;
        int64_t handle = 0;
        ok(aether_style_runtime_add_stylesheet(runtime,sheet.c_str(),2,"visibility.css",&handle,nullptr));
        resolve(); fresh_map(); ok(aether_layout_create(measure,nullptr,&engine)); relayout();
    }
    ~Fixture() {
        aether_layout_result_destroy(layout); aether_layout_destroy(engine);
        aether_style_map_destroy(map);
        for(auto old:retired_maps) aether_style_map_destroy(old);
        aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
    }
    aether_node_t node(const char* id) { return aether_doc_get_element_by_id(doc,id); }
    aether_layout_box_t box(const char* id) {
        auto n = node(id); auto b = aether_layout_result_find_primary_box(layout,n);
        if (b) return b;
        for (int i=0;i<aether_layout_result_get_paint_order_count(layout);++i) {
            auto candidate = aether_layout_result_get_paint_order_box(layout,i);
            if (aether_layout_box_get_element(candidate)==n) return candidate;
        }
        return nullptr;
    }
    aether_rectf rect(const char* id) { return aether_layout_box_get_border_box(box(id)); }
    void fresh_map() {
        // A retained layout can still borrow its original map. Keep every map
        // alive until layout destruction, including during map replacement.
        if(map) retired_maps.push_back(map);
        map=nullptr;
        ok(aether_style_runtime_create_resolved_style_map(runtime,doc,&map));
    }
    std::vector<Update> resolve(aether_node_t changed = nullptr) {
        aether_style_update_t* updates=nullptr;
        auto root=aether_doc_get_document_element(doc);
        int count = changed ? aether_style_runtime_resolve_state_changes(runtime,root,&changed,1,&updates)
                            : aether_style_runtime_resolve_subtree(runtime,root,&updates);
        if(count<0) std::exit(2);
        std::vector<Update> result; std::vector<aether_node_t> nodes;
        for(int i=0;i<count;++i) {
            Update u{}; aether_computed_style_handle_t h=nullptr;
            ok(aether_style_runtime_get_update(updates[i],&u.node,&h,&u.impact));
            aether_computed_style_t s{}; ok(aether_computed_style_copy(h,&s));
            bool found=false; for(auto& v:visibility) if(v.first==u.node) { v.second=bool(s.visible); found=true; }
            if(!found) visibility.emplace_back(u.node,bool(s.visible));
            aether_computed_style_free(&s); aether_computed_style_handle_destroy(h);
            result.push_back(u); nodes.push_back(u.node);
        }
        if(map) ok(aether_style_runtime_update_resolved_style_map(runtime,map,nodes.data(),int(nodes.size())));
        aether_style_updates_free(updates,count); return result;
    }
    void relayout() {
        aether_layout_result_destroy(layout); layout=nullptr;
        ok(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&layout));
    }
    bool visible(const char* id) {
        for(auto v:visibility) if(v.first==node(id)) return v.second;
        check(false,"node has computed update"); return true;
    }
    Paint paint(aether_node_t focus=nullptr) {
        aether_display_list_builder_t builder=nullptr; aether_display_list_t list=nullptr;
        ok(aether_display_list_builder_create(measure,nullptr,&builder));
        ok(aether_display_list_build_with_styles(builder,layout,map,focus,&list));
        Paint p; std::vector<int> stack; int transforms=0, clips=0, scrolls=0;
        for(int i=0;i<aether_display_list_get_count(list);++i) {
            aether_paint_command_t c{}; ok(aether_display_list_get_command(list,i,&c)); ++p.tags[c.tag];
            int opening=-1, closing=-1;
            switch(c.tag) {
                case AETHER_PAINT_PUSH_TRANSFORM: opening=c.tag; ++transforms; break;
                case AETHER_PAINT_PUSH_CLIP: opening=c.tag; ++clips; break;
                case AETHER_PAINT_PUSH_SCROLL_OFFSET: opening=c.tag; ++scrolls; break;
                case AETHER_PAINT_BEGIN_PAINT_CHUNK: opening=c.tag; break;
                case AETHER_PAINT_POP_TRANSFORM: closing=AETHER_PAINT_PUSH_TRANSFORM; --transforms; break;
                case AETHER_PAINT_POP_CLIP: closing=AETHER_PAINT_PUSH_CLIP; --clips; break;
                case AETHER_PAINT_POP_SCROLL_OFFSET: closing=AETHER_PAINT_PUSH_SCROLL_OFFSET; --scrolls; break;
                case AETHER_PAINT_END_PAINT_CHUNK: closing=AETHER_PAINT_BEGIN_PAINT_CHUNK; break;
                default: break;
            }
            if(opening>=0) stack.push_back(opening);
            if(closing>=0) { if(stack.empty() || stack.back()!=closing) p.balanced=false; else stack.pop_back(); }
            if(c.tag==AETHER_PAINT_DRAW_TEXT && c.data.draw_text.text) p.texts.emplace_back(c.data.draw_text.text);
            if(c.tag==AETHER_PAINT_FILL_ROUNDED_RECT) {
                auto color=c.data.fill_rounded_rect.color;
                if(near(color.r,1)&&near(color.g,0)&&near(color.b,0)) ++p.red;
                if(near(color.r,0)&&near(color.g,1)&&near(color.b,0)) {
                    ++p.green; p.green_alpha=color.a; p.green_scoped=transforms>0&&clips>0&&scrolls>0;
                }
            }
            aether_paint_command_free(&c);
        }
        p.balanced=p.balanced&&stack.empty(); aether_display_list_destroy(list); aether_display_list_builder_destroy(builder); return p;
    }
    aether_node_t hit(int api,float x,float y) {
        if(api==0) return aether_layout_result_hit_test(layout,x,y);
        if(api==1) return aether_layout_result_hit_test_with_scroll_offsets(layout,x,y,nullptr,nullptr);
        return aether_layout_result_hit_test_with_styles_and_scroll_offsets(layout,map,x,y,nullptr,nullptr);
    }
};
bool has(const Paint& p,const char* text) {
    for(const auto& s:p.texts) if(s==text) return true;
    return false;
}
void compare_rect(aether_rectf a,aether_rectf b) {
    check(near(a.x,b.x)&&near(a.y,b.y)&&near(a.width,b.width)&&near(a.height,b.height),"geometry equals visible reference");
}
void compare_boxes(aether_layout_box_t a,aether_layout_box_t b) {
    check(bool(a)==bool(b),"same box existence"); if(!a||!b) return;
    check(aether_layout_box_get_kind(a)==aether_layout_box_get_kind(b),"same box kind/order");
    compare_rect(aether_layout_box_get_border_box(a),aether_layout_box_get_border_box(b));
    compare_rect(aether_layout_box_get_margin_box(a),aether_layout_box_get_margin_box(b));
    compare_rect(aether_layout_box_get_content_box(a),aether_layout_box_get_content_box(b));
    auto x=aether_layout_box_get_scroll_metrics(a),y=aether_layout_box_get_scroll_metrics(b);
    check(near(x.scrollable_overflow_x,y.scrollable_overflow_x)&&near(x.scrollable_overflow_y,y.scrollable_overflow_y)&&near(x.scroll_offset_x,y.scroll_offset_x)&&near(x.scroll_offset_y,y.scroll_offset_y),"same overflow metrics");
    int n=aether_layout_box_get_child_count(a),m=aether_layout_box_get_child_count(b);
    check(n==m,"same child box count");
    for(int i=0;i<n&&i<m;++i) compare_boxes(aether_layout_box_get_child(a,i),aether_layout_box_get_child(b,i));
}
void pair(const char* label,const char* html,const char* css,const char* selector="#target") {
    context=label; Fixture visible(html,css);
    for(const char* keyword:{"hidden","collapse"}) {
        Fixture hidden(html,std::string(css)+selector+"{visibility:"+keyword+"}");
        compare_boxes(aether_layout_result_get_root(visible.layout),aether_layout_result_get_root(hidden.layout));
    }
}
void geometry() {
    pair("block auto/margins/overflow","<body><div id='target'><div>AA AA</div><div style='width:130px;height:80px'></div></div><div id='after'>END</div></body>","#target{width:90px;max-height:50px;margin:7px;padding:3px;border:2px solid red;overflow:auto}");
    pair("inline whitespace/wrap/br/atomic","<body><div>A <span id='target'>BB <br>CC <span style='visibility:visible'>DD</span><input value='X'><span style='display:inline-block;width:23px;height:13px'>E</span></span> FF</div></body>","body{width:80px}");
    pair("flex grow/shrink/gap/wrap","<body><div id='flex'><div id='target'>AAAA</div><div>BBBB</div><div>CC</div></div></body>","#flex{display:flex;width:115px;gap:7px;flex-wrap:wrap}#flex>div{flex:1 1 45px}");
    pair("flex intrinsic/direct anonymous","<body><div id='target'>AAA <span>BB</span> CC</div><div>END</div></body>","#target{display:inline-flex;gap:4px}");
    pair("table cell intrinsic","<body><table><tr><td id='target'>AAAA</td><td>BB</td></tr><tr><td>CC</td><td>D</td></tr></table></body>","table{width:140px}td{padding:3px}");
    pair("table row","<body><table><tr id='target'><td>AAAA</td><td style='visibility:visible'>BB</td></tr><tr><td>C</td></tr></table></body>","td{padding:2px}");
    pair("table row group","<body><table><tbody id='target'><tr><td>AAA</td><td style='visibility:visible'>BB</td></tr></tbody></table></body>","td{padding:2px}");
    for(const char* position:{"absolute","fixed","relative"}) {
        std::string css=std::string("#target{position:")+position+";left:13px;top:17px;width:80px;height:40px}#child{visibility:visible}";
        pair(position,"<body><div id='target'><div id='child'>AA</div></div><div>END</div></body>",css.c_str());
    }
    context="display none negative control";
    Fixture f("<body><div id='target'><div id='child'>NO</div></div><div id='after'>YES</div></body>","#target{display:none;height:40px}#child{visibility:visible}");
    check(!f.box("target")&&!f.box("child"),"display none excludes whole subtree"); check(near(f.rect("after").y,0),"display none reserves no height"); check(!has(f.paint(),"NO"),"display none descendants do not paint");
}
void inheritance() {
    context="inheritance helper"; auto p=aether_computed_style_default(); p.visible=AETHER_FALSE;
    auto c=aether_computed_style_inherit_from(&p); check(!c.visible,"public helper inherits visibility"); aether_computed_style_free(&c); aether_computed_style_free(&p);
    context="runtime keywords and pseudo";
    Fixture f("<body><div id='target'><div id='omitted'>OMIT<div id='grand'>GRAND</div></div><div id='inherit'>INHERIT</div><div id='unset'>UNSET</div><div id='initial'>INITIAL</div><div id='shown'>SHOWN<div id='hidden'>HIDDEN</div></div></div></body>","#target{visibility:hidden}#inherit{visibility:inherit}#unset{visibility:unset}#initial{visibility:initial}#shown{visibility:visible}#hidden{visibility:hidden}#target::before{content:'PSEUDO-HIDDEN'}#target::after{content:'PSEUDO-SHOWN';visibility:visible}");
    for(const char* id:{"target","omitted","grand","inherit","unset","hidden"}) check(!f.visible(id),"computed hidden inheritance/keyword");
    check(f.visible("shown")&&f.visible("initial"),"visible and initial override hidden");
    auto paint=f.paint(); check(has(paint,"SHOWN")&&has(paint,"INITIAL")&&has(paint,"PSEUDO-SHOWN"),"explicit visible descendants and pseudo paint");
    for(const char* text:{"OMIT","GRAND","INHERIT","UNSET","HIDDEN","PSEUDO-HIDDEN"}) check(!has(paint,text),"inherited hidden text suppressed");
}
void painting() {
    context="hidden structural scopes";
    Fixture f("<body><div id='target'><div id='shown'>YES</div><div>NO</div></div></body>","#target{visibility:hidden;width:80px;height:35px;overflow:scroll;transform:translate(4px,3px);opacity:.5;background:red;border:2px solid red;box-shadow:1px 1px 2px red;backdrop-filter:blur(2px)}#shown{visibility:visible;width:120px;height:90px;background:rgb(0,255,0)}");
    auto p=f.paint(); check(p.balanced,"all nested scope brackets match"); check(p.red==0,"hidden background suppressed"); check(p.green==1&&near(p.green_alpha,.5f),"visible child inherits paint opacity"); check(p.green_scoped,"visible child remains within transform clip scroll"); check(has(p,"YES")&&!has(p,"NO"),"only visible text emitted");
    for(auto tag:{AETHER_PAINT_DRAW_BOX_SHADOW,AETHER_PAINT_FILL_BORDER_SIDE,AETHER_PAINT_APPLY_BACKDROP_FILTER,AETHER_PAINT_DRAW_SCROLLBAR_CHROME}) check(p.tags[tag]==0,"hidden own decoration/chrome suppressed");
    ok(aether_element_set_attribute(f.node("shown"),"style","visibility:hidden"));f.resolve(); p=f.paint();check(p.balanced&&p.green==0,"empty hidden scopes remain balanced");check(p.tags[AETHER_PAINT_BEGIN_PAINT_CHUNK]>0,"hidden keeps stable paint chunks");
    context="hidden control focus/caret";
    Fixture control("<body><input id='target' value='SECRET'></body>","#target{visibility:hidden;width:90px;height:30px;background:red;border:2px solid red}");
    p=control.paint(control.node("target"));check(p.balanced&&!has(p,"SECRET"),"hidden control value absent even when focused");
    for(auto tag:{AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_FILL_ROUNDED_RECT,AETHER_PAINT_STROKE_ROUNDED_RECT,AETHER_PAINT_FILL_BORDER_SIDE}) check(p.tags[tag]==0,"hidden control paint focus and caret absent");
    context="hidden image gradient text shadow";
    Fixture decorated("<body><div id='target'>SECRET</div></body>","#target{width:100px;height:40px;background-image:url('asset://image-a'),linear-gradient(red,blue);text-shadow:1px 1px 2px red}.hide{visibility:hidden}");
    auto before=decorated.paint();
    check(before.tags[AETHER_PAINT_DRAW_TEXT]>0&&before.tags[AETHER_PAINT_DRAW_TEXT_SHADOW]>0,"reference actually emits text and shadow");
    check(before.tags[AETHER_PAINT_DRAW_BACKGROUND_IMAGE]>0&&before.tags[AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT]>0,"reference actually emits image and gradient");
    ok(aether_element_set_attribute(decorated.node("target"),"class","hide"));decorated.resolve();p=decorated.paint();
    for(auto tag:{AETHER_PAINT_DRAW_TEXT,AETHER_PAINT_DRAW_TEXT_SHADOW,AETHER_PAINT_DRAW_TEXT_GRADIENT,AETHER_PAINT_DRAW_BACKGROUND_IMAGE,AETHER_PAINT_FILL_ROUNDED_RECT_GRADIENT}) check(p.tags[tag]==0,"hidden suppresses own image gradient and text effects");
    context="hidden text gradient";
    Fixture text_gradient("<body><div id='target'>GRADIENT</div></body>","#target{width:100px;height:40px;background:linear-gradient(red,blue);background-clip:text}.hide{visibility:hidden}");
    check(text_gradient.paint().tags[AETHER_PAINT_DRAW_TEXT_GRADIENT]>0,"visible reference emits a text gradient");
    ok(aether_element_set_attribute(text_gradient.node("target"),"class","hide"));text_gradient.resolve();
    check(text_gradient.paint().tags[AETHER_PAINT_DRAW_TEXT_GRADIENT]==0,"hidden suppresses a configured text gradient");
    context="canvas propagation";
    Fixture canvas("<html><body><div>NO</div></body></html>","body{visibility:hidden;background:red}");p=canvas.paint();check(p.red==1,"propagated body canvas painted exactly once");check(!has(p,"NO"),"canvas exception does not reveal body text");
}
aether_scroll_offset_t scroll_offset(aether_node_t node,void* user) {
    return {0.f,node==static_cast<aether_node_t>(user)?25.f:0.f};
}
aether_scroll_offset_t excessive_scroll(aether_node_t node,void* user) {
    return {0.f,node==static_cast<aether_node_t>(user)?10000.f:0.f};
}
void hits() {
    context="three hit APIs and clips";
    Fixture f("<body><div id='target'><div id='shown'></div><div id='inherited'></div></div></body>","#target{visibility:hidden;width:80px;height:40px;overflow:hidden}#shown{visibility:visible;width:60px;height:20px}#inherited{width:60px;height:20px}");
    for(int api=0;api<3;++api) {
        check(f.hit(api,5,5)==f.node("shown"),"visible descendant hit through hidden parent");
        check(f.hit(api,5,25)!=f.node("inherited")&&f.hit(api,70,10)!=f.node("target"),"hidden own and inherited targets rejected");
    }
    Fixture clip("<body><div id='target'><div id='shown'></div></div></body>","#target{width:50px;height:20px;overflow:hidden}#shown{width:80px;height:60px}");
    for(int api=0;api<3;++api) {check(clip.hit(api,5,5)==clip.node("shown"),"inside clip hit");check(clip.hit(api,5,35)!=clip.node("shown"),"all APIs reject outside clip");}
    Fixture pointer("<body><div id='target'><div id='shown'></div></div></body>","#target{pointer-events:none;width:80px;height:40px}#shown{pointer-events:auto;opacity:0;width:60px;height:20px}");
    for(int api=0;api<3;++api) check(pointer.hit(api,5,5)==pointer.node("shown"),"pointer override and opacity zero remain hittable");
    context="scroll through hidden parent";
    Fixture scroll("<body><div id='target'><div style='height:25px'></div><div id='shown'></div></div></body>","#target{visibility:hidden;width:80px;height:20px;overflow:scroll}#shown{visibility:visible;width:50px;height:20px}");
    auto owner=scroll.node("target");
    check(aether_layout_result_hit_test_with_scroll_offsets(scroll.layout,5,5,scroll_offset,owner)==scroll.node("shown"),"captured hit applies retained scroll offset");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(scroll.layout,scroll.map,5,5,scroll_offset,owner)==scroll.node("shown"),"current hit applies retained scroll offset");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(scroll.layout,scroll.map,5,25,scroll_offset,owner)!=scroll.node("shown"),"scrolled child still clipped");
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(scroll.layout,scroll.map,5,5,excessive_scroll,owner)==scroll.node("shown"),"excessive offset clamps to scroll range");
    aether_style_map_t empty=nullptr;
    ok(aether_css_resolve(scroll.doc,nullptr,400,300,&empty));
    check(aether_layout_result_hit_test_with_styles_and_scroll_offsets(scroll.layout,empty,5,5,scroll_offset,owner)==scroll.node("shown"),"missing current-map entry falls back to captured style");
    aether_style_map_destroy(empty);
    context="underlying sibling hit";
    Fixture overlap("<body><div id='under'></div><div id='target'></div></body>","#under,#target{position:absolute;left:0;top:0;width:80px;height:40px}#target{visibility:hidden}");
    for(int api=0;api<3;++api)check(overlap.hit(api,10,10)==overlap.node("under"),"hidden sibling does not block underlying target");

}
uint32_t impact(const std::vector<Update>& updates,aether_node_t node) { for(auto u:updates) if(u.node==node)return u.impact; return 0; }
void pure(const std::vector<Update>& updates,aether_node_t node) {
    check(impact(updates,node)==(AETHER_STYLE_IMPACT_PAINT|AETHER_STYLE_IMPACT_HITTEST|AETHER_STYLE_IMPACT_INHERITED),"pure visibility delta exactly paint/hittest/inherited");
    for(auto u:updates) check(!(u.impact&(AETHER_STYLE_IMPACT_LAYOUT|AETHER_STYLE_IMPACT_TEXT_LAYOUT)),"visibility propagation excludes layout/text layout");
}
void dynamic() {
    for(bool initially_hidden:{false,true}) {
        context=initially_hidden?"retained initially hidden":"retained initially visible";
        const std::string html=std::string("<body><div id='target' class='")+(initially_hidden?"hide":"")+"'><div id='inherited'>OWN</div><div id='shown'>YES</div></div><div id='after'>END</div></body>";
        Fixture f(html.c_str(),"#target{width:80px;height:60px}.hide{visibility:hidden}#inherited,#shown{height:20px}#shown{visibility:visible}");
        auto old_layout=f.layout; auto old_rect=f.rect("after"); auto old_box=f.box("target");
        for(int i=0;i<4;++i) {
            bool hidden=(i%2==0)?!initially_hidden:initially_hidden;
            ok(aether_element_set_attribute(f.node("target"),"class",hidden?"hide":""));auto updates=f.resolve();pure(updates,f.node("target"));
            if(i%2)f.fresh_map();
            check(f.layout==old_layout&&f.box("target")==old_box,"layout identity retained");compare_rect(f.rect("after"),old_rect);
            auto p=f.paint();check(has(p,"OWN")==!hidden&&has(p,"YES"),"current paint honors toggled inheritance/override");
            check((f.hit(2,5,5)==f.node("inherited"))==!hidden,"current map hit updates");
            for(int api=0;api<2;++api)check((f.hit(api,5,5)==f.node("inherited"))==!initially_hidden,"no-map APIs retain captured visibility");
            check((aether_layout_result_hit_test_with_styles_and_scroll_offsets(f.layout,nullptr,5,5,nullptr,nullptr)==f.node("inherited"))==!initially_hidden,"explicit null map retains snapshot");
        }
        f.relayout();compare_rect(f.rect("after"),old_rect);
    }
    context="inline removal/semantic no-op/mixed delta";
    Fixture f("<body><div id='target' style='width:80px;height:40px;visibility:hidden'>OWN</div></body>","");
    ok(aether_element_set_attribute(f.node("target"),"style","width:80px;height:40px"));pure(f.resolve(),f.node("target"));
    ok(aether_element_set_attribute(f.node("target"),"style","width:80px;height:40px;visibility:visible"));check(impact(f.resolve(),f.node("target"))==0,"equivalent declaration emits no update");
    ok(aether_element_set_attribute(f.node("target"),"style","height:40px;visibility:hidden"));check(impact(f.resolve(),f.node("target"))&AETHER_STYLE_IMPACT_LAYOUT,"mixed width removal retains layout impact");
    context="visible child override removal";
    Fixture child("<body><div style='visibility:hidden'><div id='target' style='width:80px;height:40px;visibility:visible'>OWN</div></div></body>","");
    ok(aether_element_set_attribute(child.node("target"),"style","width:80px;height:40px"));pure(child.resolve(),child.node("target"));check(!child.visible("target"),"removing visible restores inherited hidden");
    context="incremental state visibility";
    Fixture state("<body><div id='target'><div id='child'>OWN</div></div></body>","#target{width:80px;height:40px}#target:hover{visibility:hidden}");
    for(int i=0;i<4;++i) {aether_element_set_state(state.node("target"),i%2?AETHER_STATE_NONE:AETHER_STATE_HOVER);pure(state.resolve(state.node("target")),state.node("target"));check(state.visible("child")==bool(i%2),"state path propagates inheritance");}
    context="anonymous flex live owner";
    Fixture anon("<body><div id='target'>ANON</div></body>","#target{display:flex;width:100px;height:30px}.hide{visibility:hidden}");
    for(int i=0;i<4;++i) {
        bool hidden=i%2==0;ok(aether_element_set_attribute(anon.node("target"),"class",hidden?"hide":""));pure(anon.resolve(),anon.node("target"));if(i%2)anon.fresh_map();
        check(has(anon.paint(),"ANON")==!hidden,"anonymous text uses live owner visibility");
        auto n=anon.hit(2,5,5);check(hidden?n!=anon.node("target"):n==anon.node("target"),"anonymous hit is real DOM owner, never synthetic");
        check(anon.hit(0,5,5)==anon.node("target"),"anonymous captured hit resolves real owner");
    }
    context="anonymous live pointer-events only";
    Fixture pe("<body><div id='target'>ANON</div></body>","#target{display:flex;width:100px;height:30px}.none{pointer-events:none}");
    for(int i=0;i<4;++i) {
        bool disabled=i%2==0;
        ok(aether_element_set_attribute(pe.node("target"),"class",disabled?"none":""));pe.resolve();if(i%2)pe.fresh_map();
        check((pe.hit(2,5,5)==pe.node("target"))==!disabled,"anonymous live owner pointer-events toggles");
        check(has(pe.paint(),"ANON"),"pointer-events does not suppress anonymous paint");
    }
    context="retained metrics and scroll offsets";
    Fixture retained("<body><div id='target'><div id='shown'></div></div></body>","#target{width:80px;height:20px;overflow:scroll}.hide{visibility:hidden}#shown{visibility:visible;width:50px;height:80px}");
    aether_layout_result_destroy(retained.layout);retained.layout=nullptr;
    ok(aether_layout_layout(retained.engine,retained.doc,retained.map,400,300,scroll_offset,retained.node("target"),&retained.layout));
    auto metrics=aether_layout_box_get_scroll_metrics(retained.box("target"));
    // Scroll composition is host-owned; compare stored metrics without assuming
    // the layout callback is baked into the current phase-1 box snapshot.
    for(int i=0;i<4;++i) {
        ok(aether_element_set_attribute(retained.node("target"),"class",i%2?"":"hide"));pure(retained.resolve(),retained.node("target"));
        auto current=aether_layout_box_get_scroll_metrics(retained.box("target"));
        check(near(metrics.scroll_offset_y,current.scroll_offset_y)&&near(metrics.scrollable_overflow_y,current.scrollable_overflow_y),"visibility retains scroll offsets and metrics");
    }
    context="anonymous flex starts hidden and pointer override";
    Fixture initial("<body><div id='target' class='hide'>ANON<span id='shown'>YES</span></div></body>","#target{display:flex;width:130px;height:30px}.hide{visibility:hidden;pointer-events:none}#shown{visibility:visible;pointer-events:auto}");
    check(!has(initial.paint(),"ANON")&&has(initial.paint(),"YES"),"initial anonymous text inherits hidden while real child overrides");
    ok(aether_element_set_attribute(initial.node("target"),"class",""));initial.resolve();
    check(has(initial.paint(),"ANON"),"anonymous text initially hidden reveals without relayout");
    check(initial.hit(2,5,5)==initial.node("target"),"anonymous live owner restores pointer events");
    check(initial.hit(0,5,5)!=initial.node("target"),"anonymous snapshot stays hidden");

}
} // namespace
int main() {
    inheritance();geometry();painting();hits();dynamic();
    std::printf("visibility: %d assertions, %d failures\n",assertions,failures);
    return failures?1:0;
}
