#include "interaction_native.h"
#include "../../style_runtime/transaction_preview.h"
#include "session_validation.h"
#include "measure_bridge.h"
#include "paint_finalize.h"
#include "raster_coordinate.h"
#include "../preparation/native_command_source.h"
#include "../../layout/layout_internal.h"
#include "../display_list_types.h"
#include <map>
#include <optional>
#include <thread>
namespace aether::rendering::html_ltr {
using namespace session_validation;
namespace {
struct StyleBundle {
 aether_style_revision_t revision{};
 aether_style_map_t map{};
 std::int32_t width{},height{};
 ~StyleBundle(){aether_style_map_destroy(map);aether_style_revision_destroy(revision);}
};
void viewport_valid(std::int32_t w,std::int32_t h){
 if(w<0||h<0||w>8192||h>8192||std::uint64_t(w)*std::uint64_t(h)*4>64u*1024u*1024u)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
}
}
struct InteractionNativeDocument::State {
 OwnedBudget budget;
 Inventory inventory;
 FontProvider fonts;
 aether_doc_t doc{};
 aether_style_runtime_t runtime{};
 std::shared_ptr<StyleBundle> styles;
 std::vector<aether_node_t> nodes;
 OwnedVector<std::size_t> control_ordinals;
 OwnedVector<InteractionControlSeed> seed;
 std::size_t immutable_scalars{};
 std::uint64_t epoch{},nonstate{};
 std::thread::id thread=std::this_thread::get_id();
 State(OwnedBudget::Handle d,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b)
 :budget(d),fonts(std::move(r),std::move(b),FontProvider::PersistentInteractionTag{budget}),control_ordinals(OwnedAllocator<std::size_t>(d,OwnedCategory::values)),seed(OwnedAllocator<InteractionControlSeed>(d,OwnedCategory::values)){}
 ~State(){styles.reset();aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void valid()const {if(thread!=std::this_thread::get_id())throw Error{AETHER_ERROR_INVALID_STATE};require(doc&&doc->identity->alive&&doc->identity->structure_epoch==epoch&&doc->identity->non_state_epoch==nonstate);}
 std::size_t control_index(std::size_t ordinal)const {auto it=std::find(control_ordinals.begin(),control_ordinals.end(),ordinal);require(it!=control_ordinals.end());return static_cast<std::size_t>(it-control_ordinals.begin());}
 void init(std::string_view html,std::int32_t width,std::int32_t height){
  viewport_valid(width,height);
  if(html.size()>65536)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  inventory=admit(html,2);
  OwnedString input(html.begin(),html.end(),OwnedAllocator<char>(budget,OwnedCategory::temporary));
  core(aether_doc_create(&doc));core(aether_doc_parse_html(doc,input.c_str()));
  session_validation::verify_dom(doc,inventory,nodes);
  aether_style_runtime_options_t options{};aether_style_environment_t env{float(std::max(1,width)),float(std::max(1,height)),1,16,0,0,0};
  core(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime));
  OwnedString stylesheet(ua,OwnedAllocator<char>(budget,OwnedCategory::temporary));
  stylesheet.replace(0,std::strlen("html,body,div,h1,p {"),"html,body,div,h1,p,input,button {");
  int64_t id{};core(aether_style_runtime_add_stylesheet(runtime,stylesheet.c_str(),0,"private-html-ltr",&id,nullptr));
  auto current=std::allocate_shared<StyleBundle>(OwnedAllocator<StyleBundle>(budget,OwnedCategory::document_style));
  current->width=width;current->height=height;
  aether_style_transaction_t tx{};core(aether_style_runtime_prepare_revision(runtime,nodes[0],&env,&tx));
  const auto status=aether_style_runtime_commit_revision(runtime,tx,&current->revision);aether_style_transaction_destroy(tx);core(status);
  core(aether_style_revision_create_map(current->revision,&current->map));
  styles=std::move(current);epoch=doc->identity->structure_epoch;nonstate=doc->identity->non_state_epoch;
  immutable_scalars=inventory.scalar_count;
  control_ordinals.reserve(inventory.control_count);seed.reserve(inventory.control_count);
  for(std::size_t i=0;i<nodes.size();++i){verify_style(styles->map,nodes[i],inventory.nodes[i]);const auto& n=inventory.nodes[i];if(!n.control())continue;
   control_ordinals.push_back(i);
   const auto kind=n.tag=="button"?InteractionControlKind::button:n.type=="password"?InteractionControlKind::password:InteractionControlKind::text;
   seed.push_back({kind,n.input()?std::string_view(n.value):std::string_view{},n.input()?std::string_view(n.placeholder):std::string_view{}});
   if(n.input())for(std::size_t at=0;at<n.value.size();){(void)decode_scalar_utf8(n.value,at);require(immutable_scalars>0);--immutable_scalars;}
  }
 }
};
struct InteractionNativeScene::State {
 struct Owner {size_t ordinal;aether::style::FrozenNodeRef record;std::shared_ptr<const FontProduct> whole;std::shared_ptr<const ControlPresentation> control;bool placeholder{};};
 struct Occurrence {std::shared_ptr<const text::ShapedRun> run;std::shared_ptr<const FontProduct> product;aether_node_s* owner;uint64_t id;};
 std::shared_ptr<InteractionNativeDocument::State> document;
 std::shared_ptr<StyleBundle> styles;
 OwnedBudget::Reservation paint_objects;
 FontProvider& fonts;
 const Inventory& inventory;
 const std::vector<aether_node_t>& nodes;
 aether_doc_t doc;
 aether_style_map_t map;
 const std::uint32_t profile=2;
 Viewport viewport;
 std::uint32_t focus_id;
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::map<aether_node_t,Owner> owners;std::map<const text::ShapedRun*,Occurrence> occurrences;std::unique_ptr<NativePaintList> paint;
 std::vector<std::shared_ptr<const text::GlyphMaskA8>> masks;std::optional<preparation::PreparedFrame> prepared;
 std::uint64_t epoch,nonstate,serial{};aether_status_t bridge_error=AETHER_OK;
 State(std::shared_ptr<InteractionNativeDocument::State> d,const InteractionCandidateView& v)
 :document(std::move(d)),styles(document->styles),paint_objects(document->budget.reserve(OwnedCategory::paint,sizeof(NativePaintList)+sizeof(aether_display_list_builder_s))),fonts(document->fonts),inventory(document->inventory),nodes(document->nodes),doc(document->doc),map(styles->map),viewport{v.metadata.width,v.metadata.height,1},focus_id(v.metadata.focus_id),epoch(document->epoch),nonstate(document->nonstate){}
 ~State(){prepared.reset();paint.reset();aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);}
 void valid()const {document->valid();require(aether_style_map_structure_valid(map)&&aether_style_map_inputs_current(map));}
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
  const auto& a=inventory.nodes[c->ordinal];require(st.font_size==a.inherited.font_size&&st.font_weight==int(a.inherited.font_weight)&&st.line_height==a.inherited.line_height&&same(st.color,color(a.inherited.color_rgb)));require(a.tag=="input"&&a.type==c->type&&c->shows_placeholder==o.placeholder);
  const auto kind=c->text.empty()?ControlKind::empty:(c->shows_placeholder?ControlKind::placeholder:ControlKind::value);require(c->kind==kind);
  require(same(c->color,c->shows_placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:color(a.inherited.color_rgb)));
 }
 void run(const InteractionCandidateView& view){
  require(!prepared);valid();
  for(size_t i=0;i<nodes.size();++i){const auto& e=inventory.nodes[i];if(e.leaf()||e.tag=="input"){
   auto record=map->current->backing()->find_owner(nodes[i]);require(bool(record));std::shared_ptr<const ControlPresentation> presentation;
   std::string shown=e.text;
   if(e.tag=="input"){
    require(profile==2);fonts.reserve_payload(sizeof(ControlPresentation)+sizeof(AtomicControlPlacement)*4+sizeof(NativeControlEntry)*4+(view.presentations[document->control_index(i)].text.size()+e.placeholder.size())*8+1024);
    HTML_LTR_STAGE(control_presentation);
    const auto control=document->control_index(i);
    const auto& input=view.presentations[control];
    shown.assign(input.text.data(),input.text.size());
    const bool placeholder=input.shows_placeholder;
    auto c=std::make_shared<ControlPresentation>();c->profile=profile;c->ordinal=i;c->record=record;c->epoch=epoch;c->nonstate=nonstate;c->type=e.type;c->kind=shown.empty()?ControlKind::empty:(placeholder?ControlKind::placeholder:ControlKind::value);c->text=shown;c->shows_placeholder=placeholder;c->color=placeholder?aether_color_rgba{0.42f,0.45f,0.5f,1.f}:color(e.inherited.color_rgb);presentation=std::move(c);
   }
   auto whole=std::make_shared<const FontProduct>(fonts.product(shown,int(e.inherited.font_size),int(e.inherited.font_weight),int(e.inherited.line_height)));
   owners.emplace(nodes[i],Owner{i,record,whole,presentation,presentation?presentation->shows_placeholder:false});
  }}
  core(aether_layout_create(nullptr,nullptr,&engine));InteractionNativeDocument::install(engine,std::allocate_shared<Bridge>(OwnedAllocator<Bridge>(document->budget,OwnedCategory::layout),this));
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
  core(aether_display_list_builder_create(nullptr,nullptr,&builder));core(build_native_interaction(builder,layout,map,focus_id?nodes[document->control_ordinals[focus_id-1]]:nullptr,paint));valid();
  #ifdef AETHER_HTML_LTR_TESTING
  testing::inspect(layout,paint.get());
#endif
  for(const auto& [node,o]:owners)if(o.control){aether::layout::BoxStyleSnapshot st(o.record->style(),o.record->measures());check_control(o.control,node,st);}
  masks=finalize_paint(*paint,final,fonts,controls,profile);valid();HTML_LTR_STAGE(snapshot);auto snapshot=preparation::snapshot_native_opacity(*paint);
  if(auto e=std::get_if<preparation::Error>(&snapshot))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};
  HTML_LTR_STAGE(preparation);auto frame=preparation::prepare(std::get<preparation::Snapshot>(snapshot));if(auto e=std::get_if<preparation::Error>(&frame))throw Error{e->code==preparation::ErrorCode::OUT_OF_MEMORY?AETHER_ERROR_OUT_OF_MEMORY:e->code==preparation::ErrorCode::LIMIT_EXCEEDED?AETHER_ERROR_LIMIT_EXCEEDED:AETHER_ERROR_INTERNAL};valid();prepared.emplace(std::get<preparation::PreparedFrame>(std::move(frame)));
 }
};
void InteractionNativeDocument::install(aether_layout_engine_s* e,std::shared_ptr<HtmlLtrMeasureBridge> b){require(e&&!e->layout_in_progress&&!e->html_ltr_bridge_&&!e->text_selection_&&!e->synthetic_text_policy_&&!e->text_measurer&&!e->contextual_text_measurer);e->html_ltr_bridge_=std::move(b);}
InteractionNativeDocument::InteractionNativeDocument(std::shared_ptr<State> state):state_(std::move(state)){}
InteractionNativeDocument::~InteractionNativeDocument()=default;
std::shared_ptr<InteractionNativeDocument> InteractionNativeDocument::create(OwnedBudget& budget,std::string_view html,std::shared_ptr<const text::FontSource> r,std::shared_ptr<const text::FontSource> b,std::int32_t width,std::int32_t height){
 auto state=std::allocate_shared<State>(OwnedAllocator<State>(budget,OwnedCategory::document_style),budget.handle(),std::move(r),std::move(b));
 state->init(html,width,height);
 return std::allocate_shared<InteractionNativeDocument>(OwnedAllocator<InteractionNativeDocument>(budget,OwnedCategory::document_style),std::move(state));
}
std::span<const InteractionControlSeed> InteractionNativeDocument::seeds()const noexcept{return state_->seed;}
std::size_t InteractionNativeDocument::immutable_scalar_count()const noexcept{return state_->immutable_scalars;}
void InteractionNativeDocument::validate(const InteractionCandidateView& view){
 auto& s=*state_;s.valid();viewport_valid(view.metadata.width,view.metadata.height);
 require(view.presentations.size()==s.seed.size()&&view.metadata.focus_id<=s.seed.size());
 auto operation=s.fonts.operation();
 for(std::size_t i=0;i<s.inventory.nodes.size();++i){const auto& n=s.inventory.nodes[i];if(!n.leaf()&&!n.input())continue;
  std::string_view shown=n.text;
  if(n.input()){const auto id=s.control_index(i);require(view.presentations[id].kind==s.seed[id].kind);shown=view.presentations[id].text;}
  (void)s.fonts.product(shown,int(n.inherited.font_size),int(n.inherited.font_weight),int(n.inherited.line_height));
 }
 if(s.styles->width!=view.metadata.width||s.styles->height!=view.metadata.height){
  auto next=std::allocate_shared<StyleBundle>(OwnedAllocator<StyleBundle>(s.budget,OwnedCategory::document_style));
  next->width=view.metadata.width;next->height=view.metadata.height;
  // A suspended viewport has no layout. Use a positive contextual environment
  // until a later resize restores dimensions; authoritative dimensions stay zero.
  aether_style_environment_t env{float(std::max(1,next->width)),float(std::max(1,next->height)),1,16,0,0,0};
  aether_style_transaction_t raw{};
  core(aether_style_runtime_prepare_revision(s.runtime,s.nodes[0],&env,&raw));
  std::unique_ptr<aether_style_transaction_s,decltype(&aether_style_transaction_destroy)> tx(raw,aether_style_transaction_destroy);
  core(aether::style::transaction_create_pending_map(tx.get(),&next->map));
  // Never consume the staged map. Immutable admitted CSS has no viewport
  // queries; contextual measures are recomputed by the same runtime.
  // This runtime was created with null diagnostic callback/user data, hence
  // commit's replay returns immediately, without callbacks or reentry.
  core(aether_style_runtime_commit_revision(s.runtime,tx.get(),&next->revision));
  s.styles.swap(next); // no fallible work follows publication
 }
}
std::shared_ptr<InteractionNativeScene> InteractionNativeDocument::prepare(const InteractionCandidateView& view){
 state_->valid();require(view.presentations.size()==state_->seed.size());
 if(view.metadata.width==0||view.metadata.height==0)throw Error{AETHER_ERROR_SUSPENDED};
 require(view.metadata.width==state_->styles->width&&view.metadata.height==state_->styles->height);
 auto operation=state_->fonts.operation();
 auto scene=std::allocate_shared<InteractionNativeScene::State>(OwnedAllocator<InteractionNativeScene::State>(state_->budget,OwnedCategory::layout),state_,view);
 scene->run(view);
 return std::allocate_shared<InteractionNativeScene>(OwnedAllocator<InteractionNativeScene>(state_->budget,OwnedCategory::layout),std::move(scene));
}
InteractionNativeScene::InteractionNativeScene(std::shared_ptr<State> s):state_(std::move(s)){}
InteractionNativeScene::~InteractionNativeScene()=default;
const preparation::PreparedFrame& InteractionNativeScene::prepared()const noexcept{return *state_->prepared;}
const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& InteractionNativeScene::masks()const noexcept{return state_->masks;}
std::uint32_t InteractionNativeScene::hit(float x,float y)const {
 const auto& s=*state_;s.valid();
 if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=s.viewport.width||y>=s.viewport.height)return 0;
 // Strict profile2 has normal-flow rectangular controls and no authored clips
 // except the viewport. Reverse paint/document order resolves overlapping edges.
 for(std::size_t n=s.document->control_ordinals.size();n>0;--n){auto* box=aether_layout_result_find_primary_box(s.layout,s.nodes[s.document->control_ordinals[n-1]]);require(box);const auto& r=box->border_box;
  if(r.width>0&&r.height>0&&x>=r.x&&y>=r.y&&x<r.x+r.width&&y<r.y+r.height)return static_cast<std::uint32_t>(n);
 }
 return 0;
}
}
