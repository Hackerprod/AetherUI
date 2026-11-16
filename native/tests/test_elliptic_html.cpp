#ifdef NDEBUG
#error Assertions required
#endif
#include "elliptic_t2b_oracle.h"
#include "../src/rendering/software/software_renderer.h"
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include "../../tests/software_render_smoke/png_writer.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
using namespace aether::rendering;namespace p=aether::rendering::preparation;namespace sw=aether::rendering::software;namespace o=elliptic_paint_oracle;namespace co=elliptic_composition_oracle;
constexpr int W=32,H=32;
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){assert(false&&"text outside HTML fixture scope");return {};}
static std::string readfile(const std::string& path){std::ifstream f(path);assert(f);std::ostringstream b;b<<f.rdbuf();assert(!f.bad());return b.str();}
static void binary(const std::string& path,const o::Bytes& b){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(b.data()),b.size());f.close();assert(f);}
static Frame read(FrameStore& store){std::optional<Frame> f;assert(store.copy_confirmed(f)==FrameStatus::ok&&f);return *f;}
struct Scene{
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t styles{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 Scene(const std::string& html,const std::string& css){
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);aether_style_environment_t env{W,H,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);int64_t sheet{};int32_t rules{};assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"elliptic-html",&sheet,&rules)==AETHER_OK);assert(rules>0);aether_style_update_t* updates{};auto count=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_body(doc),&updates);assert(count>=0);aether_style_updates_free(updates,count);assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&styles)==AETHER_OK);assert(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);assert(aether_layout_layout(engine,doc,styles,W,H,nullptr,nullptr,&layout)==AETHER_OK);assert(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
 }
 ~Scene(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(styles);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static bool rect(p::Rect r,float x,float y,float w,float h){return r.x==x&&r.y==y&&r.width==w&&r.height==h;}
static bool radii(p::Radii r,const std::array<float,8>& a){return r.top_left_x==a[0]&&r.top_left_y==a[1]&&r.top_right_x==a[2]&&r.top_right_y==a[3]&&r.bottom_right_x==a[4]&&r.bottom_right_y==a[5]&&r.bottom_left_x==a[6]&&r.bottom_left_y==a[7];}
static void dump_shape(std::ostream& stream,const char* tag,p::Rect r,p::Radii a){stream<<tag<<','<<r.x<<','<<r.y<<','<<r.width<<','<<r.height<<','<<a.top_left_x<<','<<a.top_left_y<<','<<a.top_right_x<<','<<a.top_right_y<<','<<a.bottom_right_x<<','<<a.bottom_right_y<<','<<a.bottom_left_x<<','<<a.bottom_left_y<<'\n';}
static o::Bytes expected(int offset,const o::Bytes& image){
 // Independent literals from authored fixture, NOT extracted commands/layout.
 const o::Shape solid{0,0,16,12,{{{6,6},{2,2},{4,4},{3,3}}}},ellipse{0,12,16,24,{{{6,3},{2,5},{4,6},{3,4}}}},child{(long double)offset,0,(long double)offset+32,8,{}};
 auto out=co::render(W,H,{{solid,{}, {0,0,1,1}},{child,{solid},{0,1,0,1}}});
 o::Gradient gradient{{0,12,16,24,{}},{ellipse},{0,12,16,24},false,1,{{0,{0,0,1,1}},{1,{1,0,0,1}}}};auto g=o::gradient(W,H,gradient);co::composite(out,g,1);
 o::Image im{2,2,image,{0,0,2,2},{(long double)offset,12,(long double)offset+32,20,{}},{ellipse},true,1};auto pixels=o::image(W,H,im);co::composite(out,pixels,1);return out;
}
int main(int argc,char** argv){assert(argc==3);const std::string fixtures=argv[1],out=argv[2];
 const o::Bytes source{0,0,255,255,0,255,0,255,255,0,0,255,255,255,255,255};const std::array<float,8> circular{6,6,2,2,4,4,3,3},elliptic{6,3,2,5,4,6,3,4};
 for(int variant=0;variant<2;++variant){const std::string name=variant?"left-overflow":"panels";int offset=variant?-4:4;auto html=readfile(fixtures+"/"+name+".html"),css=readfile(fixtures+"/"+name+".css");auto wanted=expected(offset,source);FrameStore store;assert(store.resize({W,H,1})==FrameStatus::ok);std::optional<p::PreparedFrame> prepared;std::weak_ptr<const p::ImageResource> weak;Frame live;
  {
   Scene scene(html,css);auto resource=std::get<p::ImageRef>(p::ImageResource::copy(2,2,source));weak=resource;NativeImageCatalog catalog{{"hero",resource}};std::unique_ptr<NativePaintList> native;
   auto built=build_native_opacity(scene.builder,scene.layout,scene.styles,nullptr,catalog,native);std::printf("HTML %s build=%d\n",name.c_str(),int(built));std::fflush(stdout);assert(built==AETHER_OK&&native);assert(native->text_runs.empty());assert(native->images.size()==1&&native->images[0].resource==resource);
   auto* raw=NativeListAccess::list(*native);assert(raw);
   // Catalog-only overload means initial zero offsets for all real scopes.
   // Observe the actual universe; never reuse IDs from another document.
   std::vector<std::int64_t> scope_ids,state_ids;
   for(const auto& command:raw->commands){if(command.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET)scope_ids.push_back(command.data.push_scroll_offset.scroll_container_id);if(command.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME)scope_ids.push_back(command.data.draw_scrollbar_chrome.scroll_container_id);}
   std::sort(scope_ids.begin(),scope_ids.end());scope_ids.erase(std::unique(scope_ids.begin(),scope_ids.end()),scope_ids.end());
   std::ofstream scroll_log(out+"/"+name+"-scroll.csv");scroll_log<<"id,x,y,range_x,range_y\n";for(const auto& state:native->scroll){scroll_log<<state.id<<','<<state.x<<','<<state.y<<','<<state.range_x<<','<<state.range_y<<'\n';state_ids.push_back(state.id);}scroll_log.flush();assert(scroll_log);scroll_log.close();assert(scroll_log);
   std::ofstream ids_log(out+"/"+name+"-scope-ids.csv");ids_log<<"id\n";for(auto id:scope_ids)ids_log<<id<<'\n';ids_log.flush();assert(ids_log);ids_log.close();assert(ids_log);
   std::printf("HTML %s scope_ids=%zu scroll_states=%zu zero_offset_policy=true\n",name.c_str(),scope_ids.size(),native->scroll.size());std::fflush(stdout);
   assert(!scope_ids.empty()&&scope_ids==state_ids);for(const auto& state:native->scroll)assert(state.id>0&&state.x==0&&state.y==0&&std::isfinite(state.range_x)&&std::isfinite(state.range_y)&&state.range_x>=0&&state.range_y>=0&&state.range_x<=65536&&state.range_y<=65536);
   std::ofstream native_log(out+"/"+name+"-native.csv");native_log<<"index,tag\n";for(std::size_t i=0;i<raw->commands.size();++i)native_log<<i<<','<<int(raw->commands[i].tag)<<'\n';native_log.close();assert(native_log);
   auto snapshot_result=p::snapshot_native_opacity(*native);std::printf("HTML %s snapshot_ok=%d\n",name.c_str(),int(std::holds_alternative<p::Snapshot>(snapshot_result)));std::fflush(stdout);assert(std::holds_alternative<p::Snapshot>(snapshot_result));auto snapshot=std::get<p::Snapshot>(std::move(snapshot_result));unsigned solid=0,child=0,clip_solid=0,clip_ellipse=0,gradients=0,images=0;std::ofstream dump(out+"/"+name+"-snapshot.csv");dump<<"type,x,y,width,height,tlx,tly,trx,try,brx,bry,blx,bly\n";
   for(const auto& command:snapshot.commands){
    if(auto f=std::get_if<p::Fill>(&command)){dump_shape(dump,"fill",f->rect,f->radii);if(rect(f->rect,0,0,16,12)&&radii(f->radii,circular)){assert(f->color.r==1&&f->color.g==0&&f->color.b==0&&f->color.a==1);++solid;}if(rect(f->rect,float(offset),0,32,8)){assert(f->color.r==0&&f->color.g==1&&f->color.b==0&&f->color.a==1);++child;}}
    else if(auto c=std::get_if<p::PushClip>(&command)){dump_shape(dump,"clip",c->rect,c->radii);if(rect(c->rect,0,0,16,12)&&radii(c->radii,circular))++clip_solid;if(rect(c->rect,0,12,16,12)&&radii(c->radii,elliptic))++clip_ellipse;}
    else if(auto g=std::get_if<p::LinearGradientFill>(&command)){dump_shape(dump,"gradient",g->rect,g->radii);assert(rect(g->rect,0,12,16,12)&&rect(g->gradient_rect,0,12,16,12));assert(g->direction_x==1&&g->direction_y==0&&g->opacity==1&&g->stops.size()==2);assert(g->stops[0].offset==0&&g->stops[0].color.r==1&&g->stops[0].color.g==0&&g->stops[0].color.b==0&&g->stops[0].color.a==1);assert(g->stops[1].offset==1&&g->stops[1].color.r==0&&g->stops[1].color.g==0&&g->stops[1].color.b==1&&g->stops[1].color.a==1);++gradients;}
    else if(auto i=std::get_if<p::Image>(&command)){dump_shape(dump,"image",i->destination,{});assert(i->resource==resource&&rect(i->source,0,0,2,2)&&rect(i->destination,float(offset),12,32,8)&&i->opacity==1&&i->sampling_bounds==p::ImageSamplingBounds::FullResource);assert(std::equal(source.begin(),source.end(),i->resource->pixels().begin()));++images;}
    else assert(!std::holds_alternative<p::Text>(command)&&!std::holds_alternative<p::Stroke>(command)&&!std::holds_alternative<p::BorderGroup>(command)&&!std::holds_alternative<p::BoxShadow>(command));
   }
   dump.close();assert(dump);std::printf("HTML %s solid=%u child=%u circular_clips=%u elliptical_clips=%u gradients=%u images=%u\n",name.c_str(),solid,child,clip_solid,clip_ellipse,gradients,images);std::fflush(stdout);assert(solid==1&&child==1&&clip_solid>=1&&clip_ellipse>=1&&gradients==1&&images==1);
   auto pf=p::prepare(snapshot);assert(std::holds_alternative<p::PreparedFrame>(pf));prepared.emplace(std::get<p::PreparedFrame>(std::move(pf)));snapshot.commands.clear();auto rendered=sw::render(store,*prepared);std::printf("HTML %s live_status=%d budget=%llu\n",name.c_str(),int(rendered.status),(unsigned long long)rendered.pixel_budget);std::fflush(stdout);assert(rendered.status==sw::Status::ok);live=read(store);binary(out+"/"+name+"-live.bgra",live.pixels);
  } // Native list, catalog, local resource, DOM, styles and layout destroyed.
  assert(!weak.expired());auto result=sw::render(store,*prepared);std::printf("HTML %s retained_status=%d budget=%llu\n",name.c_str(),int(result.status),(unsigned long long)result.pixel_budget);std::fflush(stdout);assert(result.status==sw::Status::ok);auto actual=read(store);
  binary(out+"/"+name+".actual.bgra",actual.pixels);binary(out+"/"+name+".expected.bgra",wanted);binary(out+"/"+name+"-REAL-HTML.png",smoke_png::png(smoke_png::rgba(actual),W,H));std::ofstream diff(out+"/"+name+".csv");diff<<"offset,expected,actual\n";assert(actual.pixels.size()==wanted.size());for(std::size_t i=0;i<wanted.size();++i)if(actual.pixels[i]!=wanted[i])diff<<i<<','<<unsigned(wanted[i])<<','<<unsigned(actual.pixels[i])<<'\n';diff.close();assert(diff);assert(actual.pixels==wanted&&live.pixels==actual.pixels&&actual.sequence==live.sequence+1);assert(actual.viewport.width==W&&actual.viewport.height==H&&actual.viewport.dpi==1&&actual.stride==W*4);
  auto pixel=[&](int x,int y,std::array<unsigned,4> b){for(int k=0;k<4;++k)assert(actual.pixels[(y*W+x)*4+k]==b[k]);};pixel(8,4,{0,255,0,255});pixel(8,10,{0,0,255,255});pixel(8,22,{135,0,120,255});pixel(0,0,{0,0,0,0});pixel(0,12,{0,0,0,0});pixel(16,4,{0,0,0,0});pixel(16,16,{0,0,0,0});pixel(8,24,{0,0,0,0});prepared.reset();assert(weak.expired());std::printf("HTML %s ownership_released=true bytes=%zu PASS\n",name.c_str(),actual.pixels.size());std::fflush(stdout);
 }
 std::puts("ELLIPTIC_T3_HTML_PASS fixtures=2 native_pipeline=true coverage_complete=false browser_parity=NOT_RUN");
}
