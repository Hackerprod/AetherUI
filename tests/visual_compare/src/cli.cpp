#include "aether_visual_compare/cli.hpp"
#include "aether_visual_compare/artifact_store.hpp"
#include "aether_visual_compare/resource_manifest.hpp"
#ifdef AVC_NATIVE_HTML_LTR
#include "aether_visual_compare/native_capture.hpp"
#endif
#include <charconv>
#include <chrono>
#include <map>
namespace avc {
namespace {
bool dimension(std::string_view s,unsigned& result) {
 if(s.empty())return false;
 for(char c:s)if(c<'0'||c>'9')return false;
 auto [p,e]=std::from_chars(s.data(),s.data()+s.size(),result);
 return e==std::errc{}&&p==s.data()+s.size()&&result>=1&&result<=16384;
}
}
std::optional<Request> parse_cli(std::span<const std::string_view> args) {
 if(args.empty())return {};
 Request q;
 if(args.size()==1&&args[0]=="--help")return q;
 if(args.size()==1&&args[0]=="--version"){q.command=Command::version;return q;}
 if(args[0]=="inspect")q.command=Command::inspect;else if(args[0]=="capture")q.command=Command::capture;else return {};
 if(args.size()!=13&&args.size()!=19&&args.size()!=21)return {};
 std::map<std::string_view,std::string_view> options;
 for(std::size_t i=1;i<args.size();i+=2) {
  auto key=args[i];if(key!="--html"&&key!="--resource-root"&&key!="--manifest"&&key!="--out"&&key!="--viewport"&&key!="--scale"&&key!="--backend"&&key!="--profile"&&key!="--regular-font"&&key!="--bold-font")return {};
  if(!options.emplace(key,args[i+1]).second)return {};
 }
 if(options.size()==9){
  if(q.command!=Command::capture||options["--backend"]!="native-b0-source"||options.contains("--profile"))return {};
  q.backend="native-b0-source";q.profile=0;
  q.regular_font=options["--regular-font"];q.bold_font=options["--bold-font"];
  if(!valid_resource_path(q.regular_font)||!valid_resource_path(q.bold_font)||q.regular_font==q.bold_font)return {};
 } else if(options.size()==10){
  if(q.command!=Command::capture||options["--backend"]!="native-html-ltr")return {};
  const auto profile=options["--profile"];if(profile!="1"&&profile!="2")return {};
  q.backend="native-html-ltr";q.profile=profile=="1"?1:2;
  q.regular_font=options["--regular-font"];q.bold_font=options["--bold-font"];
  if(!valid_resource_path(q.regular_font)||!valid_resource_path(q.bold_font)||q.regular_font==q.bold_font)return {};
 } else if(options.contains("--backend")||options.contains("--profile")||options.contains("--regular-font")||options.contains("--bold-font"))return {};
 q.html=options["--html"];q.resource_root=options["--resource-root"];q.manifest=options["--manifest"];q.out=options["--out"];
 if(!valid_local_path(q.html)||!valid_local_path(q.resource_root)||!valid_local_path(q.manifest)||!valid_local_path(q.out))return {};
 auto v=options["--viewport"];auto x=v.find('x');if(x==v.npos||!dimension(v.substr(0,x),q.viewport.width)||!dimension(v.substr(x+1),q.viewport.height))return {};
 auto scale=options["--scale"];
 if(scale=="1")q.viewport.scale_quarters=4;else if(scale=="1.25")q.viewport.scale_quarters=5;else if(scale=="1.5")q.viewport.scale_quarters=6;else if(scale=="2")q.viewport.scale_quarters=8;else return {};
 q.viewport.pixel_width=(q.viewport.width*q.viewport.scale_quarters+2)/4;
 q.viewport.pixel_height=(q.viewport.height*q.viewport.scale_quarters+2)/4;
 if(q.viewport.pixel_width>32768||q.viewport.pixel_height>32768)return {};
 return q;
}
int run_cli(std::span<const std::string_view> args,FileSystem& f,const CancellationToken& token,
 const std::function<std::string()>& run_id,std::ostream& out,std::ostream& err) {
 try {
  const auto q=parse_cli(args);if(!q){err<<"INVALID_ARGUMENTS\n";return 2;}
  if(q->command==Command::help){out<<"aether_visual_compare\ninspect|capture --html PATH --resource-root PATH --manifest PATH --out PATH --viewport WIDTHxHEIGHT --scale 1|1.25|1.5|2\nPaths must be absolute local portable paths. Legacy capture is NOT_IMPLEMENTED. Native capture requires --backend native-html-ltr --profile 1|2 --regular-font INVENTORY_PATH --bold-font INVENTORY_PATH and an explicitly enabled installed backend. B0 source capture uses --backend native-b0-source with both fonts and no --profile.\n";return 0;}
  if(q->command==Command::version){
#ifdef AVC_NATIVE_HTML_LTR
   out<<"0.2.0-native-profile\n";
#else
   out<<"0.1.0-contract\n";
#endif
   return 0;
  }
  Report r;r.request=*q;r.run_id=run_id();
  if(r.run_id.size()!=32||r.run_id.find_first_not_of("0123456789abcdef")!=std::string::npos){err<<"INTERNAL_FAILURE\n";return 14;}
  r.stages[0].status="passed";r.stages[0].reason_code="OK";
  if(!prepare_output(f,*q)){err<<"OUTPUT_IO\n";return 13;}
  r.stages[1].status="passed";r.stages[1].reason_code="OK";
  if(q->command==Command::capture&&!q->backend.empty()){
   for(std::size_t i=6;i<9;++i)r.stages[i].reason_code="OUTSIDE_SCOPE";
#ifdef AVC_NATIVE_HTML_LTR
   ValidatedInputs snapshot;
   const auto start=std::chrono::steady_clock::now();
   inspect_resources(f,*q,r,token,&snapshot);
   r.host_inspection_wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
   if(r.errors.empty()){
    if(q->backend=="native-b0-source")apply_b0_capture(r,capture_b0(*q,snapshot,token));
    else apply_native_capture(r,capture_native(*q,snapshot,token));
   }
#else
   r.stages[5].status="not_implemented";r.stages[5].reason_code="NOT_IMPLEMENTED";
   add_error(r,{11,"aether_capture","NOT_IMPLEMENTED",{}});
#endif
  } else if(q->command==Command::capture){
   r.stages[5].status="not_implemented";r.stages[5].reason_code="NOT_IMPLEMENTED";
   add_error(r,{11,"aether_capture","NOT_IMPLEMENTED",{}});
  } else {
   for(std::size_t i=5;i<9;++i)r.stages[i].reason_code="OUTSIDE_SCOPE";
   const auto start=std::chrono::steady_clock::now();inspect_resources(f,*q,r,token);
   r.host_inspection_wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  }
  const auto result=commit_report(f,r,token);
  if(result==13)err<<"OUTPUT_IO\n";else if(result==14)err<<"INTERNAL_FAILURE\n";else if(result!=0)err<<primary_error(r)<<'\n';
  return result;
 } catch(...) {err<<"INTERNAL_FAILURE\n";return 14;}
}
}
