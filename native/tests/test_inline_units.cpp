// Public ABI: cross-node line units retain physical fragment ownership.
#define main hanging_reference_main
#include "test_inline_hanging.cpp"
#undef main
#include <algorithm>
#include <limits>
#include <aether/aether_contextual_style.h>
static std::string mode_css(const char* mode, int width=50) {
 return "#host{white-space:"+std::string(mode)+";width:"+std::to_string(width)+"px}";
}
static aether_node_t node(Fixture& f,const char* id){return aether_doc_get_element_by_id(f.doc,id);}
static aether_rectf rect(Fixture& f,const char* id){auto b=aether_layout_result_find_primary_box(f.result,node(f,id));need(b,"named primary");return aether_layout_box_get_border_box(b);}
static void paint_matches(Fixture& f) {
 auto runs=f.runs();aether_display_list_builder_t b{};aether_display_list_t l{};
 need(aether_display_list_builder_create(Meter::measure,&f.meter,&b)==AETHER_OK,"paint builder");
 need(aether_display_list_build(b,f.result,nullptr,&l)==AETHER_OK,"paint list");
 size_t n=0;
 for(int i=0;i<aether_display_list_get_count(l);++i){aether_paint_command_t c{};need(aether_display_list_get_command(l,i,&c)==AETHER_OK,"paint command");
 if(c.tag==AETHER_PAINT_DRAW_TEXT){check(n<runs.size(),"no extra paint fragment");if(n<runs.size()){auto&r=runs[n++];auto&t=c.data.draw_text;check(r.text==t.text,"paint source order");near(t.rect.x,r.rect.x,"paint x");near(t.rect.y,r.rect.y,"paint y");near(t.rect.width,r.rect.width,"paint width");}}
 aether_paint_command_free(&c);}
 check(n==runs.size(),"all fragments painted");aether_display_list_destroy(l);aether_display_list_builder_destroy(b);
}
static void owners(Fixture& f) {
 auto b=f.box();int words=0;
 for(int i=0;i<aether_layout_box_get_child_count(b);++i){auto child=aether_layout_box_get_child(b,i);char*t=aether_layout_box_get_text(child);
 if(t&&std::strcmp(t,"abc")==0){check(aether_layout_box_get_element(child)==node(f,"left"),"left source owner");++words;}
 if(t&&std::strcmp(t,"def")==0){check(aether_layout_box_get_element(child)==node(f,"right"),"right source owner");++words;}aether_string_free(t);}
 check(words==2,"two independently owned fragments");
}

struct RejectMeasure{
 int calls=0;bool contextual=false;
 static aether_text_metrics_t measure(const char*t,const aether_computed_style_t*,float,void*p){
  auto&c=*static_cast<RejectMeasure*>(p);++c.calls;return {c.calls==2?std::numeric_limits<float>::quiet_NaN():float(std::strlen(t))*10,20,15};
 }
 static aether_text_metrics_t context(const char*t,const aether_computed_style_t*s,float,float w,void*p){return measure(t,s,w,p);}
};


struct ReentrantMeasure{
 Fixture* inner=nullptr;int calls=0;bool entered=false;
 static aether_text_metrics_t measure(const char*t,const aether_computed_style_t*,float,void*p){
  auto&c=*static_cast<ReentrantMeasure*>(p);
  if(++c.calls==2&&!c.entered){c.entered=true;c.inner->layout();near(c.inner->rect().height,40,"nested independent separator wrapping");}
  return {float(std::strlen(t))*10,20,15};
 }
 static aether_text_metrics_t context(const char*t,const aether_computed_style_t*s,float,float w,void*p){return measure(t,s,w,p);}
};

