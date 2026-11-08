// Public ABI regression: no private headers or helper substitutions.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
static unsigned checks=0, failures=0, fixtures=0;
static std::string scenario;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::fprintf(stderr,"FAIL [%s] line %d: %s\n",scenario.c_str(),__LINE__,#x); } } while(0)
#define REQUIRE(x) do { CHECK(x); if(failures) std::exit(2); } while(0)
static bool near(float a,float b){return std::fabs(a-b)<.01f;}
struct Meter {
 std::vector<std::string> calls;
 static aether_text_metrics_t measure(const char* text,const aether_computed_style_t*,float,void* ctx){
  auto& m=*static_cast<Meter*>(ctx); m.calls.emplace_back(text); float width=0;
  for(unsigned char c:std::string(text)) width+=(c>='A'&&c<='Z')?13:(c>='a'&&c<='z')?7:c==' '?3:5;
  return {width,20,15};
 }
};
struct Run{std::string text; aether_node_t owner; aether_rectf rect;};
struct Fixture {
 aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_style_map_t map{};
 aether_layout_engine_t engine{}; aether_layout_result_t result{}; Meter meter;
 Fixture(std::string content,std::string css=""){
  ++fixtures; REQUIRE(aether_doc_create(&doc)==AETHER_OK);
  REQUIRE(aether_doc_parse_html(doc,("<html><body><div id='host'>"+content+"</div></body></html>").c_str())==AETHER_OK);
  aether_style_environment_t env{640,480,1,16,0,0,0};
  REQUIRE(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  int64_t id{}; std::string rules="html,body,div{display:block;margin:0;padding:0;border:0}#host{width:200px;font-size:16px;line-height:20px;text-transform:capitalize}"+css;
  REQUIRE(aether_style_runtime_add_stylesheet(runtime,rules.c_str(),0,"capitalization",&id,nullptr)==AETHER_OK);
  resolve(); REQUIRE(aether_layout_create(Meter::measure,&meter,&engine)==AETHER_OK); layout();
 }
 ~Fixture(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id="host"){return aether_doc_get_element_by_id(doc,id);}
 void resolve(){aether_style_update_t* updates{};int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates); REQUIRE(n>=0);aether_style_updates_free(updates,n);aether_style_map_destroy(map);map=nullptr; REQUIRE(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK);}
 void layout(){aether_layout_result_destroy(result);result=nullptr;meter.calls.clear();REQUIRE(aether_layout_layout(engine,doc,map,640,480,nullptr,nullptr,&result)==AETHER_OK);}
 static void collect(aether_layout_box_t b,std::vector<Run>& runs){char* t=aether_layout_box_get_text(b);if(t&&*t)runs.push_back({t,aether_layout_box_get_element(b),aether_layout_box_get_border_box(b)});aether_string_free(t);for(int i=0;i<aether_layout_box_get_child_count(b);++i)collect(aether_layout_box_get_child(b,i),runs);}
 std::vector<Run> runs(){std::vector<Run> r;auto b=aether_layout_result_find_primary_box(result,node());REQUIRE(b);collect(b,r);return r;}
 std::string paint(){aether_display_list_builder_t builder{};aether_display_list_t list{};REQUIRE(aether_display_list_builder_create(Meter::measure,&meter,&builder)==AETHER_OK);REQUIRE(aether_display_list_build(builder,result,nullptr,&list)==AETHER_OK);std::string s;for(int i=0;i<aether_display_list_get_count(list);++i){aether_paint_command_t c{};REQUIRE(aether_display_list_get_command(list,i,&c)==AETHER_OK);if(c.tag==AETHER_PAINT_DRAW_TEXT)s+=c.data.draw_text.text;aether_paint_command_free(&c);}aether_display_list_destroy(list);aether_display_list_builder_destroy(builder);return s;}
 static std::string dom(aether_node_t n){std::string s;if(aether_node_get_type(n)==AETHER_NODE_TEXT){char* t=aether_node_get_text_content(n);if(t)s=t;aether_string_free(t);}for(int i=0;i<aether_node_get_child_count(n);++i)s+=dom(aether_node_get_child(n,i));return s;}
};
static void test(std::string name,std::string html,std::string expected,float width,std::string css=""){
 scenario=name;Fixture f(html,css);auto runs=f.runs();std::string stored;float w=0;for(auto& r:runs){stored+=r.text;w+=r.rect.width;std::printf("%s run=%s width=%.0f owner=%p x=%.0f y=%.0f\n",name.c_str(),r.text.c_str(),r.rect.width,(void*)r.owner,r.rect.x,r.rect.y);}auto painted=f.paint();auto source=Fixture::dom(f.node());for(auto& c:f.meter.calls)std::printf("%s measured=%s\n",name.c_str(),c.c_str());std::printf("%s DOM=%s stored=%s painted=%s width=%.0f\n",name.c_str(),source.c_str(),stored.c_str(),painted.c_str(),w);
 CHECK(stored==expected);CHECK(painted==expected);CHECK(near(w,width));CHECK(source=="hello");
 if(name=="split"){CHECK(runs.size()==2);if(runs.size()==2){CHECK(runs[0].owner==f.node());CHECK(runs[1].owner==f.node("suffix"));CHECK(near(runs[0].rect.x+runs[0].rect.width,runs[1].rect.x));}}
}
int main(){test("unsplit","hello","Hello",41);test("split","hel<span id='suffix'>lo</span>","Hello",41);std::printf("fixtures=%u checks=%u failures=%u\n",fixtures,checks,failures);return failures?1:0;}
