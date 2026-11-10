// Paint-only transforms must retain the newly resolved hanging-space geometry.
#define main hanging_geometry_suite
#include "test_inline_hanging.cpp"
#undef main
#include <aether/aether_contextual_style.h>
struct SavedBox{aether_layout_box_t box; aether_rectf rect;};
static void save(aether_layout_box_t b,std::vector<SavedBox>&v){v.push_back({b,aether_layout_box_get_border_box(b)});for(int i=0;i<aether_layout_box_get_child_count(b);++i)save(aether_layout_box_get_child(b,i),v);}
static void unchanged(Fixture&f,const std::vector<SavedBox>&saved){
 std::vector<SavedBox> now;save(f.box(),now);near(float(now.size()),float(saved.size()),"retained box count");
 for(size_t i=0;i<saved.size()&&i<now.size();++i){check(saved[i].box==now[i].box,"same retained box pointer");near(now[i].rect.x,saved[i].rect.x,"unchanged x");near(now[i].rect.y,saved[i].rect.y,"unchanged y");near(now[i].rect.width,saved[i].rect.width,"unchanged width");near(now[i].rect.height,saved[i].rect.height,"unchanged height");}
}
static void paint(Fixture&f,aether_display_list_builder_t builder,aether_style_map_t styles,const std::vector<SavedBox>&saved,const std::vector<float>&matrix,bool clipped){
 aether_display_list_t list{};need((styles?aether_display_list_build_with_styles(builder,f.result,styles,nullptr,&list):aether_display_list_build(builder,f.result,nullptr,&list))==AETHER_OK,"paint");
 int transforms=0,clips=0,spaces=0,words=0;bool in_transform=false;
 for(int i=0;i<aether_display_list_get_count(list);++i){aether_paint_command_t c{};need(aether_display_list_get_command(list,i,&c)==AETHER_OK,"command");
 if(c.tag==AETHER_PAINT_PUSH_TRANSFORM){++transforms;in_transform=true;auto m=c.data.push_transform.matrix;float values[]={m.m11,m.m12,m.m21,m.m22,m.dx,m.dy};for(int j=0;j<6;++j)near(values[j],matrix[j],"transform matrix");}
 if(c.tag==AETHER_PAINT_PUSH_CLIP)++clips;
 if(c.tag==AETHER_PAINT_DRAW_TEXT){check(in_transform,"text within transform");auto&t=c.data.draw_text;std::string s=t.text?t.text:"";if(s=="    "){++spaces;near(t.rect.x,20,"paint spaces x");near(t.rect.y,0,"paint spaces first line");near(t.rect.width,40,"paint complete hanging width");}if(s=="bb"){++words;near(t.rect.y,20,"paint bb second line");}}
 aether_paint_command_free(&c);}
 near(float(transforms),1,"single host transform");near(float(spaces),1,"spaces still painted");near(float(words),1,"bb still painted");if(clipped)check(clips>0,"clip retained");
 aether_display_list_destroy(list);unchanged(f,saved);
}

