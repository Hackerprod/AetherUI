#include "aether_visual_compare/native_capture.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
using namespace avc;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
static std::string read(std::string p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 CHECK(argc==3);std::string root=argv[1],out=argv[2];Request q;q.backend="native-html-ltr";q.profile=2;q.regular_font="fonts/DejaVuSans.ttf";q.bold_font="fonts/DejaVuSans-Bold.ttf";q.viewport={480,320,4,480,320};
 ValidatedInputs in;in.html=read(root+"/profile2-static-controls.html");
 for(auto p:{q.regular_font,q.bold_font}){ValidatedResource a;a.resource.path=p;a.resource.mime="font/ttf";a.resource.role="font";a.bytes=read(root+"/"+p);in.assets.push_back(a);}
 auto run=[&](){return capture_native(q,in,{});};auto c=run();if(!c.captured)std::cerr<<c.reason_code<<" status="<<c.public_status<<'\n';CHECK(c.captured);CHECK(c.limitations==15&&c.sequence==1);CHECK(run().png==c.png);
 std::ofstream(out+"/profile2.png",std::ios::binary)<<c.png;
 q.profile=1;in.html=read(root+"/profile1-transparent.html");c=run();CHECK(c.captured&&c.limitations==3);std::ofstream(out+"/profile1.png",std::ios::binary)<<c.png;
 for(auto scale:{5u,6u,8u}){q.viewport.scale_quarters=scale;CHECK(run().reason_code=="NATIVE_UNSUPPORTED");}q.viewport.scale_quarters=4;
 q.viewport.width=2049;CHECK(run().reason_code=="NATIVE_LIMIT_EXCEEDED");q.viewport.width=480;
 auto saved=in.html;in.html=std::string(65537,'x');CHECK(run().reason_code=="NATIVE_LIMIT_EXCEEDED");in.html="<html><body><p>\xc0\xaf</p></body></html>";CHECK(!run().captured);in.html=saved;
 q.bold_font=q.regular_font;CHECK(run().reason_code=="NATIVE_FONT_SELECTION");q.bold_font="fonts/DejaVuSans-Bold.ttf";
 in.assets.push_back(in.assets.back());CHECK(!run().captured);in.assets.pop_back();in.assets.back().resource.role="image";CHECK(run().reason_code=="NATIVE_RESOURCE_UNSUPPORTED");in.assets.back().resource.role="font";
 auto saved_font=in.assets.back().bytes;in.assets.back().bytes="not a TTF";CHECK(!run().captured);in.assets.back().bytes=saved_font;
 in.assets.back().bytes=std::string(16u*1024u*1024u,'x');CHECK(run().reason_code=="NATIVE_LIMIT_EXCEEDED");in.assets.back().bytes=std::move(saved_font);
 q.profile=3;CHECK(run().reason_code=="NATIVE_INVALID_ARGUMENT");q.profile=1;
 q.regular_font="missing.ttf";CHECK(run().reason_code=="NATIVE_FONT_SELECTION");q.regular_font="fonts/DejaVuSans.ttf";
 for(int cancel_at=1;cancel_at<=5;++cancel_at){int calls=0;auto cancelled=capture_native(q,in,{[&]{return ++calls>=cancel_at;}});CHECK(!cancelled.captured&&cancelled.png.empty()&&cancelled.exit_code==12);}
 std::uint8_t pixels[]={10,20,30,255, 0,32,64,128, 7,8,9,0, 1,1,1,1};auto png=encode_native_png(4,1,pixels);std::ofstream(out+"/channels.png",std::ios::binary)<<png;
 bool threw=false;try{encode_native_png(0,1,pixels);}catch(...){threw=true;}CHECK(threw);
 std::cout<<"PASS native capture profiles, deterministic PNG, bounds, unsupported scale, resource selection, UTF-8, cancellation and encoder\n";
}
