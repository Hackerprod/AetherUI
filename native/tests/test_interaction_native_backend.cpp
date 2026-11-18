#include "rendering/html_ltr/interaction_native.h"
#include "rendering/html_ltr/owned_font_transport.h"
#include <cassert>
#include "style_runtime/transaction_preview.h"
#include <thread>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace aether::rendering;using namespace html_ltr;
static long fail_after=-1;
void* operator new(size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new(size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;void* p=nullptr;if(posix_memalign(&p,std::max(size_t(a),sizeof(void*)),n?n:1))throw std::bad_alloc();return p;}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}void operator delete(void* p,size_t,std::align_val_t)noexcept{std::free(p);}
static auto read(const char* p){std::ifstream f(p,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
static void transaction_checks(){
 for(int scenario=0;scenario<5;++scenario){
  aether_doc_t doc{};assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,"<html><body><p>A</p></body></html>")==AETHER_OK);
  aether_style_environment_t env{320,200,1,16,0,0,0};aether_style_runtime_options_t options{};aether_style_runtime_t runtime{},other{};
  assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&other)==AETHER_OK);
  aether_style_transaction_t tx{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&tx)==AETHER_OK);
  aether_style_map_t pending{};assert(aether::style::transaction_create_pending_map(tx,&pending)==AETHER_OK);
  aether_style_revision_t revision{};aether_status_t status{};
  if(scenario==0)status=aether_style_runtime_commit_revision(other,tx,&revision);
  if(scenario==1){std::thread t([&]{status=aether_style_runtime_commit_revision(runtime,tx,&revision);});t.join();}
  if(scenario==2){aether_style_transaction_t second{};assert(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&second)==AETHER_OK);aether_style_revision_t newer{};assert(aether_style_runtime_commit_revision(runtime,second,&newer)==AETHER_OK);aether_style_transaction_destroy(second);aether_style_revision_destroy(newer);status=aether_style_runtime_commit_revision(runtime,tx,&revision);}
  if(scenario==3){int64_t id{};assert(aether_style_runtime_add_stylesheet(runtime,"p {color:red;}",0,"test",&id,nullptr)==AETHER_OK);status=aether_style_runtime_commit_revision(runtime,tx,&revision);}
  if(scenario==4){assert(aether_element_set_attribute(aether_doc_get_body(doc),"class","changed")==AETHER_OK);status=aether_style_runtime_commit_revision(runtime,tx,&revision);}
  assert(status==AETHER_ERROR_INVALID_STATE&&!revision);
  aether_style_map_destroy(pending);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(other);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 }
 std::cout<<"pending-map commit checks=5 PASS\n";
}
int stage=0;
int run(int argc,char**argv){assert(argc==3);transaction_checks();OwnedBudget budget;const auto baseline=budget.total().current;auto r=read(argv[1]),b=read(argv[2]);
 {auto rs=std::allocate_shared<owned_text::Source>(OwnedAllocator<owned_text::Source>(budget,OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(r));auto bs=std::allocate_shared<owned_text::Source>(OwnedAllocator<owned_text::Source>(budget,OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(b));
 stage=1;auto doc=InteractionNativeDocument::create(budget,"<html><body><input type=\"text\" placeholder=\"Name\"><input type=\"password\" placeholder=\"Secret\"><button type=\"button\">Go</button></body></html>",owned_text::legacy_source(rs),owned_text::legacy_source(bs),320,200);
 stage=2;InteractionStateMachine machine(budget,doc->seeds(),doc->immutable_scalar_count(),320,200,*doc);
 stage=3;auto scene=doc->prepare(machine.candidate_view());assert(scene->hit(-1,0)==0);
 InteractionEvent tab{};tab.kind=uint32_t(InteractionEventKind::key_down);tab.key=uint32_t(InteractionKey::tab);
 stage=4;machine.dispatch(tab,{},*doc);scene=doc->prepare(machine.candidate_view());assert(machine.metadata().focus_id==1);
 const uint8_t text[]={'A','b'};InteractionEvent event{};event.kind=uint32_t(InteractionEventKind::text);event.text=text;event.text_length=2;
 machine.dispatch(event,{},*doc);scene=doc->prepare(machine.candidate_view());
 machine.dispatch(tab,{},*doc);machine.dispatch(event,{},*doc);scene=doc->prepare(machine.candidate_view());assert(machine.candidate_view().presentations[1].text=="\xe2\x80\xa2\xe2\x80\xa2");
 stage=7;const auto original=machine.metadata();size_t failures=0;
 for(long point=0;point<20000;++point){fail_after=point;bool ok=false;try{machine.resize(360,240,*doc);ok=true;}catch(const std::bad_alloc&){}catch(const Error& e){assert(e.status==AETHER_ERROR_OUT_OF_MEMORY);}fail_after=-1;
  if(ok)break;++failures;assert(machine.metadata()==original);assert(scene->hit(-1,0)==0);
 }
 assert(machine.metadata().width==360);scene=doc->prepare(machine.candidate_view());
 machine.resize(0,0,*doc);machine.dispatch(event,{},*doc);machine.resize(320,200,*doc);scene=doc->prepare(machine.candidate_view());
 InteractionEvent backspace{};backspace.kind=uint32_t(InteractionEventKind::key_down);backspace.key=uint32_t(InteractionKey::backspace);
 event.text_length=1;const auto stable=budget.total().current;
 const size_t cycles=std::getenv("AETHER_BACKEND_SKIP_CYCLES")?0:10000;
 for(size_t cycle=0;cycle<cycles;++cycle){machine.dispatch(backspace,{},*doc);machine.dispatch(event,{},*doc);scene=doc->prepare(machine.candidate_view());assert(budget.total().current==stable);}
 std::cout<<"backend edit/prepare cycles="<<cycles<<" owned stable PASS\n";
 doc.reset();assert(scene->hit(-1,0)==0);scene.reset();std::cout<<"backend resize OOM points="<<failures<<" PASS\n";
 }
 assert(budget.total().current==baseline);return 0;
}

int main(int argc,char**argv){try{return run(argc,argv);}catch(const Error& e){std::cerr<<"stage="<<stage<<" status="<<e.status<<"\n";return 1;}}
