// HTML geometry is exercised directly; the oracle uses the unclipped tile.
#define main positive_bridge_main
#include "test_html_image_bridge.cpp"
#undef main
#include <cmath>
#include <algorithm>
int main(int argc,char**argv){
 const std::string output=argc>1?argv[1]:"/tmp";
 int cases=0,channels=0;
 for(auto dims:{std::pair{3,7},std::pair{5,11},std::pair{17,13},std::pair{4,8}}){
  const int n=dims.first,tile=dims.second;std::vector<std::uint8_t> pixels(n*n*4);
  for(int y=0;y<n;++y)for(int x=0;x<n;++x){const int i=(y*n+x)*4;pixels[i]=(x*37+y*13)%180;pixels[i+1]=(x*11+y*47)%180;pixels[i+2]=(x*61+y*19)%180;pixels[i+3]=200;}
  auto ref=std::get<p::ImageRef>(p::ImageResource::copy(n,n,pixels));
  for(bool dyadic:{false,true}){
   const std::string position=dyadic?"50%":"33.333333333333333%";
   Html h("background-repeat:no-repeat;background-size:"+std::to_string(tile)+"px "+std::to_string(tile)+"px;background-position:"+position+" "+position+";");
   NativeImageCatalog catalog{{"hero",ref}};std::unique_ptr<NativePaintList> list;
   assert(build_native_opacity(h.builder,h.layout,h.styles,nullptr,NativeScrollInput{},catalog,list)==AETHER_OK);
   auto snapshot=p::snapshot_native_opacity(*list);assert(std::holds_alternative<p::Snapshot>(snapshot));
   int images=0;for(const auto& c:std::get<p::Snapshot>(snapshot).commands)if(auto im=std::get_if<p::Image>(&c)){assert(im->sampling_bounds==p::ImageSamplingBounds::FullResource);++images;}assert(images==1);
   auto ready=p::prepare(std::get<p::Snapshot>(snapshot));assert(std::holds_alternative<p::PreparedFrame>(ready));
   FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);assert(s::render(store,std::get<p::PreparedFrame>(ready)).status==s::Status::ok);auto f=frame(store);
   const long double position_px=(4.L-tile)*(dyadic?.5L:1.L/3.L);
   for(int y=0;y<8;++y)for(int x=0;x<8;++x)for(int c=0;c<4;++c){
    int expected=0;
    if(x<4&&y<4){long double u=std::clamp((x+.5L-position_px)/tile*n-.5L,0.L,(long double)n-1),v=std::clamp((y+.5L-position_px)/tile*n-.5L,0.L,(long double)n-1);int x0=std::floor(u),y0=std::floor(v),x1=std::min(n-1,x0+1),y1=std::min(n-1,y0+1);long double fx=u-x0,fy=v-y0;
     auto at=[&](int xx,int yy){return pixels[(yy*n+xx)*4+c];};
     expected=std::floor((at(x0,y0)*(1-fx)*(1-fy)+at(x1,y0)*fx*(1-fy)+at(x0,y1)*(1-fx)*fy+at(x1,y1)*fx*fy)+.5L);
    }
    int actual=f.pixels[y*f.stride+x*4+c];int tolerance=(x>=4||y>=4||(dyadic&&n==4&&tile==8))?0:1;
    assert(std::abs(actual-expected)<=tolerance);++channels;
   }++cases;
  }
 }
 // Asymmetric cover/contain plus fractional clipping under common effects.
 for(auto dims:{std::pair{3,2},std::pair{5,3}})for(bool cover:{false,true})for(float pos:{.25f,.75f})for(int effect=0;effect<4;++effect){
  int w=dims.first,h=dims.second;std::vector<std::uint8_t> bytes(w*h*4);for(int i=0;i<w*h;++i){bytes[i*4]=i*7%128;bytes[i*4+1]=i*11%128;bytes[i*4+2]=i*17%128;bytes[i*4+3]=128;}
  auto ref=std::get<p::ImageRef>(p::ImageResource::copy(w,h,bytes));
  std::string css=std::string("background-repeat:no-repeat;background-size:")+(cover?"cover":"contain")+";background-position:"+(pos==.25f?"25%":"75%")+" "+(pos==.25f?"25%":"75%")+";";
  if(effect==1)css+="opacity:.5;";if(effect==2)css+="transform:translate(1px,1px);";if(effect==3)css+="border-radius:1px;";
  Html html(css);NativeImageCatalog catalog{{"hero",ref}};std::unique_ptr<NativePaintList> list;
  assert(build_native_opacity(html.builder,html.layout,html.styles,nullptr,catalog,list)==AETHER_OK);
  auto snapshot=p::snapshot_native_opacity(*list);assert(std::holds_alternative<p::Snapshot>(snapshot));auto ready=p::prepare(std::get<p::Snapshot>(snapshot));assert(std::holds_alternative<p::PreparedFrame>(ready));
  FrameStore store;assert(store.resize({8,8,1})==FrameStatus::ok);assert(s::render(store,std::get<p::PreparedFrame>(ready)).status==s::Status::ok);auto actual=frame(store);
  if(w==3&&h==2&&cover&&pos==.25f&&effect==0){auto png=smoke_png::png(smoke_png::rgba(actual),8,8);std::ofstream file(output+"/f2-cover-aether.png",std::ios::binary);file.write(reinterpret_cast<const char*>(png.data()),png.size());assert(file.good());std::cout<<"artifact=f2-cover-aether.png source=3x2 viewport=8x8 box=4x4 background_size=cover position=25percent sampling=FullResource Chromium=NOT_RUN\n";}
  const float scale=cover?std::max(4.f/w,4.f/h):std::min(4.f/w,4.f/h);float tw=w*scale,th=h*scale;
  p::Image whole{ref,{0,0,float(w),float(h)},{(4-tw)*pos,(4-th)*pos,tw,th},1,p::ImageSamplingBounds::FullResource};
  std::vector<p::Command> cmds;if(effect==1)cmds.push_back(p::BeginOpacity{1,0,.5});if(effect==2){auto m=p::identity;m.dx=1;m.dy=1;cmds.push_back(p::PushTransform{m});}
  p::Radii rad{};if(effect==3)rad={1,1,1,1,1,1,1,1};cmds.push_back(p::PushClip{{0,0,4,4},rad});cmds.push_back(whole);cmds.push_back(p::PopClip{});if(effect==2)cmds.push_back(p::PopTransform{});if(effect==1)cmds.push_back(p::EndOpacity{1});
  auto oracle=p::prepare(p::Snapshot{std::move(cmds)});assert(std::holds_alternative<p::PreparedFrame>(oracle));assert(s::render(store,std::get<p::PreparedFrame>(oracle)).status==s::Status::ok);auto expected=frame(store);
  for(std::size_t i=0;i<actual.pixels.size();++i){assert(std::abs(int(actual.pixels[i])-int(expected.pixels[i]))<=1);++channels;}
  const int offset=effect==2?1:0;for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(x<offset||y<offset||x>=offset+4||y>=offset+4)for(int c=0;c<4;++c)assert(actual.pixels[y*actual.stride+x*4+c]==0);
  ++cases;
 }
 // Repeat with fractional boundary crops, and true scroll-container integration.
 {
  std::vector<std::uint8_t> bytes(36);for(int i=0;i<9;++i){bytes[i*4]=i*7;bytes[i*4+1]=i*11;bytes[i*4+2]=i*13;bytes[i*4+3]=128;}
  auto ref=std::get<p::ImageRef>(p::ImageResource::copy(3,3,bytes));NativeImageCatalog cat{{"hero",ref}};
  auto compare=[&](NativePaintList& list,std::vector<p::Command> commands){auto snap=p::snapshot_native_opacity(list);assert(std::holds_alternative<p::Snapshot>(snap));auto actual=p::prepare(std::get<p::Snapshot>(snap)),expected=p::prepare(p::Snapshot{std::move(commands)});assert(std::holds_alternative<p::PreparedFrame>(actual)&&std::holds_alternative<p::PreparedFrame>(expected));FrameStore st;assert(st.resize({8,8,1})==FrameStatus::ok);assert(s::render(st,std::get<p::PreparedFrame>(actual)).status==s::Status::ok);auto a=frame(st);assert(s::render(st,std::get<p::PreparedFrame>(expected)).status==s::Status::ok);auto b=frame(st);for(std::size_t i=0;i<a.pixels.size();++i){assert(std::abs(int(a.pixels[i])-int(b.pixels[i]))<=1);++channels;}for(int y=0;y<8;++y)for(int x=0;x<8;++x)if(b.pixels[y*b.stride+x*4+3]==0)for(int c=0;c<4;++c)assert(a.pixels[y*a.stride+x*4+c]==0);++cases;};
  Html repeat("background-repeat:repeat;background-size:3px 3px;background-position:-.5px -.5px;");std::unique_ptr<NativePaintList> list;
  assert(build_native_opacity(repeat.builder,repeat.layout,repeat.styles,nullptr,cat,list)==AETHER_OK);assert(list->images.size()==4);
  std::vector<p::Command> commands{p::PushClip{{0,0,4,4},{}}};for(float y:{-.5f,2.5f})for(float x:{-.5f,2.5f})commands.push_back(p::Image{ref,{0,0,3,3},{x,y,3,3},1,p::ImageSamplingBounds::FullResource});commands.push_back(p::PopClip{});compare(*list,std::move(commands));
  for(bool round:{false,true}){
   Html repeated(std::string("background-repeat:")+(round?"round":"space")+";background-size:1.5px 1.5px;background-position:25% 75%;");
   assert(build_native_opacity(repeated.builder,repeated.layout,repeated.styles,nullptr,cat,list)==AETHER_OK);
   const int count=round?3:2;const float side=round?4.f/3:1.5f;const float step=round?side:2.5f;
   std::vector<p::Command> expected{p::PushClip{{0,0,4,4},{}}};
   for(int y=0;y<count;++y)for(int x=0;x<count;++x)expected.push_back(p::Image{ref,{0,0,3,3},{x*step,y*step,side,side},1,p::ImageSamplingBounds::FullResource});expected.push_back(p::PopClip{});compare(*list,std::move(expected));
  }
  auto front_bytes=bytes;for(int i=0;i<9;++i){front_bytes[i*4]=0;front_bytes[i*4+1]=0;front_bytes[i*4+2]=128;front_bytes[i*4+3]=128;}
  auto front=std::get<p::ImageRef>(p::ImageResource::copy(3,3,front_bytes));NativeImageCatalog layers{{"hero",ref},{"front",front}};
  Html layered("background-image:url(front),url(hero);background-repeat:repeat;background-size:3px 3px;background-position:-.5px -.5px;");
  assert(build_native_opacity(layered.builder,layered.layout,layered.styles,nullptr,layers,list)==AETHER_OK);
  std::vector<p::Command> layered_oracle{p::PushClip{{0,0,4,4},{}}};for(const auto& image:{ref,front})for(float y:{-.5f,2.5f})for(float x:{-.5f,2.5f})layered_oracle.push_back(p::Image{image,{0,0,3,3},{x,y,3,3},1,p::ImageSamplingBounds::FullResource});layered_oracle.push_back(p::PopClip{});compare(*list,std::move(layered_oracle));
  auto* old=list.get();Html tiny("background-repeat:repeat;background-size:4px 4px;background-position:-.5px -.5px;");assert(build_native_opacity(tiny.builder,tiny.layout,tiny.styles,nullptr,cat,list)!=AETHER_OK);assert(list.get()==old);
  Html scroll("background-repeat:no-repeat;background-size:7px 7px;background-position:33.333333333333333% 33.333333333333333%;","<body><div id='outer'><div id='image'></div></div></body>","#outer{display:block;width:3px;height:3px;overflow:hidden;}");
  assert(build_native_opacity(scroll.builder,scroll.layout,scroll.styles,nullptr,cat,list)==AETHER_OK);assert(list->scroll.size()==1);auto id=list->scroll[0].id;
  for(bool shifted:{false,true}){double dx=shifted?.5:0,dy=shifted?.25:0;if(shifted)assert(build_native_opacity(scroll.builder,scroll.layout,scroll.styles,nullptr,NativeScrollInput{{id,dx,dy}},cat,list)==AETHER_OK);
   compare(*list,{p::PushClip{{0,0,3,3},{}},p::PushScroll{id,dx,dy},p::PushClip{{0,0,4,4},{}},p::Image{ref,{0,0,3,3},{-1,-1,7,7},1,p::ImageSamplingBounds::FullResource},p::PopClip{},p::PopScroll{},p::PopClip{}});
  }
 }
 std::cout<<"HTML fractional PASS cases="<<cases<<" channels="<<channels<<" exterior=exact_zero tolerance=1 non_binary Chromium=NOT_RUN\n";
}
