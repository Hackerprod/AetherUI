#ifdef NDEBUG
#error assertions required
#endif
#include <aether/aether.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <array>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include "../../native/src/rendering/native_frame.h"
#include "../software_render_smoke/png_writer.h"
namespace fs=std::filesystem;
static void write(const fs::path& p,const std::string& s){std::ofstream f(p,std::ios::binary);f.write(s.data(),s.size());assert(f.good());}
static std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static int invoke(std::vector<std::string> args){std::vector<char*> argv;for(auto& s:args)argv.push_back(s.data());argv.push_back(nullptr);pid_t pid=fork();assert(pid>=0);if(!pid){execv(argv[0],argv.data());_exit(127);}int status{};assert(waitpid(pid,&status,0)==pid);return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);}
struct Pixels{int32_t w{},h{};uint8_t* p{};explicit Pixels(const fs::path& path){auto s=read(path);assert(aether_image_decode(reinterpret_cast<const uint8_t*>(s.data()),s.size(),&w,&h,&p)==AETHER_OK);}~Pixels(){aether_image_pixels_free(p);}void at(int x,int y,std::array<int,4> expected){assert(x>=0&&x<w&&y>=0&&y<h);for(int c=0;c<4;++c)if(p[(y*w+x)*4+c]!=expected[c]){std::cerr<<"pixel mismatch "<<x<<","<<y<<","<<c<<" actual="<<int(p[(y*w+x)*4+c])<<" expected="<<expected[c]<<"\n";std::abort();}}};
int main(int argc,char**argv){assert(argc==3);const std::string bin=fs::absolute(argv[1]);const fs::path root=fs::absolute(argv[2]);assert(!fs::exists(root));fs::create_directories(root/"assets");
 auto png=smoke_png::png(smoke_png::Bytes{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255},2,2);write(root/"assets/checker.png",{reinterpret_cast<const char*>(png.data()),png.size()});
 const std::vector<std::string> catalog{"--image-root",(root/"assets").string(),"--image","hero","checker.png"};size_t count{};
 auto run=[&](const std::string& name,const std::string& value,const std::string& extra,bool ok,const std::vector<std::string>& options,const std::string& body="<div id='a'></div>"){
  const auto input=root/(name+".html"),output=root/name;
  write(input,"<html><head><style>body{margin:0;background-color:transparent}#a{display:block;width:4px;height:4px;background-image:"+value+";"+extra+"}</style></head><body>"+body+"</body></html>");
  std::vector<std::string> cmd{bin,"--geometry",input.string(),output.string(),"8","8","1"};cmd.insert(cmd.end(),options.begin(),options.end());int rc=invoke(cmd);if((rc==0)!=ok)std::cerr<<name<<" exit="<<rc<<"\n"<<read(output/"report.log");assert((rc==0)==ok);assert(fs::exists(output/"aether.png")==ok);assert(!fs::exists(output/"aether.png.pending"));
  const auto report=read(output/"report.log");if(ok||rc!=2){assert(report.find("process_wall_ms=")!=std::string::npos);assert(report.find("child_cpu_seconds=")!=std::string::npos);assert(report.find("child_peak_rss_kib=")!=std::string::npos);assert(report.find("chromium=NOT_RUN")!=std::string::npos);}
  if(ok){for(const char* metric:{"image_entries=", "image_encoded_bytes=", "image_retained_bgra_bytes=", "image_read_wall_ms=", "image_decode_wall_ms=", "image_copy_wall_ms=", "snapshot_wall_ms=", "prepare_wall_ms=", "composition_ns=", "pixel_budget=", "curved_samples="})assert(report.find(metric)!=std::string::npos);}
  assert(report.find((root/"assets").string())==std::string::npos);++count;return output;
 };
 auto good=[&](std::string n,std::string extra){return run(n,"url(hero)",extra,true,catalog);};
 auto a=good("no-repeat","background-repeat:no-repeat");Pixels(a/"aether.png").at(0,0,{0,0,255,255});Pixels(a/"aether.png").at(1,0,{0,255,0,255});Pixels(a/"aether.png").at(0,1,{255,0,0,255});Pixels(a/"aether.png").at(3,3,{0,0,0,0});
 auto b=good("repeat","background-repeat:repeat");Pixels(b/"aether.png").at(2,0,{0,0,255,255});Pixels(b/"aether.png").at(3,3,{255,255,255,255});
 auto c=good("scaled","background-repeat:no-repeat;background-size:4px 4px");Pixels(c/"aether.png").at(1,0,{0,64,191,255});
 auto d=good("opacity","background-repeat:no-repeat;opacity:.5");Pixels(d/"aether.png").at(0,0,{0,0,128,128});
 auto e=good("translated","background-repeat:no-repeat;transform:translate(2px,2px)");Pixels(e/"aether.png").at(2,2,{0,0,255,255});Pixels(e/"aether.png").at(0,0,{0,0,0,0});
 auto f=good("rounded","background-repeat:repeat;border-radius:2px");Pixels(f/"aether.png").at(1,1,{255,255,255,255});
 auto overflow=good("overflow-image","background-repeat:repeat;overflow:hidden");Pixels(overflow/"aether.png").at(2,0,{0,0,255,255});
 auto nested=run("nested-overflow","url(hero)","background-repeat:repeat;overflow:hidden;transform:translate(1px,1px);opacity:.5}#w{width:3px;height:3px;overflow:hidden",true,catalog,"<div id='w'><div id='a'></div></div>");Pixels(nested/"aether.png").at(1,1,{0,0,128,128});Pixels(nested/"aether.png").at(3,3,{0,0,0,0});
 auto inline_ok=run("inline","none","",true,catalog,"<div id='a' style='background-image:url(hero);background-repeat:no-repeat'></div>");Pixels(inline_ok/"aether.png").at(0,0,{0,0,255,255});
 auto again=good("deterministic","background-repeat:no-repeat");assert(read(a/"aether.png")==read(again/"aether.png"));
 for(const auto& v:std::vector<std::string>{"url(missing)","url('hero')","url(\"hero\")","url(h\\65ro)","url(hero),url(hero)","url(hero) garbage","var(--asset)","image-set(url(hero) 1x)"})run("css-reject-"+std::to_string(count),v,"background-repeat:no-repeat",false,catalog);
 run("no-opt-in","url(hero)","",false,{});
 run("shorthand","none","background:url(hero)",false,catalog);
 run("custom-property","none","--asset:url(hero)",false,catalog);
 run("other-property","none","cursor:url(hero)",false,catalog);
 run("unknown-property","none","unknown:url(hero)",false,catalog);
 run("keyframe","none","} @keyframes k {from{background-image:url(hero)}to{background-image:none}} #a{animation:k 1s",false,catalog);
 run("import","none","} @import url(hero); #a{color:red",false,catalog);
 run("img-element","none","",false,catalog,"<img src='hero'>");
 write(root/"assets/empty","");write(root/"assets/corrupt","not an image");write(root/"assets/truncated",{reinterpret_cast<const char*>(png.data()),20});
 fs::create_symlink("checker.png",root/"assets/link");fs::create_directory(root/"assets/directory");assert(mkfifo((root/"assets/fifo").c_str(),0600)==0);
 for(const auto& leaf:{"empty","corrupt","truncated","link","directory","fifo","missing","../checker.png","/checker.png"})run("file-reject-"+std::to_string(count),"url(hero)","",false,{"--image-root",(root/"assets").string(),"--image","hero",leaf});
 fs::create_directory_symlink(root/"assets",root/"linked-assets");run("root-symlink","url(hero)","",false,{"--image-root",(root/"linked-assets").string(),"--image","hero","checker.png"});
 fs::create_directory(root/"container");fs::create_directory_symlink(root,root/"container/ancestor");run("ancestor-symlink","url(hero)","",false,{"--image-root",(root/"container/ancestor/assets").string(),"--image","hero","checker.png"});
 run("duplicate","url(hero)","",false,{"--image-root",(root/"assets").string(),"--image","hero","checker.png","--image","hero","checker.png"});
 run("no-root","url(hero)","",false,{"--image","hero","checker.png"});run("no-entry","none","",false,{"--image-root",(root/"assets").string()});
 run("invalid-alias","url(hero)","",false,{"--image-root",(root/"assets").string(),"--image","bad.alias","checker.png"});
 std::vector<std::string> many{"--image-root",(root/"assets").string()};for(int i=0;i<65;++i){many.insert(many.end(),{"--image","a"+std::to_string(i),"checker.png"});}run("entry-limit","none","",false,many);
 std::ofstream large(root/"assets/large",std::ios::binary);large.seekp(16*1024*1024);large.put('x');large.close();run("encoded-limit","url(hero)","",false,{"--image-root",(root/"assets").string(),"--image","hero","large"});
 std::ofstream padded(root/"assets/padded.png",std::ios::binary);padded.write(reinterpret_cast<const char*>(png.data()),png.size());padded.seekp(16*1024*1024-1);padded.put('x');padded.close();
 std::vector<std::string> aggregate{"--image-root",(root/"assets").string(),"--image","hero","padded.png","--image","second","padded.png"};
 auto encoded_ok=run("encoded-boundary","url(hero)","",true,aggregate);assert(read(encoded_ok/"report.log").find("image_encoded_bytes=33554432")!=std::string::npos);
 aggregate.insert(aggregate.end(),{"--image","third","padded.png"});auto encoded_bad=run("encoded-total","url(hero)","",false,aggregate);assert(read(encoded_bad/"report.log").find("reason=IMAGE_ENCODED_LIMIT")!=std::string::npos);
 fs::copy_file("tests/isolated_capture/fixtures/image_catalog/transparent-4096.png",root/"assets/retained.png");
 std::vector<std::string> retained{"--image-root",(root/"assets").string(),"--image","large","retained.png"};auto retained_ok=run("retained-boundary","none","",true,retained);assert(read(retained_ok/"report.log").find("image_retained_bgra_bytes=67108864")!=std::string::npos);
 retained.insert(retained.end(),{"--image","hero","checker.png"});auto retained_bad=run("retained-total","none","",false,retained);assert(read(retained_bad/"report.log").find("reason=IMAGE_RETAINED_LIMIT")!=std::string::npos);
 std::cout<<"image_catalog_cli_cases="<<count<<"\npixel_oracles=PASS\nreject_no_png=PASS\ndeterministic_png=PASS\nmetrics_presence=PASS\nchromium=NOT_RUN\n";
}
