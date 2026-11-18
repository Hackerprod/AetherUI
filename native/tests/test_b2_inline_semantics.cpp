#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef AETHER_HTML_LTR_TESTING
#define AETHER_HTML_LTR_TESTING
#endif
#include <aether/aether_b0_source.h>
#include "../src/rendering/html_ltr/b2_session_provenance.h"
#include "../src/rendering/html_ltr/session.h"
#include "../src/layout/layout_internal.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
namespace b2=aether::layout::b2;
static std::string fault;
static size_t measurement_calls=0,actual_product_calls=0;
static size_t result_inspections=0;
static size_t inspections=0,wanted_lines=SIZE_MAX,wanted_contexts=SIZE_MAX;
static bool check_split_contexts=false,check_mixed_contributors=false;
static float wanted_height=-1,captured_width=0,overflow_width=-1;static bool capture_width=false,check_measurements=false;
namespace aether::rendering::html_ltr::testing {
void stage(Stage s){if(s==Stage::occurrence)++measurement_calls;if(s==Stage::font_product)++actual_product_calls;}
void inspect(aether_layout_result_s* layout,aether::rendering::NativePaintList*){
 if(wanted_height<0)return;
 std::vector<aether_layout_box_s*> todo{layout->root};bool found=false;while(!todo.empty()){auto* box=todo.back();todo.pop_back();if(box->element&&box->element->attributes.contains("id")&&box->element->attributes.at("id")=="probe"&&box->kind!=LAYOUT_BOX_TEXT){assert(box->content_box.height==wanted_height);found=true;break;}for(auto* child:box->children)todo.push_back(child);}assert(found);
}
void b2_result_inspect(B2InlineResult& r){
 ++result_inspections;
 if(fault.rfind("canonical-",0)!=0)return;
 const auto wanted=fault.find("-c-")!=std::string::npos?B2Disposition::contributor:B2Disposition::visible;
 for(auto& range:r.ranges)if(range.disposition==wanted){
  assert(range.placement<r.placements.size());auto& placement=r.placements[range.placement];
  if(fault.find("token")!=std::string::npos){
   size_t authentic=b2_no_index;for(const auto& p:r.placements)if(p.line==placement.line&&p.token!=placement.token){authentic=p.token;break;}assert(authentic!=b2_no_index);
   placement.token=authentic;if(fault.find("range-")!=std::string::npos)range.token=authentic;
  }else {placement.fragment=99999;if(fault.find("range-")!=std::string::npos)range.fragment=99999;}
  return;
 }
 assert(false);
}
void b2_inspect(b2::Ledger& l){
 ++inspections;
 if(capture_width){assert(l.lines().size()==1);captured_width=l.lines().front().width;}
 if(overflow_width>=0){assert(l.lines().size()==1&&l.lines().front().width>overflow_width);}
 if(check_measurements)assert(l.counters().used(b2::Quota::bridge_calls)>l.placements().size());
 if(wanted_lines!=SIZE_MAX)assert(l.lines().size()==wanted_lines);
 if(wanted_contexts!=SIZE_MAX)assert(l.contexts().size()==wanted_contexts);
 if(check_split_contexts){size_t parent=0,child=0;for(const auto& c:l.contexts()){auto* owner=reinterpret_cast<aether_node_s*>(c.owner);if(owner->tag_name=="div")++parent;else if(owner->tag_name=="p")++child;}assert(parent==2&&child==1);}
 if(check_mixed_contributors){
  const b2::Span *first=nullptr,*second=nullptr;size_t count=0;for(const auto& span:l.spans())if(span.fate==b2::Fate::contributor){++count;if(!first)first=&span;else second=&span;}
  assert(count==2&&first&&second&&first->placement==second->placement&&first->group==second->group);
  const auto& a=l.origins()[first->origin];const auto& b=l.origins()[second->origin];const auto& space=l.placements()[first->placement];
  assert(a.owner!=b.owner&&a.style!=b.style&&a.font!=b.font&&space.owner==a.owner&&space.style==a.style&&space.font==a.font&&space.text==" "&&l.placements().size()==4);
 }
 if(fault.empty())return;
 auto& spans=l.test_spans();auto& placements=l.test_placements();
 auto find_visible=[&](std::string_view text){for(size_t i=0;i<spans.size();++i){const auto& s=spans[i];if(s.fate==b2::Fate::visible&&std::string_view(l.origins()[s.origin].text).substr(s.begin,s.end-s.begin)==text)return i;}assert(false);return size_t(0);};
 auto find_space=[&](){for(size_t i=0;i<spans.size();++i)if(spans[i].fate==b2::Fate::contributor)return i;assert(false);return size_t(0);};
 if(fault=="duplicate-identical-origin"){const auto first=find_visible("foo");size_t second=first+1;while(second<spans.size()&&(spans[second].fate!=b2::Fate::visible||l.origins()[spans[second].origin].text!="foo"))++second;assert(second<spans.size());spans[second].origin=spans[first].origin;}
 else if(fault=="gap")++spans[find_visible("foo")].begin;
 else if(fault=="overlap"){auto s=spans[find_visible("foo")];spans.insert(spans.begin(),s);}
 else if(fault=="utf8-boundary"){auto& s=spans[find_visible("é")];s.end=s.begin+1;}
 else if(fault=="collapse-no-sp")placements[spans[find_space()].placement].text="x";
 else if(fault=="collapse-first-owner"||fault=="collapse-first-style"||fault=="collapse-first-font"){
  auto& space=placements[spans[find_space()].placement];const b2::Placement* authentic=nullptr;
  for(const auto& p:placements)if(p.owner!=space.owner&&p.style!=space.style&&p.font!=space.font){authentic=&p;break;}
  assert(authentic); // Real strong owner/style/bold font from this same DOM.
  if(fault=="collapse-first-owner")space.owner=authentic->owner;
  else if(fault=="collapse-first-style")space.style=authentic->style;
  else space.font=authentic->font;
 }
 else if(fault=="collapse-sp-duplicate")placements.push_back(placements[spans[find_space()].placement]);
 else if(fault=="discard-false"){auto& s=spans[find_space()];const auto context=l.origins()[s.origin].context;s.fate=b2::Fate::discarded;s.placement=b2::none;s.group=b2::none;s.decision=static_cast<b2::Id>(l.test_decisions().size());l.test_decisions().push_back({context,b2::none,b2::Discard::leading});}
 else if(fault=="break-duplicate")l.test_breaks().push_back(l.test_breaks().front());
 else if(fault=="break-omitted")l.test_breaks().clear();
 else if(fault=="empty-inline-omitted"){auto& p=l.test_participants();for(auto it=p.begin();it!=p.end();++it)if(l.origins()[*it].kind==b2::Kind::inline_node){p.erase(it);break;}}
 else if(fault=="style-owner")++placements.front().owner;
 else if(fault=="line-id")placements.front().line=b2::none;
 else if(fault=="baseline")++placements.front().baseline;
 else if(fault.rfind("final-",0)==0||fault.rfind("missing-",0)==0){
  auto& range=spans[fault.find("-c-")!=std::string::npos?find_space():find_visible("foo")];auto& p=placements[range.placement];
  if(fault.rfind("missing-",0)==0){if(fault.find("token")!=std::string::npos)range.expected_token=b2::none;else range.expected_fragment=b2::none;}
  else if(fault.find("token")!=std::string::npos){b2::Id authentic=b2::none;for(const auto& candidate:placements)if(candidate.line==p.line&&candidate.token!=p.token){authentic=candidate.token;break;}assert(authentic!=b2::none);p.token=authentic;}
  else p.fragment=99999;
 }
 else assert(false);
}
}
static std::vector<unsigned char> read(const char* path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static aether_b0_source_options_v1 options;
static std::string document(std::string body,std::string mode="normal"){return "<html><head><style>body{white-space:"+mode+"}p{line-height:24px;width:220px}</style></head><body>"+body+"</body></html>";}
static void run(const std::string& html,int expected=AETHER_OK){aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;const auto before=inspections,results_before=result_inspections;const auto status=aether_b0_source_create(400,500,html.data(),html.size(),&options,&session,&d);if(status!=expected)std::fprintf(stderr,"expected %d got %d stage %u fault %s\n%s\n",expected,status,d.stage,fault.c_str(),html.c_str());assert(status==expected);assert(inspections>before||result_inspections>results_before);if(expected==AETHER_OK){assert(session);assert(aether_b0_source_render(session,&d)==AETHER_OK);assert(aether_b0_source_destroy(session)==AETHER_OK);}else assert(!session);}
static void early_reject(const std::string& html,int expected,unsigned expected_stage){aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;measurement_calls=0;actual_product_calls=0;const auto result=aether_b0_source_create(400,500,html.data(),html.size(),&options,&session,&d);if(result!=expected||d.stage!=expected_stage)std::fprintf(stderr,"early rejection got %d stage %u expected %d stage %u\n",result,d.stage,expected,expected_stage);assert(result==expected&&d.stage==expected_stage&&!session&&measurement_calls==0&&actual_product_calls==0);}
static std::vector<uint8_t> legacy_pixels(const std::string& html){
 const auto before=inspections,results_before=result_inspections;aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;
 assert(aether_b0_source_create(400,500,html.data(),html.size(),&options,&session,&d)==AETHER_OK&&session);assert(inspections==before&&result_inspections==results_before);assert(aether_b0_source_render(session,&d)==AETHER_OK);
 aether_b0_source_frame_info_v1 info{};info.struct_size=sizeof info;info.version=1;assert(aether_b0_source_copy_frame(session,&info,nullptr,0)==AETHER_OK);std::vector<uint8_t> result(info.byte_count);assert(aether_b0_source_copy_frame(session,&info,result.data(),result.size())==AETHER_OK);assert(aether_b0_source_destroy(session)==AETHER_OK);return result;
}
int main(int argc,char** argv){assert(argc==3);auto regular=read(argv[1]),bold=read(argv[2]);options.struct_size=sizeof options;options.version=1;options.regular_font=regular.data();options.regular_font_size=regular.size();options.bold_font=bold.data();options.bold_font_size=bold.size();
 const auto fixture=document("<p>foo<span></span><span>foo</span> Z<br>é a  b<strong>Q</strong></p>");run(fixture);
 const std::vector<std::string> faults{
  "duplicate-identical-origin","gap","overlap","utf8-boundary","collapse-no-sp","collapse-first-owner","collapse-first-style","collapse-first-font","collapse-sp-duplicate","discard-false","break-duplicate","break-omitted","empty-inline-omitted","style-owner","line-id","baseline",
  "canonical-v-token","canonical-v-fragment","canonical-v-range-token","canonical-v-range-fragment",
  "canonical-c-token","canonical-c-fragment","canonical-c-range-token","canonical-c-range-fragment",
  "final-v-token","final-v-fragment","final-c-token","final-c-fragment",
  "missing-v-token","missing-v-fragment","missing-c-token","missing-c-fragment"};
 for(const auto& f:faults){fault=f;run(fixture,AETHER_ERROR_INTERNAL);}fault.clear();run(fixture);
 std::printf("Corruption cases=%zu: canonical V/C, coordinated range+placement, final IDs, absent refs, authentic same-document SP owner/style/font\n",faults.size());
 check_mixed_contributors=true;run(document("<p>A<span style='color:#112233'> \t</span><strong style='color:#445566'>\n \f</strong>B<strong>Q</strong></p>"));check_mixed_contributors=false;
 wanted_contexts=3;check_split_contexts=true;run(document("<div>before<p>inside</p>after</div>"));wanted_contexts=SIZE_MAX;check_split_contexts=false;
 const std::vector<std::pair<std::string,size_t>> breaks{{"",0},{"<br>",1},{"<br><br>",2},{"x<br>",1},{"<br>x",2},{"x<br><br>",2},{"x<br>y",2}};
 for(const auto& mode:{"normal","nowrap"})for(const auto& [html,lines]:breaks){wanted_lines=lines;wanted_height=float(lines*24);run(document("<p id='probe'><span></span>"+html+"</p>",mode));}
 wanted_lines=SIZE_MAX;wanted_height=-1;
 wanted_lines=3;check_measurements=true;run(document("<p style='width:30px'>ab cd ef</p>"));check_measurements=false;wanted_lines=SIZE_MAX;
 run(document("<p style='text-align:center'>Small <span style='font-size:32px;line-height:40px'>BIG</span> end</p>"));
 run(document("<p>  a\t<span>\f</span><!-- comment -->\n b  </p>"));
 capture_width=true;run(document("<p>¿Qué<span> </span>tal?</p>"));capture_width=false;const float exact=captured_width;
 const auto css_width=[](float width){std::ostringstream s;s<<std::setprecision(9)<<width;return s.str();};
 wanted_lines=1;run(document("<p style='width:"+css_width(exact)+"px'>¿Qué<span> </span>tal?</p>"));overflow_width=exact-0.25f;run(document("<p style='width:"+css_width(overflow_width)+"px'>¿Qué<span> </span>tal?</p>"));overflow_width=-1;wanted_lines=SIZE_MAX;
 {std::string body="<p>";for(size_t i=0;i<2048;++i)body+="a ";body+="</p>";aether_b0_source_t session=nullptr;aether_b0_source_diagnostic_v1 d{};d.struct_size=sizeof d;d.version=1;measurement_calls=0;auto html=document(body);auto status=aether_b0_source_create(400,500,html.data(),html.size(),&options,&session,&d);std::printf("Late quota diagnostic status=%d stage=%u measurements=%zu\n",status,d.stage,measurement_calls);assert(!session&&status==AETHER_ERROR_LIMIT_EXCEEDED&&d.stage==AETHER_B0_STAGE_LAYOUT&&measurement_calls>0);}
 early_reject(document("<p>"+std::string(4097,'a')+"</p>"),AETHER_ERROR_LIMIT_EXCEEDED,AETHER_B0_STAGE_TEXT);
 early_reject(document("<p>"+std::string(8193,' ')+"</p>"),AETHER_ERROR_LIMIT_EXCEEDED,AETHER_B0_STAGE_TEXT);
 early_reject(document("<p><span style='white-space:nowrap'></span></p>"),AETHER_ERROR_UNSUPPORTED,AETHER_B0_STAGE_STYLE);
 early_reject(document("<p>x&#11;y</p>"),AETHER_ERROR_UNSUPPORTED,AETHER_B0_STAGE_TEXT);
 for(const std::string mode:{"","nowrap","normal"})for(bool entity:{false,true}){
  const auto css=mode.empty()?std::string():" style='white-space:"+mode+"'";
  const auto text=entity?std::string("x&#11;y"):std::string("x")+char(11)+"y";
  early_reject("<html><head></head><body><p"+css+">"+text+"</p></body></html>",AETHER_ERROR_UNSUPPORTED,AETHER_B0_STAGE_TEXT);
 }
 puts("VT implicit/nowrap/normal literal+entity: 6 TEXT rejections, actual FontProvider::product calls=0");
 actual_product_calls=0;const auto plain_pixels=legacy_pixels("<html><head></head><body><p>x</p></body></html>");const auto legacy_products=actual_product_calls;assert(legacy_products>0);
 actual_product_calls=0;const auto metadata_pixels=legacy_pixels(std::string("<html><head><title>x")+char(11)+"y</title></head><body><p>x</p></body></html>");assert(plain_pixels==metadata_pixels&&actual_product_calls==legacy_products);
 std::printf("Legacy metadata VT: unchanged pixels, zero B2 callbacks, identical actual product calls=%zu\n",legacy_products);

 std::printf("Independent NBSP one-word oracle W=%.9g W-epsilon=%.9g lines=1, exact overflow verified\n",exact,exact-0.25f);
 puts("B2 integrated provenance corruption + contexts + br semantics PASS");
}
