#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <cmath>
#include <array>
#include <iostream>
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;using namespace aether::rendering;
int main(){std::array<uint8_t,64>d{};for(int i=0;i<16;++i){d[4*i]=(i*37)%180;d[4*i+1]=(i*61)%180;d[4*i+2]=(i*83)%180;d[4*i+3]=200;}
auto ref=std::get<p::ImageRef>(p::ImageResource::copy(4,4,d));FrameStore st;assert(st.resize({8,8,1})==FrameStatus::ok);int n=0;
for(float x:{0.f,.25f,.5f,.75f,1.f,2.f,3.f})for(float y:{0.f,.25f,.75f,3.f})for(float w:{1.f,1.25f,2.f})for(float h:{1.f,1.75f,2.f}){if(double(x)+w>4||double(y)+h>4)continue;
p::Image im{ref,{x,y,w,h},{0,0,8,8},1,p::ImageSamplingBounds::FullResource};auto r=p::prepare(p::Snapshot{{im}});assert(std::holds_alternative<p::PreparedFrame>(r));assert(s::render(st,std::get<p::PreparedFrame>(r)).status==s::Status::ok);std::optional<Frame> f;assert(st.copy_confirmed(f)==FrameStatus::ok);
for(int py=0;py<8;++py)for(int px=0;px<8;++px){long double u=x+(px+.5L)/8*w-.5L,v=y+(py+.5L)/8*h-.5L;u=std::max(0.L,std::min(3.L,u));v=std::max(0.L,std::min(3.L,v));int a=(int)floor(u),b=(int)floor(v),a1=std::min(3,a+1),b1=std::min(3,b+1);long double fx=u-a,fy=v-b;for(int c=0;c<4;++c){long double z=d[(b*4+a)*4+c]*(1-fx)*(1-fy)+d[(b*4+a1)*4+c]*fx*(1-fy)+d[(b1*4+a)*4+c]*(1-fx)*fy+d[(b1*4+a1)*4+c]*fx*fy;int expected=(int)floor(z+.5L);assert(f->pixels[py*f->stride+px*4+c]==expected);}}++n;}
std::cout<<"independent scalar oracle PASS cases="<<n<<"\n";}
