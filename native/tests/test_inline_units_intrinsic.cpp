#include "../src/layout/layout_internal.h"
#define AETHER_INLINE_UNITS_FIXTURE_ONLY
#include "test_inline_units.cpp"
#undef AETHER_INLINE_UNITS_FIXTURE_ONLY
static float intrinsic(Fixture& f,bool height,aether_intrinsic_size_mode_e mode,float width){
 aether_layout_result_s provisional;provisional.build_overlay=std::make_unique<aether::layout::MutableLayoutOverlay>();
 const aether::layout::StyleLookup lookup(f.map->current,provisional.build_overlay.get(),f.result->assets->default_style);
 auto n=node(f,"host");const BoxStyleSnapshot style(lookup.lookup_node_owner(n));
 f.engine->measure_styles=&lookup;f.engine->measure_document=f.doc->identity;f.meter.calls.clear();
 float v=height?measure_intrinsic_height(f.engine,&lookup,&provisional,n,style,{},{},width):
 measure_intrinsic_width(f.engine,&lookup,n,style,{},{},width,&provisional,mode);
 f.engine->measure_styles=nullptr;f.engine->measure_document.reset();
 check(provisional.primary_boxes.empty(),"intrinsic creates no primary boxes");check(provisional.paint_order.empty(),"intrinsic creates no paint");
 return v;
}

