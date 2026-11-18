// Public source profile rejects before provider/layout; no private symbols.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether_b0_source.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
static auto read(const char*p){std::ifstream f(p,std::ios::binary);assert(f);return std::vector<uint8_t>{std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_options_v1 options;
static void reject(std::string css,uint32_t stage,std::string body="<div class='row'><div class='item'></div></div>"){
 auto html="<html><head><style>"+css+"</style></head><body>"+body+"</body></html>";
 aether_b0_source_t session{};aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 const auto rc=aether_b0_source_create(640,480,html.data(),html.size(),&options,&session,&d);
 if(rc!=AETHER_ERROR_UNSUPPORTED||session||d.stage!=stage)std::fprintf(stderr,"css=%s expected stage%u actual status%d stage%u reason%u\n",css.c_str(),stage,rc,d.stage,d.reason);
 assert(rc==AETHER_ERROR_UNSUPPORTED&&!session&&d.stage==stage);
}
int main(int argc,char**argv){assert(argc==3);auto r=read(argv[1]),b=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=r.data();options.regular_font_size=r.size();options.bold_font=b.data();options.bold_font_size=b.size();
 for(const char* bad:{"min-width:none","min-height:none","max-width:auto","max-height:auto","width:101%","height:50%"})for(const char* selector:{".item",".absent"}){
  const auto css=std::string(".row{display:flex}")+selector+"{"+bad+"}";
  reject(css,AETHER_B0_STAGE_STYLE);reject(css+".item{width:10px;unknown-property:1}",AETHER_B0_STAGE_STYLE);
  reject(std::string(selector)+"{"+bad+"}",AETHER_B0_STAGE_CSS);
 }
 for(const char* bad:{"flex-wrap:wrap","flex-wrap:wrap-reverse","flex-direction:row-reverse","align-items:baseline","flex:0 1 auto","flex-grow:0","flex-shrink:1","flex-basis:auto","order:0","align-self:auto","align-content:stretch","gap:1%","unknown-property:1","--x:none;min-width:var(--x)"})for(const char* selector:{".row",".absent"})reject(std::string(".row{display:flex}")+selector+"{"+bad+"}",AETHER_B0_STAGE_CSS);
 for(const char* p:{"flex-direction:row","flex-wrap:nowrap","align-items:stretch","justify-content:start","gap:0px"})reject(std::string(".row{display:flex}.item{")+p+"}",AETHER_B0_STAGE_STYLE);
 for(const char* text:{"x","&nbsp;","<span></span>","<strong></strong>","<a></a>","<br>"})reject(".row{display:flex}",AETHER_B0_STAGE_STYLE,std::string("<div class='row'>")+text+"</div>");
 reject(".row{display:flex}.item{min-width:400px;max-width:300px}",AETHER_B0_STAGE_STYLE);
 for(const char* minimum:{"auto","0px"}){
  const auto html=std::string("<html><head><style>.row{display:flex;width:600px}.item{width:400px;min-width:")+minimum+";min-height:auto;max-width:none;max-height:none}</style></head><body><div class='row'><div class='item'><p>x</p></div><div class='item'></div></div></body></html>";
  aether_b0_source_t session{};aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
  const auto rc=aether_b0_source_create(640,480,html.data(),html.size(),&options,&session,&d);
  if(rc!=AETHER_OK)std::fprintf(stderr,"positive minimum%s status%d stage%u reason%u\n",minimum,rc,d.stage,d.reason);
  assert(rc==AETHER_OK&&session);assert(aether_b0_source_destroy(session)==AETHER_OK);
 }
 puts("B3 public source rejection, positive minimum and legacy diagnostic gates PASS");
}
