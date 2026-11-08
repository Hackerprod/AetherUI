#include <aether/aether.h>
#include <aether/aether_contextual_layout.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <cmath>
#define OK(x) do { if (!(x)) { std::fprintf(stderr,"fixture failure line %d\n",__LINE__); std::exit(2); } } while(0)
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*) {return {10,10,8};}
static int checks = 0, failures = 0;
static void rect_check(const char* mode, const char* id, aether_rectf r, aether_rectf expected) {
 ++checks;
 const bool pass = std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
  std::fabs(r.x-expected.x)<0.01f && std::fabs(r.y-expected.y)<0.01f &&
  std::fabs(r.width-expected.width)<0.01f && std::fabs(r.height-expected.height)<0.01f;
 std::printf("%s mode=%s %s actual=(%g,%g,%g,%g) expected=(%g,%g,%g,%g)\n",pass?"PASS":"FAIL",mode,id,r.x,r.y,r.width,r.height,expected.x,expected.y,expected.width,expected.height);
 if (!pass) ++failures;
}
int main() {
 for(int prepared=0;prepared<2;++prepared) {
  aether_doc_t doc{}; aether_style_runtime_t runtime{}; aether_style_map_t map{};
  aether_layout_engine_t engine{}; aether_layout_result_t result{};
  OK(aether_doc_create(&doc)==AETHER_OK);
  OK(aether_doc_parse_html(doc,"<body><table id='table'><tr id='row'><td id='short'><div id='small'></div><div id='anchor'></div></td><td id='tall'><div id='large'></div></td></tr><tr id='next'><td><div id='last'></div></td></tr></table></body>")==AETHER_OK);
  aether_style_runtime_options_t options{}; aether_style_environment_t env{400,300,1,16,0,0,0};
  OK(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  int64_t sheet{};
  OK(aether_style_runtime_add_stylesheet(runtime,"*{margin:0;padding:0;border:0;box-sizing:content-box}body,div{display:block}table{display:table;width:200px}tr{display:table-row}td{display:table-cell}#short{position:relative;background:#ff0000}#small{width:20px;height:20px}#large{width:20px;height:80px}#anchor{position:absolute;bottom:0;left:0;width:5px;height:5px}#last{width:20px;height:10px}",0,"repro",&sheet,nullptr)==AETHER_OK);
  aether_style_update_t* updates{};
  int n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates); OK(n>=0);
  aether_style_updates_free(updates,n);
  OK(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK);
  OK(aether_layout_create(measure,nullptr,&engine)==AETHER_OK);
  if(prepared) {aether_bool_t changed{}; OK(aether_layout_prepare_transition_context(engine,nullptr,map,nullptr,&changed)==AETHER_OK);}
  OK(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);
  std::printf("mode=%s\n",prepared?"prepared":"legacy");
  const char* mode = prepared ? "prepared" : "legacy";
  const char* ids[] = {"table", "row", "short", "tall", "small", "anchor", "next"};
  const aether_rectf expected[] = {{0,0,200,90},{0,0,200,80},{0,0,100,80},{100,0,100,80},{0,0,20,20},{0,75,5,5},{0,80,200,10}};
  for (int i=0;i<7;++i) {
   auto box=aether_layout_result_find_primary_box(result,aether_doc_get_element_by_id(doc,ids[i])); OK(box);
   rect_check(mode,ids[i],aether_layout_box_get_border_box(box),expected[i]);
  }
  auto hit=aether_layout_result_hit_test(result,50,50);
  bool hit_pass = hit==aether_doc_get_element_by_id(doc,"short");
  ++checks; if (!hit_pass) ++failures;
  char* hit_id=aether_element_get_attribute(hit,"id");
  std::printf("%s mode=%s hit(50,50)=%s expected=short\n",hit_pass?"PASS":"FAIL",mode,hit_id?hit_id:"(none)"); aether_string_free(hit_id);
  aether_display_list_builder_t builder{}; aether_display_list_t list{};
  OK(aether_display_list_builder_create(measure,nullptr,&builder)==AETHER_OK);
  OK(aether_display_list_build(builder,result,nullptr,&list)==AETHER_OK);
  int red_fills=0;
  for(int i=0;i<aether_display_list_get_count(list);++i) {aether_paint_command_t c{}; OK(aether_display_list_get_command(list,i,&c)==AETHER_OK);
   if(c.tag==AETHER_PAINT_FILL_ROUNDED_RECT && c.data.fill_rounded_rect.color.r==1.f && c.data.fill_rounded_rect.color.g==0 && c.data.fill_rounded_rect.color.b==0) {
    ++red_fills; rect_check(mode,"red fill",c.data.fill_rounded_rect.rect,{0,0,100,80});
   }
   aether_paint_command_free(&c);
  }
  ++checks; if (red_fills!=1) ++failures;
  std::printf("%s mode=%s red fill count=%d expected=1\n",red_fills==1?"PASS":"FAIL",mode,red_fills);
  aether_display_list_destroy(list); aether_display_list_builder_destroy(builder);
  aether_layout_result_destroy(result); aether_layout_destroy(engine); aether_style_map_destroy(map); aether_style_runtime_destroy(runtime); aether_doc_destroy(doc);
 }
 std::printf("active_checks=%d failures=%d\n",checks,failures);
 return failures ? 1 : 0;
}
