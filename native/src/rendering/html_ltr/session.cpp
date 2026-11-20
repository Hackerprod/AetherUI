#include "session.h"
#include "session_validation.h"
#include "admission.h"
#include "image_registry.h"
#include "measure_bridge.h"
#include "paint_finalize.h"
#include "raster_coordinate.h"
#include "../native_paint_list.h"
#include "control_presentation.h"
#include "../../layout/form_controls.h"
#include "../../layout/layout_internal.h"
#include "../preparation/native_command_source.h"
#include <map>
#include <set>
#include <optional>
#include <thread>
namespace aether::rendering::html_ltr {
using namespace session_validation;

struct HtmlLtrSession::State {
 struct Owner {size_t ordinal; aether::style::FrozenNodeRef record;std::shared_ptr<const FontProduct> whole;std::shared_ptr<const ControlPresentation> control;};
 struct Occurrence {std::shared_ptr<const text::ShapedRun> run;std::shared_ptr<const FontProduct> product;aether_node_s* owner;uint64_t id;};
 uint32_t profile;std::string html;Inventory inventory;FontProvider fonts;Viewport viewport;
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::shared_ptr<const ImageRegistry> registry;
 std::vector<aether_node_t> nodes;std::map<aether_node_t,Owner> owners;std::map<const text::ShapedRun*,Occurrence> occurrences;std::unique_ptr<NativePaintList> paint;
 std::vector<std::shared_ptr<const text::GlyphMaskA8>> masks;std::optional<preparation::PreparedFrame> prepared;
 uint64_t epoch{},nonstate{},serial{};std::thread::id thread=std::this_thread::get_id();aether_status_t bridge_error=AETHER_OK;
 State(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v,uint32_t p):profile(p),html(std::move(h)),inventory(admit_startup(html,r.size()+b.size(),p)),fonts(std::move(r),std::move(b),p),viewport(v){fonts.reserve_payload(html.size()+1024*1024);}
 ~State(){paint.reset();aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void valid()const {require(thread==std::this_thread::get_id()&&doc&&doc->identity->alive&&doc->identity->structure_epoch==epoch&&doc->identity->non_state_epoch==nonstate&&aether_style_map_structure_valid(map)&&aether_style_map_inputs_current(map));}
 struct Bridge final:HtmlLtrMeasureBridge {State* s;explicit Bridge(State* p):s(p){}
  std::shared_ptr<const ControlPresentation> control(aether_node_s* node,const aether::layout::BoxStyleSnapshot& st) override {
   s->valid();require(s->profile==2&&s->owners.contains(node));const auto& o=s->owners.at(node);require(o.control&&st.style_owner()==o.record->style());s->check_control(o.control,node,st);return o.control;
  }
  uint64_t occurrence(const text::ShapedRun* run) const override {s->valid();require(s->occurrences.contains(run));return s->occurrences.at(run).id;}

  text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport)override {
   try{return s->measure(raw,st,width,node,transport);}catch(const Error& e){s->bridge_error=e.status;throw;}catch(const BoundaryError& e){s->bridge_error=e.status;throw;}
  }
 };
 text::Measurement measure(const char* raw,const aether::layout::BoxStyleSnapshot& st,float width,aether_node_s* node,bool transport){
  valid();require(raw&&std::isfinite(width)&&width==0&&owners.contains(node));const auto& owner=owners.at(node);const auto& authored=inventory.nodes[owner.ordinal];
  require(node->identity==owner.record->node_lifetime()&&node->identity->alive&&node->identity->attached&&node->identity->owner==node&&st.style_owner()==owner.record->style());
  if(owner.control){check_control(owner.control,node,st);require(std::string_view(raw)==owner.control->text);}else require(authored.leaf());
  const auto& exp=authored.inherited;require(st.font_size==exp.font_size&&st.font_weight==int(exp.font_weight)&&st.line_height==exp.line_height&&same(st.color,color(exp.color_rgb)));
  HTML_LTR_STAGE(occurrence);
  auto product=std::make_shared<const FontProduct>(fonts.product(raw,int(exp.font_size),int(exp.font_weight),int(exp.line_height)));
  if(!transport)return {product->metrics,{}};
  require(*raw);fonts.reserve_payload(sizeof(Occurrence)+sizeof(text::ShapedRun)+product->glyphs.size()*sizeof(text::Glyph)+std::strlen(raw));
  validate_run_transport(raw,product->glyphs,product->metrics.width,product->metrics.height,product->metrics.baseline);
  auto run=std::make_shared<const text::ShapedRun>(product->binding->font(),raw,product->glyphs,product->metrics);
  occurrences.emplace(run.get(),Occurrence{run,product,node,++serial});return {product->metrics,run};
 }
 void check_control(const std::shared_ptr<const ControlPresentation>& c,aether_node_s* node,const aether::layout::BoxStyleSnapshot& st){
  valid();require(profile==2&&c&&c->profile==profile&&c->epoch==epoch&&c->nonstate==nonstate&&c->ordinal<nodes.size()&&nodes[c->ordinal]==node&&owners.contains(node));
  const auto& o=owners.at(node);require(o.control==c&&o.record==c->record&&c->record->style()==st.style_owner()&&node->identity==c->record->node_lifetime()&&node->identity->alive&&node->identity->attached&&node->identity->owner==node);
  require(o.whole&&c->text==o.whole->source);
  const auto& a=inventory.nodes[c->ordinal];require(st.font_size==a.inherited.font_size&&st.font_weight==int(a.inherited.font_weight)&&st.line_height==a.inherited.line_height&&same(st.color,color(a.inherited.color_rgb)));require(a.tag=="input"&&a.type==c->type&&c->shows_placeholder==a.value.empty());
  const auto kind=c->text.empty()?ControlKind::empty:(c->shows_placeholder?ControlKind::placeholder:ControlKind::value);require(c->kind==kind);
  require(same(c->color,c->shows_placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:color(a.inherited.color_rgb)));
 }
 void verify_dom(){session_validation::verify_dom(doc,inventory,nodes);}

