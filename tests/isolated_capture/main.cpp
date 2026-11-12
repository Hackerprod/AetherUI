// Linux-only manual one-shot harness. No public engine handles escape this process.
#include <aether/aether.h>
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/style_runtime/css_variable_tokens.h"
#include "../../native/src/rendering/software/software_renderer.h"
#include "../../native/src/rendering/display_list_types.h"
#include "../../native/src/rendering/preparation/native_command_source.h"
#include "../software_render_smoke/png_writer.h"
#include "../visual_compare/src/sha256.hpp"
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <iostream>
#include <set>
#include <charconv>
#include <ctime>
#include <atomic>
namespace p=aether::rendering::preparation;
using Clock=std::chrono::steady_clock;
constexpr size_t max_bytes=2*1024*1024;
struct Failure {const char* reason;};
std::ofstream logstream;
void require(bool ok,const char* reason){if(!ok)throw Failure{reason};}
struct Stage {const char* name;Clock::time_point t=Clock::now(); explicit Stage(const char* n):name(n){logstream<<"stage_started="<<name<<std::endl;} ~Stage(){logstream<<name<<"_wall_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-t).count()<<std::endl;}};
struct Objects {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 ~Objects(){if(builder)aether_display_list_builder_destroy(builder);if(layout)aether_layout_result_destroy(layout);if(engine)aether_layout_destroy(engine);if(styles)aether_style_map_destroy(styles);if(runtime)aether_style_runtime_destroy(runtime);if(doc)aether_doc_destroy(doc);}
};
aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void* state){*static_cast<bool*>(state)=true;return {};}
#include "../../native/src/style_runtime/style_property_registry_internal.h"
#include "h5_profile.h"
#include "inventory.h"
#include "h3_profile.h"
#include "h4_profile.h"
void check_css(const std::string& s,const h5::Analysis& analysis){
 css_variable_budget budget{max_bytes*4,false};css_variable_lexer lexer(s.data(),s.size(),0,&budget);
 for(;;){auto t=lexer.next();if(t.kind==css_variable_token_kind::end)break;
  require(!budget.exhausted&&t.kind!=css_variable_token_kind::error,"CSS_LEXICAL_OR_WORK_LIMIT");
  require(t.serialization!=css_variable_serialization::at_keyword||analysis.at_rules.count(t.begin),"CSS_AT_RULE_UNSUPPORTED");
  require(t.serialization!=css_variable_serialization::url&&t.serialization!=css_variable_serialization::bad_url,"CSS_RESOURCE_UNSUPPORTED");
  require(t.kind!=css_variable_token_kind::function||analysis.functions.count(t.begin),"CSS_FUNCTION_UNSUPPORTED");
 }
 require(!budget.exhausted,"CSS_WORK_LIMIT");
 require(analysis.valid,"CSS_CONTEXT_PROFILE_UNSUPPORTED");
}
std::string read_input(const char* path){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);require(fd>=0,"INPUT_OPEN_FAILED");
 struct stat st{};if(fstat(fd,&st)||!S_ISREG(st.st_mode)){close(fd);throw Failure{"INPUT_NOT_REGULAR"};}
 std::string data;char buffer[16384];
 for(;;){ssize_t n=read(fd,buffer,sizeof buffer);if(n<0){close(fd);throw Failure{"INPUT_READ_FAILED"};}if(!n)break;
  if(static_cast<size_t>(n)>max_bytes-data.size()){close(fd);throw Failure{"INPUT_BYTE_LIMIT"};}data.append(buffer,static_cast<size_t>(n));}
 close(fd);require(data.find('\0')==std::string::npos,"INPUT_NUL");return data;
}
int capture(const char* input,const std::string& out,int width,int height,bool geometry){
 logstream.open(out+"/worker.log",std::ios::out);if(!logstream)return 8;
 logstream<<"format=H3-initial-canvas\ncanvas_mode=ENABLED_INITIAL_EMPTY\njavascript_execution=NONE\ninventory_scope=DOM_EXTRACTED_CSS_AND_ELEMENTS\ninventory_locations=CSS_SOURCE_LOCAL_BYTE_OFFSETS_NOT_HTML\ninventory_semantic_context=NOT_PROVEN\noutput_domain=AetherResolvedOutput\ncss_eligibility=Unknown\ncss_reason=PARSER_COVERAGE_UNPROVEN\nauthored_css_validation=NOT_RUN\nflexbox_semantic_acceptance=false\nchromium=NOT_RUN\nnetwork_resolver=NONE\nhost_font_lookup=NONE\ninternal_parse_layout_work_limits=NOT_IMPLEMENTED\nprocess_limits=AS_1GiB_CPU_5s_STACK_8MiB_WALL_10s\ninput_limit_bytes="<<max_bytes<<"\nviewport="<<width<<"x"<<height<<"\ndpr=1\n"<<std::flush;
 try{
  std::string html;{Stage s("read");html=read_input(input);}
  logstream<<"input_sha256="<<avc::sha256(html)<<"\ninput_bytes="<<html.size()<<std::endl;
  require(geometry,"REQUESTED_CSS_DOMAIN_UNAVAILABLE");
  // Prepare immutable builtin definitions before the side-effect-free supports probe.
  style_property_registry_init();
  Objects o;require(aether_doc_create(&o.doc)==AETHER_OK,"DOC_CREATE_FAILED");
  {Stage s("parse");require(aether_doc_parse_html(o.doc,html.c_str())==AETHER_OK,"PARSE_FAILED");}
  std::vector<std::string> sheets, inline_sheets;std::set<std::string> blockers;
  std::vector<aether_node_s*> canvases, inert_scripts;
  {Stage s("dom_preflight");
   struct Item{aether_node_s* n;size_t depth;bool metadata;};std::vector<Item> todo{{o.doc,0,false}};size_t count=0;
   while(!todo.empty()){
    auto [n,depth,metadata]=todo.back();todo.pop_back();require(++count<=50000&&depth<=128,"POST_PARSE_DOM_LIMIT");
    const auto& tag=n->tag_name;bool meta=metadata||tag=="head"||tag=="style"||tag=="title"||tag=="canvas"||h3::json(n);
    if(tag=="canvas")canvases.push_back(n);
    if(h3::json(n))inert_scripts.push_back(n);
    if(tag=="canvas"||h3::json(n))diagnostic::element(n,count,"H3_CANDIDATE_REQUIRES_PROFILE_VALIDATION");
    if(tag=="style") {std::string css;for(auto* c:n->children){if(c->type!=AETHER_NODE_TEXT)blockers.insert("STYLE_CONTENT_UNSUPPORTED");else css+=c->text;}sheets.push_back(std::move(css));}
    if((tag=="script"&&!h3::json(n))||tag=="link"||tag=="img"||tag=="svg"||tag=="video"||tag=="audio"||tag=="object"||tag=="iframe"||tag=="embed"||tag=="base"){blockers.insert("RESOURCE_OR_SCRIPT_ELEMENT_UNSUPPORTED");diagnostic::element(n,count);}
    if(n->type==AETHER_NODE_TEXT&&!meta&&n->text.find_first_not_of(" \t\r\n\f")!=std::string::npos)blockers.insert("TEXT_RENDERER_UNAVAILABLE");
    for(const auto& [key,value]:n->attributes){if(h3::lower(key).starts_with("on"))blockers.insert("EVENT_HANDLER_UNSUPPORTED");if(key=="style"){inline_sheets.push_back(value);auto analysis=h5::analyze(value,false);diagnostic::css(value,"inline-dom-"+std::to_string(count),blockers,analysis);try{check_css(value,analysis);}catch(Failure f){blockers.insert(f.reason);}}if(key=="dir")blockers.insert("HTML_DIR_UNSUPPORTED");}
    for(auto it=n->children.rbegin();it!=n->children.rend();++it)todo.push_back({*it,depth+1,meta});
   }
   logstream<<"dom_nodes="<<count<<"\nstyle_blocks="<<sheets.size()<<std::endl;
   size_t sheet_index=0;for(const auto& css:sheets){auto analysis=h5::analyze(css,true);diagnostic::css(css,"style-block-"+std::to_string(sheet_index++),blockers,analysis);try{check_css(css,analysis);}catch(Failure f){blockers.insert(f.reason);}}
   if(canvases.size()>256)blockers.insert("CANVAS_COUNT_LIMIT");
   if(!canvases.empty()){
    for(const auto& css:sheets)try{h3::authored(css,true);}catch(Failure f){blockers.insert(f.reason);}
    for(const auto& css:inline_sheets)try{h3::authored(css,false);}catch(Failure f){blockers.insert(f.reason);}
   }
   for(const auto& b:blockers)logstream<<"blocker="<<b<<std::endl;
   require(blockers.empty(),"PREFLIGHT_UNSUPPORTED");
  }
  aether_style_environment_t env{static_cast<float>(width),static_cast<float>(height),1,16,0,0,0};aether_style_runtime_options_t options{};
  bool text_requested=false;
  {Stage s("style");require(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK,"STYLE_CREATE_FAILED");
   const char* ua=aether_style_runtime_defaults_css();logstream<<"ua_sha256="<<avc::sha256(ua)<<std::endl;
   int64_t handle{};int32_t nr{};require(aether_style_runtime_add_stylesheet(o.runtime,ua,0,"builtin-UA",&handle,&nr)==AETHER_OK,"UA_FAILED");
   size_t index=0;for(const auto& css:sheets){logstream<<"style_"<<index++<<"_compiled_bytes_sha256="<<avc::sha256(css)<<std::endl;require(aether_style_runtime_add_stylesheet(o.runtime,css.c_str(),1,"DOM-style",&handle,&nr)==AETHER_OK,"SHEET_FAILED");}
   aether_style_update_t* updates=nullptr;int count=aether_style_runtime_resolve_subtree(o.runtime,aether_doc_get_body(o.doc),&updates);require(count>=0,"STYLE_RESOLVE_FAILED");aether_style_updates_free(updates,count);
   require(aether_style_runtime_create_resolved_style_map(o.runtime,o.doc,&o.styles)==AETHER_OK,"STYLE_MAP_FAILED");
  }
  h3::scripts(o.styles,inert_scripts);auto expected=h3::before(o.styles,canvases);
  {Stage s("layout");require(aether_layout_create(no_text,&text_requested,&o.engine)==AETHER_OK,"LAYOUT_CREATE_FAILED");set_initial_canvas_mode(o.engine,true);require(aether_layout_layout(o.engine,o.doc,o.styles,width,height,nullptr,nullptr,&o.layout)==AETHER_OK,"LAYOUT_FAILED");require(!text_requested,"TEXT_MEASURE_REQUESTED_UNSUPPORTED");}
  h3::after(o.layout,o.styles,expected);
  logstream<<"canvas_entries="<<o.engine->initial_canvas_entries.size()<<"\ncanvas_registry_bytes="<<o.engine->initial_canvas_entries.size()*sizeof(decltype(o.engine->initial_canvas_entries)::value_type)<<"\ncanvas_registry_bytes_scope=ENTRY_PAYLOAD_EXCLUDES_ALLOCATOR_OVERHEAD\n";
  std::unique_ptr<aether::rendering::NativePaintList> list;
  {Stage s("display");require(aether_display_list_builder_create(no_text,&text_requested,&o.builder)==AETHER_OK,"BUILDER_FAILED");require(aether::rendering::build_native_opacity(o.builder,o.layout,o.styles,nullptr,list)==AETHER_OK,"NATIVE_PAINT_UNSUPPORTED");require(!text_requested,"TEXT_MEASURE_REQUESTED_UNSUPPORTED");}
  auto snap=p::snapshot_native_geometry(*list,p::ValidationDomain::ResolvedGeometryOnly);require(std::holds_alternative<p::Snapshot>(snap),"SNAPSHOT_UNSUPPORTED");
  auto frame=p::prepare(std::get<p::Snapshot>(snap));require(std::holds_alternative<p::PreparedFrame>(frame),"PREPARE_UNSUPPORTED");
  aether::rendering::FrameStore store;std::optional<aether::rendering::Frame> image;
  {Stage s("raster");require(store.resize({width,height,1})==aether::rendering::FrameStatus::ok,"FRAME_LIMIT");require(aether::rendering::software::render(store,std::get<p::PreparedFrame>(frame)).status==aether::rendering::software::Status::ok,"RASTER_UNSUPPORTED");require(store.copy_confirmed(image)==aether::rendering::FrameStatus::ok&&image.has_value(),"FRAME_FAILED");}
  {Stage s("png");auto bytes=smoke_png::png(smoke_png::rgba(*image),width,height);std::ofstream f(out+"/aether.png.pending",std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();require(bool(f),"PNG_WRITE_FAILED");}
  logstream<<"worker_status=GEOMETRY_RENDERED\npaint_coverage=AETHER_EMITTED_COMMANDS_ONLY\n"<<std::flush;return 0;
 }catch(Failure f){logstream<<"worker_status=REJECTED\nreason="<<f.reason<<std::endl;return 3;}
 catch(const std::bad_alloc&){logstream<<"worker_status=FAILED\nreason=MEMORY_LIMIT_OR_ALLOCATION\n"<<std::flush;return 4;}
 catch(...){logstream<<"worker_status=FAILED\nreason=UNEXPECTED_EXCEPTION\n"<<std::flush;return 5;}
}
int number(const char* s){int n=0;auto e=s+std::strlen(s);auto r=std::from_chars(s,e,n);return r.ec==std::errc{}&&r.ptr==e?n:0;}
int main(int argc,char** argv){
 if(argc!=7||(std::string(argv[1])!="--geometry"&&std::string(argv[1])!="--css")){std::cerr<<"usage: capture --geometry|--css INPUT NEW_OUTPUT_DIRECTORY WIDTH HEIGHT DPR(1)\n";return 2;}
 int width=number(argv[4]),height=number(argv[5]);if(width<1||height<1||width>2048||height>2048||std::string(argv[6])!="1")return 2;
 std::string out=argv[3];if(mkdir(out.c_str(),0700)!=0){std::cerr<<"Output must be a new directory\n";return 2;}
 const auto started=Clock::now();pid_t child=fork();if(child<0)return 8;
 if(child==0){
  rlimit as{1024ULL*1024*1024,1024ULL*1024*1024},cpu{5,5},stack{8*1024*1024,8*1024*1024},core{0,0};
  if(setrlimit(RLIMIT_AS,&as)||setrlimit(RLIMIT_CPU,&cpu)||setrlimit(RLIMIT_STACK,&stack)||setrlimit(RLIMIT_CORE,&core))_exit(9);
  #ifdef H1_TEST_LIMIT_PROBES
  // Test-build only. Not reachable in the production CLI.
  if(std::string(argv[2])=="CPU_PROBE")for(;;)std::atomic_signal_fence(std::memory_order_seq_cst);
  if(std::string(argv[2])=="WALL_PROBE")for(;;)pause();
  if(std::string(argv[2])=="MEMORY_PROBE"){void* memory=std::malloc(2ULL*1024*1024*1024);_exit(memory?44:42);}
#endif
  int rc=capture(argv[2],out,width,height,std::string(argv[1])=="--geometry");logstream.close();_exit(rc);
 }
 int status=0;bool timeout=false;struct rusage usage{};
 for(;;){pid_t got=wait4(child,&status,WNOHANG,&usage);if(got==child)break;if(got<0){kill(child,SIGKILL);wait4(child,&status,0,&usage);return 8;}
  if(Clock::now()-started>std::chrono::seconds(10)){timeout=true;kill(child,SIGKILL);wait4(child,&status,0,&usage);break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
 }
 bool ok=!timeout&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
 if(ok&&rename((out+"/aether.png.pending").c_str(),(out+"/aether.png").c_str()))ok=false;
 if(!ok)unlink((out+"/aether.png.pending").c_str());
 std::ofstream report(out+"/report.log.pending");std::ifstream worker(out+"/worker.log");if(worker)report<<worker.rdbuf();
 report<<"supervisor_status="<<(ok?"GEOMETRY_RENDERED":timeout?"TIMEOUT":"FAILED_OR_REJECTED")<<"\nworker_exit="<<(WIFEXITED(status)?WEXITSTATUS(status):-1)<<"\nworker_signal="<<(WIFSIGNALED(status)?WTERMSIG(status):0)<<"\nprocess_wall_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-started).count()<<"\nchild_cpu_seconds="<<usage.ru_utime.tv_sec+usage.ru_stime.tv_sec+(usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1e6<<"\nchild_peak_rss_kib="<<usage.ru_maxrss<<"\nmetrics_scope=Linux_wait4_child_only\n";
 report.close();if(!report||rename((out+"/report.log.pending").c_str(),(out+"/report.log").c_str()))return 8;
 return ok?0:3;
}
