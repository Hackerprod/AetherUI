#include "aether_visual_compare/native_capture.hpp"
#include <aether/aether.h>
#include <new>
namespace avc {
void apply_native_capture(Report& r,const NativeCaptureResult& c){
 NativeCaptureMetadata m;m.profile=c.profile;m.limitations=c.limitations;m.abi_version=c.abi_version;m.runtime_version=c.runtime_version;m.public_status=c.public_status;m.width=c.width;m.height=c.height;m.stride=c.stride;m.sequence=c.sequence;m.capabilities=c.capabilities;m.regular_sha256=c.regular_sha256;m.bold_sha256=c.bold_sha256;
 if(c.abi_version)r.native=std::move(m);else r.native.reset();
 r.png_bytes=c.png;
 r.stages[5].status=c.captured?"passed":c.exit_code==12?"cancelled":c.exit_code==10?"blocked":"failed";r.stages[5].reason_code=c.reason_code;
 if(!c.captured)add_error(r,{c.exit_code,"aether_capture",c.reason_code,{}});
}
namespace {
struct Surface {aether_surface_t value=nullptr;~Surface(){if(value)aether_surface_destroy(value);}};
void fail(NativeCaptureResult& r,int status){r.public_status=status;switch(status){
 case AETHER_ERROR_NULL_ARG:case AETHER_ERROR_INVALID_ARGUMENT:case AETHER_ERROR_PARSE_FAILED:r.exit_code=2;r.reason_code="NATIVE_INVALID_ARGUMENT";break;
 case AETHER_ERROR_UNSUPPORTED:r.exit_code=10;r.reason_code="NATIVE_UNSUPPORTED";break;
 case AETHER_ERROR_LIMIT_EXCEEDED:case AETHER_ERROR_EXHAUSTED:r.exit_code=8;r.reason_code="NATIVE_LIMIT_EXCEEDED";break;
 case AETHER_ERROR_OUT_OF_MEMORY:r.exit_code=14;r.reason_code="NATIVE_OUT_OF_MEMORY";break;
 default:r.exit_code=14;r.reason_code="NATIVE_INTERNAL";}}
}
NativeCaptureResult capture_native(const Request& q,const ValidatedInputs& inputs,const CancellationToken& token){
 NativeCaptureResult r;r.profile=q.profile;
 const auto reject=[&](int code,const char* reason){r.exit_code=code;r.reason_code=reason;return r;};
 try {
 if(token.cancelled())return reject(12,"CANCELLED");
 if(q.backend!="native-html-ltr"||(q.profile!=1&&q.profile!=2))return reject(2,"NATIVE_INVALID_ARGUMENT");
 if(q.viewport.scale_quarters!=4)return reject(10,"NATIVE_UNSUPPORTED");
 if(!q.viewport.width||!q.viewport.height||q.viewport.width>2048||q.viewport.height>2048||inputs.html.size()>65536)return reject(8,"NATIVE_LIMIT_EXCEEDED");
 const ValidatedResource *regular=nullptr,*bold=nullptr;std::size_t total=0;
 for(const auto& a:inputs.assets){
  if(a.resource.role!="font"||a.resource.mime!="font/ttf")return reject(6,"NATIVE_RESOURCE_UNSUPPORTED");
  if(a.bytes.size()>16u*1024u*1024u-total)return reject(8,"NATIVE_LIMIT_EXCEEDED");total+=a.bytes.size();
  if(a.resource.path==q.regular_font){if(regular)return reject(6,"NATIVE_FONT_SELECTION");regular=&a;}
  if(a.resource.path==q.bold_font){if(bold)return reject(6,"NATIVE_FONT_SELECTION");bold=&a;}
 }
 if(!regular||!bold||regular==bold||regular->bytes.empty()||bold->bytes.empty())return reject(6,"NATIVE_FONT_SELECTION");
 // Refuse unused inventory: validated is not synonymous with consumed.
 if(inputs.assets.size()!=2)return reject(6,"NATIVE_RESOURCE_UNSUPPORTED");
 r.regular_sha256=regular->resource.sha256;r.bold_sha256=bold->resource.sha256;
 r.abi_version=aether_abi_version();r.capabilities=aether_abi_capabilities();
 r.runtime_version=std::to_string(AETHER_ABI_VERSION_GET_MAJOR(r.abi_version))+"."+std::to_string(AETHER_ABI_VERSION_GET_MINOR(r.abi_version))+"."+std::to_string(AETHER_ABI_VERSION_GET_PATCH(r.abi_version));
 constexpr auto required=AETHER_CAPABILITY_NATIVE_SURFACE|AETHER_CAPABILITY_NATIVE_HTML_LTR;
 if(r.abi_version!=AETHER_ABI_VERSION_CURRENT||(r.capabilities&required)!=required||aether_surface_frame_info_size()!=sizeof(aether_surface_frame_info)||aether_html_ltr_options_size()!=sizeof(aether_html_ltr_options))return reject(14,"NATIVE_ABI_MISMATCH");
 r.limitations=aether_html_ltr_profile_limitations(q.profile);if(r.limitations!=(q.profile==1?3u:15u))return reject(14,"NATIVE_ABI_MISMATCH");
 aether_html_ltr_options options{};options.struct_size=sizeof(options);options.profile=q.profile;
 options.regular_font=reinterpret_cast<const std::uint8_t*>(regular->bytes.data());options.regular_font_size=regular->bytes.size();options.bold_font=reinterpret_cast<const std::uint8_t*>(bold->bytes.data());options.bold_font_size=bold->bytes.size();
 Surface surface;auto status=aether_surface_create(q.viewport.width,q.viewport.height,&surface.value);if(status!=AETHER_OK){fail(r,status);return r;}
 if(token.cancelled())return reject(12,"CANCELLED");
 status=aether_surface_render_html_ltr(surface.value,inputs.html.data(),inputs.html.size(),&options);
 // Synchronous ABI has no interruption. Discard any frame after cancellation.
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK){fail(r,status);return r;}
 aether_surface_frame_info query{};query.struct_size=sizeof(query);status=aether_surface_copy_frame(surface.value,&query,nullptr,0);if(status!=AETHER_OK){fail(r,status);return r;}
 const auto valid=[&](const aether_surface_frame_info& i){return i.struct_size==sizeof(i)&&i.width==static_cast<int>(q.viewport.width)&&i.height==static_cast<int>(q.viewport.height)&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED&&i.stride==std::uint64_t(q.viewport.width)*4&&i.byte_count==std::uint64_t(q.viewport.width)*q.viewport.height*4&&i.sequence==1;};
 if(!valid(query))return reject(14,"NATIVE_FRAME_INVALID");
 std::vector<std::uint8_t> pixels(static_cast<std::size_t>(query.byte_count));aether_surface_frame_info copied{};copied.struct_size=sizeof(copied);
 status=aether_surface_copy_frame(surface.value,&copied,pixels.data(),pixels.size());if(status!=AETHER_OK){fail(r,status);return r;}
 if(!valid(copied)||copied.sequence!=query.sequence)return reject(14,"NATIVE_FRAME_INVALID");
 if(token.cancelled())return reject(12,"CANCELLED");
 r.png=encode_native_png(q.viewport.width,q.viewport.height,pixels);
 if(token.cancelled()){r.png.clear();return reject(12,"CANCELLED");}
 r.width=q.viewport.width;r.height=q.viewport.height;r.stride=static_cast<unsigned>(copied.stride);r.sequence=copied.sequence;r.captured=true;r.exit_code=0;r.reason_code="OK";return r;
 }catch(const std::bad_alloc&){return reject(14,"NATIVE_OUT_OF_MEMORY");}catch(...){return reject(14,"NATIVE_INTERNAL");}
}
}
