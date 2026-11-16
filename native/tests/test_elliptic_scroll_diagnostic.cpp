#ifdef NDEBUG
#error Assertions required
#endif
#include <aether/aether.h>
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;
constexpr int W=32,H=32;
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){assert(false&&"text outside HTML fixture scope");return {};}
static std::string readfile(const std::string& path){std::ifstream f(path);assert(f);std::ostringstream b;b<<f.rdbuf();assert(!f.bad());return b.str();}
struct Scene{
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 Scene(const std::string& html,const std::string& css){
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);aether_style_environment_t env{W,H,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);int64_t sheet{};int32_t rules{};assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"elliptic-html",&sheet,&rules)==AETHER_OK);assert(rules>0);aether_style_update_t* updates{};auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);assert(aether_layout_layout(engine,doc,styles,W,H,nullptr,nullptr,&layout)==AETHER_OK);assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 }
 ~Scene(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
struct Audit{std::vector<int> tags;std::vector<std::int64_t> ids;std::vector<NativeScrollState> states;std::size_t images{},text{},opacity{},snapshot_commands{};};
static void observe(std::ostream& log,const char* label,aether_status_t status,const NativePaintList* before,const NativePaintList* after){
 log<<label<<','<<int(status)<<','<<static_cast<const void*>(before)<<','<<static_cast<const void*>(after)<<','<<(before==after)<<'\n';log.flush();assert(log);
 std::printf("CALL %s status=%d before=%p after=%p same=%d\n",label,int(status),(const void*)before,(const void*)after,int(before==after));std::fflush(stdout);
}
static void shape(std::ostream& s,const char* tag,p::Rect r,p::Radii a){s<<tag<<','<<r.x<<','<<r.y<<','<<r.width<<','<<r.height<<','<<a.top_left_x<<','<<a.top_left_y<<','<<a.top_right_x<<','<<a.top_right_y<<','<<a.bottom_right_x<<','<<a.bottom_right_y<<','<<a.bottom_left_x<<','<<a.bottom_left_y<<'\n';}
static Audit audit(const std::string& out,const std::string& name,const NativePaintList& list,const p::ImageRef& resource,const std::vector<std::uint8_t>& bytes){
 Audit result;const auto* raw=NativeListAccess::list(list);std::printf("LIST %s raw=%p commands=%zu scroll=%zu images=%zu text=%zu opacity=%zu\n",name.c_str(),(const void*)raw,raw?raw->commands.size():0,list.scroll.size(),list.images.size(),list.text_runs.size(),list.events.size());std::fflush(stdout);assert(raw);
 std::ofstream native(out+"/"+name+"-native.csv");native<<"index,tag,scroll_id\n";
 for(std::size_t i=0;i<raw->commands.size();++i){const auto& c=raw->commands[i];result.tags.push_back(int(c.tag));std::int64_t id=0;if(c.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET)id=c.data.push_scroll_offset.scroll_container_id;if(c.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME)id=c.data.draw_scrollbar_chrome.scroll_container_id;if(c.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET||c.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME)result.ids.push_back(id);native<<i<<','<<int(c.tag)<<','<<id<<'\n';}native.flush();assert(native);native.close();
 std::sort(result.ids.begin(),result.ids.end());result.ids.erase(std::unique(result.ids.begin(),result.ids.end()),result.ids.end());result.states=list.scroll;std::vector<std::int64_t> state_ids;
 std::ofstream states(out+"/"+name+"-scroll.csv");states<<"id,x,y,range_x,range_y\n";for(const auto& s:list.scroll){states<<s.id<<','<<s.x<<','<<s.y<<','<<s.range_x<<','<<s.range_y<<'\n';state_ids.push_back(s.id);}states.flush();assert(states);states.close();
 std::printf("IDS %s unique=%zu state_count=%zu\n",name.c_str(),result.ids.size(),state_ids.size());std::fflush(stdout);assert(!result.ids.empty()&&result.ids==state_ids);
 for(const auto& s:list.scroll)assert(s.id>0&&s.x==0&&s.y==0&&std::isfinite(s.range_x)&&std::isfinite(s.range_y)&&s.range_x>=0&&s.range_y>=0&&s.range_x<=65536&&s.range_y<=65536);
 result.images=list.images.size();result.text=list.text_runs.size();result.opacity=list.events.size();assert(result.images==1&&result.text==0);for(const auto& im:list.images)assert(im.resource==resource&&im.resource->pixels().size()==bytes.size()&&std::equal(bytes.begin(),bytes.end(),im.resource->pixels().begin()));
 auto snap=p::snapshot_native_opacity(list);auto* error=std::get_if<p::Error>(&snap);std::printf("SNAPSHOT %s valid=%d error=%d\n",name.c_str(),int(!error),error?int(error->code):-1);std::fflush(stdout);assert(!error);const auto& snapshot=std::get<p::Snapshot>(snap);result.snapshot_commands=snapshot.commands.size();unsigned images=0;
 std::ofstream dump(out+"/"+name+"-snapshot.csv");dump<<"record,values\n";for(std::size_t i=0;i<snapshot.commands.size();++i){const auto& c=snapshot.commands[i];dump<<"command,"<<i<<','<<c.index()<<'\n';if(auto v=std::get_if<p::Fill>(&c))shape(dump,"fill",v->rect,v->radii);else if(auto v=std::get_if<p::PushClip>(&c))shape(dump,"clip",v->rect,v->radii);else if(auto v=std::get_if<p::LinearGradientFill>(&c)){shape(dump,"gradient",v->rect,v->radii);shape(dump,"ramp",v->gradient_rect,{});dump<<"gradient-parameters,"<<v->direction_x<<','<<v->direction_y<<','<<v->opacity<<'\n';for(const auto& stop:v->stops)dump<<"stop,"<<stop.offset<<','<<stop.color.r<<','<<stop.color.g<<','<<stop.color.b<<','<<stop.color.a<<'\n';}else if(auto v=std::get_if<p::Image>(&c)){shape(dump,"image",v->destination,{});shape(dump,"source",v->source,{});dump<<"image-parameters,"<<v->opacity<<','<<int(v->sampling_bounds)<<'\n';++images;}else if(auto v=std::get_if<p::PushScroll>(&c))dump<<"scroll,"<<v->id<<','<<v->x<<','<<v->y<<'\n';}dump.flush();assert(dump);dump.close();assert(images==1);
 for(const auto& c:snapshot.commands)if(auto v=std::get_if<p::Image>(&c))assert(v->resource==resource&&v->resource->pixels().size()==bytes.size()&&std::equal(bytes.begin(),bytes.end(),v->resource->pixels().begin()));
 return result;
}
int main(int argc,char** argv){assert(argc==3);const std::string fixtures=argv[1],out=argv[2];const auto html=readfile(fixtures+"/panels.html"),css=readfile(fixtures+"/panels.css");const std::vector<std::uint8_t> bytes{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};std::ofstream log(out+"/calls.csv");log<<"call,status,before,after,same_identity\n";log.flush();assert(log);
 {
  Scene scene(html,css);auto resource=std::get<p::ImageRef>(p::ImageResource::copy(2,2,bytes));NativeImageCatalog catalog{{"hero",resource}};std::unique_ptr<NativePaintList> failed;
  auto status=build_native_opacity(scene.builder,scene.layout,scene.styles,nullptr,NativeScrollInput{},catalog,failed);observe(log,"A",status,nullptr,failed.get());assert(status==AETHER_ERROR_INVALID_STATE&&!failed);
 }
 {
  Scene scene(html,css);auto resource=std::get<p::ImageRef>(p::ImageResource::copy(2,2,bytes));NativeImageCatalog catalog{{"hero",resource}};std::unique_ptr<NativePaintList> implicit;
  auto status=build_native_opacity(scene.builder,scene.layout,scene.styles,nullptr,catalog,implicit);observe(log,"B",status,nullptr,implicit.get());assert(status==AETHER_OK&&implicit);auto b=audit(out,"B",*implicit,resource,bytes);
  const auto* identity=implicit.get();status=build_native_opacity(scene.builder,scene.layout,scene.styles,nullptr,NativeScrollInput{},catalog,implicit);observe(log,"A2",status,identity,implicit.get());assert(status==AETHER_ERROR_INVALID_STATE&&implicit.get()==identity);
  NativeScrollInput input;for(auto id:b.ids)input.push_back({id,0,0});std::unique_ptr<NativePaintList> explicit_zero;status=build_native_opacity(scene.builder,scene.layout,scene.styles,nullptr,input,catalog,explicit_zero);observe(log,"C",status,nullptr,explicit_zero.get());assert(status==AETHER_OK&&explicit_zero);auto c=audit(out,"C",*explicit_zero,resource,bytes);
  std::printf("COMPARE tags=%d ids=%d states_count=%d images=%d text=%d opacity=%d snapshot_count=%d B_retained=%d\n",int(b.tags==c.tags),int(b.ids==c.ids),int(b.states.size()==c.states.size()),int(b.images==c.images),int(b.text==c.text),int(b.opacity==c.opacity),int(b.snapshot_commands==c.snapshot_commands),int(implicit.get()==identity));std::fflush(stdout);
  assert(implicit.get()==identity&&b.tags==c.tags&&b.ids==c.ids&&b.states.size()==c.states.size()&&b.images==c.images&&b.text==c.text&&b.opacity==c.opacity&&b.snapshot_commands==c.snapshot_commands);
  for(std::size_t i=0;i<b.states.size();++i){const auto& x=b.states[i];const auto& y=c.states[i];assert(x.id==y.id&&x.x==y.x&&x.y==y.y&&x.range_x==y.range_x&&x.range_y==y.range_y);}
 }
 log.close();assert(log);std::puts("SCROLL_CONTRACT_DIAGNOSIS_AWAITING_D calls=4 raster=NOT_RUN T3_INTEGRATION=BLOCKED coverage_complete=false");
}
