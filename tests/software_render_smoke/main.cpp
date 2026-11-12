// Headless R12 fixture, not HTML/Chromium validation. Original PNG encoder uses
// only uncompressed DEFLATE blocks; decode verification uses existing stb_image.
#include "../../native/src/rendering/software/software_renderer.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../../native/third_party/stb_image.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef AETHER_BUILD_REV
#define AETHER_BUILD_REV "unknown"
#endif
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
using Bytes=std::vector<std::uint8_t>;
void be(Bytes& b,std::uint32_t n) {for(int i=24;i>=0;i-=8)b.push_back(std::uint8_t(n>>i));}
std::uint32_t crc(const std::uint8_t* p,std::size_t n) {
 std::uint32_t c=~0U;for(std::size_t i=0;i<n;++i){c^=p[i];for(int k=0;k<8;++k)c=(c>>1)^((c&1)?0xedb88320U:0U);}return ~c;
}
void chunk(Bytes& out,const char* kind,const Bytes& data) {
 be(out,std::uint32_t(data.size()));auto start=out.size();out.insert(out.end(),kind,kind+4);
 out.insert(out.end(),data.begin(),data.end());be(out,crc(out.data()+start,data.size()+4));
}
Bytes rgba(const aether::rendering::Frame& f) {
 Bytes b;b.reserve(f.pixels.size());
 for(std::size_t i=0;i<f.pixels.size();i+=4) {
  const unsigned a=f.pixels[i+3];
  for(int c:{2,1,0}) b.push_back(std::uint8_t(a?std::min(255U,(unsigned(f.pixels[i+std::size_t(c)])*255+a/2)/a):0));
  b.push_back(std::uint8_t(a));
 }return b;
}
Bytes png(const Bytes& pixels,int width,int height) {
 Bytes raw;for(int y=0;y<height;++y){raw.push_back(0);auto begin=pixels.begin()+std::size_t(y)*std::size_t(width)*4;raw.insert(raw.end(),begin,begin+width*4);}
 Bytes z{0x78,0x01};for(std::size_t offset=0;offset<raw.size();) {
  const auto n=std::min(std::size_t(65535),raw.size()-offset);z.push_back(offset+n==raw.size()?1:0);
  const auto len=std::uint16_t(n),inv=std::uint16_t(~len);
  z.push_back(std::uint8_t(len));z.push_back(std::uint8_t(len>>8));z.push_back(std::uint8_t(inv));z.push_back(std::uint8_t(inv>>8));
  z.insert(z.end(),raw.begin()+offset,raw.begin()+offset+n);offset+=n;
 }
 std::uint32_t a=1,b=0;for(auto c:raw){a=(a+c)%65521;b=(b+a)%65521;}be(z,(b<<16)|a);
 Bytes out{137,80,78,71,13,10,26,10},head;be(head,std::uint32_t(width));be(head,std::uint32_t(height));
 head.insert(head.end(),{8,6,0,0,0});chunk(out,"IHDR",head);chunk(out,"IDAT",z);chunk(out,"IEND",{});return out;
}
int main(int argc,char** argv) {
 try {
  if(argc!=2) throw std::runtime_error("usage: software_smoke EXISTING_OUTPUT_DIRECTORY");
  const std::filesystem::path dir(argv[1]);if(!std::filesystem::is_directory(dir)) throw std::runtime_error("output directory missing");
  const auto image=dir/"aether-r12.png",report=dir/"report.log";
  if(std::filesystem::exists(image)||std::filesystem::exists(report)) throw std::runtime_error("refusing to overwrite evidence");
  p::Snapshot snapshot{{p::Fill{{0,0,500,500},{},{.92f,.94f,.98f,1}},
   p::BoxShadow{{40,40,160,100},{},{0,2,8,0,{0,0,0,.65f},AETHER_FALSE}},
   p::Fill{{40,40,160,100},{},{1,1,1,1}},
   p::BoxShadow{{250,80,160,160},{},{10,10,32,4,{.1f,.2f,.7f,.75f},AETHER_FALSE}},
   p::Fill{{250,80,160,160},{},{1,.8f,.4f,1}},
   p::BoxShadow{{50,280,160,100},{},{-8,5,7,-3,{.2f,.7f,.3f,.8f},AETHER_FALSE}},
   p::BoxShadow{{50,280,160,100},{},{0,2,8,0,{.8f,.1f,.1f,.5f},AETHER_FALSE}},
   p::Fill{{50,280,160,100},{},{1,1,1,.65f}}}};
  auto prepared=p::prepare(snapshot);if(!std::holds_alternative<p::PreparedFrame>(prepared)) throw std::runtime_error("prepare failed");
  aether::rendering::FrameStore store;if(store.resize({500,500,1})!=aether::rendering::FrameStatus::ok) throw std::runtime_error("resize failed");
  std::vector<double> times;std::uint64_t work=0,sampled=0,bound=0,inner_sampled=0,inner_bound=0,joint=0,clip_bound=0;const auto cpu=std::clock();
  for(int i=0;i<12;++i) {
   const auto start=std::chrono::steady_clock::now();auto result=s::render(store,std::get<p::PreparedFrame>(prepared));
   const auto end=std::chrono::steady_clock::now();if(result.status!=s::Status::ok) throw std::runtime_error("render failed");
   joint=result.joint_sampled_pixels;clip_bound=result.clip_corner_pixel_bound;work=result.pixel_budget;sampled=result.sampled_pixels;bound=result.corner_pixel_bound;inner_sampled=result.inner_sampled_pixels;inner_bound=result.inner_corner_pixel_bound;times.push_back(std::chrono::duration<double,std::milli>(end-start).count());
  }
  const auto end_cpu=std::clock();
  std::optional<aether::rendering::Frame> frame;if(store.copy_confirmed(frame)!=aether::rendering::FrameStatus::ok || !frame) throw std::runtime_error("copy failed");
  const auto pixels=rgba(*frame);const auto encode_start=std::chrono::steady_clock::now();const auto bytes=png(pixels,500,500);
  const auto encode_end=std::chrono::steady_clock::now();
  {std::ofstream file(image,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.close();if(!file) throw std::runtime_error("PNG write failed");}
  std::ifstream input(image,std::ios::binary);Bytes reopened((std::istreambuf_iterator<char>(input)),{});
  int w=0,h=0,c=0;auto* decoded=stbi_load_from_memory(reopened.data(),int(reopened.size()),&w,&h,&c,4);
  bool match=decoded && w==500 && h==500 && std::equal(pixels.begin(),pixels.end(),decoded);stbi_image_free(decoded);
  if(!match) throw std::runtime_error("PNG roundtrip mismatch");
  const double first=times.front();times.erase(times.begin());std::sort(times.begin(),times.end());
  std::ofstream log(report);log<<"status=PASS\nscope=direct rectangular exterior shadows fixture, NOT HTML\nbackend=Aether own CPU R12\nbuild_revision="<<AETHER_BUILD_REV
   <<"\ncoverage_method=GL16 separable Gaussian pixel integration; same-field interior exclusion; support 4sigma\nviewport=500x500\ndpi=1\nformat=BGRA8 premultiplied encoded sRGB\ncommands="<<snapshot.commands.size()
   <<"\nsampled_pixels="<<sampled<<"\ncorner_pixel_bound="<<bound<<"\njoint_sampled_pixels="<<joint<<"\nclip_corner_pixel_bound="<<clip_bound<<"\ninner_sampled_pixels="<<inner_sampled<<"\ninner_corner_pixel_bound="<<inner_bound<<"\npixel_budget="<<work<<"\nPNG_roundtrip=PASS\nrender_calls=12\nfirst_render_ms="<<first<<"\nwarm_median_ms="<<times[times.size()/2]
   <<"\nwarm_p95_nearest_rank_ms="<<times.back()<<"\nencode_ms="<<std::chrono::duration<double,std::milli>(encode_end-encode_start).count();
  if(cpu!=std::clock_t(-1)&&end_cpu!=std::clock_t(-1)) log<<"\nrender_loop_process_cpu_ms="<<1000.0*double(end_cpu-cpu)/CLOCKS_PER_SEC;
  else log<<"\nrender_loop_process_cpu_ms=NOT_AVAILABLE";
  log<<"\npeak_RSS=NOT_MEASURED\nparse_style_layout=NOT_RUN\ntext=UNSUPPORTED\nchromium=NOT_RUN\nperformance_comparison=NOT_RUN\n";
  log.close();if(!log) throw std::runtime_error("report write failed");std::cout<<"PNG and report verified\n";
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
