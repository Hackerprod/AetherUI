#include "../src/rendering/software/border_math.h"
#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <cassert>
#include <cmath>
#include <array>
#include <iostream>
#include <new>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;
p::PreparedFrame prep(std::vector<p::Command> c){auto r=p::prepare({std::move(c)});assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));}
Frame get(FrameStore& s){std::optional<Frame> f;assert(s.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
p::BorderGroup group(float shift=0,bool dashed=false){p::BorderGroup g;g.outer={1+shift,1+shift,10,10};g.inner={3+shift,4+shift,6,5};g.sides={p::BorderSide{true,dashed?AETHER_BORDER_STYLE_DASHED:AETHER_BORDER_STYLE_SOLID,{1,0,0,.5f}},p::BorderSide{true,AETHER_BORDER_STYLE_SOLID,{0,1,0,.5f}},p::BorderSide{true,AETHER_BORDER_STYLE_SOLID,{0,0,1,.5f}},p::BorderSide{true,AETHER_BORDER_STYLE_SOLID,{1,1,0,.5f}}};return g;}
// Independent normalized-distance partition, not polygon cross products.
std::array<double,4> reference(const p::BorderGroup& g,p::Rect clip,int x,int y,int n){
 std::array<double,4> sum{};const auto o=g.outer,i=g.inner;
 const double t[]{double(i.y)-o.y,double(o.x)+o.width-i.x-i.width,double(o.y)+o.height-i.y-i.height,double(i.x)-o.x};
 for(int sy=0;sy<n;++sy)for(int sx=0;sx<n;++sx){double px=x+(sx+.5)/n,py=y+(sy+.5)/n;
 if(px<clip.x||py<clip.y||px>=double(clip.x)+clip.width||py>=double(clip.y)+clip.height||px<o.x||py<o.y||px>=double(o.x)+o.width||py>=double(o.y)+o.height)continue;
 if(px>=i.x&&px<double(i.x)+i.width&&py>=i.y&&py<double(i.y)+i.height)continue;
 const double d[]{py-o.y,double(o.x)+o.width-px,double(o.y)+o.height-py,px-o.x};int side=-1;double best=1e100;for(int k=0;k<4;++k)if(t[k]>0&&d[k]/t[k]<best){side=k;best=d[k]/t[k];}
 if(side<0||!g.sides[side].present)continue;
 const auto& c=g.sides[side];
 if(c.style==AETHER_BORDER_STYLE_DASHED){double L=side%2==0?o.width:o.height;double periods=std::max(1.,std::floor(L/(6*t[side])+.5));double P=L/periods,u=side%2==0?px-o.x:py-o.y;double phase=std::fmod(u+P/4,P);if(phase>=P/2)continue;}
 sum[0]+=c.color.b*c.color.a;sum[1]+=c.color.g*c.color.a;sum[2]+=c.color.r*c.color.a;sum[3]+=c.color.a;
 }for(auto& a:sum)a/=double(n)*n;return sum;
}
struct Cancel:p::Cancellation{int count{},at;explicit Cancel(int n):at(n){}bool cancelled()noexcept override{return ++count==at;}};
int main(){FrameStore store;assert(store.resize({14,14,1})==FrameStatus::ok);double error=0,convergence=0;
 // Fixed corpus before execution; no tuning of threshold after result.
 for(float shift:{0.f,.25f,.5f,.125f})for(bool dash:{false,true}){
 auto g=group(shift,dash);p::Rect clip{.25f,.125f,12,12};auto f=prep({p::PushClip{clip,{}},g,p::PopClip{}});assert(s::render(store,f).status==s::Status::ok);auto out=get(store);
 for(int y=0;y<14;++y)for(int x=0;x<14;++x){auto a=reference(g,clip,x,y,128),b=reference(g,clip,x,y,256);for(int k=0;k<4;++k){convergence=std::max(convergence,std::abs(a[k]-b[k]));error=std::max(error,std::abs(out.pixels[y*out.stride+x*4+k]/255.-b[k]));}}
 }
 assert(error<=16./255);assert(convergence<2./255);
 auto g=group();auto f=prep({g});assert(s::render(store,f).status==s::Status::ok);auto saved=get(store);
 for(int z=0;z<7;++z){auto bad=g;if(z==0)bad.inner.width=0;if(z==1)bad.outer_radii={1,1,1,1,1,1,1,1};if(z==2)bad.sides[0].style=AETHER_BORDER_STYLE_DOTTED;if(z==3)bad.inner.x=0;if(z==4)bad.outer.x=70000;if(z==5){bad.inner.y=bad.outer.y+.001f;bad.sides[0].style=AETHER_BORDER_STYLE_DASHED;}if(z==6){bad.outer.width=8192;bad.inner.width=8188;bad.inner.y=bad.outer.y+1.f/64;bad.sides[0].style=AETHER_BORDER_STYLE_DASHED;}
 assert(s::render(store,prep({g,bad})).status==s::Status::unsupported);assert(get(store).pixels==saved.pixels);}
 auto tiny=g;tiny.inner.y=tiny.outer.y;assert(s::render(store,prep({tiny})).status==s::Status::ok);
 Cancel total(99999);assert(s::render(store,f,&total).status==s::Status::ok);saved=get(store);for(int j=1;j<=total.count;++j){Cancel c(j);assert(s::render(store,f,&c).status==s::Status::cancelled);assert(get(store).pixels==saved.pixels);}
 bool fail=false;FrameStore oom([&]{if(fail)throw std::bad_alloc{};});assert(oom.resize({14,14,1})==FrameStatus::ok);assert(s::render(oom,f).status==s::Status::ok);saved=get(oom);fail=true;assert(s::render(oom,f).status==s::Status::allocation_failed);fail=false;assert(get(oom).pixels==saved.pixels);
 assert(s::render(store,prep(std::vector<p::Command>(2000,g))).status==s::Status::limit_exceeded);
 auto sub=g;sub.inner.y=std::nextafter(sub.outer.y,2.f);sub.sides[0].style=AETHER_BORDER_STYLE_DASHED;assert(s::render(store,prep({sub})).status==s::Status::unsupported);
 auto limit=g;limit.outer={0,0,100,9000};limit.inner={2,8192,96,800};limit.sides[0].style=AETHER_BORDER_STYLE_DASHED;
 assert(s::render(store,prep({limit})).status==s::Status::ok);limit.inner.y=8193;assert(s::render(store,prep({limit})).status==s::Status::unsupported);
 auto phase=g;phase.outer={0,0,12,12};phase.inner={2,2,8,8};
 assert(s::border_math::dash(phase,0,0,0));assert(!s::border_math::dash(phase,0,3,0));assert(!s::border_math::dash(phase,0,9,0));assert(s::border_math::dash(phase,0,12,0));
 std::cout<<"coverage_max_error="<<error<<" reference_convergence="<<convergence<<"\n";
}