struct StyledIntrinsicMeter{
 std::vector<std::string> calls;
 static aether_text_metrics_t measure(const char*t,const aether_computed_style_t*s,float,void*p){
  static_cast<StyledIntrinsicMeter*>(p)->calls.emplace_back(t);
  return {float(std::strlen(t))*(s->font_size==38?20.f:10.f),20,15};
 }
 static aether_text_metrics_t context(const char*t,const aether_computed_style_t*s,float,float w,void*p){return measure(t,s,w,p);}
};
int main(){
 using M=aether_intrinsic_size_mode_e;
 struct Case{const char*body;float normal_min,normal_max,pre_min,pre_max;};
 for(const char*mode:{"normal","pre-line","nowrap","pre"})for(auto c:std::vector<Case>{
  {"<span>aa</span><span>bb</span>",40,40,40,40},
  {"<span>aa</span> <span>bb</span>",20,50,50,50},
  {"<span>aa</span><!--comment--><span></span><span><span>bb</span></span>",40,40,40,40},
  {"  <span>aa</span><span>  </span><span>bb</span>  ",20,50,100,100},
  {"<span> </span><span> </span>",0,0,20,20},
  {"<span>aa</span><br><span>bb</span>",20,20,20,20}
 }){
  scenario=std::string("intrinsic ")+mode+"/"+c.body;Fixture f(c.body,mode_css(mode));
  const bool pre=std::string(mode)=="pre",nowrap=std::string(mode)=="nowrap";
  for(float width:{1.f,30.f,500.f}){
   near(intrinsic(f,false,M::min_content,width),pre?c.pre_min:nowrap?c.normal_max:c.normal_min,"min-content independent available width");
   near(intrinsic(f,false,M::max_content,width),pre?c.pre_max:c.normal_max,"max-content independent available width");
  }
 }
 for(const char*mode:{"normal","pre-line","nowrap","pre"}){
  scenario=std::string("intrinsic source newlines separated by markup ")+mode;
  Fixture f("<span>aa\r</span><span></span><span>\nbb</span>",mode_css(mode,100));
  bool forced=std::string(mode)=="pre"||std::string(mode)=="pre-line";
  near(intrinsic(f,false,M::min_content,1),forced?20:std::string(mode)=="nowrap"?50:20,"CRLF min");
  near(intrinsic(f,false,M::max_content,1),forced?20:50,"CRLF max");
  near(intrinsic(f,true,M::max_content,100),forced?60:20,"two source breaks separated by markup");
  Fixture joined("<span>abc</span><span>def</span>",mode_css(mode));
  near(intrinsic(joined,true,M::max_content,50),20,"intrinsic overflow word stays intact");
 }

 // CSS Text hanging spaces / WPT white-space-intrinsic-size-021:
 // preserved spaces contribute to max-content but not min-content.
 struct PreWrapCase {const char* body;float minimum,maximum;};
 for(bool contextual:{false,true})for(auto c:std::vector<PreWrapCase>{
  {"<span>aa</span><span>bb</span>",40,40},
  {"<span>aa</span><!--c--><span></span><span><span>bb</span></span>",40,40},
  {"  ",0,20},{"   ",0,30},{"  X",10,30},{"X  ",10,30},
  {"<span> </span><span></span><span><span> </span></span>",0,20},
  {"<span>aa</span><span> </span><span> </span><span>bb</span>",20,60},
  {"<span>X</span> <span> </span><span> </span>Y",10,50},
  {"  <span>aa</span><span>  </span><span>bb</span>  ",20,100},
  {"aa  <br>bb ",20,40},{"aa  \nbb ",20,40},
  {"aa  \r\nbb ",20,40},{"<span>aa  \r</span><span></span><span>\nbb </span>",20,40},
  {"aa  <br>",20,40},{"aa  \n",20,40},{"aa  ",20,40},
  {"  <br>   ",0,30},{"<br><br>",0,0}
 }){
  scenario=std::string("pre-wrap sizing/")+c.body+(contextual?"/contextual":"/legacy");
  Fixture f(c.body,mode_css("pre-wrap",100),false,contextual);
  const auto before=f.runs();const auto height=f.rect().height;
  for(float width:{1.f,30.f,500.f}){
   near(intrinsic(f,false,M::min_content,width),c.minimum,"pre-wrap min");
   near(intrinsic(f,false,M::max_content,width),c.maximum,"pre-wrap max");
  }
  const auto after=f.runs();check(before.size()==after.size(),"sizing retains fragment count");
  for(size_t i=0;i<before.size()&&i<after.size();++i){
   check(before[i].text==after[i].text,"sizing retains owner text");
   near(before[i].rect.x,after[i].rect.x,"sizing retains x");
   near(before[i].rect.y,after[i].rect.y,"sizing retains y");
   near(before[i].rect.width,after[i].rect.width,"sizing retains width");
  }
  near(f.rect().height,height,"sizing retains height");paint_matches(f);
 }
 {scenario="pre-wrap flex min";
  Fixture f("<span>abc</span><span>def</span>"," #outer{display:flex;width:20px}#host{width:auto;flex:0 1 auto;white-space:pre-wrap}");
  near(f.rect().width,60,"flex uses complete min-content word");
 }
 {scenario="pre-wrap table min";
  Fixture f("<table><tr><td id='cell'><span>abc</span><span>def</span></td></tr></table>",
   mode_css("pre-wrap")+"table{display:table;width:20px;border-spacing:0}tr{display:table-row}td{display:table-cell;padding:0;border:0}");
  near(rect(f,"cell").width,60,"table uses complete min-content word");
 }

 for(bool contextual:{false,true}){
  scenario="pre-wrap styles and source-order callbacks";
  Fixture f("<span id='left'>ab</span><span id='right' style='font-size:38px'>cd</span><span> </span><span style='font-size:38px'> </span>X",mode_css("pre-wrap",100));
  StyledIntrinsicMeter meter;aether_layout_destroy(f.engine);f.engine=nullptr;
  need(aether_layout_create(StyledIntrinsicMeter::measure,&meter,&f.engine)==AETHER_OK,"styled engine");
  if(contextual)aether_layout_set_contextual_text_measurer(f.engine,StyledIntrinsicMeter::context,&meter);
  near(intrinsic(f,false,M::min_content,1),60,"word sums different owner metrics");
  check(meter.calls==std::vector<std::string>{"ab","cd"," "," ","X"},"source slices exactly once");
  meter.calls.clear();
  near(intrinsic(f,false,M::max_content,1),100,"spaces retain distinct owner metrics");
  check(meter.calls==std::vector<std::string>{"ab","cd"," "," ","X"},"max source slices exactly once");
 }
 for(const char* body:{"<span>aa</span><span>bb!</span>",
  "<span>aa</span><span style='padding-left:1px'><span>bb</span></span>",
  "<span>aa</span><span style='white-space:normal'>bb</span>",
  "<span>aa</span><span style='position:relative'>bb</span>",
  "<span>aa</span><span>bb\xc3\xa9</span>"}){
  scenario=std::string("pre-wrap whole IFC fallback/")+body;Fixture f(body,mode_css("pre-wrap",100));
  // Old generic helper uses max fragment, not whole word, for excluded IFCs.
  const float expected=std::strstr(body,"!")?30:std::strstr(body,"\xc3\xa9")?40:20;
  near(intrinsic(f,false,M::min_content,1),expected,"unsupported suffix never partially admitted");
 }
 for(bool contextual:{false,true}){
  scenario="pre-wrap callback failure in intrinsic flex sizing";
  Fixture f("<span>abc</span><span>def</span>","#outer{display:flex;width:20px}#host{white-space:pre-wrap;width:auto;flex:0 1 auto}");
  auto retained=f.result;auto before=f.runs();RejectMeasure c;aether_layout_engine_t engine{};
  need(aether_layout_create(RejectMeasure::measure,&c,&engine)==AETHER_OK,"rejecting engine");
  if(contextual)aether_layout_set_contextual_text_measurer(engine,RejectMeasure::context,&c);
  auto out=retained;auto status=aether_layout_layout(engine,f.doc,f.map,600,400,nullptr,nullptr,&out);
  check(status!=AETHER_OK,"invalid intrinsic metrics abort");check(out==retained,"output remains retained");
  check(c.calls==2,"no callbacks after invalid metric");check(f.runs().size()==before.size(),"retained fragments");paint_matches(f);
  aether_layout_destroy(engine);
  scenario="pre-wrap intrinsic reentry";
  Fixture inner("<span>aa</span> <span>bb</span>",mode_css("normal",30));
  Fixture outer("<span>abc</span><span>def</span>",mode_css("pre-wrap"));
  ReentrantMeasure re;re.inner=&inner;aether_layout_destroy(outer.engine);outer.engine=nullptr;
  need(aether_layout_create(ReentrantMeasure::measure,&re,&outer.engine)==AETHER_OK,"reentrant engine");
  if(contextual)aether_layout_set_contextual_text_measurer(outer.engine,ReentrantMeasure::context,&re);
  near(intrinsic(outer,false,M::min_content,1),60,"outer word survives nested layout");
  check(re.entered,"nested layout during source measurement");check(re.calls==2,"outer callbacks not repeated");
 }
 {scenario="pre-wrap source newlines separated by markup";
  Fixture f("<span>aa\r</span><span></span><span>\nbb</span>",mode_css("pre-wrap",100));
  near(f.rect().height,60,"markup separates two source breaks");
  near(intrinsic(f,true,M::max_content,100),60,"height measurement not routed to sizing-only units");
  near(intrinsic(f,false,M::min_content,100),20,"two source breaks intrinsic min");
  near(intrinsic(f,false,M::max_content,100),20,"two source breaks intrinsic max");
 }
 for(int map_mode=0;map_mode<3;++map_mode){
  scenario="pre-wrap sizing map producer "+std::to_string(map_mode);Fixture f("<span>abc</span><span>def</span>",mode_css("pre-wrap"),false,true);
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
  near(intrinsic(f,false,M::min_content,1),60,"all map producers min word");
  near(intrinsic(f,false,M::max_content,1),60,"all map producers max word");paint_matches(f);
 }
 std::printf("inline-units-intrinsic: %d checks, %d failures\n",checks,failures);return failures?1:0;
}

