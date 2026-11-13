#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/css/measure_state.h"
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/style_map_types.h"
#include "../../native/src/css/style_revision.h"
#include <aether/aether_contextual_style.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
using namespace aether::style;
static long fail_after=-1;
[[gnu::noinline]] void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p) noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t) noexcept{std::free(p);}
static unsigned checks=0;
static void checked(bool ok,int line){if(!ok){std::cerr<<"T1.9 failure line="<<line<<" check="<<checks+1<<'\n';std::abort();}++checks;}
#define ck(x) checked((x),__LINE__)
struct Snapshot {
 aether_style_revision_t revision{};aether_style_map_t map{};
 Snapshot()=default;Snapshot(const Snapshot&)=delete;
 Snapshot(Snapshot&& o):revision(o.revision),map(o.map){o.revision=nullptr;o.map=nullptr;}
 ~Snapshot(){aether_style_map_destroy(map);aether_style_revision_destroy(revision);}
 const FrozenNodeRecord* node(aether_node_t n)const{auto* r=map->current->backing()->find(n);ck(r&&r->typography());return r;}
 const TypographyEvidence& evidence(aether_node_t n)const{return node(n)->typography()->evidence;}
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};
 aether_style_environment_t env{400,300,1,16,0,0,0};
 Fixture(const std::string& body,const aether_style_runtime_options_t* options=nullptr){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,body.c_str())==AETHER_OK);ck(aether_style_runtime_create(options,&env,nullptr,nullptr,&runtime)==AETHER_OK);}
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t id(const char* id){auto* n=aether_doc_get_element_by_id(doc,id);ck(n);return n;}
 int64_t sheet(const std::string& css){int64_t handle{};int32_t rules{};ck(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"T18 fixture",&handle,&rules)==AETHER_OK);return handle;}
 Snapshot resolve(){aether_style_transaction_t tx{};Snapshot s;ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);ck(aether_style_runtime_commit_revision(runtime,tx,&s.revision)==AETHER_OK);aether_style_transaction_destroy(tx);ck(aether_style_revision_create_map(s.revision,&s.map)==AETHER_OK);return s;}
 void style(aether_node_t n,const char* css){ck(aether_element_set_attribute(n,"style",css)==AETHER_OK);}
};
static void known_new(const TypographyEvidence& e){ck(e.size_state==TypographyFieldState::known);ck(e.weight_state==TypographyFieldState::known);ck(e.line_height_state==TypographyFieldState::known);ck(!e.coverage_complete);}
int main(){
 {Fixture f("<body><div id='p' style='font-size:16px;font-weight:500;line-height:1.5'><span id='n' style='font-size:20px'>N</span><span id='a' style='font-size:20px;line-height:24px'>A</span></div></body>");auto p=f.id("p"),n=f.id("n"),a=f.id("a");auto s=f.resolve();for(auto x:{p,n,a})known_new(s.evidence(x));
  ck(s.evidence(p).line_height_used_px==24);ck(s.evidence(n).line_height_used_px==30);ck(s.evidence(a).line_height_used_px==24);
  ck(s.evidence(n).line_height_kind==TypographyLineHeightKind::number);ck(s.evidence(a).line_height_kind==TypographyLineHeightKind::absolute_css_px);
  ck(s.evidence(n).provenance[5].resolved_source=="1.5");ck(s.evidence(n).provenance[5].inherited);ck(s.evidence(n).provenance[3].accepted_source=="20px");
  f.style(p,"font-size:16px;font-weight:500;line-height:24px");auto t=f.resolve();ck(t.evidence(n).line_height_used_px==24);ck(t.evidence(n).line_height_kind==TypographyLineHeightKind::absolute_css_px);ck(s.evidence(n).line_height_used_px==30);
 }
 {Fixture f("<html style='font-size:10px'><body><div id='p' style='font-size:20px;font-weight:normal;line-height:normal'><span id='x' style='font-size:150%'>T</span></div></body></html>");auto x=f.id("x");auto s=f.resolve();known_new(s.evidence(x));ck(s.evidence(x).size_px==30);ck(s.evidence(x).size_dependencies&MEASURE_PARENT);ck(s.evidence(x).provenance[3].resolved_source=="150%");ck(s.evidence(x).line_height_kind==TypographyLineHeightKind::normal);ck(s.evidence(x).line_height_used_px==36);
  f.style(x,"font-size:2rem");auto r=f.resolve();ck(r.evidence(x).size_px==20);ck(r.evidence(x).size_dependencies&MEASURE_ROOT_FONT);
  f.style(x,"font-size:10vw");auto v=f.resolve();ck(v.evidence(x).size_px==40);ck(v.evidence(x).size_dependencies&MEASURE_VIEWPORT);
 }
 {Fixture f("<body><div id='p' style='font-size:18px;font-weight:bold;line-height:2'><span id='x'>T</span></div></body>");auto x=f.id("x");for(const char* value:{"inherit","unset","var(--missing, inherit)","var(--missing, unset)"}){std::string css="font-size:"+std::string(value)+";font-weight:"+value+";line-height:"+value;f.style(x,css.c_str());auto s=f.resolve();known_new(s.evidence(x));ck(s.evidence(x).size_px==18);ck(s.evidence(x).weight==700);ck(s.evidence(x).line_height_used_px==36);}
  f.style(x,"font-size:initial;font-weight:initial;line-height:initial");auto s=f.resolve();ck(s.evidence(x).size_state==TypographyFieldState::unknown);ck(s.evidence(x).weight_state==TypographyFieldState::unknown);ck(s.evidence(x).line_height_state==TypographyFieldState::unknown);
 }
 {Fixture f("<body><div id='x' style='--s:20px;--w:600;--h:1.5;font-size:var(--s);font-weight:var(--w);line-height:var(--h)'>T</div></body>");auto x=f.id("x");auto s=f.resolve();known_new(s.evidence(x));ck(s.evidence(x).provenance[3].accepted_source=="var(--s)");ck(s.evidence(x).provenance[3].resolved_source=="20px");ck(s.evidence(x).weight==600);ck(s.evidence(x).line_height_used_px==30);
  for(const char* css:{"font-size:var(--missing)","font-weight:450.5","font-weight:bolder","line-height:var(--missing)","--bad:var(--bad);font-size:var(--s,20px);font-weight:var(--w,600);line-height:var(--h,1.5)"}){f.style(x,css);auto t=f.resolve();ck(t.evidence(x).size_state!=TypographyFieldState::known);ck(t.evidence(x).weight_state!=TypographyFieldState::known);ck(t.evidence(x).line_height_state!=TypographyFieldState::known);}
 }
 {Fixture f("<body><div id='x' style='font-size:20px;font-weight:500;line-height:1.5'>T</div></body>");auto x=f.id("x");auto h=f.sheet("#x{font-size:24px!important;font-weight:700!important;line-height:2!important}");auto s=f.resolve();known_new(s.evidence(x));ck(s.evidence(x).size_px==24);ck(s.evidence(x).weight==700);ck(s.evidence(x).line_height_used_px==48);for(int i=3;i<6;++i)ck(s.evidence(x).provenance[i].important);
  ck(aether_style_runtime_remove_stylesheet(f.runtime,h));auto t=f.resolve();ck(t.evidence(x).size_px==20);ck(t.evidence(x).weight==500);ck(t.evidence(x).line_height_used_px==30);
  f.style(x,"font-size:20px;font-weight:medium;line-height:30px");auto same=f.resolve();ck(same.evidence(x).weight==500);ck(same.evidence(x).provenance[4].resolved_source=="medium");ck(same.evidence(x)!=t.evidence(x));
 }
 unsigned failures=0;bool completed=false;
 {Fixture f("<body><div id='x' style='font-size:16px;font-weight:400;line-height:1.5'>T</div></body>");auto x=f.id("x");auto before=f.resolve();const auto generation=aether_style_revision_generation(before.revision);f.style(x,"--s:20px;--w:700;--h:24px;font-size:var(--s);font-weight:var(--w);line-height:var(--h)");
  for(long n=0;n<3000&&!completed;++n){aether_style_transaction_t tx{};fail_after=n;auto status=aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx);fail_after=-1;
   if(status==AETHER_OK){aether_style_revision_t fresh{};ck(aether_style_runtime_commit_revision(f.runtime,tx,&fresh)==AETHER_OK);ck(aether_style_revision_generation(fresh)>generation);aether_style_revision_destroy(fresh);completed=true;}
   else{ck(status==AETHER_ERROR_OUT_OF_MEMORY);ck(!tx);++failures;aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==generation);aether_style_revision_destroy(retained);}
   aether_style_transaction_destroy(tx);ck(before.evidence(x).size_px==16);ck(before.evidence(x).weight==400);ck(before.evidence(x).line_height_kind==TypographyLineHeightKind::number);
  }
 }
 ck(completed);ck(failures>0);std::cout<<"T1.9 runtime PASS checks="<<checks<<" prepare_OOM_points="<<failures<<" coverage=PARTIAL render=CLOSED\n";
}
