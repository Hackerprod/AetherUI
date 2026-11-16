#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_cmap.h"
#include "../src/rendering/text/truetype_raster.h"
#include "../src/rendering/native_frame.h"
#include "../../tests/software_render_smoke/png_writer.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
namespace t=aether::rendering::text;namespace q=t::truetype;
int main(int argc,char** argv){
 assert(argc==3);std::ifstream in(argv[1],std::ios::binary|std::ios::ate);assert(in);auto length=in.tellg();assert(length>0&&length<=std::streamoff(t::max_font_bytes));in.seekg(0);
 auto source=t::FontSource::read_exact(in,std::size_t(length),"explicit local fixture, not redistributed");
 // Fixed local DejaVuSans fixture oracle, not a generic-font expected mapping.
 assert(source->digest=="57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e");
 auto face=std::get<std::shared_ptr<const q::ValidatedFace>>(q::ValidatedFace::create(source));
 auto start=std::chrono::steady_clock::now();q::CmapWork work;auto map=std::get<std::shared_ptr<const q::UnicodeMap>>(q::UnicodeMap::create(face,nullptr,&work));auto ready=std::chrono::steady_clock::now();
 assert(map->format()==12&&face->glyph_count()==6253&&work.ranges==281);
 std::filesystem::create_directories(argv[2]);
 for(auto [cp,gid]:{std::pair{32u,3u},{65u,36u},{97u,68u},{233u,171u},{0x1f600u,5857u}}){
  auto lookup_start=std::chrono::steady_clock::now();auto mapped=std::get<q::Mapped>(map->lookup(cp));assert(mapped.glyph_id==gid);
  auto lookup_end=std::chrono::steady_clock::now();auto shape=face->outline(mapped.glyph_id);auto outline_end=std::chrono::steady_clock::now();
  std::cout<<"scalar="<<cp<<" lookup_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(lookup_end-lookup_start).count()<<" outline_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(outline_end-lookup_end).count()<<" timing=single_shot_diagnostic\n";
  // Q1d-T admits this integer-translation composite; independently flattened
  // fontTools geometry/A8 is checked by run_truetype_composite_local.sh.
  if(cp==233){auto e=std::get<std::shared_ptr<const q::Outline>>(shape);assert(e->glyph_id==171&&e->contours.size()==3&&e->metrics.advance==1260&&e->metrics.lsb==113&&e->metrics.pp1==0);assert(e->bounds.x_min==113&&e->bounds.y_min==-29&&e->bounds.x_max==1151&&e->bounds.y_max==1638);}
  auto outline=std::get<std::shared_ptr<const q::Outline>>(shape);auto instance=std::make_shared<const t::FontInstance>(source,0,48*64,q::q1_unhinted);q::RasterWork rw;
  auto raster_start=std::chrono::steady_clock::now();auto result=std::get<q::RasterOutput>(q::rasterize(outline,instance,0,0,nullptr,&rw));auto mask=result.mask;auto raster_end=std::chrono::steady_clock::now();
  std::cout<<"scalar="<<cp<<" raster_ns="<<std::chrono::duration_cast<std::chrono::nanoseconds>(raster_end-raster_start).count()<<" raster_units="<<rw.units<<" timing=single_shot_diagnostic\n";
  assert(mask->font->source==source&&mask->glyph_id==gid);
  if(cp==32){assert(mask->bytes.empty());continue;}
  smoke_png::Bytes rgba;for(unsigned y=0;y<mask->height;++y)for(unsigned x=0;x<mask->width;++x){const auto a=mask->bytes[y*mask->stride+x];rgba.insert(rgba.end(),{0,0,0,a});}
  auto png=smoke_png::png(rgba,mask->width,mask->height);auto path=std::filesystem::path(argv[2])/("U"+std::to_string(cp)+".png");{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(png.data()),png.size());assert(out);}
  int w=0,h=0,channels=0;auto decoded=stbi_load(path.c_str(),&w,&h,&channels,4);assert(decoded&&w==int(mask->width)&&h==int(mask->height)&&std::equal(rgba.begin(),rgba.end(),decoded));stbi_image_free(decoded);
  auto hash=t::FontSource(png).digest;std::cout<<"scalar="<<cp<<" gid="<<gid<<" png="<<path.filename().string()<<" png_sha256="<<hash<<" width="<<w<<" height="<<h<<" raster_units="<<rw.units<<'\n';
 }
 assert(std::holds_alternative<q::Missing>(map->lookup(0x10ffff)));rusage usage{};assert(getrusage(RUSAGE_SELF,&usage)==0);
 std::cout<<"font_sha256="<<source->digest<<" cmap_groups="<<work.ranges<<" cmap_units="<<work.units<<" cmap_wall_us="<<std::chrono::duration_cast<std::chrono::microseconds>(ready-start).count()<<" process_user_us="<<usage.ru_utime.tv_sec*1000000LL+usage.ru_utime.tv_usec<<" process_system_us="<<usage.ru_stime.tv_sec*1000000LL+usage.ru_stime.tv_usec<<" process_maxrss_kib="<<usage.ru_maxrss<<" pipeline=PASS HTML=NOT_RUN shaping=NOT_RUN Chromium=NOT_RUN\n";
}