 void run(){
  require(!prepared);core(aether_doc_create(&doc));core(aether_doc_parse_html(doc,html.c_str()));verify_dom();
  HTML_LTR_STAGE(runtime);
  aether_style_runtime_options_t options{};aether_style_environment_t env{float(viewport.width),float(viewport.height),1,16,0,0,0};core(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));int64_t id{};std::string stylesheet=ua;
  if(profile==2){auto start=stylesheet.find("html,body,div,h1,p {");require(start==0);stylesheet.replace(0,std::strlen("html,body,div,h1,p {"),"html,body,div,h1,p,input,button {");}
  core(aether_style_runtime_add_stylesheet(runtime,stylesheet.c_str(),0,"private-html-ltr",&id,nullptr));
  aether_style_transaction_t tx{};core(aether_style_runtime_prepare_revision(runtime,nodes[0],&env,&tx));const auto committed=aether_style_runtime_commit_revision(runtime,tx,&revision);aether_style_transaction_destroy(tx);core(committed);core(aether_style_revision_create_map(revision,&map));
  epoch=doc->identity->structure_epoch;nonstate=doc->identity->non_state_epoch;
  for(size_t i=0;i<nodes.size();++i){verify_style(map,nodes[i],inventory.nodes[i]);auto& e=inventory.nodes[i];if(e.leaf()||e.tag=="input"){
   auto record=map->current->backing()->find_owner(nodes[i]);require(bool(record));std::shared_ptr<const ControlPresentation> presentation;
   std::string shown=e.text;
   if(e.tag=="input"){
    require(profile==2);fonts.reserve_payload(sizeof(ControlPresentation)+sizeof(AtomicControlPlacement)*4+sizeof(NativeControlEntry)*4+(e.value.size()*3+e.placeholder.size())*8+1024);
    HTML_LTR_STAGE(control_presentation);
    aether::layout::BoxStyleSnapshot st(record->style(),record->measures());
    shown=aether_opaque_control_display_text(nodes[i],st.scalars());
    const bool placeholder=e.value.empty();std::string expected;
    if(placeholder)expected=e.placeholder;else if(e.type=="text")expected=e.value;else {for(unsigned char ch:e.value)if((ch&0xc0)!=0x80)expected+="\xe2\x80\xa2";}
    require(shown==expected);
    auto c=std::make_shared<ControlPresentation>();c->profile=profile;c->ordinal=i;c->record=record;c->epoch=epoch;c->nonstate=nonstate;c->type=e.type;c->kind=shown.empty()?ControlKind::empty:(placeholder?ControlKind::placeholder:ControlKind::value);c->text=shown;c->shows_placeholder=placeholder;c->color=placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:color(e.inherited.color_rgb);presentation=std::move(c);
   }
   auto whole=std::make_shared<const FontProduct>(fonts.product(shown,int(e.inherited.font_size),int(e.inherited.font_weight),int(e.inherited.line_height)));
   owners.emplace(nodes[i],Owner{i,record,whole,presentation});
  }}
  core(aether_layout_create(nullptr,nullptr,&engine));HtmlLtrSession::install(engine,std::make_shared<Bridge>(this));
  HTML_LTR_STAGE(layout);
  auto status=aether_layout_layout(engine,doc,map,float(viewport.width),float(viewport.height),nullptr,nullptr,&layout);
  if(status!=AETHER_OK){if(bridge_error!=AETHER_OK)throw Error{bridge_error};core(status);}valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,nullptr);
#endif
  FinalTexts final;FinalControls controls;std::map<aether_node_t,std::string> collected;std::map<aether_node_t,float> baselines;std::vector<const aether_layout_box_s*> todo{layout->root};size_t placed=0;
  while(!todo.empty()){auto* box=todo.back();todo.pop_back();require(box);auto r=box->content_box;for(double d:{double(r.x),double(r.y),double(r.width),double(r.height)})if(!std::isfinite(d))throw Error{AETHER_ERROR_INTERNAL};
   if(box->is_atomic_control){
    require(profile==2&&owners.contains(box->element));const auto& o=owners.at(box->element);const auto& a=box->atomic_control;check_control(a.presentation,box->element,box->style);require(a.presentation==o.control&&box->control_display_text==o.control->text&&box->control_shows_placeholder==o.control->shows_placeholder);
    require(std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>=0&&r.height>=0);
    aether_rectf rr{};if(o.control->text.empty()){require(!a.text.run&&!a.occurrence&&a.text.baseline==0);}else{
     const auto run=a.text.run;require(run&&occurrences.contains(run.get()));const auto& occ=occurrences.at(run.get());require(occ.owner==box->element&&occ.id==a.occurrence&&run->source==o.control->text&&a.text.baseline==run->metrics.baseline);
     rr={r.x,r.y+std::max(0.f,(r.height-run->metrics.height)/2.f),run->metrics.width,run->metrics.height};
     reserve_count(placed,run->glyphs.size(),16384);require(final.emplace(run.get(),FinalText{run,occ.product,rr,o.control->color,true}).second);
    }
    require(controls.emplace(box->element,FinalControl{a,r,rr}).second);collected[box->element]=o.control->text;
   }else {require(!box->atomic_control.presentation&&!box->atomic_control.text.run&&!box->atomic_control.occurrence);}
   if(box->kind==LAYOUT_BOX_TEXT){auto run=box->native_text.run;require(run&&occurrences.contains(run.get()));const auto& occurrence=occurrences.at(run.get());require(occurrence.owner==box->element&&run->source==box->text&&run->metrics.width==r.width&&run->metrics.height==r.height&&run->metrics.baseline==box->native_text.baseline);require(owners.at(box->element).record->style()==box->style.style_owner());
    const float baseline=r.y+run->metrics.baseline;if(baselines.contains(box->element))require(baselines.at(box->element)==baseline);else baselines.emplace(box->element,baseline);
    reserve_count(placed,run->glyphs.size(),16384);collected[box->element]+=box->text;require(final.emplace(run.get(),FinalText{run,occurrence.product,r,color(inventory.nodes[owners.at(box->element).ordinal].inherited.color_rgb),true}).second);
   }
   for(auto it=box->children.rbegin();it!=box->children.rend();++it)todo.push_back(*it);
  }
  for(size_t i=1;i<nodes.size();++i)require(aether_layout_result_find_primary_box(layout,nodes[i]));
  for(const auto& [node,owner]:owners){if(owner.control)require(controls.contains(node));require(collected[node]==(owner.control?owner.control->text:inventory.nodes[owner.ordinal].text));}
  core(aether_display_list_builder_create(nullptr,nullptr,&builder));
  if(registry)aether_display_list_builder_set_image_dimensions_callback(builder,&ImageRegistry::dimensions,const_cast<ImageRegistry*>(registry.get()));
  core(build_native_opacity(builder,layout,map,nullptr,paint));valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,paint.get());
