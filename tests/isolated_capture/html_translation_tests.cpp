#include "h5_profile.h"
#include "../../native/src/css/transforms.h"
#include "../../native/src/rendering/transform_matrix.h"
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
static long budget=-1;
void* operator new(size_t n){if(budget==0)throw std::bad_alloc();if(budget>0)--budget;if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
int main(){style_property_registry_init();size_t checks=0;
 for(auto v:{"translate(0)","translate(1px,-2px)","translateX(.25px)","translateY(-65536px)","translate(65536px)",R"(translat\65 (1p\78 , 0))","translate(/*a*/2px/*b*/)"}){
  assert(h4::value("transform",v));auto a=h5::analyze(std::string("div{transform:")+v+"}",true);assert(a.valid&&a.functions.size()==1);
  aether_transform_list list{};assert(aether_transform_list_try_parse(v,&list)&&list.count==1);aether_transform_list_free(&list);++checks;
 }
 for(auto v:{"translate(65536.001px)","translate(-65537px)","translate(1e-999px)","translate(1e999px)","translate(0%)","translate(calc(0px))","translate(1em)","translate(1)","translate(1px 2px)","translate(1px,)","translate(1px,,2px)","translate(1px,2px,3px)","translateX(1px,2px)","translate(0) translate(0)","matrix(1,0,0,1,0,0)","scale(1)","rotate(0)","skewX(0)","translate3d(0,0,0)","translate /**/ (0)"}){assert(!h4::value("transform",v));++checks;}
 aether_transform_list list{};assert(aether_transform_list_try_parse("translate(.125px,-.25px)",&list));
 aether_transform_origin origin{};origin.x.value=50;origin.x.unit=AETHER_CSS_UNIT_PERCENT;origin.y=origin.x;
 for(float position:{0.f,32768.f,1e9f}){auto m=aether_transform_matrix_resolve(list,origin,{position,position,100,100},16,16,{100,100});assert(m.dx==.125f&&m.dy==-.25f&&m.m11==1&&m.m22==1);++checks;}
 aether_transform_list_free(&list);
 assert(aether_transform_list_try_parse("scale(2)",&list));auto m=aether_transform_matrix_resolve(list,origin,{0,0,100,100},16,16,{100,100});assert(m.dx==-50&&m.dy==-50&&m.m11==2);aether_transform_list_free(&list);++checks;
 bool done=false;size_t failures=0;std::string v="translate(12.5px,-8px)";for(long n=0;n<500;++n){budget=n;try{bool ok=h4::value("transform",v);budget=-1;assert(ok);done=true;break;}catch(const std::bad_alloc&){budget=-1;++failures;}}assert(done&&failures);
 std::cout<<"HTML_TRANSLATE gate+producer="<<checks<<" OOM="<<failures<<" Chromium=NOT_RUN\n";
}
