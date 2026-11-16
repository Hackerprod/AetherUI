#ifdef NDEBUG
#error Assertions required
#endif
#include "elliptic_t1_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;namespace eo=elliptic_oracle;
struct Fixture{std::string name;int w,h;p::Rect rect;p::Radii radii;p::Color color;bool clip=false;};
static p::PreparedFrame prep(std::vector<p::Command> c){auto result=p::prepare({std::move(c),true});assert(std::holds_alternative<p::PreparedFrame>(result));return std::get<p::PreparedFrame>(std::move(result));}
static Frame read(FrameStore& store){std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
static eo::Shape shape(const Fixture& f){const auto& r=f.radii;return {f.rect.x,f.rect.y,static_cast<long double>(f.rect.x)+f.rect.width,static_cast<long double>(f.rect.y)+f.rect.height,{{{r.top_left_x,r.top_left_y},{r.top_right_x,r.top_right_y},{r.bottom_right_x,r.bottom_right_y},{r.bottom_left_x,r.bottom_left_y}}}};}
static void binary(const std::string& path,const std::vector<std::uint8_t>& bytes){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();assert(f);}
static Frame render_fixture(const Fixture& f,sw::Result& result){FrameStore store;assert(store.resize({f.w,f.h,1})==FrameStatus::ok);std::vector<p::Command> c;if(f.clip)c={p::PushClip{f.rect,f.radii},p::Fill{{0,0,float(f.w),float(f.h)},{},f.color},p::PopClip{}};else c={p::Fill{f.rect,f.radii,f.color}};result=sw::render(store,prep(std::move(c)));std::printf("STATUS %s %d budget=%llu\n",f.name.c_str(),int(result.status),(unsigned long long)result.pixel_budget);std::fflush(stdout);assert(result.status==sw::Status::ok);return read(store);}
static void dump(const std::string& out,const Fixture& f,const Frame& actual,const std::vector<std::uint8_t>& expected){binary(out+"/"+f.name+".actual.bgra",actual.pixels);binary(out+"/"+f.name+".expected.bgra",expected);std::ofstream csv(out+"/"+f.name+".diff.csv");csv<<"offset,expected,actual\n";for(std::size_t i=0;i<expected.size();++i)if(expected[i]!=actual.pixels[i])csv<<i<<','<<unsigned(expected[i])<<','<<unsigned(actual.pixels[i])<<'\n';csv.close();assert(csv);binary(out+"/"+f.name+".png",smoke_png::png(smoke_png::rgba(actual),f.w,f.h));}
static std::vector<Fixture> cases(){const p::Color white{1,1,1,1},tint{.8f,.4f,.2f,.5f};return {
 {"distinct",32,24,{1.25f,2.5f,28,19},{8,3,4,7,9,5,2,8},white},
 {"wide",32,24,{0,0,32,24},{24,2,0,0,5,4,0,0},tint},
 {"tall",32,24,{0,0,32,24},{2,20,0,0,3,2,0,0},white},
 {"only-tr",32,24,{0,0,32,24},{0,0,17,9,0,0,0,0},white},
 {"only-br",32,24,{0,0,32,24},{0,0,0,0,13,7,0,0},white},
 {"only-bl",32,24,{0,0,32,24},{0,0,0,0,0,0,11,5},white},
 {"degenerate",32,24,{0,0,32,24},{999,0,0,999,9,4,7,3},white},
 {"all-degenerate",32,24,{0,0,32,24},{999,0,0,999,2,0,0,7},white},
 {"exact-sums",32,24,{0,0,32,24},{20,15,12,4,8,20,24,9},white},
 {"offscreen",64,48,{-7.25f,-3.5f,44,37},{27,12,5,18,21,9,4,16},tint},
 {"fractional-116",32,24,{.25f,.25f,2,2},{1,.5f,0,0,0,0,0,0},white},
 {"diagonal",100,100,{0,0,100,100},{90,90,0,0,90,90,0,0},white},
 {"diagonal-h",100,100,{0,0,100,100},{0,0,90,90,0,0,90,90},white},
 {"diagonal-v",100,100,{0,0,100,100},{0,0,90,90,0,0,90,90},white},
 {"diagonal-hv",100,100,{0,0,100,100},{90,90,0,0,90,90,0,0},white}
 };}
// The hook observes only FrameStore pixel allocation, not preflight allocations.
static void budget_combination(const std::string& out){
 const p::Color white{1,1,1,1};const p::Radii ellipse{1,2,0,0,0,0,0,0},circle{1,1,1,1,1,1,1,1};
 auto chain=[&](int size,int depth){std::vector<p::Command> cmds;for(int i=0;i<depth;++i)cmds.push_back(p::PushClip{{-1,-1,float(size+2),float(size+2)},circle});cmds.push_back(p::Fill{{0,0,float(size),float(size)},ellipse,white});for(int i=0;i<depth;++i)cmds.push_back(p::PopClip{});return prep(std::move(cmds));};
 Fixture f{"budget-direct",16,16,{0,0,16,16},ellipse,white};auto expected=eo::solid(shape(f),16,16,{1,1,1,1});
 sw::Result dr;auto direct=render_fixture(f,dr);dump(out,f,direct,expected);assert(direct.pixels==expected&&dr.pixel_budget==525056);
 FrameStore small;assert(small.resize({16,16,1})==FrameStatus::ok);auto positive=sw::render(small,chain(16,4));
 std::printf("BUDGET positive status=%d budget=%llu bound=%llu sampled=%llu nodes=%llu depth=%llu\n",int(positive.status),(unsigned long long)positive.pixel_budget,(unsigned long long)positive.clip_corner_pixel_bound,(unsigned long long)positive.joint_sampled_pixels,(unsigned long long)positive.clip_node_count,(unsigned long long)positive.max_clip_depth);std::fflush(stdout);
 assert(positive.status==sw::Status::ok);auto actual=read(small);f.name="budget-four-central";dump(out,f,actual,expected);assert(actual.pixels==expected&&actual.pixels==direct.pixels);assert(positive.pixel_budget==3672832&&positive.clip_corner_pixel_bound==256&&positive.joint_sampled_pixels==256&&positive.clip_node_count==4&&positive.max_clip_depth==4);
 FrameStore later;assert(later.resize({16,16,1})==FrameStatus::ok);auto tagged=prep({p::Fill{{0,0,16,16},{},{0,0,0,0}},p::PushClip{{0,0,16,16},ellipse},p::Fill{{0,0,16,16},{},white},p::PopClip{}});auto result=sw::render(later,tagged);
 std::printf("BUDGET noninitial-clip status=%d budget=%llu\n",int(result.status),(unsigned long long)result.pixel_budget);std::fflush(stdout);assert(result.status==sw::Status::ok);auto clipped=read(later);f.name="budget-noninitial-clip";dump(out,f,clipped,expected);assert(clipped.pixels==expected&&clipped.pixels==direct.pixels&&result.pixel_budget==1574144);
 bool armed=false;unsigned calls=0;FrameStore store([&]{if(armed){++calls;throw std::bad_alloc();}});assert(store.resize({100,100,1})==FrameStatus::ok);
 FrameStore::Token token;assert(store.prepare(token)==FrameStatus::ok);std::span<std::uint8_t> bytes;assert(store.candidate(token,bytes)==FrameStatus::ok);for(std::size_t i=0;i<bytes.size();i+=4){bytes[i]=std::uint8_t((i/4)%127);bytes[i+1]=17;bytes[i+2]=53;bytes[i+3]=255;}assert(store.commit(token)==FrameStatus::ok);
 auto before=read(store);auto large=chain(100,256);armed=true;auto rejected=sw::render(store,large);armed=false;auto after=read(store);
 binary(out+"/budget-rejected-before.bgra",before.pixels);binary(out+"/budget-rejected-after.bgra",after.pixels);
 std::ofstream evidence(out+"/budget-rejected.json");evidence<<"{\"status\":"<<int(rejected.status)<<",\"index\":"<<rejected.command_index<<",\"budget\":"<<rejected.pixel_budget<<",\"frame_allocation_calls\":"<<calls<<",\"before_sequence\":"<<before.sequence<<",\"after_sequence\":"<<after.sequence<<",\"hook_armed\":"<<(armed?"true":"false")<<"}\n";evidence.close();assert(evidence);
 std::printf("BUDGET rejected status=%d index=%zu budget=%llu allocation_calls=%u\n",int(rejected.status),rejected.command_index,(unsigned long long)rejected.pixel_budget,calls);std::fflush(stdout);
 assert(!armed&&calls==0&&rejected.status==sw::Status::limit_exceeded&&rejected.frame_status==FrameStatus::ok&&rejected.command_index==256&&rejected.pixel_budget==25630000);
 assert(before.sequence==after.sequence&&before.viewport.width==after.viewport.width&&before.viewport.height==after.viewport.height&&before.viewport.dpi==after.viewport.dpi&&before.stride==after.stride&&before.pixels==after.pixels);
 std::puts("BUDGET_COMBINATION_PASS positive=3 negative=1 hooks_disabled=true");
}
int main(int argc,char** argv){assert(argc==3);std::string mode=argv[1],out=argv[2];
 if(mode=="legacy"){
  const p::Color tint{.8f,.4f,.2f,.5f};std::vector<Fixture> f{{"legacy-rect",32,24,{.25f,1.5f,29,19},{},tint},{"legacy-round",32,24,{.25f,1.5f,29,19},{7,7,7,7,7,7,7,7},tint},{"legacy-clip",32,24,{.25f,1.5f,29,19},{7,7,7,7,7,7,7,7},tint,true}};
  for(const auto& item:f){sw::Result r;auto actual=render_fixture(item,r);binary(out+"/"+item.name+".bgra",actual.pixels);std::printf("LEGACY %s %d %llu %llu %llu %llu %llu %llu %llu\n",item.name.c_str(),int(r.status),(unsigned long long)r.pixel_budget,(unsigned long long)r.corner_pixel_bound,(unsigned long long)r.sampled_pixels,(unsigned long long)r.clip_corner_pixel_bound,(unsigned long long)r.joint_sampled_pixels,(unsigned long long)r.clip_node_count,(unsigned long long)r.clip_node_visits);}
  return 0;
 }
 assert(mode=="geometry");eo::controls();auto list=cases();std::vector<Frame> frames;
 for(const auto& f:list){sw::Result result;auto actual=render_fixture(f,result);auto expected=eo::solid(shape(f),f.w,f.h,{f.color.b,f.color.g,f.color.r,f.color.a});dump(out,f,actual,expected);assert(actual.pixels==expected);frames.push_back(actual);std::printf("GEOMETRY %s bytes=%zu PASS\n",f.name.c_str(),expected.size());std::fflush(stdout);}
 for(int c=0;c<4;++c)assert(frames[10].pixels[c]==116);
 // Reflexions compare independent executions, not merely two oracle buffers.
 for(int y=0;y<100;++y)for(int x=0;x<100;++x)for(int c=0;c<4;++c){auto at=(std::size_t(y)*100+x)*4+c;assert(frames[11].pixels[at]==frames[12].pixels[(std::size_t(y)*100+99-x)*4+c]);assert(frames[11].pixels[at]==frames[13].pixels[(std::size_t(99-y)*100+x)*4+c]);assert(frames[11].pixels[at]==frames[14].pixels[(std::size_t(99-y)*100+99-x)*4+c]);}
 // Primitive clip geometry, same sample grid as direct fill. No large-diagonal
 // clip here: its conservative new-profile budget intentionally exceeds 50M.
 for(std::size_t i=0;i<11;++i){auto f=list[i];f.clip=true;f.name+="-clip";sw::Result result;auto actual=render_fixture(f,result);auto expected=eo::solid(shape(f),f.w,f.h,{f.color.b,f.color.g,f.color.r,f.color.a});dump(out,f,actual,expected);assert(actual.pixels==expected);assert(actual.pixels==frames[i].pixels);std::printf("GEOMETRY %s bytes=%zu PASS\n",f.name.c_str(),expected.size());std::fflush(stdout);}
 budget_combination(out);
 std::puts("ELLIPTIC_T1_GEOMETRY_PASS direct=15 clip=11 legacy_gate_separate coverage_complete=false");
}
