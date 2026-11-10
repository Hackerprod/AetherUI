// Public-ABI hanging-space regression. Checks stay active under NDEBUG.
#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <initializer_list>
#if defined(__unix__)
#include <dlfcn.h>
#endif
static int checks=0, failures=0;
static std::string scenario;
static void check(bool ok,const char* what){++checks;if(!ok){++failures;std::fprintf(stderr,"FAIL [%s] %s\n",scenario.c_str(),what);}}
static void need(bool ok,const char* what){check(ok,what);if(!ok)std::exit(2);}
static void near(float a,float b,const char* what){++checks;if(!std::isfinite(a)||std::fabs(a-b)>.01f){++failures;std::fprintf(stderr,"FAIL [%s] %s: actual=%g expected=%g\n",scenario.c_str(),what,a,b);}}
struct Meter {
 bool baseline=false; unsigned contextual=0; std::vector<std::string> calls;
 static aether_text_metrics_t measure(const char*t,const aether_computed_style_t*s,float,void*p){
  auto&m=*static_cast<Meter*>(p);m.calls.emplace_back(t);
  return {float(std::strlen(t))*10,20,m.baseline && s->font_size==38?2.f:m.baseline?18.f:15.f};
 }
 static aether_text_metrics_t context(const char*t,const aether_computed_style_t*s,float,float w,void*p){
  ++static_cast<Meter*>(p)->contextual;return measure(t,s,w,p);
 }
};
struct Run {std::string text; aether_rectf rect;};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_layout_engine_t engine{};aether_layout_result_t result{};Meter meter;
 Fixture(const std::string&content,const std::string&css="",bool baseline=false,bool context=false){
  meter.baseline=baseline;
  need(aether_doc_create(&doc)==AETHER_OK,"doc");
  auto html="<html><body><div id='outer'><div id='host'>"+content+"</div></div></body></html>";
  need(aether_doc_parse_html(doc,html.c_str())==AETHER_OK,"parse");
  aether_style_environment_t env{600,400,1,16,0,0,0};
  need(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK,"runtime");
  std::string rules="html,body{margin:0;padding:0}#outer{display:block;width:50px}#host{display:block;width:50px;white-space:pre-wrap;line-height:20px}"+css;int64_t sheet{};
  need(aether_style_runtime_add_stylesheet(runtime,rules.c_str(),0,"inline-hanging",&sheet,nullptr)==AETHER_OK,"stylesheet");
  aether_style_update_t*u{};int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&u);
  need(n>=0,"resolve");aether_style_updates_free(u,n);
  need(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK,"map");
  need(aether_layout_create(Meter::measure,&meter,&engine)==AETHER_OK,"engine");
  if(context)aether_layout_set_contextual_text_measurer(engine,Meter::context,&meter);
  layout();
 }
 ~Fixture(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void layout(){aether_layout_result_destroy(result);result=nullptr;need(aether_layout_layout(engine,doc,map,600,400,nullptr,nullptr,&result)==AETHER_OK&&result,"layout");}
 aether_layout_box_t box() {auto b=aether_layout_result_find_primary_box(result,aether_doc_get_element_by_id(doc,"host"));need(b,"host box");return b;}
 aether_rectf rect(){return aether_layout_box_get_border_box(box());}
 static void collect(aether_layout_box_t b,std::vector<Run>&r){char*t=aether_layout_box_get_text(b);if(t&&*t)r.push_back({t,aether_layout_box_get_border_box(b)});aether_string_free(t);for(int i=0;i<aether_layout_box_get_child_count(b);++i)collect(aether_layout_box_get_child(b,i),r);}
 std::vector<Run> runs(){std::vector<Run> r;collect(box(),r);return r;}
 void expect(std::initializer_list<Run> wanted){auto got=runs();near(float(got.size()),float(wanted.size()),"fragment count");size_t i=0;auto h=rect();for(auto&w:wanted){if(i>=got.size())break;auto&r=got[i++];check(r.text==w.text,"fragment text/order");near(r.rect.x-h.x,w.rect.x,"fragment x");near(r.rect.y-h.y,w.rect.y,"fragment y");near(r.rect.width,w.rect.width,"physical width");near(r.rect.height,w.rect.height,"physical height");}}
};
static std::string column(bool zero){return std::string("#outer{display:flex;flex-direction:column;height:20px;align-items:stretch}#host{width:auto;height:120px;flex:0 1 auto}")+(zero?"#host{min-height:0}":"");}
int main(){
#if defined(__unix__)
 Dl_info info{};need(dladdr(reinterpret_cast<void*>(&aether_layout_layout),&info)!=0,"DSO identity");std::printf("DSO=%s\n",info.dli_fname);
 if(const char*e=std::getenv("AETHER_EXPECT_DSO")){char*a=realpath(info.dli_fname,nullptr),*b=realpath(e,nullptr);need(a&&b&&!std::strcmp(a,b),"exact DSO");std::free(a);std::free(b);}
#endif
 for(int mode=0;mode<3;++mode){scenario="40/20/40 mode "+std::to_string(mode);Fixture f("aa    bb",mode?column(mode==1):"");near(f.rect().height,mode==1?20:40,"host height");f.expect({{"aa",{0,0,20,20}},{"    ",{20,0,40,20}},{"bb",{0,20,20,20}}});}
 for(const char*align:{"start","center","end"}){
  std::string a=align;float x=a=="center"?15:a=="end"?30:0;
  scenario="soft wrap alignment "+a;Fixture f("aa    bb","#host{text-align:"+a+"}");near(f.rect().height,40,"two lines");
  f.expect({{"aa",{x,0,20,20}},{"    ",{x+20,0,40,20}},{"bb",{x,20,20,20}}});}
 for(const char*suffix:{"","<br>","\n","\r\n"}){
  scenario="trailing preserved "+std::string(suffix);Fixture f("aa    "+std::string(suffix));near(f.rect().height,20,"trailing does not create phantom line");f.expect({{"aa",{0,0,20,20}},{"    ",{20,0,40,20}}});}
 {scenario="split styles preserve spaces";Fixture f("<span>aa </span><span style='color:red'>   </span><span>bb</span>");near(f.rect().height,40,"split height");f.expect({{"aa",{0,0,20,20}},{" ",{20,0,10,20}},{"   ",{30,0,30,20}},{"bb",{0,20,20,20}}});}
 {scenario="leading preserved overflow";Fixture f("      aa");near(f.rect().height,40,"leading height");f.expect({{"      ",{0,0,60,20}},{"aa",{0,20,20,20}}});}
 {scenario="mixed baseline";Fixture f("aa<span style='font-size:38px'>    </span>bb",column(false),true);near(f.rect().height,56,"baseline minimum");f.expect({{"aa",{0,0,20,20}},{"    ",{20,16,40,20}},{"bb",{0,36,20,20}}});}
 {scenario="contextual callbacks";Fixture f("aa    bb",column(false),false,true);near(f.rect().height,40,"contextual height");check(f.meter.contextual>0,"contextual called");bool spaces=false;for(auto&s:f.meter.calls)spaces|=s=="    ";check(spaces,"full space span measured");f.layout();near(f.rect().height,40,"repeat layout");}
 {scenario="text transform";Fixture f("aa    bb","#host{text-transform:uppercase}");near(f.rect().height,40,"transformed height");f.expect({{"AA",{0,0,20,20}},{"    ",{20,0,40,20}},{"BB",{0,20,20,20}}});char*t=aether_node_get_text_content(aether_node_get_child(aether_doc_get_element_by_id(f.doc,"host"),0));check(t&&!std::strcmp(t,"aa    bb"),"DOM unchanged");aether_string_free(t);}
 {scenario="hanging excluded from scroll overflow";Fixture f("aa    bb","#host{overflow:auto}");auto m=aether_layout_box_get_scroll_metrics(f.box());near(m.scrollable_overflow_x,0,"hanging spaces excluded from scroll");near(m.scrollable_overflow_y,0,"two lines fit");}

 for(const char*align:{"start","center","end"}){
  std::string a=align;float k=a=="center"?.5f:a=="end"?1.f:0.f;
  scenario="trailing conditional alignment "+a;
  Fixture f("aa    ","#host{text-align:"+a+";overflow:auto}");
  near(f.rect().height,20,"EOF height");f.expect({{"aa",{0,0,20,20}},{"    ",{20,0,40,20}}});
  near(aether_layout_box_get_scroll_metrics(f.box()).scrollable_overflow_x,0,"EOF hanging overflow excluded");
  scenario="short trailing alignment "+a;
  Fixture g("aa  ","#host{text-align:"+a+"}");
  g.expect({{"aa",{10*k,0,20,20}},{"  ",{20+10*k,0,20,20}}});
  scenario="soft hanging physical scroll "+a;
  Fixture h("aa    bb","#host{text-align:"+a+";overflow:auto}");
  near(aether_layout_box_get_scroll_metrics(h.box()).scrollable_overflow_x,0,"aligned hanging overflow excluded");
 }
 {scenario="full preserved advance participates in fit";Fixture f("aa  bb");near(f.rect().height,40,"not fit after subtracting suffix");f.expect({{"aa",{0,0,20,20}},{"  ",{20,0,20,20}},{"bb",{0,20,20,20}}});}
 {scenario="nowrap unit across runs";Fixture f("aa  <span style='white-space:nowrap'>b<span>b</span> cc</span>");near(f.rect().height,40,"one indivisible nowrap second line");f.expect({{"aa",{0,0,20,20}},{"  ",{20,0,20,20}},{"b",{0,20,10,20}},{"b",{10,20,10,20}},{" ",{20,20,10,20}},{"cc",{30,20,20,20}}});}
 {scenario="explicit break then content";Fixture f("aa    <br>bb");near(f.rect().height,40,"br two lines");f.expect({{"aa",{0,0,20,20}},{"    ",{20,0,40,20}},{"bb",{0,20,20,20}}});}
 {scenario="transparent empty span";Fixture f("aa <span></span>   bb");near(f.rect().height,40,"empty span no barrier");f.expect({{"aa",{0,0,20,20}},{" ",{20,0,10,20}},{"   ",{30,0,30,20}},{"bb",{0,20,20,20}}});}
 std::printf("inline-hanging: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
