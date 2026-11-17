// Private white-box classifier inspection; oracle and expected domains are independent.
#include "../../native/src/rendering/software/software_renderer.cpp"
#define main original_g1_main
#include "g1_gradient_tests.cpp"
#undef main
static std::array<int,4> legacy(const p::LinearGradientFill& g,int x,int y,p::Rect clip={-100,-100,1000,1000},double cr=0){
 const double l=std::max({double(x),double(g.rect.x),double(clip.x)}),r=std::min({x+1.,double(g.rect.x)+g.rect.width,double(clip.x)+clip.width});
 const double t=std::max({double(y),double(g.rect.y),double(clip.y)}),b=std::min({y+1.,double(g.rect.y)+g.rect.height,double(clip.y)+clip.height});
 if(r<=l||b<=t)return {};
 auto inside=[](double x,double y,p::Rect r,double radius){double cx=std::max(double(r.x)+radius,std::min(x,double(r.x)+r.width-radius)),cy=std::max(double(r.y)+radius,std::min(y,double(r.y)+r.height-radius));return !radius||(x-cx)*(x-cx)+(y-cy)*(y-cy)<=radius*radius;};
 double sum[4]{};
 for(int yy=0;yy<16;++yy)for(int xx=0;xx<16;++xx){double px=l+(xx+.5)*(r-l)/16,py=t+(yy+.5)*(b-t)/16;
 if(!inside(px,py,g.rect,g.radii.top_left_x)||!inside(px,py,clip,cr))continue;
 double z=.5+((px-g.gradient_rect.x-g.gradient_rect.width/2.)*g.direction_x+(py-g.gradient_rect.y-g.gradient_rect.height/2.)*g.direction_y)/(std::abs(g.direction_x)*g.gradient_rect.width+std::abs(g.direction_y)*g.gradient_rect.height);
 size_t a=0,k=0;while(k<g.stops.size()&&g.stops[k].offset<=z)++k;
 a=k?k-1:0;size_t end=k<g.stops.size()?k:g.stops.size()-1;double f=k==0||k==g.stops.size()?0:(z-g.stops[a].offset)/(g.stops[end].offset-g.stops[a].offset);
 auto ca=g.stops[a].color,cb=g.stops[end].color;double av[4]{ca.b*ca.a,ca.g*ca.a,ca.r*ca.a,ca.a},bv[4]{cb.b*cb.a,cb.g*cb.a,cb.r*cb.a,cb.a};for(int c=0;c<4;++c)sum[c]+=(av[c]+(bv[c]-av[c])*f)*g.opacity;
 }
 std::array<int,4> out{};for(int c=0;c<4;++c)out[c]=int(std::floor(std::clamp(sum[c]*(r-l)*(b-t)/256,0.,1.)*255+.5));for(int c=0;c<3;++c)out[c]=std::min(out[c],out[3]);return out;
}
static void differential(p::LinearGradientFill g,p::Rect clip={0,0,24,20},double cr=0){
 FrameStore store;ck(store.resize({24,20,1})==FrameStatus::ok);p::Radii radii{};radii={float(cr),float(cr),float(cr),float(cr),float(cr),float(cr),float(cr),float(cr)};
 ck(s::render(store,prepared({p::PushClip{clip,radii},g,p::PopClip{}})).status==s::Status::ok);auto f=frame(store);
 s::Draw d{};d.rect=s::intersect(s::rect(g.rect),s::rect(clip));d.original=s::rect(g.rect);d.radius=g.radii.top_left_x;d.gradient=&g;d.gradient_rect=s::rect(g.gradient_rect);d.clip={s::rect(clip),cr};
 size_t fast=0,slow=0;int maxdiff=0;
 for(int y=0;y<20;++y)for(int x=0;x<24;++x){auto b=s::intersect(d.rect,{double(x),double(y),x+1.,y+1.});bool quick=!s::empty(b)&&s::gradient_fast(d,b);if(!s::empty(b))(quick?fast:slow)++;
 auto want=legacy(g,x,y,clip,cr),got=pixel(f,x,y);for(int c=0;c<4;++c){maxdiff=std::max(maxdiff,std::abs(want[c]-got[c]));ck(std::abs(want[c]-got[c])<=(quick?1:0));}}
 std::cout<<"differential fast="<<fast<<" slow="<<slow<<" max_byte_difference="<<maxdiff<<'\n';
}
static void cache_budget_checks(){
 // Independent known cardinal geometry: 24 endpoint pixels slow,168 interior fast.
 // Only the new once-per-draw cache reservation differs from frozen C.
 FrameStore small;ck(small.resize({24,20,1})==FrameStatus::ok);
 auto g=gradient();constexpr std::uint64_t clear=480,old_draw=24*2080+168*41;
 auto empty=gradient({1000,1000,16,12});
 auto zero=s::render(small,prepared({empty}));ck(zero.status==s::Status::ok);ck(zero.pixel_budget==clear);
 auto one=s::render(small,prepared({g}));ck(one.status==s::Status::ok);ck(one.pixel_budget==clear+old_draw+4);
 auto two=s::render(small,prepared({g,empty,g}));ck(two.status==s::Status::ok);ck(two.pixel_budget==clear+2*(old_draw+4));
 // Empty after clipping also reserves no geometry cache.
 auto clipped=s::render(small,prepared({p::PushClip{{20,18,1,1},{}},g,p::PopClip{}}));ck(clipped.status==s::Status::ok);ck(clipped.pixel_budget==clear);
 // Fill-only preflight reaches 49,999,998 units exactly. The next nonempty
 // gradient must fail specifically on its 4-unit cache reserve, before pixels
 // or FrameStore candidate allocation. The same prefix plus an empty gradient
 // reaches the following event, avoiding expensive drawing without relying on
 // the production budget helper.
 std::size_t allocations=0;FrameStore store([&]{++allocations;});ck(store.resize({1280,800,1})==FrameStatus::ok);
 ck(s::render(store,prepared({gradient({0,0,2,2})})).status==s::Status::ok);auto saved=frame(store);
 std::vector<p::Command> commands;for(int i=0;i<47;++i)commands.push_back(p::Fill{{0,0,1280,800},{},{1,0,0,1}});
 commands.push_back(p::Fill{{0,0,1280,662},{},{1,0,0,1}});commands.push_back(p::Fill{{0,662,638,1},{},{1,0,0,1}});
 commands.push_back(gradient({0,0,1,1}));auto allocation_before=allocations;
 auto rejected=s::render(store,prepared(commands));ck(rejected.status==s::Status::limit_exceeded);ck(rejected.pixel_budget==49999998);ck(rejected.command_index==49);ck(allocations==allocation_before);
 auto kept=frame(store);ck(kept.sequence==saved.sequence&&kept.pixels==saved.pixels);
 // A cache-free empty draw at that same boundary proceeds to the next command.
 commands.back()=empty;commands.push_back(gradient({0,0,1,1}));allocation_before=allocations;
 rejected=s::render(store,prepared(commands));ck(rejected.status==s::Status::limit_exceeded);ck(rejected.pixel_budget==49999998);ck(rejected.command_index==50);ck(allocations==allocation_before);
 kept=frame(store);ck(kept.sequence==saved.sequence&&kept.pixels==saved.pixels);
 std::cout<<"cache_budget exact_per_draw=4 single="<<one.pixel_budget<<" two="<<two.pixel_budget<<" empty="<<zero.pixel_budget<<" cache_exhaustion_budget="<<rejected.pixel_budget<<" preallocation=PASS\n";
}
int main(){
 cache_budget_checks();
 for(auto d:std::array<std::array<float,2>,6>{{{1,0},{-1,0},{0,1},{0,-1},{.6f,.8f},{-.6f,-.8f}}}){auto g=gradient({1.25f,2.5f,16.5f,12.25f},d[0],d[1]);differential(g,{2.125f,3.375f,13.5f,10.5f});}
 auto g=gradient();g.gradient_rect={5,4,3,2};differential(g); // constant exterior tails
 g=gradient();g.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,1,0,1}},{std::nextafter(.5f,1.f),{0,0,1,1}},{1,{0,0,1,1}}};differential(g);
 g=gradient();g.stops.clear();for(int i=0;i<64;++i)g.stops.push_back({i/63.f,{i/63.f,1-i/63.f,.25f,1}});differential(g);
 g=gradient();g.radii={3,3,3,3,3,3,3,3};differential(g,{2.25f,2.5f,12,9},2);
 g=gradient();g.stops={{0,{1,0,0,0}},{1,{0,0,1,1}}};differential(g,{2.125f,3.375f,13.5f,10.5f});
 g=gradient();g.gradient_rect={0,0,1e-20f,1e-20f};differential(g); // tiny but representable extent
 g=gradient();g.gradient_rect={-65536,-65536,65536,65536};differential(g);
 // Exact frontier, duplicate and adjacent-stop classifier assertions.
 g=gradient();s::Draw d{};d.original=d.rect=s::rect(g.rect);d.gradient_rect=d.rect;d.gradient=&g;
 ck(!s::gradient_fast(d,{0,0,1,1}));ck(s::gradient_fast(d,{2,2,3,3}));
 g.stops={{0,{1,0,0,1}},{.5f,{1,0,0,1}},{.5f,{0,0,1,1}},{1,{0,0,1,1}}};ck(!s::gradient_fast(d,{7.5,2,8.5,3}));ck(!s::gradient_fast(d,{7,2,8,3}));
 for(auto dir:std::array<std::array<float,2>,2>{{{1,0},{.6f,.8f}}})for(auto size:std::array<std::array<int,2>,2>{{{200,200},{1280,800}}}){
 int w=size[0],h=size[1];FrameStore st;ck(st.resize({w,h,1})==FrameStatus::ok);g=gradient({0,0,float(w),float(h)},dir[0],dir[1]);auto result=s::render(st,prepared({g}));ck(result.status==s::Status::ok);ck(result.pixel_budget<50000000);auto f=frame(st);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){double z=.5+((x+.5-w/2.)*dir[0]+(y+.5-h/2.)*dir[1])/(w*std::abs(dir[0])+h*std::abs(dir[1]));auto p=pixel(f,x,y);ck(std::abs(p[0]-int(std::floor(z*255+.5)))<=1);ck(p[1]==0&&p[3]==255);ck(std::abs(p[2]-int(std::floor((1-z)*255+.5)))<=1);}
 std::cout<<"large "<<w<<'x'<<h<<" direction="<<dir[0]<<','<<dir[1]<<" budget="<<result.pixel_budget<<" bytes="<<f.pixels.size()<<" sequence="<<f.sequence<<'\n';
 }
 // Failure atomicity: both cancellation passes, OOM and preflight overpaint.
 size_t allocations=0;bool fail_alloc=false;FrameStore atomic([&]{++allocations;if(fail_alloc)throw std::bad_alloc();});ck(atomic.resize({24,20,1})==FrameStatus::ok);
 g=gradient();auto pf=prepared({g});Cancel count(1000000);ck(s::render(atomic,pf,&count).status==s::Status::ok);auto saved=frame(atomic);
 for(int at=1;at<=count.calls;++at){Cancel c(at);ck(s::render(atomic,pf,&c).status==s::Status::cancelled);auto kept=frame(atomic);ck(kept.sequence==saved.sequence&&kept.pixels==saved.pixels);}
 fail_alloc=true;ck(s::render(atomic,pf).status==s::Status::allocation_failed);fail_alloc=false;auto kept=frame(atomic);ck(kept.sequence==saved.sequence&&kept.pixels==saved.pixels);
 FrameStore excess([&]{++allocations;});ck(excess.resize({1280,800,1})==FrameStatus::ok);g=gradient({0,0,1280,800});ck(s::render(excess,prepared({g})).status==s::Status::ok);saved=frame(excess);auto before_allocations=allocations;
 auto rejected=s::render(excess,prepared({g,g}));ck(rejected.status==s::Status::limit_exceeded);ck(rejected.pixel_budget<=50000000);ck(allocations==before_allocations);kept=frame(excess);ck(kept.sequence==saved.sequence&&kept.pixels==saved.pixels);
 std::cout<<"atomicity cancellation_points="<<count.calls<<" oom=PASS overpaint_preallocation=PASS rejected_budget="<<rejected.pixel_budget<<'\n';
 std::cout<<"useful gradient checks="<<checks<<'\n';
}
