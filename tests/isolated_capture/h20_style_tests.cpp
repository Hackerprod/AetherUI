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
 int64_t add(const std::string& css){int64_t h{};int32_t n{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h20",&h,&n)==AETHER_OK);return h;}
 void resolve(){aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(rt,aether_doc_get_body(doc),&env,&tx)==AETHER_OK);aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);rev=next;}
 aether_computed_style_handle_t get(const char* id,aether_style_revision_t r=nullptr){if(!r)r=rev;auto node=aether_doc_get_element_by_id(doc,id);for(int i=0;i<aether_style_revision_style_count(r);++i){aether_node_t n{};aether_computed_style_handle_t h{};ck(aether_style_revision_get_style(r,i,&n,&h,nullptr)==AETHER_OK);if(n==node)return h;aether_computed_style_handle_destroy(h);}ck(false);return nullptr;}
 bool rtl(const char* id,aether_style_revision_t r=nullptr){auto h=get(id,r);bool b=aether::style::MeasuresReadView(*h->measures).direction_rtl();aether_computed_style_handle_destroy(h);return b;}
};
int main(){
 for(auto parent:{"ltr","rtl"}) for(auto child:{"", "ltr", "rtl", "initial", "inherit", "unset", "var(--missing)", "var(--missing,rtl)", "var(--bad)", "rTl", R"(r\74 l)"}) {
  Scene s;s.add(std::string("#p{direction:")+parent+";--bad:bogus}#x{"+(child[0]?std::string("direction:")+child:std::string())+"}");s.resolve();bool expected=std::string(parent)=="rtl";std::string c=child;
  if(c=="ltr"||c=="initial")expected=false;else if(c=="rtl"||c=="rTl"||c==R"(r\74 l)"||c=="var(--missing,rtl)")expected=true;
  ck(s.rtl("p")== (std::string(parent)=="rtl"));ck(s.rtl("x")==expected);
 }
 {Scene s;s.add("#p{direction:rtl}#x{direction:ltr;direction:var(--missing)}");s.resolve();ck(s.rtl("x"));auto old=s.get("x");aether_computed_style_handle_t clone{};ck(aether_computed_style_handle_clone(old,&clone)==AETHER_OK);auto h=s.add("#p{direction:ltr}");s.resolve();ck(!s.rtl("x"));ck(aether::style::MeasuresReadView(*old->measures).direction_rtl());ck(aether::style::MeasuresReadView(*clone->measures).direction_rtl());ck(aether_style_runtime_remove_stylesheet(s.rt,h)!=0);s.resolve();ck(s.rtl("x"));aether_computed_style_t flat{};ck(aether_computed_style_copy(old,&flat)==AETHER_ERROR_INVALID_STATE);aether_computed_style_handle_destroy(old);aether_style_runtime_destroy(s.rt);s.rt=nullptr;ck(aether::style::MeasuresReadView(*clone->measures).direction_rtl());aether_computed_style_handle_destroy(clone);}
 {Scene s;s.add("#p{direction:rtl!important}#p{direction:ltr}#x{direction:inherit}");s.resolve();ck(s.rtl("p")&&s.rtl("x"));}
 {Scene s;s.add("#p{direction:rtl}#x{direction:initial!important;direction:rtl}");s.resolve();ck(!s.rtl("x"));}
 size_t oom=0;{Scene s;s.add("#p{direction:ltr}");s.resolve();auto old=s.get("x");auto gen=aether_style_revision_generation(s.rev);s.add("#p{direction:rtl}");bool done=false;
  for(long i=0;i<2000;++i){aether_style_transaction_t tx{};budget=i;auto st=aether_style_runtime_prepare_revision(s.rt,aether_doc_get_body(s.doc),&s.env,&tx);budget=-1;ck(!aether::style::MeasuresReadView(*old->measures).direction_rtl());aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(s.rt,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==gen);aether_style_revision_destroy(retained);
   if(st==AETHER_OK){aether_style_revision_t next{};ck(aether_style_runtime_commit_revision(s.rt,tx,&next)==AETHER_OK);aether_style_transaction_destroy(tx);ck(s.rtl("x",next));aether_style_revision_destroy(next);done=true;break;}
   ck(st==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr);++oom;
  }ck(done&&oom);aether_computed_style_handle_destroy(old);
 }
 std::cout<<"H20 style checks="<<checks<<" OOM="<<oom<<"\n";
}
