#include "hit_order_oom_helpers.h"
// OOM transaction checks through public ABI; shared-core operator-new interposition.
#include <new>
#include <cstdlib>
static long fail_after=-1;
static bool fault_delivered=false;
void* operator new(std::size_t n){if(fail_after>=0&&fail_after--==0){fault_delivered=true;throw std::bad_alloc();}if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void*p,const std::nothrow_t&) noexcept {std::free(p);}
#include "fault_inject_aligned.h"
#define main hanging_geometry_suite
#include "test_inline_hanging.cpp"
#undef main
struct RetainedBox { aether_layout_box_t box; aether_rectf rect; };
static void save_boxes(aether_layout_box_t b,std::vector<RetainedBox>& out){
 out.push_back({b,aether_layout_box_get_border_box(b)});
 for(int i=0;i<aether_layout_box_get_child_count(b);++i)save_boxes(aether_layout_box_get_child(b,i),out);
}
static std::string paint_signature(Fixture& f){
 aether_display_list_builder_t b{};aether_display_list_t list{};
 need(aether_display_list_builder_create(Meter::measure,&f.meter,&b)==AETHER_OK,"snapshot paint builder");
 need(aether_display_list_build(b,f.result,nullptr,&list)==AETHER_OK,"snapshot paint list");
 std::string signature;
 for(int i=0;i<aether_display_list_get_count(list);++i){aether_paint_command_t c{};need(aether_display_list_get_command(list,i,&c)==AETHER_OK,"snapshot command");signature+=std::to_string(int(c.tag))+":";
  if(c.tag==AETHER_PAINT_DRAW_TEXT){auto&t=c.data.draw_text;signature+=std::string(t.text?t.text:"")+":"+std::to_string(t.rect.x)+","+std::to_string(t.rect.y)+","+std::to_string(t.rect.width)+","+std::to_string(t.rect.height);}
  signature+=";";aether_paint_command_free(&c);
 }
 aether_display_list_destroy(list);aether_display_list_builder_destroy(b);return signature;
}
static int sweep(const char* name,const std::string& content,const std::string& css){
 scenario=name;Fixture f(content,css,true,true);
 std::vector<RetainedBox> saved;save_boxes(f.box(),saved);const auto paint_before=paint_signature(f);
 auto original=f.result;const auto original_rect=f.rect();auto before=f.runs();int delivered=0;bool completed_sweep=false;
 for(long ordinal=0;ordinal<10000;++ordinal){
  auto out=original;fault_delivered=false;fail_after=ordinal;
  auto status=aether_layout_layout(f.engine,f.doc,f.map,600,400,nullptr,nullptr,&out);
  fail_after=-1;
  if(!fault_delivered){need(status==AETHER_OK,"successful terminating layout");aether_layout_result_destroy(out);completed_sweep=true;break;}
  ++delivered; if(status==AETHER_OK) check(hit_oom_optional_success(out,original),"optional cache OOM preserves full geometry");
  else {check(status==AETHER_ERROR_OUT_OF_MEMORY,"OOM status");check(out==original,"output unchanged on OOM");}
  near(f.rect().height,original_rect.height,"retained result height");
  std::vector<RetainedBox> now;save_boxes(f.box(),now);check(now.size()==saved.size(),"retained full subtree count");
  for(size_t i=0;i<now.size()&&i<saved.size();++i){check(now[i].box==saved[i].box,"retained subtree pointer");near(now[i].rect.x,saved[i].rect.x,"retained subtree x");near(now[i].rect.y,saved[i].rect.y,"retained subtree y");near(now[i].rect.width,saved[i].rect.width,"retained subtree width");near(now[i].rect.height,saved[i].rect.height,"retained subtree height");}
  check(paint_signature(f)==paint_before,"retained paint order and text geometry");
  auto after=f.runs();check(before.size()==after.size(),"retained fragments");
  for(size_t i=0;i<before.size()&&i<after.size();++i){check(before[i].text==after[i].text,"retained text");near(before[i].rect.x,after[i].rect.x,"retained x");near(before[i].rect.y,after[i].rect.y,"retained y");}
  if(status==AETHER_OK&&out!=original)aether_layout_result_destroy(out);
 }
 check(completed_sweep,"fault sweep reached uninjected success");check(delivered>20,"fault sweep reaches layout allocations");aether_layout_result_t retry{};
 need(aether_layout_layout(f.engine,f.doc,f.map,600,400,nullptr,nullptr,&retry)==AETHER_OK,"retry after OOM");aether_layout_result_destroy(retry);
 check(paint_signature(f)==paint_before,"retained paint after successful retry");
 std::printf("%s: %d injected\n",name,delivered);return delivered;
}
int main(){
 int total=0;
 for(const char* mode:{"normal","pre-line","nowrap","pre","pre-wrap"})
 total+=sweep(mode,"<span>abc</span><span>def</span><span> </span><span>xy</span><br><span>a\r</span><span>\nb</span>",column(false)+"#host{white-space:"+std::string(mode)+"}");
 total+=sweep("pre-wrap intrinsic flex","<span>abc</span><span>def</span><span> </span><span> </span>xy  ","#outer{display:flex;width:20px}#host{width:auto;white-space:pre-wrap;flex:0 1 auto}");
 std::printf("inline-units-oom: %d checks, %d failures, %d injected\n",checks,failures,total);return failures?1:0;
}
