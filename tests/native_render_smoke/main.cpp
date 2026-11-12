// Real SDK runner. Do not compile/run where graphics validation is restricted.
#include "fixture.h"
#include "report.h"
#include "../../native/src/rendering/cairo/cairo_frame_renderer.h"
#include "../../native/src/rendering/text/pango_layout.h"
#include <cairo.h>
#include <pango/pangocairo.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
namespace fs=std::filesystem;
namespace sm=aether::smoke;
namespace ar=aether::rendering;
using Surface=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
using Context=std::unique_ptr<cairo_t,decltype(&cairo_destroy)>;
namespace {
template<class F> bool phase(sm::Report& r,sm::Phase id,F work) {
 auto& p=r.phases[static_cast<std::size_t>(id)];
 auto start=std::chrono::steady_clock::now();bool good=false;
 try {good=work();}catch(...){good=false;}
 auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
 p.status=good?sm::Status::pass:sm::Status::fail;
 p.reason=good?"":"phase_failed";
 if(elapsed>=0)p.duration_ns=elapsed;
 else {p.duration_ns.reset();p.reason=good?"clock_invalid":"phase_failed_clock_invalid";}
 return good;
}
void skipped(sm::Report& r,sm::Phase first,sm::Phase last) {
 for(auto i=static_cast<std::size_t>(first);i<=static_cast<std::size_t>(last);++i)
  r.phases[i]={sm::Status::skip,"dependency_failed",{}};
}
bool png(const ar::Frame& frame,const fs::path& destination) {
 Surface surface(cairo_image_surface_create_for_data(const_cast<unsigned char*>(frame.pixels.data()),
  CAIRO_FORMAT_ARGB32,frame.viewport.width,frame.viewport.height,static_cast<int>(frame.stride)),cairo_surface_destroy);
 if(!surface||cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)return false;
 cairo_surface_mark_dirty(surface.get());
 return cairo_surface_write_to_png(surface.get(),destination.string().c_str())==CAIRO_STATUS_SUCCESS;
}
bool verify(const fs::path& file,int width,int height,bool text) {
 Surface surface(cairo_image_surface_create_from_png(file.string().c_str()),cairo_surface_destroy);
 if(!surface||cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)return false;
 if(cairo_image_surface_get_width(surface.get())!=width||cairo_image_surface_get_height(surface.get())!=height)return false;
 auto format=cairo_image_surface_get_format(surface.get());
 if(format!=CAIRO_FORMAT_ARGB32&&format!=CAIRO_FORMAT_RGB24)return false;
 cairo_surface_flush(surface.get());
 if(cairo_surface_status(surface.get())!=CAIRO_STATUS_SUCCESS)return false;
 auto* data=cairo_image_surface_get_data(surface.get());int stride=cairo_image_surface_get_stride(surface.get());
 if(!data||stride<width*4)return false;
 auto pixel=[&](int x,int y){std::uint32_t value;std::memcpy(&value,data+std::size_t(y)*stride+std::size_t(x)*4,4);return value;};
 auto rgb=[&](int x,int y){return pixel(x,y)&0x00ffffffU;};
 if(format==CAIRO_FORMAT_ARGB32)
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)if((pixel(x,y)>>24)!=255)return false;
 if(text){
  std::size_t changed=0;
  for(int y=0;y<height;++y)for(int x=0;x<width;++x)changed+=rgb(x,y)!=0x00ffffffU;
  return changed>0&&rgb(0,0)==0x00ffffffU;
 }
 for(const auto& p:sm::geometry_probes)if(rgb(p.x,p.y)!=((p.r<<16)|(p.g<<8)|p.b))return false;
 return true;
}
bool write_report(const fs::path& dir,const sm::Report& r) {
 std::ofstream out(dir/"report.tmp",std::ios::binary|std::ios::out|std::ios::trunc);
 if(!out)return false;
 out<<sm::serialize(r);out.flush();if(!out)return false;
 out.close();if(!out)return false;
 std::error_code error;fs::rename(dir/"report.tmp",dir/"report.log",error);return !error;
}
int run(const fs::path& dir,const std::string& id) {
 if(!sm::valid_run_id(id)){std::cerr<<"Invalid run id\n";return 2;}
 std::error_code error;
 if(!fs::create_directory(dir,error)||error){std::cerr<<"Output directory must be newly created; cannot reuse output\n";return 2;}
 sm::Report report;report.run_id=id;report.provenance=sm::Provenance::measured;
 report.cairo_version=cairo_version_string();report.pango_version=pango_version_string();
 report.phases[0]={sm::Status::pass,"executable_started_not_aggregate_build",{}};
 ar::FrameStore store;std::optional<ar::Frame> confirmed;
 bool good=phase(report,sm::Phase::geometry_render,[&]{
  if(store.resize({sm::geometry_width,sm::geometry_height,1})!=ar::FrameStatus::ok)return false;
  auto prepared=sm::prep::prepare(sm::geometry_fixture());
  if(!std::holds_alternative<sm::prep::PreparedFrame>(prepared))return false;
  auto result=ar::cairo_adapter::render_cairo(store,std::get<sm::prep::PreparedFrame>(prepared),{1,1,1,1});
  return result.status==ar::cairo_adapter::RenderStatus::ok&&store.copy_confirmed(confirmed)==ar::FrameStatus::ok&&confirmed.has_value();
 });
 if(good){good=phase(report,sm::Phase::geometry_png,[&]{return png(*confirmed,dir/"geometry.png");});
  if(good)phase(report,sm::Phase::geometry_verify,[&]{return verify(dir/"geometry.png",sm::geometry_width,sm::geometry_height,false);});
  else skipped(report,sm::Phase::geometry_verify,sm::Phase::geometry_verify);
 }else skipped(report,sm::Phase::geometry_png,sm::Phase::geometry_verify);
 std::unique_ptr<ar::text::PangoLayoutOwner> layout;
 good=phase(report,sm::Phase::text_layout,[&]{
  auto request=ar::text::paint(sm::text_fixture());if(!std::holds_alternative<ar::text::Request>(request))return false;
  auto built=ar::text::PangoLayoutOwner::create(std::get<ar::text::Request>(request));
  if(!std::holds_alternative<std::unique_ptr<ar::text::PangoLayoutOwner>>(built))return false;
  layout=std::move(std::get<std::unique_ptr<ar::text::PangoLayoutOwner>>(built));
  auto result=layout->metrics();if(!std::holds_alternative<ar::text::LogicalMetrics>(result))return false;
  const auto m=std::get<ar::text::LogicalMetrics>(result);
  report.text_width=m.width;report.text_height=m.height;report.text_baseline=m.baseline;return true;
 });
 Surface text_surface(nullptr,cairo_surface_destroy);
 if(good){good=phase(report,sm::Phase::text_draw,[&]{
   text_surface.reset(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,sm::text_width,sm::text_height));
   if(!text_surface||cairo_surface_status(text_surface.get())!=CAIRO_STATUS_SUCCESS)return false;
   Context context(cairo_create(text_surface.get()),cairo_destroy);
   if(!context||cairo_status(context.get())!=CAIRO_STATUS_SUCCESS)return false;
   cairo_set_operator(context.get(),CAIRO_OPERATOR_SOURCE);cairo_set_source_rgb(context.get(),1,1,1);cairo_paint(context.get());
   if(layout->draw(context.get(),8,8,{0,0,0,1}))return false;
   if(cairo_status(context.get())!=CAIRO_STATUS_SUCCESS)return false;
   context.reset();cairo_surface_flush(text_surface.get());
   return cairo_surface_status(text_surface.get())==CAIRO_STATUS_SUCCESS;
  });
  if(good){good=phase(report,sm::Phase::text_png,[&]{return cairo_surface_write_to_png(text_surface.get(),(dir/"text.png").string().c_str())==CAIRO_STATUS_SUCCESS;});
   if(good)phase(report,sm::Phase::text_verify,[&]{return verify(dir/"text.png",sm::text_width,sm::text_height,true);});
   else skipped(report,sm::Phase::text_verify,sm::Phase::text_verify);
  }else skipped(report,sm::Phase::text_png,sm::Phase::text_verify);
 }else skipped(report,sm::Phase::text_draw,sm::Phase::text_verify);
 if(!sm::coherent(report)){std::cerr<<"Inconsistent report\n";return 3;}
 if(!write_report(dir,report)){std::cerr<<"Failed to finalize report.log\n";return 3;}
 return sm::accepts_real_smoke(report)?0:1;
}
}
int main(int argc,char** argv) {
 if(argc!=3){std::cerr<<"Usage: aether_native_render_smoke NEW_OUTPUT_DIRECTORY RUN_ID\n";return 2;}
 try{return run(argv[1],argv[2]);}catch(const std::exception& e){std::cerr<<"Smoke failed: "<<e.what()<<'\n';return 3;}
 catch(...){std::cerr<<"Smoke failed\n";return 3;}
}
