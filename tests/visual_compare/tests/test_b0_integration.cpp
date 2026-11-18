#include "aether_visual_compare/native_capture.hpp"
#include "aether_visual_compare/artifact_store.hpp"
#include "aether_visual_compare/report.hpp"
#include "sha256.hpp"
#include <fstream>
#include <iostream>
using namespace avc;
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
static std::string read(const std::string& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char** argv){
 CHECK(argc==3);std::string root=argv[1],evidence=argv[2];
 Request q;q.command=Command::capture;q.backend="native-b0-source";q.profile=0;q.regular_font="fonts/DejaVuSans.ttf";q.bold_font="fonts/DejaVuSans-Bold.ttf";q.viewport={480,640,4,480,640};
 ValidatedInputs in;in.html=read(root+"/native/tests/fixtures/b0/b2_inline_wrap_page.html");CHECK(!in.html.empty());
 for(const auto& p:{q.regular_font,q.bold_font}){ValidatedResource a;a.resource.path=p;a.resource.mime="font/ttf";a.resource.role="font";a.bytes=read(root+"/native/tests/fixtures/"+p);CHECK(!a.bytes.empty());a.resource.sha256=sha256(a.bytes);in.assets.push_back(a);}
 int samples=0;auto c=capture_b0(q,in,{[&]{++samples;return false;}});CHECK(c.captured&&c.metadata.sequence==1&&samples>=8);
 for(int n=1;n<=samples;++n){int calls=0;auto fail=capture_b0(q,in,{[&]{return ++calls>=n;}});CHECK(!fail.captured&&fail.png.empty()&&fail.exit_code==12);}
 auto html=in.html;in.html=std::string(65537,'x');auto fail=capture_b0(q,in,{});CHECK(fail.exit_code==8&&!fail.metadata.public_status);in.html=html;
 q.viewport.scale_quarters=5;fail=capture_b0(q,in,{});CHECK(fail.exit_code==10&&!fail.metadata.public_status);q.viewport.scale_quarters=4;
 for(const auto& name:{"b0-full","b1-full","b2-full","image","shadow","gradient","corrupt-font"}){
  auto path=evidence+"/"+name+"/";auto m=read(path+"manifest.json"),j=read(path+"report.json"),l=read(path+"report.log"),p=read(path+"aether.png");
  CHECK(!m.empty()&&!j.empty()&&!l.empty());CHECK(verify_committed(m,j,l,p));
  auto altered=j;auto pos=altered.find("report/3");CHECK(pos!=std::string::npos);altered.replace(pos,8,"report/2");CHECK(!verify_committed(m,altered,l,p));
  altered=m;pos=altered.find("artifacts/3");CHECK(pos!=std::string::npos);altered.replace(pos,11,"artifacts/2");CHECK(!verify_committed(altered,j,l,p));
  altered=j;pos=altered.find("native_b0_source_capture");CHECK(pos!=std::string::npos);altered.replace(pos,24,"native_profile_capture");CHECK(!verify_committed(m,altered,l,p));
  altered=m;pos=altered.find("\"run_id\":\"");CHECK(pos!=std::string::npos);pos+=10;altered[pos]=altered[pos]=='0'?'1':'0';CHECK(!verify_committed(altered,j,l,p));
  CHECK(!verify_committed(m,j,l+"tamper",p));
  if(!p.empty()){p[p.size()/2]^=1;CHECK(!verify_committed(m,j,l,p));}
 }
 std::cout<<"PASS B0 cancellation at "<<samples<<" samples, pre-ABI absence, committed v3 verification and schema/scope/run_id/report/log/PNG tamper rejection\n";
}
