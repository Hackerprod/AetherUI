// Flex direction geometry in horizontal-tb/LTR. Numeric expectations are
// independent of the implementation, fonts and external browser execution.
// Checks remain active in Release builds. Wrap-reverse and signed scrolling
// deliberately are not implied by these tests.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_style.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
int failures=0, checks=0, scenarios=0;
unsigned long long measure_calls=0;
std::string label;
void need(bool v,const char* msg) { if(!v){std::fprintf(stderr,"SETUP %s: %s\n",label.c_str(),msg);std::exit(2);} }
void check(bool v,const char* msg) {++checks;if(!v){++failures;std::fprintf(stderr,"FAIL %s: %s\n",label.c_str(),msg);} }
void eq(float a,float e,const char* msg) { bool ok=std::isfinite(a)&&std::fabs(a-e)<.01f;if(!ok)std::fprintf(stderr,"  actual=%g expected=%g\n",a,e);check(ok,msg); }
aether_text_metrics_t measure(const char* s,const aether_computed_style_t*,float,void*) {++measure_calls;return {float(std::strlen(s))*5,10,8};}
const char* reset="*{margin:0;padding:0;border:0;box-sizing:border-box;}html,body,div{display:block;}body{width:600px;}";
const char* trio="<body><div id='p'><div id='a'><div id='d'></div></div><div id='b'></div><div id='c'></div></div></body>";
struct Fixture {
 aether_doc_t doc=nullptr; aether_style_runtime_t runtime=nullptr; aether_style_map_t map=nullptr; aether_layout_engine_t engine=nullptr; bool prepared;
 Fixture(const std::string& css,bool p,const char* html=trio):prepared(p){
  need(aether_doc_create(&doc)==AETHER_OK,"doc");need(aether_doc_parse_html(doc,html)==AETHER_OK,"html");
  aether_style_runtime_options_t options{};aether_style_environment_t env{640,480,1,16,0,0,0};
  need(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK,"runtime");
  add(std::string(reset)+css);need(aether_layout_create(measure,nullptr,&engine)==AETHER_OK,"engine");++scenarios;
 }
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void add(const std::string& css){int64_t id=0;need(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"reverse.css",&id,nullptr)==AETHER_OK,"stylesheet");
  aether_style_update_t* updates=nullptr;int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);need(n>=0,"resolve");aether_style_updates_free(updates,n);
  aether_style_map_t next=nullptr;if(prepared){aether_style_revision_t rev=nullptr;need(aether_style_runtime_retain_revision(runtime,&rev)==AETHER_OK,"revision");need(aether_style_revision_create_map(rev,&next)==AETHER_OK,"prepared map");aether_style_revision_destroy(rev);}else need(aether_style_runtime_create_resolved_style_map(runtime,doc,&next)==AETHER_OK,"map");aether_style_map_destroy(map);map=next;
 }
 aether_node_t node(const char* id){auto n=aether_doc_get_element_by_id(doc,id);need(n!=nullptr,"node");return n;}
 aether_layout_result_t layout(){aether_layout_result_t r=nullptr;need(aether_layout_layout(engine,doc,map,640,480,nullptr,nullptr,&r)==AETHER_OK&&r,"layout");return r;}
 aether_layout_box_t box(aether_layout_result_t r,const char* id){auto b=aether_layout_result_find_primary_box(r,node(id));need(b!=nullptr,"box");return b;}
 aether_rectf border(aether_layout_result_t r,const char* id){return aether_layout_box_get_border_box(box(r,id));}
 aether_rectf content(aether_layout_result_t r,const char* id){return aether_layout_box_get_content_box(box(r,id));}
 aether_computed_style_t style(const char* id){auto h=aether_style_map_get(map,node(id));need(h!=nullptr,"style handle");aether_computed_style_t s{};need(aether_computed_style_copy(h,&s)==AETHER_OK,"style copy");aether_computed_style_handle_destroy(h);return s;}
};
float main_origin(aether_rectf r,bool col){return col?r.y:r.x;}
float main_size(aether_rectf r,bool col){return col?r.height:r.width;}
std::string setup(bool col,const char* dir,const char* justify="flex-start",float extent=300){
 return std::string("#p{display:flex;align-items:flex-start;position:relative;margin-left:17px;margin-top:23px;padding:7px 11px;border:3px solid;box-sizing:content-box;width:")+
 (col?"60":std::to_string(extent))+"px;height:"+(col?std::to_string(extent):"60")+"px;gap:10px;flex-direction:"+dir+";justify-content:"+justify+";}#a,#b,#c{flex:none;width:20px;height:20px;}#a{"+(col?"height":"width")+":40px}#b{"+(col?"height":"width")+":60px}#c{"+(col?"height":"width")+":30px}#d{width:5px;height:5px}";
}
void order(Fixture& f,aether_layout_result_t r){
 auto p=f.box(r,"p");const char* ids[]={"a","b","c"};
 for(int i=0;i<3;++i){check(aether_node_get_child(f.node("p"),i)==f.node(ids[i]),"DOM remains source order");check(aether_layout_box_get_element(aether_layout_box_get_child(p,i))==f.node(ids[i]),"box children remain source order");}
 int previous=-1;for(auto id:ids){int found=-1,count=0;for(int i=0;i<aether_layout_result_get_paint_order_count(r);++i)if(aether_layout_result_get_paint_order_box(r,i)==f.box(r,id)){found=i;++count;}check(count==1&&found>previous,"primary paint registrations preserve source order");previous=found;}
}
void geometry(bool prepared){
 const char* justifies[]={"flex-start","flex-end","center","space-between","start","end"};
 const float reverse[6][3]={{260,190,150},{110,40,0},{185,115,75},{260,115,0},{110,40,0},{260,190,150}};
 const float forward[6][3]={{0,50,120},{150,200,270},{75,125,195},{0,125,270},{0,50,120},{150,200,270}};
 for(bool col:{false,true})for(bool rev:{false,true})for(int j=0;j<6;++j){
  const char* dir=col?(rev?"column-reverse":"column"):(rev?"row-reverse":"row");label=std::string(dir)+"/"+justifies[j]+(prepared?"/prepared":"/legacy");
  Fixture f(setup(col,dir,justifies[j]),prepared);auto r=f.layout();auto content=f.content(r,"p");
  const char* ids[]={"a","b","c"};const float sizes[]={40,60,30};for(int i=0;i<3;++i){auto b=f.border(r,ids[i]);eq(main_origin(b,col)-main_origin(content,col),(rev?reverse:forward)[j][i],"main placement from final content");eq(main_size(b,col),sizes[i],"used size unchanged");}
  auto a=f.content(r,"a"),d=f.border(r,"d");eq(d.x,a.x,"descendant moves once x");eq(d.y,a.y,"descendant moves once y");order(f,r);aether_layout_result_destroy(r);
 }
 for(bool col:{false,true})for(const char* justify:{"flex-start","flex-end","center","space-between"}){
  label=std::string("negative-space/")+(col?"column/":"row/")+justify;
  Fixture f(setup(col,col?"column-reverse":"row-reverse",justify,100),prepared);auto r=f.layout();float e[3]={60,-10,-50};float shift=std::strcmp(justify,"flex-end")==0?50:std::strcmp(justify,"center")==0?25:0;
  int i=0;for(auto id:{"a","b","c"})eq(main_origin(f.border(r,id),col)-main_origin(f.content(r,"p"),col),e[i++]+shift,"signed overflow retained");aether_layout_result_destroy(r);
 }
 for(bool col:{false,true})for(const char* justify:{"flex-start","flex-end","center","space-between"}){
  label=std::string("single-overflow/")+justify;Fixture f(setup(col,col?"column-reverse":"row-reverse",justify,100)+"#b,#c{display:none}#a{"+(col?"height":"width")+":140px}",prepared);auto r=f.layout();float e=std::strcmp(justify,"flex-end")==0?0:std::strcmp(justify,"center")==0?-20:-40;
  eq(main_origin(f.border(r,"a"),col)-main_origin(f.content(r,"p"),col),e,"one item fallback");aether_layout_result_destroy(r);
 }
}
void wrap_and_sizes(bool prepared){
 for(bool col:{false,true}){
  label=col?"column reverse wrap":"row reverse wrap";
  const char* html="<body><div id='p'><div id='a'></div><div id='b'></div><div id='c'></div><div id='e'></div></div></body>";
  Fixture f(setup(col,col?"column-reverse":"row-reverse","flex-start",100)+"#p{flex-wrap:wrap;width:100px;height:100px}#a,#b,#c{width:20px;height:20px}#a,#b,#c,#e{"+(col?"height":"width")+":40px}",prepared,html);
  auto r=f.layout();auto p=f.content(r,"p");const char* ids[]={"a","b","c","e"};for(int i=0;i<4;++i){auto b=f.border(r,ids[i]);eq(main_origin(b,col)-main_origin(p,col),i%2?10:60,"source line pair reversed geometrically");eq(col?b.x-p.x:b.y-p.y,i<2?0:55,"normal wrap line order");}aether_layout_result_destroy(r);
  // Unequal sizes force [a,b]/[c,e]. Reversing input order would form [e,c,b]/[a].
  f.add(std::string("#a{")+(col?"height":"width")+":65px}#b{"+(col?"height":"width")+":20px}#c,#e{"+(col?"height":"width")+":25px}");r=f.layout();p=f.content(r,"p");float positions[]={35,5,75,40};for(int i=0;i<4;++i){auto b=f.border(r,ids[i]);eq(main_origin(b,col)-main_origin(p,col),positions[i],"unequal source grouping");eq(col?b.x-p.x:b.y-p.y,i<2?0:55,"unequal line identity");}aether_layout_result_destroy(r);
 }
 for(const char* height:{"height:auto","height:auto;min-height:200px","height:auto;max-height:100px"}){
  label=std::string("auto column/")+height;Fixture f(setup(true,"column-reverse")+"#p{"+height+"}",prepared);auto r=f.layout();auto p=f.content(r,"p");float expected=std::strstr(height,"min")?200:std::strstr(height,"max")?100:150;eq(p.height,expected,"final auto/min/max content height");eq(f.border(r,"a").y-p.y,expected-40,"reverse anchored at final auto height");aether_layout_result_destroy(r);
 }
 label="fractional grow shrink control";
 for(bool col:{false,true}){
  std::vector<float> normal;for(bool rev:{false,true}){Fixture f(setup(col,col?(rev?"column-reverse":"column"):(rev?"row-reverse":"row"))+"#a,#b,#c{min-width:0;min-height:0;flex:1 1 0px}",prepared);auto r=f.layout();for(auto id:{"a","b","c"}){float v=main_size(f.border(r,id),col);if(!rev)normal.push_back(v);else eq(v,normal[std::strcmp(id,"a")==0?0:std::strcmp(id,"b")==0?1:2],"reverse leaves flex resolution invariant");}aether_layout_result_destroy(r);}
 }
}
void edges_and_definiteness(bool prepared){
 for(bool col:{false,true})for(bool border_box:{false,true}){
  label="fractional decoration and box sizing";
  auto css=setup(col,col?"column-reverse":"row-reverse","flex-start",300.5f)+"#p{gap:10.25px}#a{"+(col?"height":"width")+":40.25px}#b{"+(col?"height":"width")+":60.5px}#c{"+(col?"height":"width")+":30.75px}";
  if(border_box)css+=std::string("#p{box-sizing:border-box;")+(col?"height:320.5px":"width:328.5px")+"}";
  Fixture f(css,prepared);auto r=f.layout();auto p=f.content(r,"p");eq(main_size(p,col),300.5f,"content extent independent of box sizing");float expected[]={260.25f,189.5f,148.5f};int i=0;for(auto id:{"a","b","c"})eq(main_origin(f.border(r,id),col)-main_origin(p,col),expected[i++],"fractional final placement");aether_layout_result_destroy(r);
 }
 for(bool col:{false,true}){
  label="empty and zero reverse";Fixture f(setup(col,col?"column-reverse":"row-reverse","flex-start",0)+"#a,#b,#c{display:none}",prepared);auto r=f.layout();eq(float(aether_layout_box_get_child_count(f.box(r,"p"))),0,"empty source remains empty");aether_layout_result_destroy(r);
 }
 label="auto min-height preserves indefinite percentages";
 const char* html="<body><div id='p'><div id='a'><div id='d'><div id='e'></div></div></div><div id='b'></div><div id='c'></div></div></body>";
 for(bool reverse:{false,true}){
  Fixture f(setup(true,reverse?"column-reverse":"column")+"#p{height:auto;min-height:200px}#a{height:auto;min-height:0;flex:0 0 auto}#b,#c{display:none}#d{height:50%}#e{height:20px}",prepared,html);auto r=f.layout();auto p=f.content(r,"p");eq(p.height,200,"numeric min-height content");eq(f.border(r,"a").height,20,"auto flex item natural height");eq(f.border(r,"d").height,20,"percentage descendant retains natural content fallback");eq(f.border(r,"a").y-p.y,reverse?180:0,"placement uses min-height without percentage proof");aether_layout_result_destroy(r);
 }
}
void margins(bool prepared){
 for(bool col:{false,true})for(int kind=0;kind<6;++kind){
  label=std::string("physical margins/")+(col?"column/":"row/")+std::to_string(kind);
  const char* before[]={"7px","-7px","auto","7px","auto","auto"};const char* after[]={"13px","13px","13px","auto","auto","auto"};
  auto css=setup(col,col?"column-reverse":"row-reverse","flex-start",kind==5?30:300)+"#b,#c{display:none}#a{margin-"+(col?"top":"left")+":"+before[kind]+";margin-"+(col?"bottom":"right")+":"+after[kind]+";}";
  Fixture f(css,prepared);auto r=f.layout();auto p=f.content(r,"p"),a=f.border(r,"a");auto m=aether_layout_box_get_margin_box(f.box(r,"a"));
  const float expected[]={247,247,247,7,130,-10};eq(main_origin(a,col)-main_origin(p,col),expected[kind],"physical border origin");if(kind==0)eq(main_origin(m,col)-main_origin(p,col),240,"asymmetric margin origin");if(kind==1)eq(main_size(m,col),46,"negative margin outer size");aether_layout_result_destroy(r);
 }
 label="relative child offset once";Fixture f(setup(false,"row-reverse")+"#a{position:relative;left:9px;top:5px}",prepared);auto r=f.layout();auto p=f.content(r,"p");eq(f.border(r,"a").x-p.x,269,"relative x applied once");eq(f.border(r,"a").y-p.y,5,"relative y applied once");eq(f.border(r,"d").x,f.content(r,"a").x,"descendant relative translation");aether_layout_result_destroy(r);
}
void parser(bool prepared){
 const char* dirs[]={"row","column","RoW-ReVeRsE","COLUMN-REVERSE"};
 for(int i=0;i<4;++i){label=std::string("parser/")+dirs[i];Fixture f(std::string("#p{display:flex;flex-direction:")+dirs[i]+";justify-content:start}",prepared);auto s=f.style("p");eq(float(s.flex_direction),float(i),"four stable direction codes");eq(float(s.justify_content),4,"logical start separate code");aether_computed_style_free(&s);}
 for(const char* keyword:{"initial","unset","nonsense"}){label=std::string("default/")+keyword;Fixture f(std::string("body{flex-direction:column-reverse;justify-content:end}#p{display:flex;flex-direction:")+keyword+";justify-content:"+keyword+"}",prepared);auto s=f.style("p");eq(float(s.flex_direction),0,"direction initial fallback");eq(float(s.justify_content),0,"default remains flex-start");aether_computed_style_free(&s);}
 for(const char* keyword:{"inherit","initial","unset"}){
  label=std::string("CSS-wide variable fallback/")+keyword;
  Fixture f(std::string("body{flex-direction:column-reverse;justify-content:end}#p{display:flex;flex-direction:var(--missing, ")+keyword+");justify-content:var(--missing, "+keyword+")}",prepared);
  auto s=f.style("p");bool inherited=std::strcmp(keyword,"inherit")==0;eq(float(s.flex_direction),inherited?3:0,"var fallback CSS-wide direction");eq(float(s.justify_content),inherited?5:0,"var fallback CSS-wide alignment");aether_computed_style_free(&s);
 }
 for(const char* keyword:{"inherit","initial","unset"}){
  label=std::string("custom-property own CSS-wide semantics/")+keyword;
  Fixture f(std::string("body{--d:column-reverse;--j:end}#p{display:flex;--d:")+keyword+";--j:"+keyword+";flex-direction:var(--d);justify-content:var(--j)}",prepared);
  auto s=f.style("p");bool inherited=std::strcmp(keyword,"initial")!=0;eq(float(s.flex_direction),inherited?3:0,"inherited custom direction tokens");eq(float(s.justify_content),inherited?5:0,"inherited custom alignment tokens");aether_computed_style_free(&s);
 }
 {label="absent declarations";Fixture f("#p{display:flex}",prepared);auto s=f.style("p");eq(float(s.flex_direction),0,"absent direction is row");eq(float(s.justify_content),0,"absent alignment is flex-start");aether_computed_style_free(&s);}
 label="inherit and aliases";Fixture f("body{flex-direction:column-reverse;justify-content:end}#p{display:flex;flex-direction:inherit;justify-content:inherit}",prepared);auto s=f.style("p");eq(float(s.flex_direction),3,"explicit inherited reverse");eq(float(s.justify_content),5,"explicit inherited logical end");aether_computed_style_free(&s);
 f.add("#p{justify-content:flex-end;flex-direction:row-reverse;flex-direction:invalid}");s=f.style("p");eq(float(s.justify_content),2,"historic flex-end code");eq(float(s.flex_direction),2,"invalid declaration preserves valid cascade");aether_computed_style_free(&s);
}
void positioned(bool prepared){
 const char* html="<body><div id='p'><div id='a'></div><div id='b'></div><div id='c'></div><div id='abs'></div><div id='abs2'></div><div id='fixed'></div><div id='inset'></div></div></body>";
 const char* justifies[]={"flex-start","center","flex-end","start","end","space-between"};
 for(bool col:{false,true})for(bool rev:{false,true})for(int j=0;j<6;++j){label=std::string("static/")+(col?"column/":"row/")+(rev?"reverse/":"forward/")+justifies[j];Fixture f(setup(col,col?(rev?"column-reverse":"column"):(rev?"row-reverse":"row"),justifies[j])+"#abs,#abs2,#fixed,#inset{position:absolute;width:20px;height:20px}#fixed{position:fixed}#inset{left:5px;top:6px}",prepared,html);auto r=f.layout();auto p=f.content(r,"p");float offset=j==1?140:j==3?0:j==4?280:((j==2)?(rev?0:280):(rev?280:0));for(auto id:{"abs","abs2","fixed"})eq(main_origin(f.border(r,id),col)-main_origin(p,col),offset,"independent static anchor");auto padding=aether_layout_box_get_padding_box(f.box(r,"p"));eq(f.border(r,"inset").x-padding.x,5,"physical left wins static");eq(f.border(r,"inset").y-padding.y,6,"physical top wins static");aether_layout_result_destroy(r);}
 label="static auto/min-height";Fixture f(setup(true,"column-reverse")+"#p{height:auto;min-height:200px}#abs{position:absolute;width:20px;height:20px}",prepared,html);auto r=f.layout();eq(f.border(r,"abs").y-f.content(r,"p").y,180,"static anchor final min height");aether_layout_result_destroy(r);
}
void intrinsic_controls(bool prepared){
 const char* html="<body><div id='outer'><div id='p'>anon<table id='a'><tr><td><div style='width:55px;height:20px'></div></td></tr></table><input id='b' value='text'><div id='c'>words</div></div></div></body>";
 for(bool col:{false,true}){
  std::vector<aether_rectf> baseline;
  for(bool reverse:{false,true}){
   label=std::string("intrinsic table/control/anonymous/")+(col?"column/":"row/")+(reverse?"reverse":"forward");
   const char* direction=col?(reverse?"column-reverse":"column"):(reverse?"row-reverse":"row");
   Fixture f(std::string("#outer{display:inline-block}#p{display:flex;flex-direction:")+direction+";align-items:flex-start;gap:10px}#a,#b,#c{flex:none}",prepared,html);
   auto r=f.layout();int i=0;for(auto id:{"outer","p","a","b","c"}){auto rect=f.border(r,id);if(!reverse)baseline.push_back(rect);else{eq(rect.width,baseline[i].width,"intrinsic width unchanged by reverse");eq(rect.height,baseline[i].height,"intrinsic height unchanged by reverse");}++i;}aether_layout_result_destroy(r);
  }
 }
}
void anonymous_measurements(bool prepared){
 const char* html="<body><div id='p'>alpha<div id='a'>nested<div id='d'></div></div>omega<div id='b'></div><div id='c'></div></div></body>";
 for(bool col:{false,true}){
  unsigned long long calls=0;int paints=0;std::vector<aether_rectf> sizes;
  for(bool reverse:{false,true}){
   label=std::string("anonymous sizing and callbacks/")+(col?"column/":"row/")+(reverse?"reverse":"forward");
   Fixture f(setup(col,col?(reverse?"column-reverse":"column"):(reverse?"row-reverse":"row"))+"#a{display:flex;flex-direction:column;align-items:flex-start}",prepared,html);
   std::vector<aether_node_t> dom;for(int i=0;i<aether_node_get_child_count(f.node("p"));++i)dom.push_back(aether_node_get_child(f.node("p"),i));
   measure_calls=0;auto r=f.layout();auto measured=measure_calls;
   int count=aether_layout_result_get_paint_order_count(r);
   if(!reverse){calls=measured;paints=count;for(auto id:{"a","b","c","d"})sizes.push_back(f.border(r,id));}
   else {check(measured==calls&&calls>0,"direction does not add measurement callbacks");eq(float(count),float(paints),"anonymous tree registration count invariant");int i=0;for(auto id:{"a","b","c","d"}){auto b=f.border(r,id);eq(b.width,sizes[i].width,"nested/anonymous width invariant");eq(b.height,sizes[i].height,"nested/anonymous height invariant");++i;}}
   eq(float(aether_layout_box_get_child_count(f.box(r,"p"))),5,"two anonymous and three authored items");
   for(size_t i=0;i<dom.size();++i)check(aether_node_get_child(f.node("p"),int(i))==dom[i],"anonymous item synthesis preserves authored DOM");
   aether_layout_result_destroy(r);
  }
 }
}
void paint_hit_lifetime(bool prepared){
 for(bool rev:{false,true})for(int eligibility=0;eligibility<3;++eligibility){label=std::string("paint/hit/")+(rev?"reverse":"forward")+std::to_string(eligibility);
  Fixture f(setup(false,rev?"row-reverse":"row")+"#p{padding:0;border:0;margin:0;gap:0}#a,#b{width:100px;height:40px}#b{position:relative;background:blue}#a{background:red}#c{display:none}#d{display:none}"+(rev?"#b{left:100px}":"#b{left:-100px}")+(eligibility==1?"#b{pointer-events:none}":eligibility==2?"#b{visibility:hidden}":""),prepared);auto r=f.layout();auto a=f.border(r,"a"),b=f.border(r,"b");
  float x=std::max(a.x,b.x)+5,y=5;auto expected=f.node(eligibility?"a":"b");check(aether_layout_result_hit_test(r,x,y)==expected,"DOM hit source-later precedence/eligibility");aether_input_hit_part_t part=0;aether_node_t owner=nullptr;need(aether_layout_result_hit_test_input(r,nullptr,x,y,nullptr,nullptr,&part,&owner)==AETHER_OK,"input hit");check(owner==expected&&part==AETHER_INPUT_HIT_CONTENT,"input hit source-later precedence/eligibility");
  aether_display_list_builder_t builder=nullptr;aether_display_list_t list=nullptr;need(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK,"painter");need(aether_display_list_build(builder,r,nullptr,&list)==AETHER_OK,"paint list");int red=-1,blue=-1;for(int i=0;i<aether_display_list_get_count(list);++i){aether_paint_command_t command{};need(aether_display_list_get_command(list,i,&command)==AETHER_OK,"paint command");if(command.tag==AETHER_PAINT_FILL_ROUNDED_RECT){auto c=command.data.fill_rounded_rect.color;if(c.r>.9f&&c.b<.1f)red=i;if(c.b>.9f&&c.r<.1f)blue=i;}aether_paint_command_free(&command);}check(red>=0,"red item painted");check(eligibility==2?blue<0:blue>red,"source-later background paint order");aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);aether_layout_result_destroy(r);
 }
 label="retained frames normal reverse normal";Fixture f(setup(false,"row"),prepared);auto first=f.layout();auto old=f.border(first,"a");aether_layout_result_t clone=nullptr;need(aether_layout_clone_snapshot(first,&clone)==AETHER_OK,"clone");f.add("#p{flex-direction:row-reverse}");auto second=f.layout();eq(f.border(second,"a").x-f.content(second,"p").x,260,"new reverse style applied");eq(f.border(first,"a").x,old.x,"retained frame unchanged");eq(f.border(clone,"a").x,old.x,"retained clone unchanged");f.add("#p{flex-direction:row}");auto third=f.layout();eq(f.border(third,"a").x,old.x,"back to normal");aether_layout_result_destroy(first);aether_layout_result_destroy(second);aether_layout_result_destroy(third);eq(f.border(clone,"a").x,old.x,"clone outlives frames");aether_layout_result_destroy(clone);
}
}
int main(){
 static_assert(AETHER_FLEX_DIRECTION_ROW==0&&AETHER_FLEX_DIRECTION_COLUMN==1&&AETHER_FLEX_DIRECTION_ROW_REVERSE==2&&AETHER_FLEX_DIRECTION_COLUMN_REVERSE==3);
 static_assert(AETHER_JUSTIFY_START==0&&AETHER_JUSTIFY_CENTER==1&&AETHER_JUSTIFY_END==2&&AETHER_JUSTIFY_SPACE_BETWEEN==3&&AETHER_JUSTIFY_LOGICAL_START==4&&AETHER_JUSTIFY_LOGICAL_END==5);
 static_assert(AETHER_JUSTIFY_FLEX_START==AETHER_JUSTIFY_START&&AETHER_JUSTIFY_FLEX_END==AETHER_JUSTIFY_END);
#if defined(__linux__) && defined(__x86_64__)
 static_assert(sizeof(aether_computed_style_t)==1352&&alignof(aether_computed_style_t)==8);
 static_assert(offsetof(aether_computed_style_t,flex_direction)==4&&offsetof(aether_computed_style_t,justify_content)==8&&offsetof(aether_computed_style_t,flex_wrap)==60);
#endif
 label="ABI";check(aether_abi_capabilities()==127,"capabilities unchanged");check(aether_computed_style_size()==sizeof(aether_computed_style_t),"runtime struct ABI");
 for(bool prepared:{false,true}){geometry(prepared);wrap_and_sizes(prepared);edges_and_definiteness(prepared);margins(prepared);parser(prepared);positioned(prepared);intrinsic_controls(prepared);anonymous_measurements(prepared);paint_hit_lifetime(prepared);}
 std::printf("flex reverse: %d scenarios, %d checks, %d failures\n",scenarios,checks,failures);return failures?1:0;
}