#ifndef AETHER_INLINE_UNITS_FIXTURE_ONLY
int main(){
 for(const char* mode:{"normal","pre-line","nowrap","pre"})for(bool contextual:{false,true}){
  scenario=std::string("glued ")+mode+(contextual?"/contextual":"/legacy");
  Fixture f("<span id='left' style='color:red'>abc</span><span id='right' style='color:blue'>def</span>",mode_css(mode),false,contextual);
  near(f.rect().height,20,"word overflows intact, old 40 forbidden");
  f.expect({{"abc",{0,0,30,20}},{"def",{30,0,30,20}}});owners(f);paint_matches(f);
  check(std::find(f.meter.calls.begin(),f.meter.calls.end(),"abcdef")==f.meter.calls.end(),"never concatenate callback text");
  check(std::find(f.meter.calls.begin(),f.meter.calls.end(),"abc")!=f.meter.calls.end(),"left measured");
  check(std::find(f.meter.calls.begin(),f.meter.calls.end(),"def")!=f.meter.calls.end(),"right measured");
  if(contextual)check(f.meter.contextual>0,"contextual callback exercised");
  f.layout();near(f.rect().height,20,"repeat layout");
  scenario=std::string("standalone separator ")+mode;
  Fixture s("<span>abc</span><span> </span><span>def</span>",mode_css(mode,100));
  s.expect({{"abc",{0,0,30,20}},{" ",{30,0,10,20}},{"def",{40,0,30,20}}});paint_matches(s);
  scenario=std::string("narrow separator ")+mode;
  Fixture narrow("<span>abc</span><span> </span><span>def</span>",mode_css(mode));
  near(narrow.rect().height,(std::string(mode)=="normal"||std::string(mode)=="pre-line")?40:20,"separator wrap mode");
  scenario=std::string("split CRLF ")+mode;
  Fixture cr("<span>abc\r</span><span>\ndef</span>",mode_css(mode,100));
  bool forced=std::string(mode)=="pre"||std::string(mode)=="pre-line";
  near(cr.rect().height,forced?40:20,"CRLF is one separator/break");
  auto rr=cr.runs();need(!rr.empty(),"CRLF fragments");near(rr.back().rect.y-cr.rect().y,forced?20:0,"no double CRLF line");near(rr.back().rect.x-cr.rect().x,forced?0:40,"CRLF collapse width");
  scenario=std::string("br forced ")+mode;Fixture br("<span>abc</span><br><span>def</span>",mode_css(mode,100));
  near(br.rect().height,40,"br preserved");br.expect({{"abc",{0,0,30,20}},{"def",{0,20,30,20}}});
 }
 for(const char* mode:{"normal","pre-line"}){
  scenario=std::string("prefix intact ")+mode;Fixture f("x <span>abc</span><span>def</span>",mode_css(mode,60));
  f.expect({{"x",{0,0,10,20}},{"abc",{0,20,30,20}},{"def",{30,20,30,20}}});near(f.rect().height,40,"pending word moves as one");
  scenario=std::string("column intrinsic minimum ")+mode;
  Fixture col("<span>abc</span><span>def</span>",column(false)+mode_css(mode));
  near(col.rect().height,20,"automatic minimum shares indivisible word");
  scenario=std::string("flex min content ")+mode;
  Fixture row("<span>abc</span><span>def</span>","#outer{display:flex;width:20px}#host{width:auto;flex:0 1 auto;white-space:"+std::string(mode)+"}");
  near(row.rect().width,60,"min-content word is 60 not old 30");near(row.rect().height,20,"row word remains one line");
  scenario=std::string("table min content ")+mode;
  Fixture table("<table id='table'><tr><td id='cell'><span>abc</span><span>def</span></td></tr></table>",mode_css(mode)+"table{display:table;width:20px;border-spacing:0}tr{display:table-row}td{display:table-cell;padding:0;border:0}");
  near(rect(table,"cell").width,60,"table receives joined min-content");near(rect(table,"cell").height,20,"table final height");
 }

 // Map producers must make identical unit decisions.
 for(int map_mode=0;map_mode<3;++map_mode){
  scenario="map producer "+std::to_string(map_mode);Fixture f("<span>abc</span><span>def</span>",mode_css("normal"),false,true);
  if(map_mode){aether_layout_result_destroy(f.result);f.result=nullptr;aether_style_map_destroy(f.map);f.map=nullptr;
   if(map_mode==1){aether_style_transaction_t tx{};aether_style_revision_t rev{};
    need(aether_style_runtime_prepare_revision(f.runtime,aether_doc_get_document_element(f.doc),nullptr,&tx)==AETHER_OK,"prepare revision");
    need(aether_style_runtime_commit_revision(f.runtime,tx,&rev)==AETHER_OK,"commit revision");aether_style_transaction_destroy(tx);
    need(aether_style_revision_create_map(rev,&f.map)==AETHER_OK,"revision map");aether_style_revision_destroy(rev);
   }else{need(aether_css_resolve(f.doc,nullptr,600,400,&f.map)==AETHER_OK,"empty partial map");
    std::vector<aether_node_t> nodes;nodes.push_back(aether_doc_get_document_element(f.doc));
    for(size_t i=0;i<nodes.size();++i)for(int j=0;j<aether_node_get_child_count(nodes[i]);++j)nodes.push_back(aether_node_get_child(nodes[i],j));
    nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[](aether_node_t n){return aether_node_get_type(n)!=AETHER_NODE_ELEMENT;}),nodes.end());
    need(aether_style_runtime_update_resolved_style_map(f.runtime,f.map,nodes.data(),int(nodes.size()))==AETHER_OK,"partial map update");}
   f.layout();
  }
  near(f.rect().height,20,"all map producers preserve word");paint_matches(f);
 }
 for(const char*mode:{"normal","pre-line","nowrap","pre"}){
  for(const char*body:{"<span>abc</span><!--c--><span></span><span><span>def</span></span>","<span>abc</span><span>def</span>"}){
   scenario=std::string("transparent topology ")+mode;Fixture f(body,mode_css(mode));near(f.rect().height,20,"transparent nodes never split word");
  }
  for(const char*ending:{"<br>","<br><br>","<br><br>def"}){
   scenario=std::string("forced break parity ")+mode+ending;Fixture split("<span>a</span><span>b</span>"+std::string(ending),mode_css(mode,100));
   Fixture single("ab"+std::string(ending),mode_css(mode,100));near(split.rect().height,single.rect().height,"BR consecutive/final existing contract");
  }
 }
 {scenario="source order and baseline";Fixture f("<span id='left'>abc</span><span id='right' style='font-size:38px'>def</span>",mode_css("normal"),true,true);
  near(f.rect().height,36,"baseline union of indivisible word");
  f.expect({{"abc",{0,0,30,20}},{"def",{30,16,30,20}}});owners(f);paint_matches(f);
  check(f.meter.calls==std::vector<std::string>{"abc","def"},"one source ordered measurement per fragment, no closure remeasure");
 }
 {scenario="PRE spaces retained";Fixture f(" <span>aa</span><span>  </span><span>bb</span> ",mode_css("pre"));
  f.expect({{" ",{0,0,10,20}},{"aa",{10,0,20,20}},{"  ",{30,0,20,20}},{"bb",{50,0,20,20}},{" ",{70,0,10,20}}});near(f.rect().height,20,"PRE spaces overflow without soft wrap");
 }


 {scenario="whitespace-only does not disturb block margins";
  const std::string css=mode_css("normal")+"#before,#after{display:block;height:10px}#before{margin-bottom:30px}#after{margin-top:20px}";
  Fixture plain("<div id='before'></div><div id='after'></div>",css);
  Fixture blanks("<div id='before'></div><span> </span> <span></span><div id='after'></div>",css);
  near(rect(blanks,"after").y,rect(plain,"after").y,"pending margins survive invisible whitespace");
  near(blanks.rect().height,plain.rect().height,"no phantom inline line between blocks");
 }
 for(const char*mode:{"normal","pre-line","nowrap"}){
  scenario=std::string("leading trailing collapse ")+mode;
  Fixture f("<span> </span><span>abc</span><span>def</span><span> </span>",mode_css(mode));
  near(f.rect().height,20,"edge spaces do not create line");
  f.expect({{"abc",{0,0,30,20}},{"def",{30,0,30,20}}});
 }
 for(const char*body:{"<span style='padding-left:1px'><span>abc</span><span>def</span></span>","<span>abc</span><span></span><span>de!</span>"}){
  scenario="ancestor/late exclusion";Fixture f(body,mode_css("normal"));near(f.rect().height,40,"whole IFC fallback chosen before prefix emission");
 }


 for(bool contextual:{false,true}){
  scenario="invalid callback while collecting cross-node unit";Fixture f("<span>abc</span><span>def</span>",mode_css("normal"));
  auto retained=f.result;auto before=f.runs();RejectMeasure c;aether_layout_engine_t engine{};
  need(aether_layout_create(RejectMeasure::measure,&c,&engine)==AETHER_OK,"rejecting engine");
  if(contextual)aether_layout_set_contextual_text_measurer(engine,RejectMeasure::context,&c);
  auto out=retained;auto status=aether_layout_layout(engine,f.doc,f.map,600,400,nullptr,nullptr,&out);
  check(status!=AETHER_OK,"invalid cross-node metrics abort");check(out==retained,"callback failure leaves output untouched");check(c.calls==2,"no callbacks after invalid metric");
  auto after=f.runs();check(after.size()==before.size(),"retained fragments intact");
  for(size_t i=0;i<before.size()&&i<after.size();++i){check(before[i].text==after[i].text,"retained text");near(before[i].rect.x,after[i].rect.x,"retained x");near(before[i].rect.y,after[i].rect.y,"retained y");}
  paint_matches(f);aether_layout_destroy(engine);
 }


 for(bool contextual:{false,true}){
  scenario="reentrant cross-node collection";
  Fixture inner("<span>aa</span> <span>bb</span>",mode_css("normal",30));
  Fixture outer("<span>abc</span><span>def</span>",mode_css("normal"));
  ReentrantMeasure c;c.inner=&inner;aether_layout_destroy(outer.engine);outer.engine=nullptr;
  need(aether_layout_create(ReentrantMeasure::measure,&c,&outer.engine)==AETHER_OK,"reentrant engine");
  if(contextual)aether_layout_set_contextual_text_measurer(outer.engine,ReentrantMeasure::context,&c);
  outer.layout();check(c.entered,"nested layout ran during second fragment");near(outer.rect().height,20,"outer pending word survives reentry");
  outer.expect({{"abc",{0,0,30,20}},{"def",{30,0,30,20}}});near(inner.rect().height,40,"inner separate state retained");
  paint_matches(outer);paint_matches(inner);
 }


 {scenario="fallback capitalization keeps legacy whitespace boundary";
  Fixture f("<span>ab</span> <span>cd!</span>",mode_css("normal",100)+"#host{text-transform:capitalize}");
  f.expect({{"Ab",{0,0,20,20}},{"Cd!",{20,0,30,20}}});paint_matches(f);
  Fixture within("ab <span>cd!</span>",mode_css("normal",100)+"#host{text-transform:capitalize}");
  within.expect({{"Ab",{0,0,20,20}},{" ",{20,0,10,20}},{"Cd!",{30,0,30,20}}});paint_matches(within);
 }

 // Unsupported whole sequences keep the old behavior rather than partly adopting units.
 for(const char* body:{"<span>abc</span><span style='padding-left:1px'>def</span>","<span>abc</span><span style='white-space:pre'>def</span>","<span>abc</span><span>de!</span>","<span>abc</span><span>de\xc3\xa9</span>"}){
  scenario=std::string("excluded fallback ")+body;Fixture f(body,mode_css("normal"));near(f.rect().height,std::strstr(body,"white-space:pre")?20:40,"legacy whole-sequence wrap");
 }
 scenario="pre-wrap excluded";Fixture pw("<span>abc</span><span>def</span>");near(pw.rect().height,20,"pre-wrap retains legacy split");
 std::printf("inline-units: %d checks, %d failures\n",checks,failures);return failures?1:0;
}


#endif