#endif
  for(const auto& [node,o]:owners)if(o.control){aether::layout::BoxStyleSnapshot st(o.record->style(),o.record->measures());check_control(o.control,node,st);}
  if(registry)registry->attach(NativeListAccess::list(*paint),*paint);
  masks=finalize_paint(*paint,final,fonts,controls,profile);valid();HTML_LTR_STAGE(snapshot);auto snapshot=preparation::snapshot_native_opacity(*paint);
  if(auto e=std::get_if<preparation::Error>(&snapshot))throw Error{e->code==preparation::ErrorCode::UNSUPPORTED_TAG?AETHER_ERROR_UNSUPPORTED:e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};
  HTML_LTR_STAGE(preparation);auto frame=preparation::prepare(std::get<preparation::Snapshot>(snapshot));if(auto e=std::get_if<preparation::Error>(&frame))throw Error{e->code==preparation::ErrorCode::UNSUPPORTED_TAG?AETHER_ERROR_UNSUPPORTED:e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};valid();prepared.emplace(std::get<preparation::PreparedFrame>(std::move(frame)));
 }
};
void HtmlLtrSession::install(aether_layout_engine_s* e,std::shared_ptr<HtmlLtrMeasureBridge> b){require(e&&!e->layout_in_progress&&!e->html_ltr_bridge_&&!e->text_selection_&&!e->synthetic_text_policy_&&!e->text_measurer&&!e->contextual_text_measurer);e->html_ltr_bridge_=std::move(b);}
HtmlLtrSession::HtmlLtrSession(std::string h,std::vector<uint8_t> r,std::vector<uint8_t> b,Viewport v,uint32_t p):state_(std::make_unique<State>(std::move(h),std::move(r),std::move(b),v,p)){}
HtmlLtrSession::~HtmlLtrSession()=default;
void HtmlLtrSession::set_image_registry(std::shared_ptr<const ImageRegistry> registry){state_->registry=std::move(registry);}
const preparation::PreparedFrame& HtmlLtrSession::prepare(){state_->run();return *state_->prepared;}
const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& HtmlLtrSession::masks()const noexcept{return state_->masks;}
}
