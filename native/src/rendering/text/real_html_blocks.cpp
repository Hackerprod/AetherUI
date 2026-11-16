#include "real_html_blocks.h"
#include "real_html_style_transition.h"
#include "real_html_blocks_admission.h"
#include "real_html_mask_registry.h"
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
void effective(Operation& o,const std::vector<aether_node_s*>& nodes,const RealFontBinding& binding,LineBox box,const HtmlBlocksAdmission& proof,Cancellation* cancel){
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
   const HtmlAdmissionNode* authored=nullptr;std::size_t node_index=0;
   for(;node_index<nodes.size();++node_index)if(nodes[node_index]==n){authored=&proof.nodes[node_index];break;}
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
   for(const auto& owner:proof.owners)if(owner.node_index==node_index) {
    aether::style::MeasuresReadView m(*rec->measures());
    if(s.font_size!=float(binding.font()->size_26_6)/64.f || s.font_weight!=400 || s.white_space!=(owner.profile==HtmlWhiteSpaceProfile::pre?AETHER_WHITE_SPACE_PRE:AETHER_WHITE_SPACE_NOWRAP) ||
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
HtmlBlocksResult render_real_html_blocks(std::string_view raw,const std::vector<std::string>& owner_ids,
 std::shared_ptr<const RealFontBinding> binding,LineBox line,std::uint32_t width,std::uint32_t height,Cancellation* cancel) noexcept {
 try {
  Operation o;o.check(cancel);
  if(!binding||!width||!height||width>2048||height>2048)return HtmlTextError::invalid_input;
  if(owner_ids.size()>8)return HtmlTextError::limit_exceeded;
  if(owner_ids.empty())return HtmlTextError::invalid_input;
  for(const auto& id:owner_ids)if(id.empty()||id.size()>64)return HtmlTextError::invalid_input;
  const auto ids=owner_ids; // Caller must not mutate inputs concurrently.
  auto start=Clock::now();auto admitted=admit_real_html_blocks(raw,ids,binding->font()->size_26_6,line,cancel);o.check(cancel);
  if(auto* e=std::get_if<HtmlAdmissionError>(&admitted)) {
   switch(*e){case HtmlAdmissionError::cancelled:return HtmlTextError::cancelled;case HtmlAdmissionError::out_of_memory:return HtmlTextError::out_of_memory;case HtmlAdmissionError::limit_exceeded:return HtmlTextError::limit_exceeded;case HtmlAdmissionError::unsupported:return HtmlTextError::unsupported;default:return HtmlTextError::invalid_input;}
  }
  const auto& proof=std::get<HtmlBlocksAdmission>(admitted);
  auto output=std::make_shared<HtmlBlocksFrame>();auto& total=output->work.totals;
  output->work.owners=proof.owners.size();output->work.admission_normalization_steps=proof.steps;output->work.proof_logical_bytes=proof.logical_bytes;
  auto charge=[&](std::size_t count,std::size_t size){constexpr std::size_t cap=2*1024*1024;auto& used=output->work.proof_logical_bytes;if(used>cap||(size&&count>(cap-used)/size))throw HtmlTextError::limit_exceeded;used+=count*size;};
  charge(ids.size(),sizeof(std::string));for(const auto& id:ids)charge(id.size(),1);
  charge(1,sizeof(HtmlBlocksFrame));charge(proof.owners.size(),sizeof(HtmlBlockText));
  total.raw_bytes=raw.size();require_status(aether_doc_create(&o.doc),HtmlTextError::out_of_memory);
  charge(raw.size(),1);const std::string source(raw);require_status(aether_doc_parse_html(o.doc,source.c_str()),HtmlTextError::invalid_input);
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(o.doc->identity);o.check(cancel);
  auto nodes=verify_real_html_blocks_nodes(proof,o.doc);if(nodes.size()!=proof.nodes.size())return HtmlTextError::invalid_input;
  std::vector<aether_node_s*> owners;for(const auto& owner:proof.owners)owners.push_back(nodes.at(owner.node_index));
  total.admission_ns=elapsed(start);start=Clock::now();
  aether_style_environment_t env{float(width),float(height),1,16,0,0,0};
  require_status(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&o.runtime),HtmlTextError::out_of_memory);
  int64_t sheet{};require_status(aether_style_runtime_add_stylesheet(o.runtime,"html,body{display:block;margin:0;padding:0;border-width:0;}",0,"real-block-profile-scaffold",&sheet,nullptr),HtmlTextError::invalid_input);
  o.check(cancel);const RealHtmlStyleTransition transition(o.doc);
  require_status(aether_style_runtime_prepare_revision(o.runtime,o.doc,&env,&o.transaction),HtmlTextError::invalid_input);
  require_status(aether_style_runtime_commit_revision(o.runtime,o.transaction,&o.revision),HtmlTextError::invalid_input);
  require_status(aether_style_revision_create_map(o.revision,&o.map),HtmlTextError::invalid_input);
  if(!transition.accepts(o.doc,o.map)||verify_real_html_blocks_nodes(proof,o.doc)!=nodes)return HtmlTextError::invalid_input;
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(o.doc->identity);o.check(cancel);
  effective(o,nodes,*binding,line,proof,cancel);o.check(cancel);total.styles_ns=elapsed(start);start=Clock::now();
  output->owners.reserve(owners.size());std::size_t entries=0,pieces=0,decoded=0,normalized=0;
  for(const auto& authored:proof.owners) {
   o.check(cancel);HtmlBlockText block;charge(authored.id.size(),1);block.id=authored.id;
   block.normalization=HtmlNormalizationProof::create_budgeted(authored.profile,binding,authored.pieces,output->work.admission_normalization_steps,output->work.proof_logical_bytes,cancel);o.check(cancel);
   const auto& p=*block.normalization;if(p.decoded_input!=authored.decoded_text)return HtmlTextError::invalid_input;
   auto sum=[](std::size_t& n,std::size_t delta,std::size_t cap){if(n>cap||delta>cap-n)throw HtmlTextError::limit_exceeded;n+=delta;};
   sum(decoded,p.decoded_input.size(),4096);sum(normalized,p.normalized_output.size(),4096);sum(entries,p.map.size(),4224);sum(pieces,p.pieces.size(),128);
   for(unsigned char c:p.normalized_output){if(output->work.admission_normalization_steps>=1000000)throw HtmlTextError::limit_exceeded;++output->work.admission_normalization_steps;o.check(cancel);if((c&192)!=128)sum(output->work.normalized_scalars,1,256);}
   charge(p.source_map.size(),sizeof(HtmlSourcePiece));block.source_map=p.source_map;
   block.work.decoded_input_bytes=p.decoded_input.size();block.work.normalized_output_bytes=p.normalized_output.size();block.work.normalization_entries=p.map.size();block.work.source_map_bytes=p.source_map.size()*sizeof(HtmlSourcePiece);block.work.normalization_map_bytes=p.map.size()*sizeof(HtmlNormalizationEntry);
   block.work.normalization_logical_bytes=sizeof(HtmlNormalizationProof)+block.work.normalization_map_bytes+block.work.source_map_bytes+p.pieces.size()*sizeof(std::string)+2*p.decoded_input.size()+p.normalized_output.size();
   if(block.work.normalization_logical_bytes>std::numeric_limits<std::size_t>::max()-total.normalization_logical_bytes)throw HtmlTextError::limit_exceeded;
   total.normalization_logical_bytes+=block.work.normalization_logical_bytes;
   output->owners.push_back(std::move(block));
  }
  total.decoded_input_bytes=decoded;total.normalized_output_bytes=normalized;total.normalization_entries=entries;total.normalization_map_bytes=entries*sizeof(HtmlNormalizationEntry);total.source_map_bytes=pieces*sizeof(HtmlSourcePiece);total.normalization_ns=elapsed(start);
  // No run or raster work starts before every owner's normalization and total
  // scalar checks have succeeded. Reservations remain consumed on failure.
  RunBudget budget;std::vector<std::shared_ptr<const PreparedSimpleRun>> runs;
  std::vector<std::shared_ptr<const HtmlNormalizationProof>> normalizations;
  auto add_work=[&](const RunWork& work){
   for(auto member:{&RunWork::decoded,&RunWork::lookups,&RunWork::lookup_comparisons,&RunWork::outline_reserved,&RunWork::outline_units,&RunWork::raster_reserved,&RunWork::raster_units,&RunWork::retained_mask_bytes,&RunWork::source_pixel_visits,&RunWork::clear_pixels,&RunWork::conversion_pixels,&RunWork::cancel_checks,&RunWork::cmap_reserved,&RunWork::mask_reserved,&RunWork::canvas_reserved,&RunWork::composite_reserved}) {
    auto& value=total.run.*member;const auto delta=work.*member;if(delta>std::numeric_limits<std::uint64_t>::max()-value)throw HtmlTextError::limit_exceeded;value+=delta;
   }
   total.run.composition_started=total.run.composition_started||work.composition_started;
   total.run.canvas_complete=total.run.canvas_complete&&work.canvas_complete;
  };
  total.run.canvas_complete=true;
  start=Clock::now();
  for(auto& block:output->owners) {
   o.check(cancel);charge(block.normalization->normalized_output.size(),1); // Q2b retained source_text.
   auto rs=Clock::now();auto result=build_simple_ltr_run_budgeted(binding,block.normalization->normalized_output,line,budget,cancel,&block.work.run);o.check(cancel);
   if(auto* e=std::get_if<RunError>(&result))return run_error(*e);
   block.run=std::get<std::shared_ptr<const PreparedSimpleRun>>(result);block.work.run_ns=elapsed(rs);block.work.run_canvas_bytes=block.run->canvas.rgba.size();block.work.retained_mask_bytes=block.work.run.retained_mask_bytes;
   add_work(block.work.run);
   total.run_canvas_bytes+=block.work.run_canvas_bytes;total.retained_mask_bytes+=block.work.retained_mask_bytes;runs.push_back(block.run);normalizations.push_back(block.normalization);
  }
  total.run_ns=elapsed(start);total.font_bytes=binding->source()->bytes.size();
  output->work.outline_reserved=256000000-budget.outline.remaining;output->work.cmap_reserved=4352-budget.cmap.remaining;output->work.raster_reserved=512000000-budget.raster.remaining;
  output->work.mask_reserved=64ULL*1024*1024-budget.masks.remaining;output->work.canvas_pixels_reserved=16ULL*1024*1024-budget.canvas_pixels.remaining;output->work.composite_visits_reserved=16ULL*1024*1024-budget.composite_visits.remaining;
  require_status(aether_layout_create(nullptr,nullptr,&o.engine),HtmlTextError::out_of_memory);
  charge(normalized,1); // Per-record ShapedRun retained source; empty runs copy zero bytes.
  if(!aether::layout::RealSimpleTextAccess::install_blocks(*o.engine,o.doc,o.map,owners,runs,normalizations))return HtmlTextError::invalid_input;
  start=Clock::now();require_status(aether_layout_layout(o.engine,o.doc,o.map,float(width),float(height),nullptr,nullptr,&o.layout),HtmlTextError::layout_failed);o.check(cancel);total.layout_ns=elapsed(start);
  std::vector<std::shared_ptr<const ShapedRun>> transports(owners.size());std::vector<bool> seen(owners.size(),false);std::vector<aether_layout_box_s*> stack{o.layout->root};
  while(!stack.empty()) {
   o.check(cancel);auto* b=stack.back();stack.pop_back();if(!b)continue;
   std::string id;if(b->element){auto it=b->element->attributes.find("id");if(it!=b->element->attributes.end()){charge(it->second.size(),1);id=it->second;}}
   charge(1,sizeof(HtmlBox));charge(id.size(),1);HtmlBox snapshot{id,b->kind==LAYOUT_BOX_TEXT,b->content_box,b->border_box};charge(id.size(),1);output->boxes.push_back(snapshot);
   if(b->kind==LAYOUT_BOX_TEXT) {
    auto found=std::find(owners.begin(),owners.end(),b->element);if(found==owners.end())return HtmlTextError::layout_failed;auto i=std::size_t(found-owners.begin());if(seen[i])return HtmlTextError::layout_failed;seen[i]=true;
    if(!std::isfinite(b->content_box.x)||!std::isfinite(b->content_box.y)||std::floor(b->content_box.x)!=b->content_box.x||std::floor(b->content_box.y)!=b->content_box.y)return HtmlTextError::unsupported;
    const auto& run=runs[i];if(b->content_box.width!=float(run->width_26_6)/64.f||b->content_box.height!=float(line.height26_6)/64.f)return HtmlTextError::layout_failed;
    if(bool(b->native_text.run)!=!run->source_text.empty())return HtmlTextError::invalid_input;
    if(b->native_text.run && (b->native_text.run->font!=binding->font()||b->native_text.run->source!=run->source_text))return HtmlTextError::invalid_input;
    transports[i]=b->native_text.run;output->owners[i].text_box=std::move(snapshot);
   }
   for(auto it=b->children.rbegin();it!=b->children.rend();++it)stack.push_back(*it);
  }
  for(bool present:seen)if(!present)return HtmlTextError::layout_failed;
  total.known_layout_box_bytes=output->boxes.size()*sizeof(aether_layout_box_s);start=Clock::now();
  require_status(aether_display_list_builder_create(nullptr,nullptr,&o.builder),HtmlTextError::out_of_memory);std::unique_ptr<NativePaintList> list;
  require_status(build_native_opacity(o.builder,o.layout,o.map,nullptr,NativeScrollInput{},list),HtmlTextError::preparation_failed);
  total.native_glyph_commands=list->text_runs.size();total.image_commands=list->images.size();if(total.image_commands)return HtmlTextError::preparation_failed;
  for(const auto& entry:list->text_runs) {
   o.check(cancel);auto found=std::find(transports.begin(),transports.end(),entry.placement.run);if(found==transports.end()||!entry.placement.run)return HtmlTextError::preparation_failed;
   auto& block=output->owners[std::size_t(found-transports.begin())];if(block.has_command)return HtmlTextError::preparation_failed;block.has_command=true;block.command_index=entry.command_index;block.work.native_glyph_commands=1;
  }
  for(const auto& block:output->owners)if(block.has_command!=!block.run->source_text.empty())return HtmlTextError::preparation_failed;
  Adapter adapter(o,cancel);auto snapshot=preparation::snapshot_native_opacity(*list,&adapter);adapter.rethrow();o.check(cancel);
  auto prep_error=[](const preparation::Error& e){switch(e.code){case preparation::ErrorCode::OUT_OF_MEMORY:return HtmlTextError::out_of_memory;case preparation::ErrorCode::CANCELLED:return HtmlTextError::cancelled;case preparation::ErrorCode::LIMIT_EXCEEDED:return HtmlTextError::limit_exceeded;default:return HtmlTextError::preparation_failed;}};
  if(auto* e=std::get_if<preparation::Error>(&snapshot))return prep_error(*e);
  auto prepared=preparation::prepare(std::get<preparation::Snapshot>(snapshot),&adapter);adapter.rethrow();o.check(cancel);if(auto* e=std::get_if<preparation::Error>(&prepared))return prep_error(*e);
  RealHtmlMaskRegistry registry;for(const auto& run:runs)for(const auto& glyph:run->glyphs){registry.add(glyph.mask,[&]{o.check(cancel);});if(registry.masks_.size()>256)return HtmlTextError::limit_exceeded;}
  total.registry_entries=registry.masks_.size();total.registry_metadata_bytes=registry.metadata_bytes();total.preparation_ns=elapsed(start);start=Clock::now();
  FrameStore store;auto resized=store.resize({int(width),int(height),1});if(resized==FrameStatus::allocation_failed)return HtmlTextError::out_of_memory;if(resized!=FrameStatus::ok)return HtmlTextError::render_failed;
  total.software=software::render_a8(store,std::get<preparation::PreparedFrame>(prepared),registry.masks_,&adapter);adapter.rethrow();o.check(cancel);
  if(total.software.status!=software::Status::ok){if(total.software.status==software::Status::allocation_failed||total.software.frame_status==FrameStatus::allocation_failed)return HtmlTextError::out_of_memory;if(total.software.status==software::Status::cancelled)return HtmlTextError::cancelled;if(total.software.status==software::Status::limit_exceeded)return HtmlTextError::limit_exceeded;return HtmlTextError::render_failed;}
  std::optional<Frame> frame;auto copied=store.copy_confirmed(frame);if(copied==FrameStatus::allocation_failed)return HtmlTextError::out_of_memory;if(copied!=FrameStatus::ok||!frame)return HtmlTextError::render_failed;
  output->frame=std::move(*frame);total.render_ns=elapsed(start);o.check(cancel);return std::shared_ptr<const HtmlBlocksFrame>(std::move(output));
 }catch(HtmlTextError e){return e;}catch(const std::bad_alloc&){return HtmlTextError::out_of_memory;}catch(...){return HtmlTextError::invalid_input;}
}
}
