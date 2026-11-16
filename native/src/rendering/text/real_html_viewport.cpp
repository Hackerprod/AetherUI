#include "real_html_viewport.h"
#include "../native_viewport_capture.h"
#include "../display_list_types.h"
#include "real_html_style_transition.h"
#include "real_html_blocks_images_admission.h"
#include "real_html_mask_registry.h"
#include "../../layout/real_simple_text_policy.h"
#include "../native_paint_list.h"
#include "../preparation/native_command_source.h"
#include <chrono>
#include <array>
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
bool intrinsic_size(int32_t kind,aether_css_length x,aether_css_length y){return kind==AETHER_BACKGROUND_SIZE_EXPLICIT && x.unit==AETHER_CSS_UNIT_AUTO && y.unit==AETHER_CSS_UNIT_AUTO && zero(x) && zero(y);}
bool zeros(aether_css_edges e){return zero(e.top)&&zero(e.bottom)&&zero(e.left)&&zero(e.right);}
bool same_color(aether_color_rgba got,const std::string& value) {
 auto hex=[](char c){return c<='9'?c-'0':(c>='a'?c-'a'+10:c-'A'+10);};
 return value.size()==7 && got.r==float(hex(value[1])*16+hex(value[2]))/255.f && got.g==float(hex(value[3])*16+hex(value[4]))/255.f && got.b==float(hex(value[5])*16+hex(value[6]))/255.f && got.a==1;
}
void effective(Operation& o,const std::vector<aether_node_s*>& nodes,const RealFontBinding& binding,LineBox box,const HtmlBlocksAdmission& proof,const std::vector<HtmlBlockImageTarget>& images,Cancellation* cancel){
 auto version=aether::style::pin_style_map(o.map);if(!version)throw HtmlTextError::invalid_input;
 std::vector<aether_node_s*> stack{o.doc};
 while(!stack.empty()) {
  o.check(cancel);auto* n=stack.back();stack.pop_back();
  if(n->type==AETHER_NODE_ELEMENT) {
   auto rec=version->backing()->find_owner(n);if(!rec||!rec->measures())throw HtmlTextError::preparation_failed;
   aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());auto view=s.view();
   if(s.display!=AETHER_DISPLAY_BLOCK || s.position!=AETHER_POSITION_STATIC || s.opacity!=1 ||
      s.overflow_x!=AETHER_OVERFLOW_VISIBLE || s.overflow_y!=AETHER_OVERFLOW_VISIBLE || !zeros(s.margin) ||
      !view.transforms().empty() || !view.backdrop_filters().empty() || !view.box_shadows().empty() ||
      !view.text_shadows().empty() || s.has_text_gradient)
    throw HtmlTextError::preparation_failed;
   // The actual parser/runtime must implement every admitted modeled value,
   // not merely accept the source spelling. Properties with no modeled field
   // remain an explicit closed-profile proof, never a claim of CSS support.
   const HtmlAdmissionNode* authored=nullptr;std::size_t node_index=0;
   for(;node_index<nodes.size();++node_index)if(nodes[node_index]==n){authored=&proof.nodes[node_index];break;}
   if(!authored)throw HtmlTextError::preparation_failed;
   const HtmlBlockImageTarget* image=nullptr;for(const auto& candidate:images)if(candidate.node_index==node_index){image=&candidate;break;}
   auto layers=view.background_layers();
   if(image) {
    if(layers.size()!=1)throw HtmlTextError::preparation_failed;
    const auto layer=layers[0].view();const auto& v=layer.scalars;
    if(v.kind!=AETHER_BACKGROUND_LAYER_IMAGE_URL || !layer.image_source.equals(image->key) ||
       v.repeat_x!=AETHER_BACKGROUND_NO_REPEAT || v.repeat_y!=AETHER_BACKGROUND_NO_REPEAT ||
       !intrinsic_size(v.size_kind,v.size_x,v.size_y) || v.position_x.unit!=AETHER_CSS_UNIT_PX ||
       v.position_y.unit!=AETHER_CSS_UNIT_PX || !zero(v.position_x) || !zero(v.position_y) || !zeros(s.border_width))
     throw HtmlTextError::preparation_failed;
   } else if(!layers.empty())throw HtmlTextError::preparation_failed;
   for(const auto& [key,value]:authored->declarations) {
    o.check(cancel);
    const aether_css_length* length=nullptr;
    if(key=="width")length=&s.width;
    for(auto side:{"top","right","bottom","left"}) {
     auto choose=[&](const aether_css_edges& edges)->const aether_css_length* {if(std::string_view(side)=="top")return &edges.top;if(std::string_view(side)=="right")return &edges.right;if(std::string_view(side)=="bottom")return &edges.bottom;return &edges.left;};
     if(key==std::string("padding-")+side)length=choose(s.padding);
     if(key==std::string("border-")+side+"-width")length=choose(s.border_width);
    }
    if(length) {float expected=std::stof(value);if(length->unit!=AETHER_CSS_UNIT_PX || length->value!=expected)throw HtmlTextError::preparation_failed;}
    if(key=="background-color" && !same_color(s.background_color,value))throw HtmlTextError::preparation_failed;
    if(key=="color" && !same_color(s.color,value))throw HtmlTextError::preparation_failed;
    for(auto side:{"top","right","bottom","left"}) {
     auto color=std::string_view(side)=="top"?s.border_colors.top:std::string_view(side)=="right"?s.border_colors.right:std::string_view(side)=="bottom"?s.border_colors.bottom:s.border_colors.left;
     auto style=std::string_view(side)=="top"?s.border_styles.top:std::string_view(side)=="right"?s.border_styles.right:std::string_view(side)=="bottom"?s.border_styles.bottom:s.border_styles.left;
     if(key==std::string("border-")+side+"-color" && !same_color(color,value))throw HtmlTextError::preparation_failed;
     if(key==std::string("border-")+side+"-style" && style!=AETHER_BORDER_STYLE_SOLID)throw HtmlTextError::preparation_failed;
    }
   }
   for(const auto& owner:proof.owners)if(owner.node_index==node_index) {
    aether::style::MeasuresReadView m(*rec->measures());
    if(s.font_size!=float(binding.font()->size_26_6)/64.f || s.font_weight!=400 || s.white_space!=(owner.profile==HtmlWhiteSpaceProfile::pre?AETHER_WHITE_SPACE_PRE:AETHER_WHITE_SPACE_NOWRAP) ||
       s.text_transform!=AETHER_TEXT_TRANSFORM_NONE || s.text_align!=AETHER_TEXT_ALIGN_LEFT ||
       std::string(s.font_family.data(),strnlen(s.font_family.data(),s.font_family.size()))!="aether-explicit" ||
       m.direction_rtl() || m.writing_mode()!=0 ||
       aether::style::measure_used_line_height(&m,s)!=float(box.height26_6)/64.f ||
       s.color.r!=0||s.color.g!=0||s.color.b!=0||s.color.a!=1)throw HtmlTextError::preparation_failed;
   }
  }
  for(auto* child:n->children)stack.push_back(child);
 }
}
HtmlTextError run_error(RunError e){switch(e){case RunError::cancelled:return HtmlTextError::cancelled;case RunError::out_of_memory:return HtmlTextError::out_of_memory;case RunError::limit_exceeded:return HtmlTextError::limit_exceeded;case RunError::invalid_input:return HtmlTextError::invalid_input;default:return HtmlTextError::unsupported;}}
}
HtmlViewportResult render_real_html_viewport(std::string_view raw,const std::vector<std::string>& owner_ids,
 std::shared_ptr<const RealFontBinding> binding,LineBox line,std::uint32_t width,std::uint32_t height,const NativeImageCatalog& supplied,ViewportOffset offset,Cancellation* cancel) noexcept {
 try {
  Operation o;o.check(cancel);
  if(!binding||!width||!height||width>2048||height>2048)return HtmlTextError::invalid_input;
  if(offset.x>65536||offset.y>65536)return HtmlTextError::limit_exceeded;
  if(owner_ids.size()>8)return HtmlTextError::limit_exceeded;
  if(owner_ids.empty())return HtmlTextError::invalid_input;
  for(const auto& id:owner_ids)if(id.empty()||id.size()>64)return HtmlTextError::invalid_input;
  if(supplied.size()>8)return HtmlTextError::limit_exceeded;
  std::size_t key_bytes=0;std::array<preparation::ImageRef,8> refs;
  for(std::size_t i=0;i<supplied.size();++i) {
   o.check(cancel);const auto& item=supplied[i];const auto& key=item.source;
   if(key.empty()||key.size()>64||!item.resource)return HtmlTextError::invalid_input;
   for(std::size_t j=0;j<key.size();++j){const char c=key[j];const bool letter=(c>='A'&&c<='Z')||(c>='a'&&c<='z');if(!letter&&!(j&&(c=='_'||c=='-'||(c>='0'&&c<='9'))))return HtmlTextError::invalid_input;}
   if(key.size()>512-key_bytes)return HtmlTextError::limit_exceeded;
   key_bytes+=key.size();
   for(std::size_t j=0;j<i;++j)if(supplied[j].source==key)return HtmlTextError::invalid_input;
   if(!item.resource->width()||!item.resource->height())return HtmlTextError::invalid_input;
   if(item.resource->width()>1024||item.resource->height()>1024)return HtmlTextError::limit_exceeded;
  }
  // Reserve named logical copies before ImageSet retention or catalog copying.
  // Pointer-sized set keys are not unordered-set node/capacity measurements.
  std::size_t image_logical=sizeof(HtmlViewportFrame)+sizeof(NativeViewportAuthority)+sizeof(aether::layout::LayoutOperationPin)+sizeof(NativeScrollState)+sizeof(refs)+sizeof(preparation::ImageSet)+
    supplied.size()*(sizeof(NativeImageInput)+sizeof(preparation::ImageRef)+sizeof(preparation::ImageResource*))+key_bytes;
  if(image_logical>2*1024*1024)return HtmlTextError::limit_exceeded;
  for(std::size_t i=0;i<supplied.size();++i){o.check(cancel);refs[i]=supplied[i].resource;}
  auto image_set=preparation::ImageSet::create(std::span<const preparation::ImageRef>(refs.data(),supplied.size()),4*1024*1024,8);o.check(cancel);
  if(auto* error=std::get_if<preparation::ImageError>(&image_set)){if(*error==preparation::ImageError::OutOfMemory)return HtmlTextError::out_of_memory;if(*error==preparation::ImageError::LimitExceeded)return HtmlTextError::limit_exceeded;return HtmlTextError::invalid_input;}
  // Bounds were checked before retaining the catalog copy. The immutable pixels
  // existed in the caller already; only unique retained buffers use the 4MiB cap.
  const NativeImageCatalog catalog=supplied;
  const auto ids=owner_ids; // Caller must not mutate inputs concurrently.
  auto start=Clock::now();auto admitted=admit_real_html_blocks_images(raw,ids,binding->font()->size_26_6,line,cancel,image_logical);o.check(cancel);
  if(auto* e=std::get_if<HtmlAdmissionError>(&admitted)) {
   switch(*e){case HtmlAdmissionError::cancelled:return HtmlTextError::cancelled;case HtmlAdmissionError::out_of_memory:return HtmlTextError::out_of_memory;case HtmlAdmissionError::limit_exceeded:return HtmlTextError::limit_exceeded;case HtmlAdmissionError::unsupported:return HtmlTextError::unsupported;default:return HtmlTextError::invalid_input;}
  }
  const auto& scene=std::get<HtmlBlocksImagesAdmission>(admitted);const auto& proof=scene.blocks;
  if(scene.images.size()>8)return HtmlTextError::limit_exceeded;
  std::array<bool,8> used{};
  for(const auto& image:scene.images){o.check(cancel);auto it=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item){return item.source==image.key;});if(it==catalog.end())return HtmlTextError::invalid_input;auto index=std::size_t(it-catalog.begin());if(used[index])return HtmlTextError::unsupported;used[index]=true;}
  for(std::size_t i=0;i<catalog.size();++i)if(!used[i])return HtmlTextError::invalid_input;
  if(proof.logical_bytes>2*1024*1024)return HtmlTextError::limit_exceeded;
  auto viewport_result=std::make_shared<HtmlViewportFrame>();auto* result=&viewport_result->scene;auto* output=&result->blocks;auto& total=output->work.totals;
  result->images=std::get<std::shared_ptr<const preparation::ImageSet>>(std::move(image_set));
  result->work={catalog.size(),key_bytes,result->images->resources().size(),result->images->bytes(),scene.images.size(),0,0};
  output->work.owners=proof.owners.size();output->work.admission_normalization_steps=proof.steps;output->work.proof_logical_bytes=proof.logical_bytes;
  auto charge=[&](std::size_t count,std::size_t size){constexpr std::size_t cap=2*1024*1024;auto& used=output->work.proof_logical_bytes;if(used>cap||(size&&count>(cap-used)/size))throw HtmlTextError::limit_exceeded;used+=count*size;};
  result->work.logical_image_metadata_bytes=image_logical;
  auto charge_image=[&](std::size_t count,std::size_t size){charge(count,size);result->work.logical_image_metadata_bytes+=count*size;};
  charge(ids.size(),sizeof(std::string));for(const auto& id:ids)charge(id.size(),1);
  charge(1,sizeof(HtmlBlocksFrame));charge(proof.owners.size(),sizeof(HtmlBlockText));
  charge_image(scene.images.size(),sizeof(HtmlImageTarget));result->targets.reserve(scene.images.size());
  for(const auto& image:scene.images){charge_image(image.id.size()+image.key.size(),1);auto it=std::find_if(catalog.begin(),catalog.end(),[&](const auto& item){return item.source==image.key;});result->targets.push_back({image.id,image.key,it->resource,{},{},0});}
  total.raw_bytes=raw.size();require_status(aether_doc_create(&o.doc),HtmlTextError::out_of_memory);
  charge(raw.size(),1);const std::string source(raw);require_status(aether_doc_parse_html(o.doc,source.c_str()),HtmlTextError::invalid_input);
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(o.doc->identity);o.check(cancel);
  auto nodes=verify_real_html_blocks_nodes(proof,o.doc);if(nodes.size()!=proof.nodes.size())return HtmlTextError::preparation_failed;
  std::vector<aether_node_s*> owners;for(const auto& owner:proof.owners)owners.push_back(nodes.at(owner.node_index));
  total.admission_ns=elapsed(start);start=Clock::now();
  aether_style_environment_t env{float(width),float(height),1,16,0,0,0};
  require_status(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&o.runtime),HtmlTextError::out_of_memory);
  int64_t sheet{};require_status(aether_style_runtime_add_stylesheet(o.runtime,"html,body{display:block;margin:0;padding:0;border-width:0;}",0,"real-block-profile-scaffold",&sheet,nullptr),HtmlTextError::invalid_input);
  o.check(cancel);const RealHtmlStyleTransition transition(o.doc);
  require_status(aether_style_runtime_prepare_revision(o.runtime,o.doc,&env,&o.transaction),HtmlTextError::invalid_input);
  require_status(aether_style_runtime_commit_revision(o.runtime,o.transaction,&o.revision),HtmlTextError::invalid_input);
  require_status(aether_style_revision_create_map(o.revision,&o.map),HtmlTextError::invalid_input);
  if(!transition.accepts(o.doc,o.map)||verify_real_html_blocks_nodes(proof,o.doc)!=nodes)return HtmlTextError::preparation_failed;
  o.epochs=std::make_unique<aether::layout::CallbackEpochGuard>(o.doc->identity);o.check(cancel);
  effective(o,nodes,*binding,line,proof,scene.images,cancel);o.check(cancel);total.styles_ns=elapsed(start);start=Clock::now();
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
  auto granted=NativeViewportAuthority::create(o.doc,o.layout,o.map,width,height,offset);
  if(auto* error=std::get_if<HtmlTextError>(&granted))return *error;
  const auto& authority=std::get<NativeViewportAuthority>(granted);
  viewport_result->viewport=authority.diagnostics();o.check(cancel);
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
  auto rect_equal=[](aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;};
  auto integral=[](float v){return std::isfinite(v)&&std::floor(v)==v;};
  charge_image(scene.images.size(),sizeof(aether_rectf));
  std::vector<aether_rectf> target_rects;target_rects.reserve(scene.images.size());
  for(std::size_t i=0;i<scene.images.size();++i){
   o.check(cancel);const auto& authored=scene.images[i];auto it=o.layout->primary_boxes.find(nodes.at(authored.node_index));
   if(it==o.layout->primary_boxes.end()||!it->second)return HtmlTextError::preparation_failed;
   const auto* box=it->second;const auto rect=box->padding_box;auto& target=result->targets[i];
   if(!rect_equal(rect,box->border_box)||!integral(rect.x)||!integral(rect.y)||!integral(rect.width)||!integral(rect.height)||rect.width<=0||rect.height<=0||rect.x<0||rect.y<0||target.resource->width()>rect.width||target.resource->height()>rect.height)return HtmlTextError::unsupported;
   if(double(rect.x)+rect.width>65536||double(rect.y)+rect.height>65536)return HtmlTextError::limit_exceeded;
   target.crop={0,0,float(target.resource->width()),float(target.resource->height())};target.destination={rect.x,rect.y,float(target.resource->width()),float(target.resource->height())};target_rects.push_back(rect);
  }
  // Geometry admission is after glyph raster/layout measurement, before paint
  // and software image sampling. It is not a pre-raster or global CPU bound.
  total.known_layout_box_bytes=output->boxes.size()*sizeof(aether_layout_box_s);start=Clock::now();
  require_status(aether_display_list_builder_create(nullptr,nullptr,&o.builder),HtmlTextError::out_of_memory);std::unique_ptr<NativePaintList> list;
  // Builder-owned catalog/ref copies, image sidecars and command source strings.
  // Snapshot/preparation/software metadata retain their existing independent budgets.
  charge_image(catalog.size(),sizeof(NativeImageInput)+sizeof(preparation::ImageRef));
  charge_image(key_bytes,1);
  charge_image(scene.images.size(),sizeof(NativeImageEntry));
  charge_image(key_bytes,1);
  charge_image(key_bytes+scene.images.size(),1);
  require_status(build_native_viewport_capture(o.builder,o.layout,o.map,authority,catalog,list),HtmlTextError::preparation_failed);
  total.native_glyph_commands=list->text_runs.size();total.image_commands=list->images.size();if(total.image_commands!=result->targets.size())return HtmlTextError::preparation_failed;
  for(const auto& entry:list->text_runs) {
   o.check(cancel);auto found=std::find(transports.begin(),transports.end(),entry.placement.run);if(found==transports.end()||!entry.placement.run)return HtmlTextError::preparation_failed;
   auto& block=output->owners[std::size_t(found-transports.begin())];if(block.has_command)return HtmlTextError::preparation_failed;block.has_command=true;block.command_index=entry.command_index;block.work.native_glyph_commands=1;
  }
  for(const auto& block:output->owners)if(block.has_command!=!block.run->source_text.empty())return HtmlTextError::preparation_failed;
  const auto* raw_list=NativeListAccess::list(*list);if(!raw_list)return HtmlTextError::preparation_failed;
  std::size_t scroll_commands=0,chrome_commands=0;
  for(const auto& command:raw_list->commands) {
   o.check(cancel);
   if(command.tag==AETHER_PAINT_PUSH_SCROLL_OFFSET) {
    if(!authority.scrolling()||command.data.push_scroll_offset.scroll_container_id!=authority.root_id())return HtmlTextError::preparation_failed;
    ++scroll_commands;
   }
   if(command.tag==AETHER_PAINT_DRAW_SCROLLBAR_CHROME) {
    if(!authority.scrolling()||command.data.draw_scrollbar_chrome.scroll_container_id!=authority.root_id())return HtmlTextError::preparation_failed;
    ++chrome_commands;
   }
  }
  const std::size_t expected_scroll=authority.scrolling()?1:0;
  if(list->scroll.size()!=expected_scroll||scroll_commands!=expected_scroll||chrome_commands!=expected_scroll)return HtmlTextError::preparation_failed;
  if(!authority.accepts(o.layout,o.map))return HtmlTextError::preparation_failed;
  std::array<bool,8> image_seen{};std::size_t command_images=0;
  for(const auto& command:raw_list->commands){o.check(cancel);if(command.tag==AETHER_PAINT_DRAW_BACKGROUND_IMAGE)++command_images;}
  if(command_images!=result->targets.size())return HtmlTextError::preparation_failed;
  for(const auto& entry:list->images){
   o.check(cancel);auto found=std::find_if(result->targets.begin(),result->targets.end(),[&](const auto& target){return target.key==entry.source;});
   if(found==result->targets.end()||entry.command_index>=raw_list->commands.size())return HtmlTextError::preparation_failed;
   auto i=std::size_t(found-result->targets.begin());if(image_seen[i])return HtmlTextError::preparation_failed;image_seen[i]=true;
   const auto& command=raw_list->commands[entry.command_index];if(command.tag!=AETHER_PAINT_DRAW_BACKGROUND_IMAGE)return HtmlTextError::preparation_failed;
   const auto& payload=command.data.draw_background_image;
   if(!payload.source||entry.source_identity!=payload.source||std::string_view(payload.source)!=found->key||entry.resource!=found->resource||entry.opacity!=1||payload.opacity!=1||payload.has_resolved_geometry!=AETHER_TRUE||!rect_equal(entry.crop,found->crop)||!rect_equal(entry.destination,found->destination)||!rect_equal(payload.source_rect,found->crop)||!rect_equal(payload.destination_rect,found->destination)||!rect_equal(payload.paint_rect,target_rects[i])||!intrinsic_size(payload.size_kind,payload.size_x,payload.size_y)||payload.repeat_x!=AETHER_BACKGROUND_NO_REPEAT||payload.repeat_y!=AETHER_BACKGROUND_NO_REPEAT||payload.position_x.unit!=AETHER_CSS_UNIT_PX||payload.position_y.unit!=AETHER_CSS_UNIT_PX||!zero(payload.position_x)||!zero(payload.position_y))return HtmlTextError::preparation_failed;
   found->command_index=entry.command_index;
  }
  for(std::size_t i=0;i<result->targets.size();++i)if(!image_seen[i])return HtmlTextError::preparation_failed;
  result->work.image_commands=total.image_commands;
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
  output->frame=std::move(*frame);total.render_ns=elapsed(start);o.check(cancel);return std::shared_ptr<const HtmlViewportFrame>(std::move(viewport_result));
 }catch(HtmlTextError e){return e;}catch(const std::bad_alloc&){return HtmlTextError::out_of_memory;}catch(...){return HtmlTextError::invalid_input;}
}
}