static void contextual_overflow_cases(){
 struct Projection {const char*css;float visible_x,visible_y,clip_x,clip_y;};
 for(auto c:std::vector<Projection>{{"translate(7px,11px)",17,11,7,11},{"scale(2,.5)",70,0,50,0},{"rotate(90deg)",10,20,0,10},{"skewX(45deg)",50,0,40,0}}){
  for(bool clip:{false,true}){
   scenario=std::string("contextual projected hanging overflow ")+c.css+(clip?" clipped":" visible");
   Fixture f("aa    bb",std::string("#outer{height:40px;overflow:auto}#host{transform-origin:0 0;transform:")+c.css+";"+(clip?"overflow:hidden;":"")+"}");
   aether_layout_result_destroy(f.result);f.result=nullptr;aether_style_map_destroy(f.map);f.map=nullptr;
   aether_style_transaction_t tx{};aether_style_revision_t rev{};
   need(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&tx)==AETHER_OK,"prepare contextual revision");
   need(aether_style_runtime_commit_revision(f.runtime,tx,&rev)==AETHER_OK,"commit contextual revision");aether_style_transaction_destroy(tx);
   need(aether_style_revision_create_map(rev,&f.map)==AETHER_OK,"contextual map");aether_style_revision_destroy(rev);f.layout();
   near(f.rect().height,40,"contextual new height");
   auto outer=aether_layout_result_find_primary_box(f.result,aether_doc_get_element_by_id(f.doc,"outer"));need(outer,"outer scroll box");
   auto metrics=aether_layout_box_get_scroll_metrics(outer);near(metrics.scrollable_overflow_x,clip?c.clip_x:c.visible_x,"projected right overflow");near(metrics.scrollable_overflow_y,clip?c.clip_y:c.visible_y,"projected bottom overflow");
  }
 }
 // Legacy input follows the documented logical border-box domain, not inverse
 // CSS transforms. Probe the hanging-only region and its clipping boundary.
 for(bool clip:{false,true}){
  scenario=clip?"hanging clipped hit ownership":"hanging logical hit ownership";
  Fixture f("aa    bb",std::string("#host{transform:translate(100px,50px);")+(clip?"overflow:hidden;":"")+"}");
  auto host=aether_doc_get_element_by_id(f.doc,"host");
  check(aether_layout_result_hit_test(f.result,5,25)==host,"second line logical owner");
  check((aether_layout_result_hit_test(f.result,55,5)==host)==!clip,"hanging physical region and clip");
  for(auto map:{aether_style_map_t(nullptr),f.map}){
   aether_input_hit_part_t part{};aether_node_t owner{};
   need(aether_layout_result_hit_test_input(f.result,map,55,5,nullptr,nullptr,&part,&owner)==AETHER_OK,"input hit");
   check((owner==host)==!clip,"input hanging owner respects physical clip");
  }
 }
}

int main(){
 contextual_overflow_cases();
 struct Case{const char*css;std::vector<float>m;};
 for(auto c:std::vector<Case>{{"translate(7px,11px)",{1,0,0,1,7,11}},{"scale(2,.5)",{2,0,0,.5,0,0}},{"rotate(90deg)",{0,1,-1,0,0,0}},{"skewX(45deg)",{1,0,1,1,0,0}}}){
  for(bool clip:{false,true}){scenario=std::string("hanging paint ")+c.css+(clip?" clip":"");
   Fixture f("aa    bb",std::string("#host{transform-origin:0 0;transform:")+c.css+";"+(clip?"overflow:hidden;":"")+"}");
   near(f.rect().height,40,"new layout height");std::vector<SavedBox> saved;save(f.box(),saved);auto result=f.result;
   aether_display_list_builder_t b{};need(aether_display_list_builder_create(Meter::measure,&f.meter,&b)==AETHER_OK,"builder");
   paint(f,b,nullptr,saved,c.m,clip);paint(f,b,f.map,saved,c.m,clip);
   int64_t sheet{};need(aether_style_runtime_add_stylesheet(f.runtime,"#host{transform:translate(100px,50px);transform-origin:50% 50%}",0,"later-paint",&sheet,nullptr)==AETHER_OK,"runtime style update");
   aether_style_update_t*u{};int n=aether_style_runtime_resolve_subtree(f.runtime,aether_doc_get_document_element(f.doc),&u);need(n>=0,"resolve later");aether_style_updates_free(u,n);
   aether_style_map_t later{};need(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&later)==AETHER_OK,"later map");
   paint(f,b,later,saved,{1,0,0,1,100,50},clip);check(f.result==result,"same result after runtime paint");
   paint(f,b,nullptr,saved,c.m,clip);
   aether_style_map_destroy(later);aether_display_list_builder_destroy(b);
  }
 }
 scenario="center transform origin from new height";Fixture f("aa    bb","#host{transform:rotate(90deg);transform-origin:50% 50%}");std::vector<SavedBox>s;save(f.box(),s);aether_display_list_builder_t b{};need(aether_display_list_builder_create(Meter::measure,&f.meter,&b)==AETHER_OK,"builder");paint(f,b,nullptr,s,{0,1,-1,0,45,-5},false);aether_display_list_builder_destroy(b);
 std::printf("inline-hanging-paint: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
