#include <aether/aether_b0_source.h>
#include "../rendering/html_ltr/b0_source_session.h"
#include "../rendering/html_ltr/image_registry.h"
#include "../rendering/html_ltr/admission.h"
#include "../rendering/html_ltr/raster_coordinate.h"
#include "../rendering/software/software_renderer.h"
#include <cstring>
#include <memory>
#include <thread>
namespace r=aether::rendering;
namespace h=r::html_ltr;
struct aether_b0_source_s {
 std::thread::id thread=std::this_thread::get_id();
 std::unique_ptr<h::B0SourceSession> scene;
 const r::preparation::PreparedFrame* prepared=nullptr;
 r::FrameStore frames;
 bool rendered=false;
};
namespace {
bool valid_diag(const aether_b0_source_diagnostic_v1* d){return !d||(d->struct_size==sizeof(*d)&&d->version==1&&!d->reserved);}
aether_status_t report(aether_b0_source_diagnostic_v1* d,aether_status_t status,uint32_t stage=0,uint32_t reason=0,uint64_t node=UINT64_MAX,uint64_t command=UINT64_MAX,uint64_t offset=UINT64_MAX){
 if(d){*d={sizeof(*d),1,status,stage,reason,0,node,command,offset};}return status;
}
uint32_t reason(aether_status_t s){return s==AETHER_ERROR_OUT_OF_MEMORY?AETHER_B0_REASON_ALLOCATION:s==AETHER_ERROR_LIMIT_EXCEEDED?AETHER_B0_REASON_LIMIT:s==AETHER_ERROR_UNSUPPORTED?AETHER_B0_REASON_UNSUPPORTED:s==AETHER_ERROR_INTERNAL?AETHER_B0_REASON_INVARIANT:AETHER_B0_REASON_ARGUMENT;}
aether_status_t frame_status(r::FrameStatus s){switch(s){case r::FrameStatus::ok:return AETHER_OK;case r::FrameStatus::allocation_failed:return AETHER_ERROR_OUT_OF_MEMORY;case r::FrameStatus::wrong_thread:return AETHER_ERROR_INVALID_STATE;default:return AETHER_ERROR_INTERNAL;}}
bool overlaps(const void* a,size_t an,const void* b,size_t bn){if(!a||!b||!an||!bn)return false;auto x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);return x<=y?y-x<an:x-y<bn;}
// Implementación compartida de create() (v1, sin imágenes) y create_v2 (v2,
// con imágenes). La validación propia de cada versión (struct_size/version/
// reserved[]/tabla de imágenes) vive en su wrapper; aquí solo lo común.
// Las imágenes se registran en el ImageRegistry antes de prepare(): un decode
// fallido se reporta en etapa INPUT (LIMIT_EXCEEDED si excede presupuesto,
// UNSUPPORTED si no decodifica, §2.8).
aether_status_t create_impl(int32_t width,int32_t height,const char* html,size_t length,const uint8_t* regular_font,size_t regular_font_size,const uint8_t* bold_font,size_t bold_font_size,const aether_b0_source_image_v1* images,size_t image_count,aether_b0_source_t* out,aether_b0_source_diagnostic_v1* d){
 if(!out)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 *out=nullptr;
 uint32_t stage=AETHER_B0_STAGE_INPUT;std::unique_ptr<aether_b0_source_s> s;
 try {
  if(!html)return report(d,AETHER_ERROR_NULL_ARG,stage,AETHER_B0_REASON_ARGUMENT);
  if(!regular_font||!bold_font||!regular_font_size||!bold_font_size||!length||width<=0||height<=0)return report(d,AETHER_ERROR_INVALID_ARGUMENT,stage,AETHER_B0_REASON_ARGUMENT);
  constexpr size_t maxfonts=16u*1024u*1024u;
  if(width>8192||height>8192||uint64_t(width)*uint64_t(height)*4>64u*1024u*1024u||length>65536||regular_font_size>maxfonts||bold_font_size>maxfonts-regular_font_size)return report(d,AETHER_ERROR_LIMIT_EXCEEDED,stage,AETHER_B0_REASON_LIMIT);
  std::string_view source(html,length);size_t offset=0;while(offset<length)h::decode_scalar_utf8(source,offset);
  s=std::make_unique<aether_b0_source_s>();
  auto code=frame_status(s->frames.resize({width,height,1}));if(code!=AETHER_OK)return report(d,code,stage,reason(code));
  std::string owned(html,length);std::vector<uint8_t> regular(regular_font,regular_font+regular_font_size),bold(bold_font,bold_font+bold_font_size);
  stage=AETHER_B0_STAGE_TEXT;
  s->scene=std::make_unique<h::B0SourceSession>(std::move(owned),std::move(regular),std::move(bold),r::Viewport{width,height,1});
  if(image_count){
   auto registry=std::make_shared<h::ImageRegistry>();
   for(size_t i=0;i<image_count;++i){
    auto icode=registry->add(images[i].source,images[i].bytes,images[i].byte_count);
    if(icode!=AETHER_OK){
     auto mapped=icode==AETHER_ERROR_PARSE_FAILED?AETHER_ERROR_UNSUPPORTED:icode;
     return report(d,mapped,AETHER_B0_STAGE_INPUT,reason(mapped));
    }
   }
   s->scene->set_image_registry(std::move(registry));
  }
  s->prepared=&s->scene->prepare();
  *out=s.release();return report(d,AETHER_OK);
 }catch(const h::B0SourceError& e){return report(d,e.status,e.stage,e.reason,e.node_ordinal,e.command_index,e.source_byte_offset);}
 catch(const h::Error& e){return report(d,e.status,s&&s->scene?s->scene->diagnostic_stage():stage,reason(e.status),s&&s->scene?s->scene->diagnostic_node():UINT64_MAX);}
 catch(const h::BoundaryError& e){return report(d,e.status,s&&s->scene?s->scene->diagnostic_stage():stage,reason(e.status),s&&s->scene?s->scene->diagnostic_node():UINT64_MAX);}
 catch(const std::bad_alloc&){return report(d,AETHER_ERROR_OUT_OF_MEMORY,s&&s->scene?s->scene->diagnostic_stage():stage,AETHER_B0_REASON_ALLOCATION);}
 catch(...){return report(d,AETHER_ERROR_INTERNAL,s&&s->scene?s->scene->diagnostic_stage():stage,AETHER_B0_REASON_INVARIANT);}
}
}
extern "C" {
AETHER_API uint32_t aether_b0_source_capabilities(void){return AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS | AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE | AETHER_B0_SOURCE_STATIC_INLINE_WRAP | AETHER_B0_SOURCE_STATIC_ROUNDED_BOXES;}
AETHER_API size_t aether_b0_source_options_v1_size(void){return sizeof(aether_b0_source_options_v1);}
AETHER_API size_t aether_b0_source_diagnostic_v1_size(void){return sizeof(aether_b0_source_diagnostic_v1);}
AETHER_API size_t aether_b0_source_frame_info_v1_size(void){return sizeof(aether_b0_source_frame_info_v1);}
AETHER_API aether_status_t aether_b0_source_create(int32_t width,int32_t height,const char* html,size_t length,const aether_b0_source_options_v1* o,aether_b0_source_t* out,aether_b0_source_diagnostic_v1* d){
 if(!valid_diag(d))return AETHER_ERROR_INVALID_ARGUMENT;
 if(!out)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 *out=nullptr;
 if(!o)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 if(o->struct_size!=sizeof(*o)||o->version!=1)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 for(auto v:o->reserved)if(v)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 return create_impl(width,height,html,length,o->regular_font,o->regular_font_size,o->bold_font,o->bold_font_size,nullptr,0,out,d);
}
AETHER_API size_t aether_b0_source_options_v2_size(void){return sizeof(aether_b0_source_options_v2);}
AETHER_API aether_status_t aether_b0_source_create_v2(int32_t width,int32_t height,const char* html,size_t length,const aether_b0_source_options_v2* o,aether_b0_source_t* out,aether_b0_source_diagnostic_v1* d){
 if(!valid_diag(d))return AETHER_ERROR_INVALID_ARGUMENT;
 if(!out)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 *out=nullptr;
 if(!o)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 if(o->struct_size!=sizeof(*o)||o->version!=2)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 for(auto v:o->reserved)if(v)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 if(o->image_count&&!o->images)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 for(size_t i=0;i<o->image_count;++i){
  const auto& img=o->images[i];
  if(!img.source||!*img.source||!img.bytes||!img.byte_count)return report(d,AETHER_ERROR_INVALID_ARGUMENT,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 }
 return create_impl(width,height,html,length,o->regular_font,o->regular_font_size,o->bold_font,o->bold_font_size,o->images,o->image_count,out,d);
}
AETHER_API aether_status_t aether_b0_source_render(aether_b0_source_t s,aether_b0_source_diagnostic_v1* d){
 if(!valid_diag(d))return AETHER_ERROR_INVALID_ARGUMENT;
 if(!s)return report(d,AETHER_ERROR_NULL_ARG,AETHER_B0_STAGE_INPUT,AETHER_B0_REASON_ARGUMENT);
 if(s->thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;
 if(s->rendered)return report(d,AETHER_OK);
 auto result=r::software::render_a8(s->frames,*s->prepared,s->scene->masks(),nullptr);
 using S=r::software::Status; aether_status_t code=AETHER_ERROR_INTERNAL;
 switch(result.status){case S::ok:s->rendered=true;return report(d,AETHER_OK);case S::allocation_failed:code=AETHER_ERROR_OUT_OF_MEMORY;break;case S::limit_exceeded:code=AETHER_ERROR_LIMIT_EXCEEDED;break;case S::unsupported:code=AETHER_ERROR_UNSUPPORTED;break;case S::frame_failed:code=frame_status(result.frame_status);break;default:break;}
 return report(d,code,AETHER_B0_STAGE_RASTER,reason(code),UINT64_MAX,result.command_index);
}
AETHER_API aether_status_t aether_b0_source_copy_frame(aether_b0_source_t s,aether_b0_source_frame_info_v1* info,uint8_t* dst,size_t capacity){
 if(!s||!info)return AETHER_ERROR_NULL_ARG;
 if(s->thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;
 if(info->struct_size!=sizeof(*info)||info->version!=1||info->reserved)return AETHER_ERROR_INVALID_ARGUMENT;
 if(!dst&&capacity)return AETHER_ERROR_NULL_ARG;
 if(overlaps(info,sizeof(*info),dst,capacity))return AETHER_ERROR_INVALID_ARGUMENT;
 try {std::optional<r::Frame> f;auto code=frame_status(s->frames.copy_confirmed(f));if(code!=AETHER_OK)return code;if(!f)return AETHER_ERROR_NO_FRAME;
  aether_b0_source_frame_info_v1 candidate{sizeof(*info),1,f->viewport.width,f->viewport.height,AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED,0,f->stride,f->pixels.size(),f->sequence};
  *info=candidate;if(dst&&capacity<f->pixels.size())return AETHER_ERROR_BUFFER_TOO_SMALL;
  if(dst)std::memcpy(dst,f->pixels.data(),f->pixels.size());
  return AETHER_OK;
 }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INTERNAL;}
}
AETHER_API aether_status_t aether_b0_source_destroy(aether_b0_source_t s){if(!s)return AETHER_OK;if(s->thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;delete s;return AETHER_OK;}
}
