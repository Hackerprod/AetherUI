#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/rendering/software/software_renderer.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace aether::rendering;
namespace p=aether::rendering::preparation; namespace s=aether::rendering::software;
static unsigned checks=0;static void ck(bool b){++checks;if(!b)std::cerr<<"independent G2 failed "<<checks<<std::endl;assert(b);}
static p::PushTransform tr(float x,float y=0){auto m=p::identity;m.dx=x;m.dy=y;return {m};}
static p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});ck(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
static Frame get(FrameStore& st){std::optional<Frame> f;ck(st.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static Frame image(std::vector<p::Command> c){FrameStore st;ck(st.resize({24,20,1})==FrameStatus::ok);ck(s::render(st,prep(std::move(c))).status==s::Status::ok);return get(st);}
// Explicit independent device-space fixtures. No production transform helper,
// matrix multiplication or border inverse mapping is used to build expectations.
static void eq(std::vector<p::Command> a,std::vector<p::Command> b){ck(image(a).pixels==image(b).pixels);}
static p::Fill fill(float x,float y,float w=6,float h=5){return {{x,y,w,h},{},{1,.25f,.5f,1}};}
static p::LinearGradientFill grad(p::Rect shape,p::Rect ramp){p::LinearGradientFill g;g.rect=shape;g.gradient_rect=ramp;g.direction_x=1;g.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};return g;}
static void reject(std::vector<p::Command> c){FrameStore st;ck(st.resize({24,20,1})==FrameStatus::ok);ck(s::render(st,prep({fill(1,1)})).status==s::Status::ok);auto old=get(st);auto r=s::render(st,prep(std::move(c)));ck(r.status==s::Status::unsupported);auto now=get(st);ck(now.sequence==old.sequence&&now.pixels==old.pixels);}
int main(){
 eq({tr(3,4),fill(1,2),p::PopTransform{}},{fill(4,6)});
 eq({tr(3,4),tr(-1,.5f),fill(1,2),p::PopTransform{},fill(10,1),p::PopTransform{},fill(1,15)}, {fill(3,6.5f),fill(13,5),fill(1,15)});
 eq({tr(32768),tr(1.f/1024),tr(-32768),fill(.5f,2,1,2),p::PopTransform{},p::PopTransform{},p::PopTransform{}},{fill(.5009765625f,2,1,2)});
 ck(image({fill(.5009765625f,2,1,2)}).pixels!=image({fill(.5f,2,1,2)}).pixels);
 reject({tr(65536),tr(1.f/1024),tr(-65536),p::PopTransform{},p::PopTransform{},p::PopTransform{}});
 reject({tr(-65536),tr(-1.f/1024),p::PopTransform{},p::PopTransform{}});
 reject({tr(65536),fill(1,1),p::PopTransform{}});
 // Fixed clip outside transform versus translated clip captured at push.
 eq({p::PushClip{{4,3,5,6},{}},tr(3,2),fill(0,0,12,12),p::PopTransform{},p::PopClip{}},{p::PushClip{{4,3,5,6},{}},fill(3,2,12,12),p::PopClip{}});
 eq({tr(3,2),p::PushClip{{1,1,5,6},{}},tr(2,1),fill(0,0,12,12),p::PopTransform{},p::PopClip{},p::PopTransform{}},{p::PushClip{{4,3,5,6},{}},fill(5,3,12,12),p::PopClip{}});
 eq({tr(4,5),p::PushScroll{1,2,3},p::BeginOpacity{1,0,.5f},grad({1,1,8,7},{0,0,12,10}),p::EndOpacity{1},p::PopScroll{},p::PopTransform{}},{p::BeginOpacity{1,0,.5f},grad({3,3,8,7},{2,2,12,10}),p::EndOpacity{1}});
 p::BorderGroup b;b.outer={1,1,10,10};b.inner={3,4,6,5};b.sides={p::BorderSide{true,AETHER_BORDER_STYLE_DASHED,{1,0,0,1}},p::BorderSide{true,AETHER_BORDER_STYLE_SOLID,{0,1,0,1}},p::BorderSide{true,AETHER_BORDER_STYLE_SOLID,{0,0,1,1}},p::BorderSide{true,AETHER_BORDER_STYLE_DASHED,{1,1,0,1}}};auto moved=b;moved.outer={4.25f,3.5f,10,10};moved.inner={6.25f,6.5f,6,5};eq({tr(3.25f,2.5f),b,p::PopTransform{}},{moved});
 for(int k=0;k<4;++k){auto m=p::identity;if(k==0)m.m11=std::nextafter(1.f,2.f);if(k==1)m.m22=2;if(k==2)m.m12=.001f;if(k==3)m.m21=-.001f;reject({p::PushTransform{m},p::PopTransform{}});reject({p::PushTransform{m},fill(30000,30000),p::PopTransform{}});}
 std::cout<<"G2 independent explicit geometry PASS checks="<<checks<<"; scope=ABI_TRANSLATIONS; HTML=NOT_RUN; Chromium=NOT_RUN\n";
}
