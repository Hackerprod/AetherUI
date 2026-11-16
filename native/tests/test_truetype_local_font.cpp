#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/truetype_outline.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
namespace t=aether::rendering::text;namespace q=t::truetype;
int main(int argc,char** argv){
 if(argc!=2){std::cout<<"local font NOT_RUN: supply local font path explicitly\n";return 0;}
 std::ifstream input(argv[1],std::ios::binary|std::ios::ate);assert(input);const auto length=input.tellg();assert(length>0&&length<=std::streamoff(t::max_font_bytes));input.seekg(0);
 auto source=t::FontSource::read_exact(input,std::size_t(length),"local test input, not redistributed");
 const auto start=std::chrono::steady_clock::now();q::Work parsed;auto result=q::ValidatedFace::create(source,0,nullptr,&parsed);
 assert(std::holds_alternative<std::shared_ptr<const q::ValidatedFace>>(result));const auto face=std::get<std::shared_ptr<const q::ValidatedFace>>(result);
 const auto ready=std::chrono::steady_clock::now();assert(face->source()==source&&face->digest()==source->digest&&face->face_index()==0);
 std::size_t admitted=0,empty=0,unsupported=0,malformed=0,limited=0,segments=0,points=0,workunits=0;
 for(std::uint32_t id=0;id<face->glyph_count();++id){q::Work work;auto out=face->outline(id,nullptr,&work);workunits+=work.units;points+=work.points_decoded;segments+=work.segments_retained;
  if(auto* glyph=std::get_if<std::shared_ptr<const q::Outline>>(&out)){++admitted;if((*glyph)->contours.empty())++empty;assert((*glyph)->glyph_id==id);assert((*glyph)->segments.size()<=q::max_segments);}
  else{switch(std::get<q::Error>(out)){case q::Error::unsupported:++unsupported;break;case q::Error::malformed:++malformed;break;case q::Error::limit_exceeded:++limited;break;default:assert(false);}}
 }
 const auto end=std::chrono::steady_clock::now();rusage usage{};assert(getrusage(RUSAGE_SELF,&usage)==0);
 std::cout<<"source_bytes="<<length<<" sha256="<<source->digest<<" face=0 units="<<face->units_per_em()<<" glyphs="<<face->glyph_count()<<'\n'
 <<"admitted="<<admitted<<" empty="<<empty<<" unsupported_profile="<<unsupported<<" malformed="<<malformed<<" limited="<<limited<<" rejection_rate="<<double(unsupported+malformed+limited)/face->glyph_count()<<'\n'
 <<"parse_wall_us="<<std::chrono::duration_cast<std::chrono::microseconds>(ready-start).count()<<" outlines_wall_us="<<std::chrono::duration_cast<std::chrono::microseconds>(end-ready).count()<<'\n'
 <<"parser_units="<<parsed.units<<" checked_bytes="<<parsed.bytes_checked<<" tables="<<parsed.tables_checked<<" outline_units="<<workunits<<" points="<<points<<" segments="<<segments<<'\n'
 <<"process_user_us="<<usage.ru_utime.tv_sec*1000000LL+usage.ru_utime.tv_usec<<" process_system_us="<<usage.ru_stime.tv_sec*1000000LL+usage.ru_stime.tv_usec<<" process_maxrss_kib="<<usage.ru_maxrss<<'\n'
 <<"raster=NOT_RUN shaping=NOT_RUN HTML=NOT_RUN font_copy=NO\n";
}
