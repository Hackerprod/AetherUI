#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_root_layout.h"
#include "../src/rendering/document_shadow_plan.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include "../src/rendering/document_viewport_capture.h"
#include "../src/rendering/display_list_types.h"
#include <thread>
#include <cstdlib>
#include <new>
#include <array>
#include <cstring>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static auto shadows(aether_layout_result_s*l,aether_style_map_s*m,const aether::rendering::DocumentViewportAuthority&a,DocumentTextBudget&b,std::function<void()>c=[]{}){return std::unique_ptr<aether::rendering::DocumentShadowPlan>(new aether::rendering::DocumentShadowPlan(l,m,a,b,c));}
static auto root(aether_layout_engine_t e,aether_doc_t d,aether_style_map_t m,aether_layout_result_t*o){return DocumentRootLayoutAccess::layout(e,d,m,1280,720,o);}
static auto viewport(aether_doc_s*d,aether_layout_result_s*l,aether_style_map_s*m,aether::rendering::text::truetype::ViewportOffset o,DocumentTextBudget&b,std::function<void()> check={}){return aether::rendering::DocumentViewportAuthority::create(d,l,m,1280,720,o,b,check);}
static bool install(aether_layout_engine_s& e,aether_doc_s*d,aether_style_map_s*m,std::shared_ptr<DocumentTextBackend>b){return DocumentTextAccess::install(e,d,m,b,{});}
static auto fragment(aether_layout_engine_s*e,std::shared_ptr<const DocumentTextSource>s,aether_node_s*o,const BoxStyleSnapshot&v){return DocumentTextAccess::fragment(e,{s},o,v,s->decoded,{{s->ordinal,0,1,0,1}});}
};}
unsigned checks=0;void need(bool b,const char*m){++checks;if(!b){fprintf(stderr,"FAIL %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
template<class F>void rejects(F f,const char*m){bool b=false;try{f();}catch(const std::invalid_argument&){b=true;}need(b,m);}
struct Backend:DocumentTextBackend {std::vector<std::shared_ptr<const DocumentTextFragment>> seen; std::shared_ptr<const aether::rendering::text::FontInstance> font=std::make_shared<const aether::rendering::text::FontInstance>(std::vector<uint8_t>{1},"test-only mock",0,1024,0);aether::rendering::text::Measurement measure(const std::shared_ptr<const DocumentTextFragment>& f,DocumentTextBudget&)override{seen.push_back(f);aether_text_metrics_t m{float(f->normalized.size()*8),20,15};std::vector<aether::rendering::text::Glyph> glyphs;for(size_t i=0;i<f->normalized.size();++i)glyphs.push_back({1,uint32_t(i),512,0,0,0});auto run=std::make_shared<const aether::rendering::text::ShapedRun>(font,f->normalized,glyphs,m);return {m,run};}void validate_style(const aether::style::FrozenNodeRef&,const BoxStyleSnapshot&)override{}};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};
 aether_style_environment_t env{1280,720,1,16,0,0,0};
 Fixture(const char*html){ok(aether_doc_create(&doc),"doc");ok(aether_doc_parse_html(doc,html),"parse");ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"runtime");sheet(aether::style::web_capture_defaults_css(),0);sheet("html{font-family:Arial;color:#112233}#parent{margin:100px;width:900px}#outside{height:200px}#target{display:flex;width:420px;min-height:72px;border:2px solid black;box-sizing:border-box;align-items:flex-start}#a,#b{width:42px;height:36px;flex-shrink:0}");resolve();ok(aether_layout_create(nullptr,nullptr,&engine),"engine");}
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 void sheet(const char*s,int layer=1){int64_t h{};ok(aether_style_runtime_add_stylesheet(runtime,s,layer,"test",&h,nullptr),"css");}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&tx),"prepare");aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next),"commit");aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);revision=next;aether_style_map_destroy(map);map=nullptr;ok(aether_style_revision_create_map(revision,&map),"map");}
};


