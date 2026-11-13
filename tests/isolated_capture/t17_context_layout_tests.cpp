#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/rendering/native_paint_list.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include <optional>
#include <thread>
#include <cassert>
#include <functional>
#include <iostream>
#include <cstdlib>
#include <new>
using namespace aether::rendering::text;
using aether::layout::SyntheticTextTestIssuer;
static long fail_after=-1;
void* operator new(std::size_t n) {if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
static unsigned checks=0;
static void checked(bool value,int line){if(!value){std::cerr<<"Failed check "<<checks+1<<" line="<<line<<"\n";std::abort();}++checks;}
#define ck(value) checked((value),__LINE__)
static aether_text_metrics_t legacy(const char*,const aether_computed_style_t*,float,void*){return {8,16,12};}
struct Provider final:ContextualMeasurementProvider {
 mutable unsigned calls=0;
 mutable std::string requested;
 std::function<void()> callback;
 int corruption=0;
 ContextualMeasurement measure(std::shared_ptr<const NativeInlineParagraph> p,size_t begin,size_t end,float width) const override {
  ++calls;ck(begin==0);ck(end==p->text.size());ck(width==0);
  requested=p->text;if(callback)callback();
  const float advance=p->text=="AV"?13:p->text=="To"?11:p->text=="ffi"?12:float(p->text.size()*8);
  std::vector<Glyph> glyphs;
  if(p->text=="ffi")glyphs.push_back({900,0,int(advance*64),0,0,0});
  else for(size_t i=0;i<p->text.size();++i)glyphs.push_back({unsigned(p->text[i]),unsigned(i),i?0:int(advance*64),0,0,0});
  if(corruption==1)glyphs.front().cluster=1;
  if(corruption==2)glyphs.front().advance_x+=64;
  auto run=std::make_shared<const ShapedRun>(p->seal->font,p->text,std::move(glyphs),aether_text_metrics_t{advance,16,12},p);
  return {p,begin,end,{{advance,16,12},run}};
 }
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_layout_engine_s engine;
 std::shared_ptr<const aether::layout::SyntheticFixtureAuthority> authority;
 aether_layout_result_t previous{};
 aether_node_t owner{};
 float width;
 std::shared_ptr<Provider> provider=std::make_shared<Provider>();
 std::shared_ptr<const FontInstance> font=std::make_shared<const FontInstance>(std::vector<uint8_t>{1,2},"SYNTHETIC_T17_NOT_REAL_FONT",0,1024,0);
 std::shared_ptr<const SyntheticFaceSeal> seal=std::make_shared<const SyntheticFaceSeal>(font,"T17NonAdditiveFixture");
 Fixture(std::string text,float w=400,std::string override_style="",std::string suffix="",bool flex=false):width(w) {
  ck(aether_doc_create(&doc)==AETHER_OK);
  std::string html=std::string(flex?"<body style='display:flex'>":"<body>")+"<div id='target' style='display:block;font-family:SyntheticTest;font-size:16px;font-weight:400;white-space:nowrap;direction:ltr;writing-mode:horizontal-tb;"+override_style+"'>"+text+"</div>"+suffix+"</body>";
  ck(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);owner=aether_doc_get_element_by_id(doc,"target");ck(owner!=nullptr);
  // Construct genuine empty text pieces before freezing the test DOM.
  for(auto* child:owner->children)if(child->type==AETHER_NODE_COMMENT && child->text=="EMPTY_TEXT"){child->type=AETHER_NODE_TEXT;child->text.clear();}
  aether_style_environment_t env{w,300,1,16,0,0,0};
  ck(SyntheticTextTestIssuer::create_fixture_styles(doc,env,&runtime,&map,authority));
  engine.text_measurer=legacy;ck(aether_layout_layout(&engine,doc,map,w,300,nullptr,nullptr,&previous)==AETHER_OK);engine.text_measurer=nullptr;
 }
 bool install(std::shared_ptr<const std::atomic_bool> cancelled={}) {return SyntheticTextTestIssuer::install(engine,doc,map,authority,{owner},seal,provider,std::move(cancelled));}
 aether_status_t run(aether_layout_result_t* out){return aether_layout_layout(&engine,doc,map,width,300,nullptr,nullptr,out);}
 ~Fixture(){aether_layout_result_destroy(previous);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static std::vector<aether_layout_box_s*> text_boxes(aether_layout_result_t result){std::vector<aether_layout_box_s*> out;std::vector<aether_layout_box_s*> stack{result->root};while(!stack.empty()){auto* b=stack.back();stack.pop_back();if(b->kind==LAYOUT_BOX_TEXT)out.push_back(b);for(auto* c:b->children)stack.push_back(c);}return out;}
int main(){
 for(const auto& item:std::vector<std::pair<std::string,std::string>>{{"AV","AV"},{"A<!--x-->V","AV"},{"To","To"},{"T<!--x-->o","To"},{"ffi","ffi"},{"f<!--x-->f<!--y-->i","ffi"},{" A <!--x-->  V ","A V"},{"<!--empty-->A<!--empty-->V<!--empty-->","AV"},{"<!--EMPTY_TEXT-->A<!--EMPTY_TEXT-->V<!--EMPTY_TEXT-->","AV"}})for(float width:{1.f,400.f}) {
  Fixture f(item.first,width);ck(f.install());auto output=f.previous;ck(f.run(&output)==AETHER_OK);ck(output!=f.previous);ck(f.provider->calls==1);ck(f.provider->requested==item.second);
  auto boxes=text_boxes(output);ck(boxes.size()==1);auto* b=boxes.front();ck(b->element==f.owner);ck(b->text==item.second);ck(b->native_text.run!=nullptr);ck(b->native_text.run->paragraph!=nullptr);ck(b->native_text.baseline==12);ck(b->content_box.width==(item.second=="AV"?13:item.second=="To"?11:item.second=="ffi"?12:24));
  if(item.second=="ffi")ck(b->native_text.run->glyphs.size()==1);
  ck(b->native_text.run->paragraph->seal->kind==FaceSealKind::SyntheticTest);
  ck(aether_layout_result_hit_test(output,b->content_box.x+.25f,b->content_box.y+.25f)==f.owner);
  for(auto* child:f.owner->children)ck(output->find_primary_box(child)==nullptr);
  aether_layout_result_destroy(output);
 }
 // Only the issuer's closed fixture-source grammar can create authority.
 // Unrepresented properties on owners OR ancestors cannot be silently dropped.
 for(const char* css:{"font-style:italic;","letter-spacing:2px;","text-indent:3px;","vertical-align:top;",
                     "font-size:banana;","direction:sideways;","width:garbage;","opacity:2;","margin:1px 2px;",
                     "font-weight:400/*comment*/;","font-\\weight:400;","unknown:thing;","width;","width:;","width:1px;;height:2px;","font:16px SyntheticTest;","--x:1;","width:var(--x);"})
  for(bool ancestor:{false,true}) {
   Fixture good("AV");aether_doc_t doc{};ck(aether_doc_create(&doc)==AETHER_OK);
   std::string html=ancestor?std::string("<body style='")+css+"'><div>AV</div></body>":std::string("<body><div style='")+css+"'>AV</div></body>";
   ck(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);
   aether_style_runtime_t runtime{};aether_style_map_t map{};auto authority=good.authority;
   aether_style_environment_t env{400,300,1,16,0,0,0};
   ck(!SyntheticTextTestIssuer::create_fixture_styles(doc,env,&runtime,&map,authority));
   ck(runtime==nullptr);ck(map==nullptr);ck(authority==good.authority);ck(good.provider->calls==0);aether_doc_destroy(doc);
  }
 for(const char* source:{"<html><head><style>div{font-style:italic}</style></head><body><div>AV</div></body></html>","<html><head><link rel='stylesheet' href='test.css'></head><body><div>AV</div></body></html>"}) {
  aether_doc_t doc{};ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,source)==AETHER_OK);
  aether_style_runtime_t runtime{};aether_style_map_t map{};std::shared_ptr<const aether::layout::SyntheticFixtureAuthority> authority;
  aether_style_environment_t env{400,300,1,16,0,0,0};ck(!SyntheticTextTestIssuer::create_fixture_styles(doc,env,&runtime,&map,authority));ck(!runtime&&!map&&!authority);aether_doc_destroy(doc);
 }
 {Fixture f("AV");
  ck(!SyntheticTextTestIssuer::install(f.engine,f.doc,f.map,{}, {f.owner},f.seal,f.provider));
  aether_style_environment_t env{400,300,1,16,0,0,0};aether_style_runtime_t external{};aether_style_map_t map{};
  aether_style_transaction_t tx{};aether_style_revision_t revision{};
  ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&external)==AETHER_OK);
  ck(aether_style_runtime_prepare_revision(external,f.doc,&env,&tx)==AETHER_OK);
  ck(aether_style_runtime_commit_revision(external,tx,&revision)==AETHER_OK);
  ck(aether_style_revision_create_map(revision,&map)==AETHER_OK);
  ck(!SyntheticTextTestIssuer::install(f.engine,f.doc,map,f.authority,{f.owner},f.seal,f.provider));
  aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(external);
  external=nullptr;map=nullptr;std::shared_ptr<const aether::layout::SyntheticFixtureAuthority> renewed;
  ck(SyntheticTextTestIssuer::create_fixture_styles(f.doc,env,&external,&map,renewed));
  ck(!SyntheticTextTestIssuer::install(f.engine,f.doc,map,f.authority,{f.owner},f.seal,f.provider));
  ck(!SyntheticTextTestIssuer::install(f.engine,f.doc,f.map,renewed,{f.owner},f.seal,f.provider));ck(f.provider->calls==0);
  aether_style_map_destroy(map);aether_style_runtime_destroy(external);
 }
 // Issuance is transactional: a failed replacement keeps the installed
 // policy operational. The replacement provider cannot receive a partial call.
 int install_failures=0;bool install_complete=false;
 {Fixture f("AV");ck(f.install());auto replacement=std::make_shared<Provider>();
  for(long at=0;at<100;++at){bool installed=false;
   fail_after=at;
   try {installed=SyntheticTextTestIssuer::install(f.engine,f.doc,f.map,f.authority,{f.owner},f.seal,replacement);}catch(const std::bad_alloc&){}
   fail_after=-1;
   if(installed){install_complete=true;break;}
   ++install_failures;const auto old_calls=f.provider->calls;auto out=f.previous;
   ck(f.run(&out)==AETHER_OK);ck(out!=f.previous);ck(f.provider->calls==old_calls+1);ck(replacement->calls==0);
   aether_layout_result_destroy(out);
  }
 }
 ck(install_complete);ck(install_failures>0);
 // Sample factory allocation failures before and within runtime preparation.
 // Each sample gets a new document: committing a style revision can change its
 // epoch, and this test does not claim rollback of document revision counters.
 int factory_failures=0;
 for(long at:{0L,1L,2L,4L,8L,16L,32L,64L,128L,256L,512L,1024L}) {
  aether_doc_t doc{};ck(aether_doc_create(&doc)==AETHER_OK);
  ck(aether_doc_parse_html(doc,"<body><div style='font-family:SyntheticTest;white-space:nowrap'>AV</div></body>")==AETHER_OK);
  aether_style_environment_t env{400,300,1,16,0,0,0};aether_style_runtime_t runtime{};aether_style_map_t map{};
  std::shared_ptr<const aether::layout::SyntheticFixtureAuthority> authority;bool created=false;
  fail_after=at;try{created=SyntheticTextTestIssuer::create_fixture_styles(doc,env,&runtime,&map,authority);}catch(const std::bad_alloc&){}fail_after=-1;
  if(!created){++factory_failures;ck(!runtime&&!map&&!authority);}else{ck(runtime&&map&&authority);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);}
  aether_doc_destroy(doc);
 }
 ck(factory_failures>0);
 // Intrinsic max-content and final layout must share one measured paragraph.
 for(const char* content:{"A<!--x-->V","T<!--x-->o","f<!--x-->fi"}) {Fixture f(content,1,"width:max-content;","",true);
 ck(f.install());auto out=f.previous;auto status=f.run(&out);
 ck(status==AETHER_OK);
 ck(f.provider->calls==1);ck(text_boxes(out).size()==1);aether_layout_result_destroy(out);}
 for(const char* text:{"","   "," <!--empty-->  "}){Fixture f(text);ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_OK);ck(f.provider->calls==0);ck(text_boxes(out).empty());aether_layout_result_destroy(out);}
 for(const char* text:{"A\tV","A<br>V","<span>A</span>V","A\nV","A&#233;V"}){Fixture f(text);ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);ck(!f.engine.layout_in_progress);}
 for(const char* style:{"white-space:normal;","font-weight:700;","font-size:18px;","font-family:Arial;","direction:rtl;","writing-mode:vertical-rl;","text-transform:uppercase;"}){Fixture f("AV",400,style);ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 {Fixture f("AV",400,"","<div>outside</div>");ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 // Already cancelled: zero callback; cancel/throw/mutate during callback must
 // not publish partial output. Mutation uses a supported epoch-changing API.
 {Fixture f("AV");auto cancel=std::make_shared<std::atomic_bool>(true);ck(f.install(cancel));auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 for(int action=0;action<3;++action){Fixture f("A<!--x-->V");auto cancel=std::make_shared<std::atomic_bool>(false);ck(f.install(cancel));f.provider->callback=[&]{if(action==0)cancel->store(true);else if(action==1)throw std::runtime_error("provider fixture failure");else ck(aether_node_remove_child(f.owner,aether_node_get_child(f.owner,0))==AETHER_OK);};auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==1);ck(!f.engine.layout_in_progress);if(action<2){f.provider->callback={};cancel->store(false);ck(f.run(&out)==AETHER_OK);ck(out!=f.previous);aether_layout_result_destroy(out);}}
 for(int corruption:{1,2}){Fixture f("AV");f.provider->corruption=corruption;ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==1);}
 {Fixture f("AV");ck(f.install());f.provider->callback=[&]{auto inner=f.previous;ck(f.run(&inner)==AETHER_ERROR_INVALID_STATE);ck(inner==f.previous);ck(!f.install());};auto out=f.previous;ck(f.run(&out)==AETHER_OK);ck(f.provider->calls==1);aether_layout_result_destroy(out);}
 // A stale document binding and a different engine cannot reuse the policy.
 {Fixture f("AV");ck(f.install());ck(aether_node_remove_child(f.owner,aether_node_get_child(f.owner,0))==AETHER_OK);auto out=f.previous;ck(f.run(&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 {Fixture f("AV");ck(f.install());aether_layout_engine_s copied=f.engine;auto out=f.previous;ck(aether_layout_layout(&copied,f.doc,f.map,400,300,nullptr,nullptr,&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 {Fixture f("AV"),other("AV");ck(f.install());auto out=f.previous;ck(aether_layout_layout(&f.engine,other.doc,other.map,400,300,nullptr,nullptr,&out)==AETHER_ERROR_INVALID_STATE);ck(out==f.previous);ck(f.provider->calls==0);}
 // Retained paragraph/run remains valid after all DOM/provider owners die.
 std::shared_ptr<const ShapedRun> retained;
 {Fixture f("ffi");ck(f.install());aether_layout_result_t out=f.previous;ck(f.run(&out)==AETHER_OK);retained=text_boxes(out).front()->native_text.run;aether_layout_result_destroy(out);}
 ck(retained->source=="ffi");ck(retained->paragraph->seal->fixture_id=="T17NonAdditiveFixture");
 // Real synthetic-layout output crosses native paint -> snapshot -> prepare,
 // then every mutable source is destroyed before a worker releases the frame.
 namespace prep=aether::rendering::preparation;
 std::optional<prep::PreparedFrame> frame;
 std::weak_ptr<const SyntheticFaceSeal> weak_seal;
 {Fixture f("f<!--x-->fi");ck(f.install());auto out=f.previous;ck(f.run(&out)==AETHER_OK);
  aether_display_list_builder_t builder{};ck(aether_display_list_builder_create(nullptr,nullptr,&builder)==AETHER_OK);
  std::unique_ptr<aether::rendering::NativePaintList> list;
  ck(aether::rendering::build_native_opacity(builder,out,f.map,nullptr,list)==AETHER_OK);
  auto snap=prep::snapshot_native_opacity(*list);ck(std::holds_alternative<prep::Snapshot>(snap));
  auto ready=prep::prepare(std::get<prep::Snapshot>(snap));ck(std::holds_alternative<prep::PreparedFrame>(ready));
  frame.emplace(std::get<prep::PreparedFrame>(std::move(ready)));weak_seal=f.seal;
  aether_display_list_builder_destroy(builder);aether_layout_result_destroy(out);
 }
 ck(!weak_seal.expired());
 bool saw_text=false;
 std::thread release([owned=std::move(frame),&saw_text]() mutable {
  for(const auto& event:owned->events())if(const auto* text=std::get_if<prep::Text>(&event.command)){
   ck(text->native && text->native->run->paragraph);ck(text->native->run->source=="ffi");
   ck(text->native->run->paragraph->seal->kind==FaceSealKind::SyntheticTest);saw_text=true;
  }
  owned.reset();
 });release.join();frame.reset();ck(saw_text);ck(weak_seal.expired());
 int failures=0;bool succeeded=false;
 {Fixture f("A<!--x-->V");ck(f.install());for(long at=0;at<5000;++at){auto out=f.previous;fail_after=at;auto status=f.run(&out);fail_after=-1;if(status==AETHER_OK){ck(out!=f.previous);aether_layout_result_destroy(out);succeeded=true;break;}ck(status==AETHER_ERROR_OUT_OF_MEMORY);ck(out==f.previous);ck(!f.engine.layout_in_progress);++failures;}}
 ck(succeeded);ck(failures>0);
 std::cout<<"T1.7 contextual layout PASS checks="<<checks<<" OOM="<<failures<<" install_OOM="<<install_failures<<" factory_OOM_samples="<<factory_failures<<"; synthetic only; real shaping/PNG/Chromium NOT_RUN\n";
}
