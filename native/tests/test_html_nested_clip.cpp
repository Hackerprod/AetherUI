#define main existing_bridge_main
#include "test_html_image_bridge.cpp"
#undef main
#include <cmath>
#include <algorithm>
struct Interval{long double lo,hi;};
static Interval vertical(long double l,long double t,long double r,long double b,long double radius,long double x){
 if(x<l||x>r)return {1,0};const long double dx=std::max({l+radius-x,0.L,x-(r-radius)});
 const long double inset=radius-std::sqrt(std::max(0.L,radius*radius-dx*dx));return {t+inset,b-inset};
}
static long double coverage(int x,int y,int n){
 long double sum=0;for(int i=0;i<n;++i){long double u=x+(i+.5L)/n;auto a=vertical(0,0,6,6,2.5L,u),b=vertical(1,0,8,7,3,u);
 sum+=std::max(0.L,std::min({(long double)y+1,a.hi,b.hi})-std::max({(long double)y,a.lo,b.lo}));}return sum/n;
}
int main(int argc,char**argv){const std::string output=argc>1?argv[1]:"/tmp";
 const std::array<std::uint8_t,16> pixels{0,0,128,128,0,128,0,128,128,0,0,128,64,64,64,128};
 auto ref=std::get<p::ImageRef>(p::ImageResource::copy(2,2,pixels));
 Html html("width:7px;height:7px;margin-left:1px;border-radius:3px;background-repeat:no-repeat;background-size:7px 7px;","<body><div id='outer'><div id='image'></div></div></body>","#outer{display:block;width:6px;height:6px;overflow:hidden;border-radius:2.5px;}");
 NativeImageCatalog catalog{{"hero",ref}};std::unique_ptr<NativePaintList> list;
 assert(build_native_opacity(html.builder,html.layout,html.styles,nullptr,catalog,list)==AETHER_OK);
 auto snap=p::snapshot_native_opacity(*list);assert(std::holds_alternative<p::Snapshot>(snap));
 int depth=0,max_depth=0;std::vector<bool> stack;for(const auto& c:std::get<p::Snapshot>(snap).commands){if(auto clip=std::get_if<p::PushClip>(&c)){bool curve=clip->radii.top_left_x>0;stack.push_back(curve);depth+=curve;max_depth=std::max(max_depth,depth);}else if(std::holds_alternative<p::PopClip>(c)){depth-=stack.back();stack.pop_back();}}assert(depth==0&&max_depth>=2);
 auto prepared=p::prepare(std::get<p::Snapshot>(snap));assert(std::holds_alternative<p::PreparedFrame>(prepared));FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);
 auto result=s::render(store,std::get<p::PreparedFrame>(prepared));assert(result.status==s::Status::ok&&result.max_clip_depth>=2);auto f=frame(store);
 long double worst=0,conv=0;for(int y=0;y<8;++y)for(int x=0;x<8;++x){long double a=coverage(x,y,4096),b=coverage(x,y,8192);conv=std::max(conv,std::abs(a-b));
 long double u=std::clamp((x+.5L-1)/7*2-.5L,0.L,1.L),v=std::clamp((y+.5L)/7*2-.5L,0.L,1.L);
 for(int c=0;c<4;++c){long double sample=pixels[c]*(1-u)*(1-v)+pixels[4+c]*u*(1-v)+pixels[8+c]*(1-u)*v+pixels[12+c]*u*v;worst=std::max(worst,std::abs(f.pixels[y*f.stride+x*4+c]/255.L-sample/255*b));if(x<1||x>=6||y>=6)assert(f.pixels[y*f.stride+x*4+c]==0);}
 for(int c=0;c<3;++c)assert(f.pixels[y*f.stride+x*4+c]<=f.pixels[y*f.stride+x*4+3]);
 }assert(conv<.1L/255&&worst<=16.L/255);
 auto png=smoke_png::png(smoke_png::rgba(f),8,8);std::ofstream file(output+"/nested-html-aether.png",std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),png.size());assert(file.good());
 std::cout<<"N1 HTML nested clip PASS max_error="<<double(worst)<<" convergence="<<double(conv)<<" depth="<<result.max_clip_depth<<" nodes="<<result.clip_node_count<<" central_tests="<<result.clip_central_tests<<" contains_tests="<<result.clip_contains_tests<<" pixel_budget="<<result.pixel_budget<<" composition_ns="<<result.composition_ns<<" Chromium=NOT_RUN\n";
}
