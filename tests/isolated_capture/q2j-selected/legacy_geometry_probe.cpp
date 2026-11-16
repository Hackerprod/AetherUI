#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "native/src/layout/selected_layout_capture.h"
#include "native/src/style_runtime/ua_stylesheet.h"
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

}

void dump(aether_layout_box_t b,unsigned d){if(!b)return;printf("%u %d %.9g %.9g %.9g %.9g\n",d,aether_layout_box_get_kind(b),aether_layout_box_get_x(b),aether_layout_box_get_y(b),aether_layout_box_get_width(b),aether_layout_box_get_height(b));for(int i=0;i<aether_layout_box_get_child_count(b);++i)dump(aether_layout_box_get_child(b,i),d+1);}
int main(){Fixture f;aether_layout_result_t out{};ok(aether_layout_layout(f.engine,f.doc,f.map,1280,720,nullptr,nullptr,&out),"legacy layout");dump(aether_layout_result_get_root(out),0);aether_layout_result_destroy(out);}
