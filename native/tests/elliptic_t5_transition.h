#pragma once
#include "elliptic_t2b_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include <cstdio>
#include <string>
namespace elliptic_transition {
namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;
using namespace aether::rendering;using Shape=elliptic_oracle::Shape;using Bytes=elliptic_paint_oracle::Bytes;using Mask=std::shared_ptr<const text::GlyphMaskA8>;
struct Evidence {std::string directory;std::uint64_t budget,nodes,depth;};
inline void write(const std::string& path,const Bytes& b){auto* f=std::fopen(path.c_str(),"wbx");assert(f);const auto written=std::fwrite(b.data(),1,b.size(),f);const auto closed=std::fclose(f);assert(written==b.size()&&closed==0);}
inline void check(const char* name,int w,int h,std::vector<p::Command> commands,const Bytes& expected,const std::vector<Mask>* masks=nullptr,const Evidence* evidence=nullptr){
 FrameStore store;assert(store.resize({w,h,1})==FrameStatus::ok);FrameStore::Token token;assert(store.prepare(token)==FrameStatus::ok);std::span<std::uint8_t> bytes;assert(store.candidate(token,bytes)==FrameStatus::ok);std::fill(bytes.begin(),bytes.end(),std::uint8_t(31));assert(store.commit(token)==FrameStatus::ok);
 std::optional<Frame> before;if(evidence){auto copied=store.copy_confirmed(before);std::printf("T5_EVIDENCE_BEFORE %s copy_status=%d has_frame=%d\n",name,int(copied),int(bool(before)));std::fflush(stdout);assert(copied==FrameStatus::ok&&before);if(!evidence->directory.empty())write(evidence->directory+"/"+name+".before.bgra",before->pixels);}
 auto r=p::prepare({std::move(commands),true});assert(std::holds_alternative<p::PreparedFrame>(r));const auto& f=std::get<p::PreparedFrame>(r);auto result=masks?sw::render_a8(store,f,*masks):sw::render(store,f);
 std::optional<Frame> out;
 if(evidence){
  std::printf("T5_EVIDENCE_RESULT %s status=%d frame_status=%d budget=%llu nodes=%llu depth=%llu\n",name,int(result.status),int(result.frame_status),(unsigned long long)result.pixel_budget,(unsigned long long)result.clip_node_count,(unsigned long long)result.max_clip_depth);std::fflush(stdout);
  auto copied=store.copy_confirmed(out);std::printf("T5_EVIDENCE_COPY %s copy_status=%d has_frame=%d\n",name,int(copied),int(bool(out)));std::fflush(stdout);assert(copied==FrameStatus::ok&&out);
  if(!evidence->directory.empty()){write(evidence->directory+"/"+name+".actual.bgra",out->pixels);write(evidence->directory+"/"+name+".expected.bgra",expected);}
  std::printf("T5_EVIDENCE_FRAME %s sequence=%llu/%llu width=%d height=%d dpi=%g stride=%zu bytes=%zu\n",name,(unsigned long long)before->sequence,(unsigned long long)out->sequence,out->viewport.width,out->viewport.height,double(out->viewport.dpi),out->stride,out->pixels.size());std::fflush(stdout);
  assert(result.status==sw::Status::ok&&result.frame_status==FrameStatus::ok&&result.pixel_budget==evidence->budget&&result.clip_node_count==evidence->nodes&&result.max_clip_depth==evidence->depth);
 }else{assert(result.status==sw::Status::ok);assert(store.copy_confirmed(out)==FrameStatus::ok&&out);}
assert(out->sequence==2&&out->viewport.width==w&&out->viewport.height==h&&out->viewport.dpi==1&&out->stride==std::size_t(w)*4&&out->pixels==expected);std::printf("T5_TRANSITION %s exact_bytes=%zu sequence=2 PASS\n",name,expected.size());
}
}
