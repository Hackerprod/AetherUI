#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include <aether/aether_contextual_layout.h>
#include "../src/rendering/html_ltr/b2_session_provenance.h"
#include "../src/rendering/html_ltr/session.h"
#include "../src/layout/layout_internal.h"
#include <cassert>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

// These expectations are calculated from simple block boxes, not the B2 ledger
// or the boxes being validated. Fractional oracle comparisons allow only the
// rounding of the independent arithmetic; production provenance remains exact.
struct Expected { std::string id; float x,y,width; bool text=true; };
static std::vector<Expected> wanted;
static std::map<std::string,aether_rectf> observed;
static size_t inspections=0, contexts=0, cases=0;
static bool check_controls=false;
static const aether::layout::b2::Ledger* active_ledger=nullptr;
static size_t peak=0,visits=0,translations=0;
static long long elapsed_us=0;
static void close(float a,float b){if(std::fabs(a-b)>0.0001f)std::fprintf(stderr,"case %zu geometry actual %.9g expected %.9g\n",cases,a,b);assert(std::fabs(a-b)<=0.0001f);}
namespace aether::rendering::html_ltr::testing {
void stage(Stage){}
void b2_result_inspect(B2InlineResult&){}
void b2_inspect(aether::layout::b2::Ledger& ledger){contexts=ledger.contexts().size();active_ledger=&ledger;}
void inspect(aether_layout_result_s* layout,aether::rendering::NativePaintList* paint){
 if(paint){if(active_ledger){peak=active_ledger->memory().peak();visits=active_ledger->counters().used(aether::layout::b2::Quota::token_visits);}return;}
 ++inspections;observed.clear();assert(layout->b2_translation_sink==nullptr);
 std::vector<aether_layout_box_s*> todo{layout->root};
 std::map<std::string,std::vector<aether_layout_box_s*>> text;
 bool password=false,placeholder=false;
 while(!todo.empty()){
  auto* b=todo.back();todo.pop_back();
  for(float f:{b->content_box.x,b->content_box.y,b->content_box.width,b->content_box.height})assert(std::isfinite(f));
  if(b->element){const auto it=b->element->attributes.find("id");if(it!=b->element->attributes.end()){
   if(b->kind==LAYOUT_BOX_TEXT){text[it->second].push_back(b);if(it->second=="password"){assert(b->text!="Demo1234"&&!b->text.empty());password=true;}if(it->second=="placeholder"){assert(b->text=="Nombre");placeholder=true;}}
   else {observed[it->second]=b->content_box;
    if(b->is_atomic_control){assert(b->atomic_control.presentation&&b->atomic_control.text.run);if(it->second=="password"){assert(b->control_display_text!="Demo1234"&&!b->control_display_text.empty()&&b->atomic_control.text.run->source==b->control_display_text);password=true;}if(it->second=="placeholder"){assert(b->control_shows_placeholder&&b->control_display_text=="Nombre"&&b->atomic_control.text.run->source=="Nombre");placeholder=true;}}
   }
  }}
  for(auto* c:b->children)todo.push_back(c);
 }
 for(const auto& e:wanted){assert(observed.contains(e.id));const auto b=observed.at(e.id);close(b.x,e.x);close(b.y,e.y);close(b.width,e.width);if(e.text){assert(text[e.id].size()==1);const auto* t=text[e.id].front();close(t->content_box.x,e.x);close(t->content_box.y,e.y);assert(t->content_box.height==24.f);assert(t->native_text.run);}}
 if(check_controls)assert(password&&placeholder);
 // Snapshot copying is independent of session ownership. Destroy it before
 // returning; the public session subsequently renders and destroys normally.
 aether_layout_result_t copy=nullptr;assert(aether_layout_clone_snapshot(layout,&copy)==AETHER_OK&&copy);
 assert(copy!=layout&&copy->root!=layout->root&&copy->hit_order_cache==nullptr&&copy->b2_translation_sink==nullptr);
 close(copy->root->content_box.height,layout->root->content_box.height);
 aether_layout_result_destroy(copy);
}
}
static std::vector<uint8_t> read(const char* p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_options_v1 options;
static std::string doc(const std::string& body,const std::string& css="",const std::string& mode="normal"){
 return "<!doctype html><html><head><style>body{white-space:"+mode+"}"+css+"</style></head><body>"+body+"</body></html>";
}
static void run(const std::string& html,float width=480){
 ++cases;active_ledger=nullptr;const auto before=inspections;aether_b0_source_t s=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 const auto rc=aether_b0_source_create(width,640,html.data(),html.size(),&options,&s,&d);
 if(rc!=AETHER_OK)std::fprintf(stderr,"case %zu status=%d stage=%u reason=%u\n%s\n",cases,rc,d.stage,d.reason,html.c_str());
 assert(rc==AETHER_OK&&s&&inspections==before+1);
 assert(aether_b0_source_render(s,&d)==AETHER_OK);
 aether_b0_source_frame_info_v1 f{};f.struct_size=sizeof f;f.version=1;assert(aether_b0_source_copy_frame(s,&f,nullptr,0)==AETHER_OK);
 std::vector<uint8_t> pixels(f.byte_count);assert(aether_b0_source_copy_frame(s,&f,pixels.data(),pixels.size())==AETHER_OK);
 assert(aether_b0_source_destroy(s)==AETHER_OK);
}
static int family(size_t count,bool report=false){
 std::string body;for(size_t i=0;i<count;++i)body+="<p>same</p>";
 const auto html=doc(body,"p{margin-top:.1px;margin-bottom:16px}");
 aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 active_ledger=nullptr;peak=visits=translations=0;
 aether::rendering::html_ltr::testing::translation_applied=[](aether::rendering::html_ltr::B2TranslationSink*,const aether::rendering::html_ltr::B2Translation&){++translations;};
 const auto start=std::chrono::steady_clock::now();
 const auto status=aether_b0_source_create(480,640,html.data(),html.size(),&options,&session,&d);
 elapsed_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
 aether::rendering::html_ltr::testing::translation_applied=nullptr;
 if(status==AETHER_OK){assert(session);assert(aether_b0_source_render(session,&d)==AETHER_OK);assert(aether_b0_source_destroy(session)==AETHER_OK);}
 else assert(status==AETHER_ERROR_LIMIT_EXCEEDED&&!session);
 if(report)std::printf("margin-family paragraphs=%zu status=%d stage=%u peak-PMR=%zu token-visits=%zu translations=%zu create-us-with-test-clone=%lld\n",count,status,d.stage,peak,visits,translations,elapsed_us);
 return status;
}
int main(int argc,char** argv){
 assert(argc==4);auto regular=read(argv[1]),bold=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=regular.data();options.regular_font_size=regular.size();options.bold_font=bold.data();options.bold_font_size=bold.size();
 wanted={{"a",0,0,480},{"b",0,40,480}};
 run(doc("<p id='a'>A</p><p id='b'>B</p>","p{margin-bottom:16px}"));
 const std::vector<std::string> margins{"0","1","8","16","32",".25",".5",".1",".2",".3"};
 for(float viewport:{320.f,480.f})for(bool fixed:{false,true})for(const auto& bottom:margins)for(const auto& top:margins){
  const float b=std::stof(bottom),t=std::stof(top),w=fixed?180.f:viewport-12.f;
  wanted={{"a",5,0,w},{"b",5,24.f+std::max(b,t),w}};
  run(doc("<p id='a'>A</p><p id='b'>B</p>","p{margin-left:5px;margin-right:7px;width:"+std::string(fixed?"180px":"auto")+"}#a{margin-bottom:"+bottom+"px}#b{margin-top:"+top+"px}"),viewport);
 }
 for(const auto& top:margins){const float t=std::stof(top);wanted={{"a",0,1.f+t,480},{"b",0,1.f+t+24.f+t,480}};run(doc("<p id='a'>A</p><p id='b'>B</p>","body{padding-top:1px}p{margin-top:"+top+"px}"));}
 // Preserve the existing root-body collapse model explicitly: the first
 // child is shifted to the root content edge after siblings are positioned.
 wanted={{"a",0,0,480},{"b",0,56,480}};
 run(doc("<p id='a'>A</p><p id='b'>B</p>","p{margin-top:16px}"));
 // First/last-child collapse through three ancestors, with a preceding sibling
 // causing another legitimate translation of every nested text occurrence.
 wanted={{"a",0,0,480},{"b",0,40,480},{"c",0,72,480}};
 run(doc("<p id='a'>A</p><div><section><div><p id='b'>B</p></div></section></div><p id='c'>C</p>","#a{margin-bottom:16px}#b{margin-top:8px;margin-bottom:8px}"));
 // Padding/border block parent-child collapse, but preserve sibling collapse.
 wanted={{"a",0,0,480},{"b",5,53,470}};
 run(doc("<p id='a'>A</p><div><p id='b'>B</p></div>","#a{margin-bottom:16px}div{padding-top:4px;padding-left:4px;padding-right:4px;padding-bottom:4px;border-width:1px;border-style:solid}#b{margin-top:8px;margin-bottom:8px}"));
 wanted={{"a",0,0,480},{"b",0,40,480}};
 run(doc("<p id='a'>A</p><div></div> \n <p id='b'>B</p>","#a{margin-bottom:16px}div{margin-top:8px;margin-bottom:8px}"));
 wanted.clear();
 run(doc("<div>before<p style='margin-top:16px;margin-bottom:8px'>inside</p>after</div>"));assert(contexts==3);
 run(doc("<p>A</p><div style='margin-top:.1px'><section style='margin-top:.2px'><div style='margin-top:.3px'><p style='margin-top:.1px;margin-bottom:.3px'>same</p><p style='margin-top:.2px'>same</p></div></section></div>"));
 for(float width:{320.f,480.f}){
  run(doc("<p>A</p><p style='margin-top:16px;text-align:center'>same <span>same</span> <strong>same</strong> <a href='#a'>same</a><br>next</p>","p{width:180px}"),width);
  run(doc("<p>A</p><p style='margin-top:16px'>One two three four five six seven eight nine ten eleven twelve <strong>bold words</strong></p>","p{width:80px}"),width);
  run(doc("<p>A</p><p style='margin-top:8px'>Small <span style='font-size:32px;line-height:40px'>BIG</span> end</p>"),width);
  check_controls=true;
  run(doc("<p>A</p><label for='placeholder'>Nombre</label><input id='placeholder' type='text' placeholder='Nombre'><label for='password'>Clave</label><input id='password' type='password' value='Demo1234'><button type='button'>Continuar</button><p>B</p>","p,label,input,button{margin-top:8px;margin-bottom:16px}input{width:120px;padding-left:5px;padding-right:5px;border-width:1px;border-style:solid}"),width);
  check_controls=false;
 }
 // Exact original complete page: margins, radii and authentic controls.
 const auto full=read(argv[3]);
 for(float width:{320.f,480.f})run(std::string(full.begin(),full.end()),width);
 // B2-disabled unchanged path must still create/clone/render/copy/destroy.
 run(doc("<p>A</p><p>B</p>","p{margin-bottom:16px}","nowrap"));
 // Find the exact admitted boundary of this family under all current quotas,
 // not the theoretical DOM maximum. A new session follows both failures.
 assert(family(2,true)==AETHER_OK);
 size_t low=2,high=512;assert(family(high)==AETHER_ERROR_LIMIT_EXCEEDED);
 while(high-low>1){const size_t middle=(low+high)/2;if(family(middle)==AETHER_OK)low=middle;else high=middle;}
 assert(family(low,true)==AETHER_OK);assert(family(high,true)==AETHER_ERROR_LIMIT_EXCEEDED);assert(family(high)==AETHER_ERROR_LIMIT_EXCEEDED);assert(family(2)==AETHER_OK);
 std::printf("B2 margin geometry matrix PASS: %zu public sessions, independent box/text coordinates, split contexts, clones and controls\n",cases);
}
