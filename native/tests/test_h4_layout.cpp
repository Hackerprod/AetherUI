#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/layout/layout_internal.h"
#include <cassert>
#include <iostream>
using namespace aether::style;
static void near(float a,float b){ if(!(std::abs(a-b)<.01f)) std::cerr << "actual=" << a << " expected=" << b << "\n"; assert(std::abs(a-b)<.01f); }
static void padding_phases(){
 auto flat=aether_computed_style_default();
 FrozenStyleRef sf;assert(freeze_style(flat,sf)==AETHER_OK);
 FrozenDocumentRef doc;assert(freeze_document({},doc)==AETHER_OK);
 measure_state_s m;m.document=std::make_shared<const document_measure_context_s>();
 aether::css::math_tree t;assert(aether::css::parse_contextual_math("max(24px,50%)",t));
 for(auto prop:{"padding-left","padding-right","padding-top","padding-bottom"})m.contextual_lengths[prop]=t;
 FrozenMeasuresRef mf;assert(freeze_runtime_measures(m,doc,mf)==AETHER_OK);
 BoxStyleSnapshot s(sf,mf);s.padding.left=s.padding.right=s.padding.top=s.padding.bottom=aether_css_length_zero();
 auto p=resolve_padding_edges(nullptr,s.padding,200,s);near(p.left,100);near(p.top,100);
 p=resolve_padding_edges(nullptr,s.padding,200,s,padding_resolution_phase_e::intrinsic_cyclic);near(p.left,24);near(p.top,24);
 p=resolve_padding_edges(nullptr,s.padding,200,s);near(p.left,100);
 p=resolve_padding_edges(nullptr,s.padding,0,s);near(p.left,24);
 bool rejected=false;try{resolve_padding_edges(nullptr,s.padding,200,s,padding_resolution_phase_e::indefinite);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
 // A lost sidecar cannot turn the marker into a valid zero.
 BoxStyleSnapshot lost(sf);lost.padding.left={NAN,AETHER_CSS_UNIT_PX,0,0,0,0,0};
 rejected=false;try{resolve_padding_edges(nullptr,lost.padding,200,lost);}catch(const std::overflow_error&){rejected=true;}assert(rejected);
 aether_computed_style_free(&flat);
}
static aether_text_metrics_t measure(const char*,const aether_computed_style_t*,float,void*){return {};}
static void layout_case(const std::string& css,float expected_width,float expected_height, const char* html = "<body><div id='parent'><div id='child'></div></div></body>"){
 std::cerr << "case " << css << "\n";
 aether_doc_t d{};aether_style_runtime_t rt{};aether_style_map_t map{};aether_layout_engine_t e{};
 assert(aether_doc_create(&d)==AETHER_OK);assert(aether_doc_parse_html(d,html)==AETHER_OK);
 aether_style_runtime_options_t opts{};aether_style_environment_t env{900,600,1,16,0,0,0};
 assert(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sheet{};int32_t rules{};
 std::string all="body{margin:0}#parent{display:flex;align-items:flex-start}#child{min-width:0;min-height:0;flex:0 0 clamp(80px,30%,180px)}"+css;
 assert(aether_style_runtime_add_stylesheet(rt,all.c_str(),0,"h4",&sheet,&rules)==AETHER_OK);
 aether_style_update_t* updates{};int count=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(d),&updates);assert(count>=0);aether_style_updates_free(updates,count);
 assert(aether_style_runtime_create_resolved_style_map(rt,d,&map)==AETHER_OK);aether_style_runtime_destroy(rt);
 assert(aether_layout_create(measure,nullptr,&e)==AETHER_OK);aether_layout_result_t r{};
 assert(aether_layout_layout(e,d,map,900,600,nullptr,nullptr,&r)==AETHER_OK);
 aether_layout_box_s* child=nullptr;for(auto* b:r->allocated_boxes)if(b->element==aether_doc_get_element_by_id(d,"child"))child=b;
 assert(child);near(child->border_box.width,expected_width);near(child->border_box.height,expected_height);
 aether_layout_result_destroy(r);aether_layout_destroy(e);aether_style_map_destroy(map);aether_doc_destroy(d);
}
int main(){padding_phases();
 layout_case("#parent{width:500px;height:300px}#child{height:20px}",150,20);
 layout_case("#parent{width:200px;height:300px}#child{height:20px}",80,20);
 layout_case("#parent{width:800px;height:300px}#child{height:20px}",180,20);
 layout_case("#parent{flex-direction:column;width:200px;height:500px}#child{width:20px}",20,150);
 // Indefinite percentage basis uses content, not the item's specified height.
 layout_case("#parent{flex-direction:column;width:200px}#child{width:20px;height:90px}#inside{display:block;height:37px}",20,37,
  "<body><div id='parent'><div id='child'><div id='inside'></div></div></div></body>");
 std::cout<<"PASS H4 padding used/cyclic/indefinite, lost context, row/column definite and content basis, frozen lifetime\n";
}
