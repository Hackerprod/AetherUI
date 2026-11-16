#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/layout/selected_layout_capture.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <memory>
#include <new>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
namespace aether::layout {
class SelectedCaptureTestAccess {
public: static std::unique_ptr<SelectedLayoutAuthority> make(aether_doc_t d,aether_style_map_t m,aether_node_t n,float w=1280,float h=720){return std::unique_ptr<SelectedLayoutAuthority>(new SelectedLayoutAuthority(d,m,n,w,h));}
};
}
namespace {
unsigned checks=0;
void need(bool b,const char*m){++checks;if(!b){fprintf(stderr,"FAIL selected-layout: %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};
 aether_style_environment_t env{1280,720,1,16,0,0,0};
 Fixture(){ok(aether_doc_create(&doc),"doc");ok(aether_doc_parse_html(doc,"<html><body><section id='parent'><div id='outside'></div><div id='target'><div id='a'></div><div id='b'></div></div></section></body></html>"),"parse");ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"runtime");sheet(aether::style::web_capture_defaults_css(),0);sheet("html{font-family:Arial;color:#112233}#parent{margin:100px;width:900px}#outside{height:200px}#target{display:flex;width:420px;min-height:72px;border:2px solid black;box-sizing:border-box;align-items:flex-start}#a,#b{width:42px;height:36px;flex-shrink:0}");resolve();ok(aether_layout_create(nullptr,nullptr,&engine),"engine");}
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 void sheet(const char*s,int layer=1){int64_t h{};ok(aether_style_runtime_add_stylesheet(runtime,s,layer,"test",&h,nullptr),"css");}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&tx),"prepare");aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next),"commit");aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);revision=next;aether_style_map_destroy(map);map=nullptr;ok(aether_style_revision_create_map(revision,&map),"map");}
 auto authority(){return aether::layout::SelectedCaptureTestAccess::make(doc,map,node("target"));}
};
void reject(Fixture&f,const aether::layout::SelectedLayoutAuthority&a,const char*why){aether_layout_result_t output{};need(aether::layout::layout_selected_capture(f.engine,f.doc,f.map,a,&output)==AETHER_ERROR_INVALID_STATE,why);need(output==nullptr,"no partial output");}
}
int main(){
 {Fixture f;auto auth=f.authority();aether_layout_result_t output{};ok(aether::layout::layout_selected_capture(f.engine,f.doc,f.map,*auth,&output),"selected layout");auto root=aether_layout_result_get_root(output);need(aether_layout_box_get_element(root)==f.node("target"),"original target identity");need(aether_layout_box_get_width(root)==420&&aether_layout_box_get_height(root)==72,"target authored dimensions no viewport stretching");need(output->assets!=nullptr,"retained layout assets");need(output->assets->base==aether::style::pin_style_map(f.map),"same original pinned map version");need(output->assets->document_lifetime==f.doc->identity,"original document assets");need(output->viewport_overflow_x==AETHER_OVERFLOW_VISIBLE&&output->viewport_overflow_y==AETHER_OVERFLOW_VISIBLE,"no viewport overflow propagation");need(aether_layout_box_get_x(root)==0&&aether_layout_box_get_y(root)==0,"selected local embedding not ancestor crop");need(!aether_layout_result_find_primary_box(output,f.node("parent"))&&!aether_layout_result_find_primary_box(output,f.node("outside")),"outside scope not laid out");auto a=aether_layout_result_find_primary_box(output,f.node("a"));auto b=aether_layout_result_find_primary_box(output,f.node("b"));need(a&&b,"original children mapping");need(aether_layout_box_get_width(a)==42&&aether_layout_box_get_x(b)-aether_layout_box_get_x(a)==42,"native flex placement");ok(aether_node_append_child(f.node("parent"),f.node("a")),"postcapture mutation");need(!output->assets->structure_valid(),"retained assets detect later structural mutation");aether_layout_result_destroy(output);}
 {Fixture f;f.sheet("#target{width:421px}");f.resolve();auto a=f.authority();aether_layout_result_t out{};ok(aether::layout::layout_selected_capture(f.engine,f.doc,f.map,*a,&out),"mutated source");need(aether_layout_box_get_width(aether_layout_result_get_root(out))==421,"width mutation not hardcoded");aether_layout_result_destroy(out);}
 {Fixture f;auto a=f.authority();std::thread t([&]{reject(f,*a,"wrong thread");});t.join();}
 {Fixture f;auto a=f.authority();ok(aether_element_set_attribute(f.node("target"),"style","width:421px"),"mutation");reject(f,*a,"stale input");}
 {Fixture f;auto a=f.authority();ok(aether_node_append_child(aether_doc_get_body(f.doc),f.node("target")),"reparent");reject(f,*a,"reparent stale");}
 {Fixture f,g;bool threw=false;try{auto a=aether::layout::SelectedCaptureTestAccess::make(f.doc,g.map,f.node("target"));}catch(const std::invalid_argument&){threw=true;}need(threw,"wrong document map");}
 {Fixture f;auto a=f.authority();aether_style_map_t old=f.map;f.map=nullptr;f.resolve();reject(f,*a,"changed map handle/version");aether_style_map_destroy(old);}
 {Fixture f;auto a=aether::layout::SelectedCaptureTestAccess::make(f.doc,f.map,f.node("target"),640,480);reject(f,*a,"style and layout viewport mismatch");}

 {Fixture f;f.sheet("#outside{display:contents}");f.resolve();auto a=f.authority();aether_layout_result_t out{};ok(aether::layout::layout_selected_capture(f.engine,f.doc,f.map,*a,&out),"unrelated display contents does not reject selected layout");aether_layout_result_destroy(out);}
 for(size_t count:{128,129}){Fixture f;auto t=f.node("target");for(size_t i=3;i<count;++i){auto*n=new aether_node_s();n->type=AETHER_NODE_COMMENT;n->parent=t;t->children.push_back(n);}f.resolve();bool thrown=false;try{auto a=f.authority();}catch(const std::length_error&){thrown=true;}need(thrown==(count==129),"authority scope includes comments 128/129");}
 {Fixture f;auto a=f.authority();aether_layout_result_t prior{};ok(aether::layout::layout_selected_capture(f.engine,f.doc,f.map,*a,&prior),"prior output");size_t failed=0;bool success=false;
  for(long budget=0;budget<2000&&!success;++budget){auto out=prior;allocation_budget=budget;auto status=aether::layout::layout_selected_capture(f.engine,f.doc,f.map,*a,&out);allocation_budget=-1;need(!f.engine->layout_in_progress,"OOM resets layout-in-progress");if(status==AETHER_OK){need(out!=prior,"success separate output");aether_layout_result_destroy(out);success=true;}else{need(status==AETHER_ERROR_OUT_OF_MEMORY,"OOM explicit status");need(out==prior,"failure preserves previous output pointer");need(aether_layout_box_get_width(aether_layout_result_get_root(prior))==420,"failure preserves previous geometry");++failed;}}
  need(success&&failed>0,"OOM eventual recovery");aether_layout_result_destroy(prior);printf("Selected layout OOM positions=%zu\n",failed);
 }
 printf("Selected layout independent checks=%u\n",checks);
}
