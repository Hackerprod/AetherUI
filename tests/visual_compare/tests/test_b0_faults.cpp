// Deterministic ABI fault injection only: not renderer or visual evidence.
#include "aether_visual_compare/native_capture.hpp"
#include <aether/aether_b0_source.h>
#include <iostream>
#include <cstring>
using namespace avc;
static int mode=0,copies=0,destroys=0;
extern "C" {
uint32_t aether_abi_version(){return AETHER_ABI_VERSION_CURRENT;}
uint32_t aether_b0_source_capabilities(){return 7u|128u;}
size_t aether_b0_source_options_v1_size(){return sizeof(aether_b0_source_options_v1);}
size_t aether_b0_source_diagnostic_v1_size(){return sizeof(aether_b0_source_diagnostic_v1);}
size_t aether_b0_source_frame_info_v1_size(){return sizeof(aether_b0_source_frame_info_v1);}
aether_status_t aether_b0_source_create(int32_t,int32_t,const char*,size_t,const aether_b0_source_options_v1*,aether_b0_source_t* out,aether_b0_source_diagnostic_v1* d){
 if(mode==1)return AETHER_ERROR_INVALID_ARGUMENT; // untouched diagnostic
 if(mode==2||mode==3){d->status=AETHER_ERROR_UNSUPPORTED;d->stage=3;d->reason=3;d->reserved=mode==2?1:0;return AETHER_ERROR_UNSUPPORTED;}
 *out=reinterpret_cast<aether_b0_source_t>(uintptr_t(1));return AETHER_OK;
}
aether_status_t aether_b0_source_render(aether_b0_source_t,aether_b0_source_diagnostic_v1*){return AETHER_OK;}
aether_status_t aether_b0_source_copy_frame(aether_b0_source_t,aether_b0_source_frame_info_v1* f,uint8_t* p,size_t n){
 ++copies;if(mode==9&&copies==2)return AETHER_OK;
 if(mode==4)return AETHER_ERROR_INVALID_ARGUMENT;
 f->width=1;f->height=1;f->format=AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED;f->stride=4;f->byte_count=4;f->sequence=1;
 if(mode==5)f->version=2;
 if(mode==6&&copies==2)f->reserved=1;
 if(mode==7)f->byte_count=UINT64_MAX;
 if(p&&n>=4)std::memset(p,255,4);return AETHER_OK;
}
aether_status_t aether_b0_source_destroy(aether_b0_source_t){++destroys;return mode==8?AETHER_ERROR_INVALID_ARGUMENT:AETHER_OK;}
}
#define CHECK(x) do{if(!(x)){std::cerr<<"FAIL "<<__LINE__<<'\n';return 1;}}while(0)
int main(){
 Request q;q.backend="native-b0-source";q.profile=0;q.regular_font="r.ttf";q.bold_font="b.ttf";
 ValidatedInputs in;in.html="dummy";
 for(auto path:{q.regular_font,q.bold_font}){ValidatedResource a;a.resource.path=path;a.resource.role="font";a.resource.mime="font/ttf";a.resource.sha256=std::string(64,'a');a.bytes="dummy";in.assets.push_back(a);}
 auto run=[&](){copies=destroys=0;return capture_b0(q,in,{});};
 auto r=run();CHECK(r.captured&&r.metadata.capabilities==135&&!r.metadata.diagnostic&&destroys==1);
 for(mode=1;mode<=9;++mode){r=run();CHECK(!r.captured&&r.png.empty());
  if(mode==3){CHECK(r.metadata.diagnostic&&!r.metadata.diagnostic->node_ordinal&&!r.metadata.diagnostic->command_index&&!r.metadata.diagnostic->source_byte_offset);}
  else CHECK(!r.metadata.diagnostic);
  if((mode>=5&&mode<=7)||mode==9)CHECK(r.reason_code=="NATIVE_FRAME_INVALID");
  if(mode==1||mode==4||mode==8)CHECK(r.metadata.public_status==AETHER_ERROR_INVALID_ARGUMENT);
 }
 std::cout<<"PASS injected untouched/malformed diagnostic, sentinel, copy/destroy status, future capability, version/reserved/length corruption\n";
}
