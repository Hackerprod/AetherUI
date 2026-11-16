#include "atlas_io.h"
#include "../../../native/src/rendering/text/truetype_raster.h"
#include <sys/resource.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <new>
namespace t=aether::rendering::text;namespace q=t::truetype;using namespace q1b_atlas;
using Clock=std::chrono::steady_clock;
static double elapsed(Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();}
static const char* error(q::Error e){switch(e){case q::Error::malformed:return "malformed";case q::Error::unsupported:return "unsupported_profile";case q::Error::limit_exceeded:return "limit_exceeded";case q::Error::out_of_memory:return "OOM";case q::Error::cancelled:return "cancelled";}return "unknown";}
static const char* error(q::RasterError e){switch(e){case q::RasterError::invalid_input:return "invalid_input";case q::RasterError::unsupported:return "unsupported_profile";case q::RasterError::numeric_failure:return "numeric_failure";case q::RasterError::budget_exceeded:return "budget_exceeded";case q::RasterError::out_of_memory:return "OOM";case q::RasterError::cancelled:return "cancelled";}return "unknown";}
struct Cancel:q::Cancellation{bool cancel{};bool cancelled()noexcept override{return cancel;}};
struct Page {std::string name,digest;std::size_t bytes{};};
int main(int argc,char** argv){
 if(argc<3||argc>4){std::cerr<<"Opt-in only: q1b_atlas LOCAL_DEJAVU_TTF NEW_OUTPUT_DIRECTORY [--test-fail=encode|oom|cancel|budget|verify|commit]\n";return 2;}
 const std::string fault=argc==4?argv[3]:"";
 if(!fault.empty()&&fault!="--test-fail=encode"&&fault!="--test-fail=oom"&&fault!="--test-fail=cancel"&&fault!="--test-fail=budget"&&fault!="--test-fail=verify"&&fault!="--test-fail=commit"){std::cerr<<"invalid failure probe\n";return 2;}
 try{
  Stage stage(fs::absolute(argv[2]));const auto begin=Clock::now();
  std::ifstream input(argv[1],std::ios::binary|std::ios::ate);require(bool(input),"font_open_failed");const auto length=input.tellg();require(length>0&&length<=std::streamoff(t::max_font_bytes),"font_size_limit");input.seekg(0);
  const auto parse_start=Clock::now();auto source=t::FontSource::read_exact(input,std::size_t(length),"local font, no redistribution");
  require(source->digest=="57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e","font_identity_not_approved_for_atlas");
  auto license=read("/usr/share/doc/fonts-dejavu-core/copyright",128*1024);require(hash(license)=="63d3ba759d12804c5b31a9d5940d855c1820d1f5999e6b0872eb1c7ff045fbc9","license_metadata_changed");license.clear();license.shrink_to_fit();
  q::Work parse_work;auto parsed=q::ValidatedFace::create(source,0,nullptr,&parse_work);require(std::holds_alternative<std::shared_ptr<const q::ValidatedFace>>(parsed),"face_validation_failed");const auto face=std::get<std::shared_ptr<const q::ValidatedFace>>(parsed);const double parse_ms=elapsed(parse_start);
  auto font=std::make_shared<const t::FontInstance>(source,0,2048,q::q1_unhinted);q::AggregateBudget budget{fault=="--test-fail=budget"?0ULL:512000000ULL};Cancel cancellation;
  std::ostringstream report;report<<std::setprecision(12)<<"Q1b bounded opt-in atlas: real unhinted A8 glyph IDs, NOT HTML/shaping/cmap\ncoverage_visualization=opaque_grayscale_RGB_equals_A8 runtime_remains_A8 black_exterior=0 alpha=255\nsource_sha256="<<source->digest<<" source_bytes="<<source->bytes.size()<<" face=0 size_px=32 size_26_6=2048 phase_x=0 phase_y=0 flags="<<q::q1_unhinted<<"\nlicense=Bitstream_Vera_and_DejaVu_changes_public_domain local_notice_sha256=63d3ba759d12804c5b31a9d5940d855c1820d1f5999e6b0872eb1c7ff045fbc9 font_redistributed=NO\n";
  const unsigned count=std::min<unsigned>(128,face->glyph_count());std::vector<Page> pages;pages.reserve(2);std::size_t total_png=0,admitted=0,rejected=0,slots_rejected=0;double outline_ms=0,raster_ms=0,encode_ms=0,verify_ms=0;
  for(unsigned page=0;page<(count+63)/64;++page){
   Bytes rgba(1024*1024*4,0);for(std::size_t i=3;i<rgba.size();i+=4)rgba[i]=255;
   for(unsigned id=page*64;id<std::min(count,(page+1)*64);++id){
    const auto a=Clock::now();q::Work ow;auto outline=face->outline(id,nullptr,&ow);outline_ms+=elapsed(a);report<<"glyph="<<id<<" outline_units="<<ow.units<<" outline_points="<<ow.points_decoded<<" outline_segments="<<ow.segments_retained;
    if(auto e=std::get_if<q::Error>(&outline)){++rejected;report<<" outline="<<error(*e)<<" raster=NOT_RUN atlas=not_placed\n";if(*e!=q::Error::unsupported&&*e!=q::Error::limit_exceeded)throw std::runtime_error(std::string("outline_")+error(*e));continue;}
    cancellation.cancel=fault=="--test-fail=cancel";
    q::RasterWork rw;const auto b=Clock::now();auto result=q::rasterize(std::get<std::shared_ptr<const q::Outline>>(outline),font,0,0,&cancellation,&rw,&budget);raster_ms+=elapsed(b);
    report<<" transformed_segments="<<rw.transformed_segments<<" split_segments="<<rw.split_segments<<" cancel_checks="<<rw.cancel_checks<<" reserved="<<rw.reserved<<" aggregate_charged="<<rw.aggregate_charged<<" edge_checks="<<rw.edge_checks<<" root_steps="<<rw.root_steps<<" sort_comparisons="<<rw.sort_comparisons<<" sort_swaps="<<rw.sort_swaps<<" sweep_operations="<<rw.sweep_operations<<" sample_tests="<<rw.sample_tests<<" output_operations="<<rw.output_operations<<" units="<<rw.units;
    if(auto e=std::get_if<q::RasterError>(&result)){++rejected;report<<" raster="<<error(*e)<<" atlas=not_placed\n";if(*e!=q::RasterError::unsupported)throw std::runtime_error(std::string("raster_")+error(*e));continue;}
    ++admitted;const auto& output=std::get<q::RasterOutput>(result);const auto& m=*output.mask;
    report<<" raster=ok width="<<m.width<<" height="<<m.height<<" bearing_left="<<m.bearing_left<<" bearing_top="<<m.bearing_top<<" advance_px="<<output.advance_px<<" advance_26_6="<<output.advance_26_6;
    if(m.width>112||m.height>112){++slots_rejected;report<<" atlas=atlas_slot_unsupported\n";continue;}
    const unsigned cell=id%64,x=(cell%8)*128+8,y=(cell/8)*128+8;
    for(unsigned row=0;row<m.height;++row)for(unsigned col=0;col<m.width;++col){const auto dst=(std::size_t(y+row)*1024+x+col)*4;rgba[dst]=rgba[dst+1]=rgba[dst+2]=m.bytes[std::size_t(row)*m.stride+col];}
    report<<" atlas=placed page="<<page<<" cell="<<cell<<" x="<<x<<" y="<<y<<"\n";
    require(report.tellp()>=0&&std::size_t(report.tellp())<max_report_bytes,"report_size_limit");
   } // mask/result/outline discarded each glyph; never a full-font cache.
   if(fault=="--test-fail=oom")throw std::bad_alloc();
   require(fault!="--test-fail=encode","injected_encode_failure");const auto a=Clock::now();auto png=smoke_png::png(rgba,1024,1024);verify_png(png,rgba);encode_ms+=elapsed(a);require(png.size()<=max_png_bytes-total_png,"png_aggregate_limit");total_png+=png.size();
   Page record{"atlas-"+std::to_string(page)+".png",hash(png),png.size()};write(stage.path()/record.name,png);pages.push_back(record);
  } // page logical buffer and encoder scratch released before next page.
  rusage usage{};
  report<<"summary requested="<<count<<" raster_admitted="<<admitted<<" rejected="<<rejected<<" rejection_rate="<<double(rejected)/count<<" atlas_slot_unsupported="<<slots_rejected<<" reserved_total="<<512000000-budget.remaining<<" aggregate_limit=512000000 png_bytes="<<total_png<<"\nparse_and_hash_wall_ms="<<parse_ms<<" outline_wall_ms="<<outline_ms<<" raster_wall_ms="<<raster_ms<<" encode_and_roundtrip_wall_ms="<<encode_ms<<"\n";
  for(const auto& p:pages)report<<"artifact="<<p.name<<" bytes="<<p.bytes<<" sha256="<<p.digest<<"\n";
  const auto verify_start=Clock::now();for(const auto& p:pages){auto bytes=read(stage.path()/p.name,max_png_bytes);require(bytes.size()==p.bytes&&hash(bytes)==p.digest,"page_reopen_hash");verify_png(bytes);}verify_ms=elapsed(verify_start);
  require(fault!="--test-fail=verify","injected_verify_failure");require(getrusage(RUSAGE_SELF,&usage)==0,"rusage_failed");report<<"process_user_us="<<usage.ru_utime.tv_sec*1000000LL+usage.ru_utime.tv_usec<<" process_system_us="<<usage.ru_stime.tv_sec*1000000LL+usage.ru_stime.tv_usec<<" process_maxrss_kib="<<usage.ru_maxrss<<"\n";report<<"verify_wall_ms="<<verify_ms<<" total_wall_ms_before_commit="<<elapsed(begin)<<"\nstatus="<<(rejected||slots_rejected?"VERIFIED_COMPLETE_WITH_REJECTIONS":"VERIFIED_COMPLETE")<<" required_supervisor_cpu_seconds=15 required_supervisor_wall_seconds=30 Cairo=NO Pango=NO Skia=NO FreeType=NO Chromium=NOT_RUN\n";
  const auto text=report.str();require(text.size()<=max_report_bytes,"report_size_limit");write(stage.path()/"report.log",{reinterpret_cast<const std::uint8_t*>(text.data()),text.size()});auto reread=read(stage.path()/"report.log",max_report_bytes);require(std::string(reread.begin(),reread.end())==text,"report_reopen_mismatch");
  require(fault!="--test-fail=commit","injected_commit_failure");stage.commit();std::cout<<(rejected||slots_rejected?"VERIFIED_COMPLETE_WITH_REJECTIONS ":"VERIFIED_COMPLETE ")<<argv[2]<<" pages="<<pages.size()<<" glyph_ids="<<count<<"\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Q1B_ATLAS_INCOMPLETE: "<<e.what()<<"; previous confirmed output unchanged\n";return 1;}
}
