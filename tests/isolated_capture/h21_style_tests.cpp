#ifdef NDEBUG
#error Assertions required
#endif
#include <aether/aether_contextual_style.h>
#include "../../native/src/css/computed_style_handle.h"
#include "../../native/src/css/frozen_measure_read.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
static long budget=-1;static size_t checks=0;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static void ck(bool b){assert(b);++checks;}
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_revision_t rev{};aether_style_environment_t env{400,200,1,16,0,0,0};
 Scene(){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,"<body><div id='p'><span id='x'></span></div></body>")==AETHER_OK);ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);}
 ~Scene(){aether_style_revision_destroy(rev);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 int64_t add(const std::string& css){int64_t h{};int32_t n{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h21",&h,&n)==AETHER_OK);return h;}
 void resolve(){aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(rt,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);rev=next;}
 aether_computed_style_handle_t get(const char* id,aether_style_revision_t r=nullptr){if(!r)r=rev;auto node=aether_doc_get_element_by_id(doc,id);for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n{};aether_computed_style_handle_t h{};ck(aether_style_revision_get_style(r,i,&n,&h,nullptr)==AETHER_OK);if(n==node)return h;aether_computed_style_handle_destroy(h);}ck(false);return nullptr;}
 uint32_t impact(const char* id){auto node=aether_doc_get_element_by_id(doc,id);for(int i=0;i<aether_style_revision_change_count(rev);++i){aether_node_t n{};aether_computed_style_handle_t h{};uint32_t impact{};ck(aether_style_revision_get_change(rev,i,&n,&h,&impact)==AETHER_OK);aether_computed_style_handle_destroy(h);if(n==node)return impact;}return 0;}
 int mode(const char* id,aether_style_revision_t r=nullptr){auto h=get(id,r);int b=aether::style::MeasuresReadView(*h->measures).writing_mode();aether_computed_style_handle_destroy(h);return b;}
};
int main(){
 const char* modes[]={"horizontal-tb","vertical-rl","vertical-lr"};
 for(int parent=0;parent<3;++parent) for(auto child:{"", "horizontal-tb", "vertical-rl", "vertical-lr", "initial", "inherit", "unset", "var(--missing)", "var(--missing,vertical-rl)", "var(--bad)", "sideways-rl", "vertical", "VERTICAL-LR"}) {
  Scene s;s.add(std::string("#p{writing-mode:")+modes[parent]+";--bad:bogus}#x{"+(child[0]?std::string("writing-mode:")+child:std::string())+"}");s.resolve();int expected=parent;std::string c=child;
  if(c=="horizontal-tb"||c=="initial")expected=0;else if(c=="vertical-rl"||c=="var(--missing,vertical-rl)")expected=1;else if(c=="vertical-lr"||c=="VERTICAL-LR")expected=2;
  ck(s.mode("p")==parent);ck(s.mode("x")==expected);
 }
 {Scene s;s.add("#p{writing-mode:vertical-rl}#x{writing-mode:horizontal-tb;writing-mode:var(--missing)}");s.resolve();ck(s.mode("x")==1);auto old=s.get("x");aether_computed_style_handle_t clone{};ck(aether_computed_style_handle_clone(old,&clone)==AETHER_OK);auto h=s.add("#p{writing-mode:vertical-lr}");s.resolve();ck(s.mode("x")==2);ck((s.impact("x")&7)==7);ck((s.impact("p")&7)==7);ck(aether::style::MeasuresReadView(*old->measures).writing_mode()==1);ck(aether::style::MeasuresReadView(*clone->measures).writing_mode()==1);ck(aether_style_runtime_remove_stylesheet(s.rt,h)!=0);s.resolve();ck(s.mode("x")==1);aether_computed_style_t flat{};ck(aether_computed_style_copy(old,&flat)==AETHER_ERROR_INVALID_STATE);aether_computed_style_handle_destroy(old);aether_style_runtime_destroy(s.rt);s.rt=nullptr;ck(aether::style::MeasuresReadView(*clone->measures).writing_mode()==1);aether_computed_style_handle_destroy(clone);}
 {Scene s;s.add("#p{writing-mode:vertical-lr!important}#p{writing-mode:horizontal-tb}#x{writing-mode:inherit}");s.resolve();ck(s.mode("p")==2&&s.mode("x")==2);}
 {Scene s;s.add("#p{writing-mode:vertical-rl}#x{writing-mode:initial!important;writing-mode:vertical-lr}");s.resolve();ck(s.mode("x")==0);}
 size_t oom=0;{Scene s;s.add("#p{writing-mode:horizontal-tb}");s.resolve();auto old=s.get("x");auto gen=aether_style_revision_generation(s.rev);s.add("#p{writing-mode:vertical-rl}");bool done=false;
  for(long i=0;i<2000;++i){aether_style_transaction_t tx{};budget=i;auto st=aether_style_runtime_prepare_revision(s.rt,aether_doc_get_body(s.doc),&s.env,&tx);budget=-1;ck(aether::style::MeasuresReadView(*old->measures).writing_mode()==0);aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(s.rt,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==gen);aether_style_revision_destroy(retained);
   if(st==AETHER_OK){aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(s.rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);ck(s.mode("x",next)==1);aether_style_revision_destroy(next);done=true;break;}
   ck(st==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr);++oom;
  }ck(done&&oom);aether_computed_style_handle_destroy(old);
 }
 std::cout<<"H21 style checks="<<checks<<" OOM="<<oom<<"\n";
}
