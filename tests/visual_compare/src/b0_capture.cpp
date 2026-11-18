#include "aether_visual_compare/native_capture.hpp"
#include <aether/aether_b0_source.h>
#include <new>
#include <limits>
namespace avc {
void apply_b0_capture(Report& r,const B0CaptureResult& c) {
 if(c.has_metadata)r.b0=c.metadata;else r.b0.reset();
 r.png_bytes=c.png;
 r.stages[5].status=c.captured?"passed":c.exit_code==12?"cancelled":c.exit_code==10?"blocked":"failed";
 r.stages[5].reason_code=c.reason_code;
 if(!c.captured)add_error(r,{c.exit_code,"aether_capture",c.reason_code,{}});
}
namespace {
struct Session {aether_b0_source_t value=nullptr;~Session(){if(value)aether_b0_source_destroy(value);}};
aether_b0_source_diagnostic_v1 diagnostic() {
 aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof(d);d.version=AETHER_B0_SOURCE_VERSION;
 // Poison status so a callee which leaves the structure untouched cannot produce
 // an apparently authentic diagnostic. No raw memory/content is serialized.
 d.status=std::numeric_limits<int32_t>::max();
 d.node_ordinal=d.command_index=d.source_byte_offset=AETHER_B0_NO_INDEX;return d;
}
void record(B0CaptureResult& r,int status,const aether_b0_source_diagnostic_v1* d=nullptr) {
 r.metadata.public_status=status;r.metadata.diagnostic.reset();
 if(d&&d->struct_size==sizeof(*d)&&d->version==AETHER_B0_SOURCE_VERSION&&d->reserved==0&&d->status==status&&d->stage<=AETHER_B0_STAGE_RASTER&&d->reason<=AETHER_B0_REASON_INVARIANT) {
  B0Diagnostic v;v.status=status;v.stage=d->stage;v.reason=d->reason;
  if(d->node_ordinal!=AETHER_B0_NO_INDEX)v.node_ordinal=d->node_ordinal;
  if(d->command_index!=AETHER_B0_NO_INDEX)v.command_index=d->command_index;
  if(d->source_byte_offset!=AETHER_B0_NO_INDEX)v.source_byte_offset=d->source_byte_offset;
  r.metadata.diagnostic=v;
 }
 if(status==AETHER_OK)return;
 switch(status) {
 case AETHER_ERROR_NULL_ARG:case AETHER_ERROR_INVALID_ARGUMENT:case AETHER_ERROR_PARSE_FAILED:r.exit_code=2;r.reason_code="NATIVE_INVALID_ARGUMENT";break;
 case AETHER_ERROR_UNSUPPORTED:r.exit_code=10;r.reason_code="NATIVE_UNSUPPORTED";break;
 case AETHER_ERROR_LIMIT_EXCEEDED:case AETHER_ERROR_EXHAUSTED:r.exit_code=8;r.reason_code="NATIVE_LIMIT_EXCEEDED";break;
 case AETHER_ERROR_OUT_OF_MEMORY:r.exit_code=14;r.reason_code="NATIVE_OUT_OF_MEMORY";break;
 default:r.exit_code=14;r.reason_code="NATIVE_INTERNAL";
 }
}
}
B0CaptureResult capture_b0(const Request& q,const ValidatedInputs& inputs,const CancellationToken& token) {
 B0CaptureResult r;
 auto reject=[&](int code,const char* reason){r.captured=false;r.png.clear();r.exit_code=code;r.reason_code=reason;return r;};
 try {
 if(token.cancelled())return reject(12,"CANCELLED");
 if(q.backend!="native-b0-source"||q.profile!=0)return reject(2,"NATIVE_INVALID_ARGUMENT");
 if(q.viewport.scale_quarters!=4)return reject(10,"NATIVE_UNSUPPORTED");
 if(!q.viewport.width||!q.viewport.height||q.viewport.width>2048||q.viewport.height>2048||inputs.html.size()>65536)return reject(8,"NATIVE_LIMIT_EXCEEDED");
 const ValidatedResource *regular=nullptr,*bold=nullptr;size_t total=0;
 for(const auto& a:inputs.assets) {
  if(a.resource.role!="font"||a.resource.mime!="font/ttf")return reject(6,"NATIVE_RESOURCE_UNSUPPORTED");
  if(a.bytes.size()>16u*1024u*1024u-total)return reject(8,"NATIVE_LIMIT_EXCEEDED");total+=a.bytes.size();
  if(a.resource.path==q.regular_font){if(regular)return reject(6,"NATIVE_FONT_SELECTION");regular=&a;}
  if(a.resource.path==q.bold_font){if(bold)return reject(6,"NATIVE_FONT_SELECTION");bold=&a;}
 }
 if(!regular||!bold||regular==bold||regular->bytes.empty()||bold->bytes.empty())return reject(6,"NATIVE_FONT_SELECTION");
 if(inputs.assets.size()!=2)return reject(6,"NATIVE_RESOURCE_UNSUPPORTED");
 auto& m=r.metadata;m.regular_sha256=regular->resource.sha256;m.bold_sha256=bold->resource.sha256;
 // Sample cancellation on both sides of every synchronous ABI operation.
 if(token.cancelled())return reject(12,"CANCELLED");
 m.abi_version=aether_abi_version();
 if(token.cancelled())return reject(12,"CANCELLED");
 m.runtime_version=std::to_string(AETHER_ABI_VERSION_GET_MAJOR(m.abi_version))+"."+std::to_string(AETHER_ABI_VERSION_GET_MINOR(m.abi_version))+"."+std::to_string(AETHER_ABI_VERSION_GET_PATCH(m.abi_version));
 r.has_metadata=true;
 m.capabilities=aether_b0_source_capabilities();
 if(token.cancelled())return reject(12,"CANCELLED");
 const auto options_size=aether_b0_source_options_v1_size();
 if(token.cancelled())return reject(12,"CANCELLED");
 const auto diagnostic_size=aether_b0_source_diagnostic_v1_size();
 if(token.cancelled())return reject(12,"CANCELLED");
 const auto frame_size=aether_b0_source_frame_info_v1_size();
 if(token.cancelled())return reject(12,"CANCELLED");
 constexpr unsigned required=AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS|AETHER_B0_SOURCE_STATIC_SEMANTIC_PAGE|AETHER_B0_SOURCE_STATIC_INLINE_WRAP;
 if(m.abi_version!=AETHER_ABI_VERSION_CURRENT||(m.capabilities&required)!=required||options_size!=sizeof(aether_b0_source_options_v1)||diagnostic_size!=sizeof(aether_b0_source_diagnostic_v1)||frame_size!=sizeof(aether_b0_source_frame_info_v1))return reject(14,"NATIVE_ABI_MISMATCH");
 aether_b0_source_options_v1 options{};options.struct_size=sizeof(options);options.version=AETHER_B0_SOURCE_VERSION;
 options.regular_font=reinterpret_cast<const uint8_t*>(regular->bytes.data());options.regular_font_size=regular->bytes.size();options.bold_font=reinterpret_cast<const uint8_t*>(bold->bytes.data());options.bold_font_size=bold->bytes.size();
 Session session;auto d=diagnostic();
 if(token.cancelled())return reject(12,"CANCELLED");
 auto status=aether_b0_source_create(q.viewport.width,q.viewport.height,inputs.html.data(),inputs.html.size(),&options,&session.value,&d);record(r,status,&d);
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK)return r;
 d=diagnostic();
 if(token.cancelled())return reject(12,"CANCELLED");
 status=aether_b0_source_render(session.value,&d);record(r,status,&d);
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK)return r;
 aether_b0_source_frame_info_v1 query{};query.struct_size=sizeof(query);query.version=AETHER_B0_SOURCE_VERSION;
 if(token.cancelled())return reject(12,"CANCELLED");
 status=aether_b0_source_copy_frame(session.value,&query,nullptr,0);record(r,status);
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK)return r;
 auto valid=[&](const aether_b0_source_frame_info_v1& i){return i.struct_size==sizeof(i)&&i.version==AETHER_B0_SOURCE_VERSION&&i.reserved==0&&i.width==static_cast<int>(q.viewport.width)&&i.height==static_cast<int>(q.viewport.height)&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED&&i.stride==uint64_t(q.viewport.width)*4&&i.byte_count==uint64_t(q.viewport.width)*q.viewport.height*4&&i.byte_count<=std::numeric_limits<size_t>::max()&&i.sequence==1;};
 if(!valid(query))return reject(14,"NATIVE_FRAME_INVALID");
 std::vector<uint8_t> pixels(static_cast<size_t>(query.byte_count));
 aether_b0_source_frame_info_v1 copied{};copied.struct_size=sizeof(copied);copied.version=AETHER_B0_SOURCE_VERSION;
 if(token.cancelled())return reject(12,"CANCELLED");
 status=aether_b0_source_copy_frame(session.value,&copied,pixels.data(),pixels.size());record(r,status);
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK)return r;
 if(!valid(copied))return reject(14,"NATIVE_FRAME_INVALID");
 if(token.cancelled())return reject(12,"CANCELLED");
 status=aether_b0_source_destroy(session.value);record(r,status);if(status==AETHER_OK)session.value=nullptr;
 if(token.cancelled())return reject(12,"CANCELLED");
 if(status!=AETHER_OK)return r;
 r.png=encode_native_png(q.viewport.width,q.viewport.height,pixels);
 if(token.cancelled())return reject(12,"CANCELLED");
 m.width=q.viewport.width;m.height=q.viewport.height;m.stride=static_cast<unsigned>(copied.stride);m.sequence=copied.sequence;
 r.captured=true;r.exit_code=0;r.reason_code="OK";return r;
 }catch(const std::bad_alloc&){return reject(14,"NATIVE_OUT_OF_MEMORY");}catch(...){return reject(14,"NATIVE_INTERNAL");}
}
}