int main(){using namespace aether::rendering;
 Fixture f("<html><body><div id='one'></div><div id='two'></div></body></html>");f.sheet("#one,#two{width:64px;height:48px;box-shadow:8px 0 0 rgba(255,0,0,.5),12px 0 0 rgba(0,0,255,.5)}");f.resolve();aether_layout_result_t l{};ok(DocumentTextTestAccess::root(f.engine,f.doc,f.map,&l),"shadow layout");DocumentTextBudget budget;auto auth=DocumentTextTestAccess::viewport(f.doc,l,f.map,{},budget);auto plan=DocumentTextTestAccess::shadows(l,f.map,auth,budget);need(plan->size()==4,"all owners/layers");
 unsigned oom=0,success=0;for(long site=0;site<48;++site){DocumentTextBudget b;try{fail_after=site;auto p=DocumentTextTestAccess::shadows(l,f.map,auth,b);fail_after=-1;++success;need(p->size()==4,"OOM success complete plan");}catch(const std::bad_alloc&){fail_after=-1;++oom;}catch(...){fail_after=-1;throw;}need(plan->accepts(l,f.map),"prior plan survives failed candidate");}need(oom>0&&success>0,"OOM sampled failures and successes");printf("shadow plan OOM=%u success=%u\n",oom,success);

 unsigned checkpoints=0;{DocumentTextBudget b;auto p=DocumentTextTestAccess::shadows(l,f.map,auth,b,[&]{++checkpoints;});need(p&&p->size()==4,"discovery publishes complete plan");}/* B1: 2 frozen shadow sources + 4 layers + sort/postsort/duplicate/final = 10. BODY without shadows no longer invokes a callback; all observed checkpoints remain cancellation-tested below. */ need(checkpoints==10,"B1 frozen-source and final checkpoints included");
 struct Cancel{};
 for(unsigned stop=1;stop<=checkpoints;++stop){
  unsigned calls=0;bool caught=false;DocumentTextBudget b;
  std::unique_ptr<DocumentShadowPlan> candidate;
  try{candidate=DocumentTextTestAccess::shadows(l,f.map,auth,b,[&]{if(++calls==stop)throw Cancel{};});}
  catch(const Cancel&){caught=true;}
  need(caught&&calls==stop,"constructor exact Cancel at each checkpoint");
  need(!candidate,"cancelled constructor publishes no candidate");
  need(plan->accepts(l,f.map),"prior plan intact after constructor cancellation");
  DocumentTextBudget recovery_budget;auto recovery=DocumentTextTestAccess::shadows(l,f.map,auth,recovery_budget);
  need(recovery&&recovery->size()==4,"constructor recovery after cancellation");
 }
 const auto epochs=[&]{const auto& t=*f.doc->identity;return std::array<std::uint64_t,4>{t.structure_epoch,t.mutation_epoch,t.style_tree_epoch,t.non_state_epoch};};
 // Explicit fixture protocol: post-sort=8, final=10. Geometry mutation itself
 // does not change epochs; only the full comparison can reject this candidate.
 for(const unsigned stop:{8u,10u}){
  unsigned calls=0;bool mutated=false,caught=false;const auto before=epochs();
  const float y=l->root->border_box.y;DocumentTextBudget b;
  std::unique_ptr<DocumentShadowPlan> candidate;
  try{candidate=DocumentTextTestAccess::shadows(l,f.map,auth,b,[&]{if(++calls==stop){l->root->border_box.y=y+1;mutated=true;}});}
  catch(const std::invalid_argument&){caught=true;}
  need(mutated&&calls==stop,"requested geometry checkpoint actually ran");
  need(epochs()==before,"geometry mutation leaves epochs unchanged");
  need(caught&&!candidate,"post-sort/final mutation cannot publish plan");
  l->root->border_box.y=y;
  need(plan->accepts(l,f.map),"prior plan valid after geometry restoration");
  DocumentTextBudget recovery_budget;auto recovery=DocumentTextTestAccess::shadows(l,f.map,auth,recovery_budget);
  need(recovery&&recovery->size()==4,"constructor recovery after geometry mutation");
 }

aether_display_list_builder_t builder{};ok(aether_display_list_builder_create(nullptr,nullptr,&builder),"builder");std::unique_ptr<NativePaintList> list;ok(build_native_document_capture(builder,l,f.map,auth,list,plan.get()),"shadow build");plan->verify(*list,[]{});need(list->shadows.size()==4,"four sidecars");std::vector<NativeShadowEntry> sentinel;builder->native_shadow_plan=plan.get();builder->native_shadows=&sentinel;
 unsigned boom=0,bok=0;for(long site=0;site<64;++site){auto* prior=list.get();fail_after=site;auto status=build_native_document_capture(builder,l,f.map,auth,list,plan.get());fail_after=-1;if(status==AETHER_OK){++bok;plan->verify(*list,[]{});}else{++boom;need(list.get()==prior,"builder OOM previous output preserved");}need(builder->native_shadow_plan==plan.get()&&builder->native_shadows==&sentinel,"builder shadow fields restored");}need(boom>0&&bok>0,"builder allocation sample outcomes");builder->native_shadow_plan=nullptr;builder->native_shadows=nullptr;printf("shadow builder failures=%u success=%u\n",boom,bok);
auto saved=list->shadows;
 unsigned verifycalls=0;plan->verify(*list,[&]{++verifycalls;});
 // Four layers plus final publication comparison = five callback boundaries.
 need(verifycalls==5,"four per-layer verification checkpoints plus final");
 const auto* prior_list=list.get();const auto& prior_commands=NativeListAccess::list(*list)->commands;
 const auto prior_command_count=prior_commands.size();
 std::vector<unsigned char> prior_command_bytes(prior_command_count*sizeof(aether_paint_command_t));
 if(!prior_command_bytes.empty())std::memcpy(prior_command_bytes.data(),prior_commands.data(),prior_command_bytes.size());
 const auto integrity=[&]{
  need(list.get()==prior_list,"verification preserves list ownership");
  const auto& current=NativeListAccess::list(*list)->commands;
  need(current.size()==prior_command_count,"verification preserves command count");
  need(prior_command_bytes.empty()||std::memcmp(prior_command_bytes.data(),current.data(),prior_command_bytes.size())==0,"verification preserves all command bytes");
  need(list->shadows.size()==saved.size(),"verification preserves sidecar count");
  for(std::size_t i=0;i<saved.size();++i)need(list->shadows[i].command_index==saved[i].command_index&&list->shadows[i].box_identity==saved[i].box_identity&&list->shadows[i].declaration_index==saved[i].declaration_index,"verification preserves sidecar identity/order");
 };
 for(unsigned stop=1;stop<=verifycalls;++stop){
  unsigned calls=0;bool caught=false;
  try{plan->verify(*list,[&]{if(++calls==stop)throw Cancel{};});}
  catch(const Cancel&){caught=true;}
  need(caught&&calls==stop,"verification exact Cancel at each checkpoint");
  integrity();need(plan->accepts(l,f.map),"plan intact after verification cancellation");
  plan->verify(*list,[]{});integrity();
 }


 list->shadows.pop_back();rejects([&]{plan->verify(*list,[]{});},"omitted layer");list->shadows=saved;
 list->shadows[1]=list->shadows[0];rejects([&]{plan->verify(*list,[]{});},"duplicate layer");list->shadows=saved;
 std::swap(list->shadows[0],list->shadows[1]);rejects([&]{plan->verify(*list,[]{});},"layer order");list->shadows=saved;
 list->shadows[0].box_identity=list->shadows[2].box_identity;rejects([&]{plan->verify(*list,[]{});},"owner substitution");list->shadows=saved;
 auto& commands=NativeListAccess::list(*list)->commands;auto index=saved[0].command_index;auto original=commands[index];commands[index].data.draw_box_shadow.shadow.offset_x+=1;rejects([&]{plan->verify(*list,[]{});},"payload changed");commands[index]=original;
 auto old=l->root->border_box.y;l->root->border_box.y+=1;need(!plan->accepts(l,f.map),"stale geometry");l->root->border_box.y=old;plan->verify(*list,[]{});
 aether_display_list_builder_destroy(builder);plan.reset();aether_layout_result_destroy(l);
 for(const char* css:{"#one{box-shadow:0 0 2px black inset}","#one{box-shadow:0 0 2px black,0 0 2px red inset}","#one{border-radius:1px;box-shadow:0 0 2px black}","#one{box-shadow:0 0 .5px black}","#one{box-shadow:0 0 65px black}"}){Fixture n("<html><body><div id='one'></div></body></html>");n.sheet("#one{width:64px;height:48px}");n.sheet(css);n.resolve();aether_layout_result_t r{};ok(DocumentTextTestAccess::root(n.engine,n.doc,n.map,&r),"negative layout");DocumentTextBudget b;auto a=DocumentTextTestAccess::viewport(n.doc,r,n.map,{},b);bool caught=false;try{auto bad=DocumentTextTestAccess::shadows(r,n.map,a,b);}catch(const DocumentShadowUnsupported&){caught=true;}need(caught,"unsupported shadow explicit");aether_layout_result_destroy(r);}
 printf("Document shadow independent checks=%u PASS\n",checks);
}
