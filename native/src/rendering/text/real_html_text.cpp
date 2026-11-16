#include "real_html_text.h"
#include "real_html_admission.h"
#include "real_html_mask_registry.h"
#include "real_html_style_transition.h"
#include "../../layout/real_simple_text_policy.h"
#include "../native_paint_list.h"
#include "../preparation/native_command_source.h"
#include <chrono>
namespace aether::rendering::text::truetype {
namespace {
using Clock=std::chrono::steady_clock;
void require_status(aether_status_t status,HtmlTextError fallback){if(status!=AETHER_OK)throw(status==AETHER_ERROR_OUT_OF_MEMORY?HtmlTextError::out_of_memory:fallback);}
auto elapsed(Clock::time_point start){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());}
struct Operation {
 const std::thread::id thread=std::this_thread::get_id();
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_style_transaction_t transaction{};aether_style_revision_t revision{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::unique_ptr<aether::layout::CallbackEpochGuard> epochs;
 ~Operation(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void valid(){if(std::this_thread::get_id()!=thread)throw HtmlTextError::invalid_input;if(epochs&&!epochs->valid())throw HtmlTextError::invalid_input;if(map){auto v=aether::style::pin_style_map(map);if(!v || !aether::style::inputs_current(v.get()))throw HtmlTextError::invalid_input;}}
 void check(Cancellation* c){valid();bool stop=c&&c->cancelled();valid();if(stop)throw HtmlTextError::cancelled;}
};
struct Adapter final:preparation::Cancellation {Operation& operation;truetype::Cancellation* cancel;std::optional<HtmlTextError> failure;
 Adapter(Operation& o,truetype::Cancellation* c):operation(o),cancel(c){}
 bool cancelled() noexcept override {if(failure)return true;try {operation.check(cancel);return false;}catch(HtmlTextError e){failure=e;return true;}catch(...){failure=HtmlTextError::invalid_input;return true;}}
 void rethrow(){if(failure)throw *failure;}
};
bool zero(aether_css_length v){return v.value==0 && v.linear_percent==0 && v.linear_em==0 && v.linear_rem==0 && v.linear_vw==0 && v.linear_vh==0;}
bool zeros(aether_css_edges e){return zero(e.top)&&zero(e.bottom)&&zero(e.left)&&zero(e.right);}
bool same_color(aether_color_rgba got,const std::string& value) {
 auto hex=[](char c){return c<='9'?c-'0':(c>='a'?c-'a'+10:c-'A'+10);};
 return value.size()==7 && got.r==float(hex(value[1])*16+hex(value[2]))/255.f && got.g==float(hex(value[3])*16+hex(value[4]))/255.f && got.b==float(hex(value[5])*16+hex(value[6]))/255.f && got.a==1;
}
void effective(Operation& o,aether_node_s* owner,const RealFontBinding& binding,LineBox box,const HtmlAdmission& proof,Cancellation* cancel){
 auto version=aether::style::pin_style_map(o.map);if(!version)throw HtmlTextError::invalid_input;
 std::vector<aether_node_s*> stack{o.doc};
 while(!stack.empty()) {
  o.check(cancel);auto* n=stack.back();stack.pop_back();
  if(n->type==AETHER_NODE_ELEMENT) {
   auto rec=version->backing()->find_owner(n);if(!rec||!rec->measures())throw HtmlTextError::unsupported;
   aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());auto view=s.view();
   if(s.display!=AETHER_DISPLAY_BLOCK || s.position!=AETHER_POSITION_STATIC || s.opacity!=1 ||
      s.overflow_x!=AETHER_OVERFLOW_VISIBLE || s.overflow_y!=AETHER_OVERFLOW_VISIBLE || !zeros(s.margin) ||
      !view.transforms().empty() || !view.backdrop_filters().empty() || !view.box_shadows().empty() ||
      !view.text_shadows().empty() || !view.background_layers().empty() || s.has_text_gradient)
    throw HtmlTextError::unsupported;
   // The actual parser/runtime must implement every admitted modeled value,
   // not merely accept the source spelling. Properties with no modeled field
   // remain an explicit closed-profile proof, never a claim of CSS support.
   const HtmlAdmissionNode* authored=nullptr;
   for(const auto& candidate:proof.nodes) {o.check(cancel);if(candidate.kind==HtmlAdmissionNode::Kind::element && candidate.tag==n->tag_name && candidate.attributes.size()==n->attributes.size()) {
    bool equal=true;for(const auto& [key,value]:candidate.attributes){auto it=n->attributes.find(key);if(it==n->attributes.end()||it->second!=value){equal=false;break;}}
    if(equal){authored=&candidate;break;}
   }}
   if(!authored)throw HtmlTextError::invalid_input;
   for(const auto& [key,value]:authored->declarations) {
    o.check(cancel);
    const aether_css_length* length=nullptr;
    if(key=="width")length=&s.width;
    for(auto side:{"top","right","bottom","left"}) {
     auto choose=[&](const aether_css_edges& edges)->const aether_css_length* {if(std::string_view(side)=="top")return &edges.top;if(std::string_view(side)=="right")return &edges.right;if(std::string_view(side)=="bottom")return &edges.bottom;return &edges.left;};
     if(key==std::string("padding-")+side)length=choose(s.padding);
     if(key==std::string("border-")+side+"-width")length=choose(s.border_width);
    }
    if(length) {float expected=std::stof(value);if(length->unit!=AETHER_CSS_UNIT_PX || length->value!=expected)throw HtmlTextError::unsupported;}
    if(key=="background-color" && !same_color(s.background_color,value))throw HtmlTextError::unsupported;
    if(key=="color" && !same_color(s.color,value))throw HtmlTextError::unsupported;
    for(auto side:{"top","right","bottom","left"}) {
     auto color=std::string_view(side)=="top"?s.border_colors.top:std::string_view(side)=="right"?s.border_colors.right:std::string_view(side)=="bottom"?s.border_colors.bottom:s.border_colors.left;
     auto style=std::string_view(side)=="top"?s.border_styles.top:std::string_view(side)=="right"?s.border_styles.right:std::string_view(side)=="bottom"?s.border_styles.bottom:s.border_styles.left;
     if(key==std::string("border-")+side+"-color" && !same_color(color,value))throw HtmlTextError::unsupported;
     if(key==std::string("border-")+side+"-style" && style!=AETHER_BORDER_STYLE_SOLID)throw HtmlTextError::unsupported;
    }
   }
   if(n==owner) {
    aether::style::MeasuresReadView m(*rec->measures());
    if(s.font_size!=float(binding.font()->size_26_6)/64.f || s.font_weight!=400 || s.white_space!=(proof.profile==HtmlWhiteSpaceProfile::pre?AETHER_WHITE_SPACE_PRE:AETHER_WHITE_SPACE_NOWRAP) ||
       s.text_transform!=AETHER_TEXT_TRANSFORM_NONE || s.text_align!=AETHER_TEXT_ALIGN_LEFT ||
       std::string(s.font_family.data(),strnlen(s.font_family.data(),s.font_family.size()))!="aether-explicit" ||
       m.direction_rtl() || m.writing_mode()!=0 ||
       aether::style::measure_used_line_height(&m,s)!=float(box.height26_6)/64.f ||
       s.color.r!=0||s.color.g!=0||s.color.b!=0||s.color.a!=1)throw HtmlTextError::unsupported;
   }
  }
  for(auto* child:n->children)stack.push_back(child);
 }
}
HtmlTextError run_error(RunError e){switch(e){case RunError::cancelled:return HtmlTextError::cancelled;case RunError::out_of_memory:return HtmlTextError::out_of_memory;case RunError::limit_exceeded:return HtmlTextError::limit_exceeded;case RunError::invalid_input:return HtmlTextError::invalid_input;default:return HtmlTextError::unsupported;}}
}
HtmlTextResult render_real_html_text(std::string_view raw,std::shared_ptr<const RealFontBinding> binding,LineBox line,
 std::uint32_t width,std::uint32_t height,Cancellation* cancel) noexcept {
 try {
  Operation o;o.check(cancel);
  if(!binding||!width||!height||width>2048||height>2048)return HtmlTextError::invalid_input;
  auto output=std::make_shared<HtmlTextFrame>();output->work.raw_bytes=raw.size();auto start=Clock::now();
  auto admission=admit_real_html(raw,binding->font()->size_26_6,line,cancel);o.check(cancel);
  if(auto* e=std::get_if<HtmlAdmissionError>(&admission)) {
   switch(*e){case HtmlAdmissionError::cancelled:return HtmlTextError::cancelled;case HtmlAdmissionError::limit_exceeded:return HtmlTextError::limit_exceeded;case HtmlAdmissionError::out_of_memory:return HtmlTextError::out_of_memory;case HtmlAdmissionError::unsupported:return HtmlTextError::unsupported;default:return HtmlTextError::invalid_input;}
  }
  const auto& proof=std::get<HtmlAdmission>(admission);
  require_status(aether_doc_create(&o.doc),HtmlTextError::out_of_memory);
  const std::string source(raw);
  require_status(aether_doc_parse_html(o.doc,source.c_str()),HtmlTextError::invalid_input);
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(o.doc->identity);o.check(cancel);
  auto* owner=verify_real_html_dom(proof,o.doc);if(!owner)return HtmlTextError::invalid_input;
  output->work.admission_ns=elapsed(start);start=Clock::now();
  aether_style_environment_t env{float(width),float(height),1,16,0,0,0};
  require_status(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&o.runtime),HtmlTextError::out_of_memory);
  int64_t sheet{};
  require_status(aether_style_runtime_add_stylesheet(o.runtime,"html,body{display:block;margin:0;padding:0;border-width:0;}",0,"real-profile-scaffold",&sheet,nullptr),HtmlTextError::invalid_input);
  o.check(cancel);
  const auto identity=o.doc->identity;
  const RealHtmlStyleTransition transition(o.doc);
  require_status(aether_style_runtime_prepare_revision(o.runtime,o.doc,&env,&o.transaction),HtmlTextError::invalid_input);
  require_status(aether_style_runtime_commit_revision(o.runtime,o.transaction,&o.revision),HtmlTextError::invalid_input);
  require_status(aether_style_revision_create_map(o.revision,&o.map),HtmlTextError::invalid_input);
  // Only this known style publication may advance the epoch guard. No user
  // callback runs in this interval; unexpected DOM/input changes are rejected.
  if(!transition.accepts(o.doc,o.map) || verify_real_html_dom(proof,o.doc)!=owner)throw HtmlTextError::invalid_input;
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(identity);
  o.check(cancel);effective(o,owner,*binding,line,proof,cancel);o.check(cancel);
  output->work.styles_ns=elapsed(start);
  start=Clock::now();
  output->normalization=HtmlNormalizationProof::create(proof.profile,binding,proof.pieces,cancel);o.check(cancel);
  if(output->normalization->decoded_input!=proof.decoded_text)return HtmlTextError::invalid_input;
  output->work.normalization_ns=elapsed(start);
  if(proof.profile==HtmlWhiteSpaceProfile::nowrap)output->profile="real_html_simple_latin1_nowrap_v1";
  output->work.decoded_input_bytes=output->normalization->decoded_input.size();
  output->work.normalized_output_bytes=output->normalization->normalized_output.size();
  output->work.normalization_entries=output->normalization->map.size();
  output->work.normalization_map_bytes=output->normalization->map.size()*sizeof(HtmlNormalizationEntry);
  output->work.normalization_logical_bytes=sizeof(HtmlNormalizationProof)+output->work.normalization_map_bytes+
    output->normalization->source_map.size()*sizeof(HtmlSourcePiece)+output->normalization->pieces.size()*sizeof(std::string)+
    2*output->work.decoded_input_bytes+output->work.normalized_output_bytes;
  // Single measurement. The retained Q2b canvas is NEVER a display-list image.
  start=Clock::now();
  auto run=build_simple_ltr_run(binding,output->normalization->normalized_output,line,cancel,&output->work.run);o.check(cancel);
  if(auto* e=std::get_if<RunError>(&run))return run_error(*e);
  output->work.run_ns=elapsed(start);
  output->run=std::get<std::shared_ptr<const PreparedSimpleRun>>(run);
  output->source_map=output->normalization->source_map;
  output->work.source_map_bytes=output->source_map.size()*sizeof(HtmlSourcePiece);
  output->work.run_canvas_bytes=output->run->canvas.rgba.size();output->work.retained_mask_bytes=output->work.run.retained_mask_bytes;output->work.font_bytes=binding->source()->bytes.size();
  require_status(aether_layout_create(nullptr,nullptr,&o.engine),HtmlTextError::out_of_memory);
  if(!aether::layout::RealSimpleTextAccess::install(*o.engine,o.doc,o.map,owner,output->run,output->normalization))return HtmlTextError::invalid_input;
  start=Clock::now();
  require_status(aether_layout_layout(o.engine,o.doc,o.map,float(width),float(height),nullptr,nullptr,&o.layout),HtmlTextError::layout_failed);
  o.check(cancel);output->work.layout_ns=elapsed(start);
  std::vector<aether_layout_box_s*> boxes{o.layout->root};std::size_t text_boxes=0;
  while(!boxes.empty()) {
   o.check(cancel);auto* b=boxes.back();boxes.pop_back();if(!b)continue;
   if(b->kind==LAYOUT_BOX_TEXT) {
    ++text_boxes;if(b->element!=owner || !std::isfinite(b->content_box.x)||!std::isfinite(b->content_box.y)||std::floor(b->content_box.x)!=b->content_box.x || std::floor(b->content_box.y)!=b->content_box.y)return HtmlTextError::unsupported;
    if(b->native_text.run && b->native_text.run->font!=binding->font())return HtmlTextError::invalid_input;
   }
   std::string id;if(b->element){auto it=b->element->attributes.find("id");if(it!=b->element->attributes.end())id=it->second;}
   output->boxes.push_back({id,b->kind==LAYOUT_BOX_TEXT,b->content_box,b->border_box});
   for(auto* child:b->children)boxes.push_back(child);
  }
  output->work.known_layout_box_bytes=output->boxes.size()*sizeof(aether_layout_box_s);
  if(text_boxes!=1)return HtmlTextError::layout_failed;
  start=Clock::now();
  require_status(aether_display_list_builder_create(nullptr,nullptr,&o.builder),HtmlTextError::out_of_memory);
  std::unique_ptr<NativePaintList> list;
  require_status(build_native_opacity(o.builder,o.layout,o.map,nullptr,NativeScrollInput{},list),HtmlTextError::preparation_failed);
  output->work.native_glyph_commands=list->text_runs.size();output->work.image_commands=list->images.size();
  if(output->work.image_commands || output->work.native_glyph_commands!=(output->run->source_text.empty()?0:1))return HtmlTextError::preparation_failed;
  Adapter adapter(o,cancel);
  auto snapshot=preparation::snapshot_native_opacity(*list,&adapter);adapter.rethrow();o.check(cancel);
  if(auto* error=std::get_if<preparation::Error>(&snapshot)) {
   if(error->code==preparation::ErrorCode::OUT_OF_MEMORY)return HtmlTextError::out_of_memory;
   if(error->code==preparation::ErrorCode::CANCELLED)return HtmlTextError::cancelled;
   if(error->code==preparation::ErrorCode::LIMIT_EXCEEDED)return HtmlTextError::limit_exceeded;
   return HtmlTextError::preparation_failed;
  }
  auto prepared=preparation::prepare(std::get<preparation::Snapshot>(snapshot),&adapter);adapter.rethrow();o.check(cancel);
  if(auto* error=std::get_if<preparation::Error>(&prepared)) {
   if(error->code==preparation::ErrorCode::OUT_OF_MEMORY)return HtmlTextError::out_of_memory;
   if(error->code==preparation::ErrorCode::CANCELLED)return HtmlTextError::cancelled;
   if(error->code==preparation::ErrorCode::LIMIT_EXCEEDED)return HtmlTextError::limit_exceeded;
   return HtmlTextError::preparation_failed;
  }
  RealHtmlMaskRegistry registry;
  for(const auto& g:output->run->glyphs)registry.add(g.mask,[&]{o.check(cancel);});
  output->work.registry_entries=registry.masks_.size();output->work.registry_metadata_bytes=registry.metadata_bytes();output->work.preparation_ns=elapsed(start);start=Clock::now();
  FrameStore store;if(store.resize({int(width),int(height),1})!=FrameStatus::ok)return HtmlTextError::render_failed;
  output->work.software=software::render_a8(store,std::get<preparation::PreparedFrame>(prepared),registry.masks_,&adapter);adapter.rethrow();o.check(cancel);
  if(output->work.software.status!=software::Status::ok) {
   if(output->work.software.status==software::Status::allocation_failed || output->work.software.frame_status==FrameStatus::allocation_failed)return HtmlTextError::out_of_memory;
   if(output->work.software.status==software::Status::cancelled)return HtmlTextError::cancelled;
   if(output->work.software.status==software::Status::limit_exceeded)return HtmlTextError::limit_exceeded;
   return HtmlTextError::render_failed;
  }
  std::optional<Frame> frame;auto copied=store.copy_confirmed(frame);if(copied==FrameStatus::allocation_failed)return HtmlTextError::out_of_memory;if(copied!=FrameStatus::ok || !frame)return HtmlTextError::render_failed;
  output->frame=std::move(*frame);output->work.render_ns=elapsed(start);o.check(cancel);
  return std::shared_ptr<const HtmlTextFrame>(std::move(output));
 }catch(HtmlTextError e){return e;}catch(const std::bad_alloc&){return HtmlTextError::out_of_memory;}catch(...){return HtmlTextError::invalid_input;}
}
}
