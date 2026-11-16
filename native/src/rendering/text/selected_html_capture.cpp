#include "selected_html_capture.h"
#include "real_html_style_transition.h"
#include "real_html_mask_registry.h"
#include "selected_text_product.h"
#include "../../layout/selected_layout_capture.h"
#include "../../layout/real_simple_text_policy.h"
#include "../../style_runtime/ua_stylesheet.h"
#include "../../style_runtime/css_scope_proof.h"
#include "../../style_runtime/h5_parser_profile.h"
#include "../../style_runtime/style_runtime_types.h"
#include "../native_paint_list.h"
#include "../display_list_types.h"
#include "../preparation/native_command_source.h"
#include <chrono>
namespace aether::rendering {
namespace tt=text::truetype;
namespace {
using Clock=std::chrono::steady_clock;
auto ns(Clock::time_point t){return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count());}
[[noreturn]] void fail(tt::HtmlTextError e,const char* reason){throw SelectedCaptureFailure{e,reason};}
void status(aether_status_t s,const char* reason){if(s!=AETHER_OK)fail(s==AETHER_ERROR_OUT_OF_MEMORY?tt::HtmlTextError::out_of_memory:tt::HtmlTextError::invalid_input,reason);}
std::string attr(aether_node_s* n,const char* key){auto it=n->attributes.find(key);return it==n->attributes.end()?std::string{}:it->second;}
bool independent_length(aether_css_length v){return (v.unit==AETHER_CSS_UNIT_PX||v.unit==AETHER_CSS_UNIT_AUTO)&&std::isfinite(v.value)&&v.linear_percent==0&&v.linear_em==0&&v.linear_rem==0&&v.linear_vw==0&&v.linear_vh==0;}
bool independent_edges(aether_css_edges e){return independent_length(e.top)&&independent_length(e.right)&&independent_length(e.bottom)&&independent_length(e.left);}
bool zero_edges(aether_css_edges e){return independent_edges(e)&&e.top.unit==AETHER_CSS_UNIT_PX&&e.right.unit==AETHER_CSS_UNIT_PX&&e.bottom.unit==AETHER_CSS_UNIT_PX&&e.left.unit==AETHER_CSS_UNIT_PX&&e.top.value==0&&e.right.value==0&&e.bottom.value==0&&e.left.value==0;}
bool blank(const std::string& s){return s.find_first_not_of(" \t\n\r\f")==std::string::npos;}
}
class SelectedCaptureOperation final {
 const std::thread::id thread=std::this_thread::get_id();
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_style_transaction_t transaction{};aether_style_revision_t revision{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::unique_ptr<aether::layout::CallbackEpochGuard> epochs;
 tt::Cancellation* cancel{};
 void check(){if(std::this_thread::get_id()!=thread)fail(tt::HtmlTextError::invalid_input,"THREAD_MISMATCH");auto valid=[&]{if(epochs&&!epochs->valid())fail(tt::HtmlTextError::invalid_input,"STALE_DOCUMENT");if(map){auto v=aether::style::pin_style_map(map);if(!v||!aether::style::inputs_current(v.get()))fail(tt::HtmlTextError::invalid_input,"STALE_STYLE");}};valid();bool stop=cancel&&cancel->cancelled();valid();if(stop)fail(tt::HtmlTextError::cancelled,"CANCELLED");}
 struct Adapter final:preparation::Cancellation {
  SelectedCaptureOperation& o;std::optional<SelectedCaptureFailure> error;
  explicit Adapter(SelectedCaptureOperation& op):o(op){}
  bool cancelled()noexcept override {if(error)return true;try{o.check();return false;}catch(const SelectedCaptureFailure& e){error=e;return true;}catch(...){return true;}}
  void rethrow(){if(error)throw *error;}
 };
 static void prep(const preparation::Error& e){fail(e.code==preparation::ErrorCode::OUT_OF_MEMORY?tt::HtmlTextError::out_of_memory:e.code==preparation::ErrorCode::CANCELLED?tt::HtmlTextError::cancelled:e.code==preparation::ErrorCode::LIMIT_EXCEEDED?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::preparation_failed,"PREPARATION_FAILED");}
public:
 ~SelectedCaptureOperation(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 std::shared_ptr<const SelectedCaptureFrame> run(std::string_view raw,std::string_view id,std::string_view selector,std::shared_ptr<const text::FontSource> font,std::uint32_t vw,std::uint32_t vh,std::uint32_t w,std::uint32_t h,tt::Cancellation* c){
  cancel=c;check();
  if(!font||id.empty()||id.size()>64||selector.size()>256||!vw||!vh||vw>2048||vh>2048||!w||!h||w>2048||h>2048)fail(tt::HtmlTextError::invalid_input,"INVALID_INPUT");
  if(raw.size()>512*1024)fail(tt::HtmlTextError::limit_exceeded,"INPUT_LIMIT");
  if(raw.find('\0')!=raw.npos)fail(tt::HtmlTextError::invalid_input,"EMBEDDED_NUL");
  auto out=std::make_shared<SelectedCaptureFrame>();out->source_hash=text::detail::sha256(raw);out->font_hash=font->digest;
  auto start=Clock::now();status(aether_doc_create(&doc),"DOCUMENT_CREATE");std::string source(raw);status(aether_doc_parse_html(doc,source.c_str()),"PARSE_FAILED");epochs=std::make_unique<aether::layout::CallbackEpochGuard>(doc->identity);check();
  // Parser allocations precede this bounded inventory. Process limits, not this
  // post-parse traversal, bound that synchronous external phase.
  std::vector<aether_node_s*> all;std::vector<std::pair<aether_node_s*,unsigned>> todo{{doc,0}};std::vector<std::string> sheets;std::size_t cssbytes=0;aether_node_s* target=nullptr;
  while(!todo.empty()){check();auto [n,d]=todo.back();todo.pop_back();if(!n||d>64||all.size()>=16384)fail(tt::HtmlTextError::limit_exceeded,"DOM_LIMIT");all.push_back(n);if(n->type==AETHER_NODE_ELEMENT){++out->work.elements;if(attr(n,"id")==id){if(target)fail(tt::HtmlTextError::invalid_input,"TARGET_AMBIGUOUS");target=n;}if(n->tag_name=="style"){std::string sheet;for(auto* child:n->children){check();if(child->type!=AETHER_NODE_TEXT)fail(tt::HtmlTextError::unsupported,"STYLE_SOURCE_UNSUPPORTED");if(child->text.size()>128*1024-cssbytes)fail(tt::HtmlTextError::limit_exceeded,"CSS_LIMIT");cssbytes+=child->text.size();sheet+=child->text;}sheets.push_back(std::move(sheet));}}
   if(n->children.size()>16384-all.size()-todo.size())fail(tt::HtmlTextError::limit_exceeded,"DOM_LIMIT");
   for(auto it=n->children.rbegin();it!=n->children.rend();++it)todo.emplace_back(*it,d+1);
  }
  out->work.nodes=all.size();if(!target)fail(tt::HtmlTextError::invalid_input,"TARGET_MISSING");
  if(!selector.empty()){
   // Native strict selector parser; bounded generic descendant query, no source
   // hash or fixture identity participates in admission.
   for(unsigned char ch:selector)if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-'||ch=='.'||ch=='#'||ch=='>'||ch==' '))fail(tt::HtmlTextError::unsupported,"SELECTOR_UNSUPPORTED");
   std::vector<compiled_selector_s> compiled;if(!selector_parse_list_strict_profile(selector,compiled)||compiled.size()!=1||compiled[0].steps.size()>8)fail(tt::HtmlTextError::unsupported,"SELECTOR_UNSUPPORTED");
   for(const auto& step:compiled[0].steps)if(step.relation_to_next!=COMB_CHILD&&step.relation_to_next!=COMB_NONE)fail(tt::HtmlTextError::unsupported,"SELECTOR_RELATION_UNSUPPORTED");
   aether_node_s* selected=nullptr;std::vector<aether_node_s*> pending(target->children.begin(),target->children.end());std::size_t visits=0;
   while(!pending.empty()){check();auto* n=pending.back();pending.pop_back();if(++visits>16384)fail(tt::HtmlTextError::limit_exceeded,"SELECTOR_LIMIT");if(n->type==AETHER_NODE_ELEMENT&&selector_match(compiled[0],n)){if(selected)fail(tt::HtmlTextError::invalid_input,"SELECTOR_AMBIGUOUS");selected=n;}for(auto* child:n->children)pending.push_back(child);}
   if(!selected)fail(tt::HtmlTextError::invalid_input,"SELECTOR_MISSING");
   target=selected;
  }
  out->work.parse_ns=ns(start);start=Clock::now();
  aether_style_environment_t env{float(vw),float(vh),1,16,0,0,0};status(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"STYLE_CREATE");std::int64_t sheetid{};
  status(aether_style_runtime_add_stylesheet(runtime,aether::style::web_capture_defaults_css(),0,"web-capture-ua",&sheetid,nullptr),"UA_FAILED");
  aether::style::css_scope::Proof evidence(doc,target);
  for(const auto& sheet:sheets){check();auto proof=evidence.inspect(sheet);for(const auto& rule:proof.rules)if(rule.scope==aether::style::css_scope::Scope::unknown)++out->work.unknown_rules;if(!proof.boundary_complete)++out->work.unknown_rules;status(aether_style_runtime_add_stylesheet(runtime,sheet.c_str(),2,"original-author",&sheetid,nullptr),"AUTHOR_STYLE_FAILED");}
  // Coverage stays INCOMPLETE: compiler losses are not attributed to the chosen
  // subtree merely because lexical source evidence was boundary-complete.
  check();const tt::RealHtmlStyleTransition transition(doc);
  status(aether_style_runtime_prepare_revision(runtime,doc,&env,&transaction),"STYLE_PREPARE");status(aether_style_runtime_commit_revision(runtime,transaction,&revision),"STYLE_COMMIT");status(aether_style_revision_create_map(revision,&map),"STYLE_MAP");if(!transition.accepts(doc,map))fail(tt::HtmlTextError::invalid_input,"STYLE_TRANSITION");epochs=std::make_unique<aether::layout::CallbackEpochGuard>(doc->identity);check();
  auto version=aether::style::pin_style_map(map);
  for(auto* n=target;n&&n->type!=AETHER_NODE_DOCUMENT;n=n->parent){check();auto rec=version->backing()->find_owner(n);if(!rec||!rec->measures())fail(tt::HtmlTextError::unsupported,"ANCESTOR_STYLE");aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());if(s.display==AETHER_DISPLAY_NONE||s.display==AETHER_DISPLAY_CONTENTS||s.position!=AETHER_POSITION_STATIC||!s.view().transforms().empty())fail(tt::HtmlTextError::unsupported,"ANCESTOR_DEPENDENCY");}
  aether::layout::SelectedLayoutAuthority authority(doc,map,target,float(vw),float(vh));out->work.selected_nodes=authority.scope().size();
  auto targetrec=version->backing()->find_owner(target);aether::layout::BoxStyleSnapshot targetstyle(targetrec->style(),targetrec->measures());if(!zero_edges(targetstyle.margin))fail(tt::HtmlTextError::unsupported,"TARGET_MARGIN_UNSUPPORTED");if(targetstyle.width.unit!=AETHER_CSS_UNIT_PX||!std::isfinite(targetstyle.width.value)||targetstyle.width.value<0)fail(tt::HtmlTextError::unsupported,"TARGET_WIDTH_UNSUPPORTED");
  std::vector<aether_node_s*> owners;std::vector<std::shared_ptr<const text::SelectedTextProduct>> products;std::vector<aether::layout::BoxStyleSnapshot> ownerstyles;std::map<std::int32_t,std::shared_ptr<const tt::RealFontBinding>> bindings;tt::RunBudget budget;
  out->work.styles_ns=ns(start);start=Clock::now();
  std::vector<aether_node_s*> scoped{target};
  while(!scoped.empty()){
   check();auto* n=scoped.back();scoped.pop_back();if(n->type!=AETHER_NODE_ELEMENT)continue;auto rec=version->backing()->find_owner(n);if(!rec||!rec->measures())fail(tt::HtmlTextError::unsupported,"SELECTED_STYLE");aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());auto view=s.view();
   aether::style::MeasuresReadView geometry(*rec->measures());
   for(auto value:{s.width,s.height,s.min_width,s.min_height,s.max_width,s.max_height,geometry.row_gap(),geometry.column_gap()})if(!independent_length(value))fail(tt::HtmlTextError::unsupported,"GEOMETRY_DEPENDENCY_UNSUPPORTED");
   if(!independent_edges(s.margin)||!independent_edges(s.padding)||!independent_edges(s.border_width)||(s.flex_basis.kind==AETHER_FLEX_BASIS_LENGTH&&!independent_length(s.flex_basis.length)))fail(tt::HtmlTextError::unsupported,"GEOMETRY_DEPENDENCY_UNSUPPORTED");
   if((s.display!=AETHER_DISPLAY_BLOCK&&s.display!=AETHER_DISPLAY_FLEX)||s.position!=AETHER_POSITION_STATIC||s.opacity!=1||s.overflow_x!=AETHER_OVERFLOW_VISIBLE||s.overflow_y!=AETHER_OVERFLOW_VISIBLE||!view.transforms().empty()||!view.backdrop_filters().empty()||!view.box_shadows().empty()||!view.text_shadows().empty()||!view.background_layers().empty()||s.has_text_gradient)fail(tt::HtmlTextError::unsupported,"SELECTED_EFFECT_UNSUPPORTED");
   bool elements=false,nonblank=false;std::vector<std::string> pieces;for(auto* child:n->children){check();if(child->type==AETHER_NODE_ELEMENT)elements=true;else if(child->type==AETHER_NODE_TEXT){pieces.push_back(child->text);nonblank|=!blank(child->text);}else if(child->type==AETHER_NODE_COMMENT)pieces.emplace_back();else fail(tt::HtmlTextError::unsupported,"SELECTED_NODE_UNSUPPORTED");}
   if(elements&&nonblank)fail(tt::HtmlTextError::unsupported,"MIXED_TEXT_UNSUPPORTED");
   if(!elements&&nonblank){if(owners.size()>=8)fail(tt::HtmlTextError::limit_exceeded,"TEXT_OWNER_LIMIT");std::string textvalue;for(const auto& piece:pieces)textvalue+=piece;if(textvalue.size()!=1||!((textvalue[0]>='A'&&textvalue[0]<='Z')||(textvalue[0]>='a'&&textvalue[0]<='z')))fail(tt::HtmlTextError::unsupported,"TEXT_LAYOUT_UNSUPPORTED");
    aether::style::MeasuresReadView m(*rec->measures());const float line=aether::style::measure_used_line_height(&m,s);if(s.white_space!=AETHER_WHITE_SPACE_NORMAL||s.font_weight!=400||s.text_transform!=AETHER_TEXT_TRANSFORM_NONE||(s.text_align!=AETHER_TEXT_ALIGN_LEFT&&s.text_align!=AETHER_TEXT_ALIGN_START)||m.direction_rtl()||m.writing_mode()!=0||!std::isfinite(s.font_size)||s.font_size<=0||s.font_size>128||std::floor(s.font_size*64)!=s.font_size*64)fail(tt::HtmlTextError::unsupported,"TEXT_STYLE_UNSUPPORTED");
    const auto size=std::int32_t(s.font_size*64);auto& binding=bindings[size];if(!binding){auto b=tt::RealFontBinding::create(font,size,tt::q1_unhinted,cancel);if(auto* error=std::get_if<tt::BindingError>(&b))fail(*error==tt::BindingError::out_of_memory?tt::HtmlTextError::out_of_memory:*error==tt::BindingError::cancelled?tt::HtmlTextError::cancelled:tt::HtmlTextError::unsupported,"FONT_BINDING_FAILED");binding=std::get<std::shared_ptr<const tt::RealFontBinding>>(b);}
    auto vertical=tt::read_vertical_metrics(binding);if(!std::holds_alternative<tt::FontVerticalMetrics>(vertical))fail(tt::HtmlTextError::unsupported,"FONT_METRICS_FAILED");
    if(budget.cmap.remaining<17||budget.outline.remaining<1000000)fail(tt::HtmlTextError::limit_exceeded,"FONT_WORK_LIMIT");
    budget.cmap.remaining-=17;budget.outline.remaining-=1000000;
    auto mapped=binding->map()->lookup(static_cast<unsigned char>(textvalue[0]));if(!std::holds_alternative<tt::Mapped>(mapped))fail(tt::HtmlTextError::unsupported,"MISSING_GLYPH");auto outline=binding->face()->outline(std::get<tt::Mapped>(mapped).glyph_id,cancel);check();if(auto* e=std::get_if<tt::Error>(&outline))fail(*e==tt::Error::out_of_memory?tt::HtmlTextError::out_of_memory:*e==tt::Error::cancelled?tt::HtmlTextError::cancelled:*e==tt::Error::limit_exceeded?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::unsupported,"OUTLINE_FAILED");
    std::shared_ptr<const text::SelectedTextProduct> product(new text::SelectedTextProduct(binding,std::get<std::shared_ptr<const tt::Outline>>(outline),std::move(pieces),std::get<tt::FontVerticalMetrics>(vertical),line));owners.push_back(n);products.push_back(std::move(product));ownerstyles.push_back(s);
   }else if(!elements&&!pieces.empty()){for(const auto& piece:pieces)if(!piece.empty())fail(tt::HtmlTextError::unsupported,"WHITESPACE_LEAF_UNSUPPORTED");}
   for(auto it=n->children.rbegin();it!=n->children.rend();++it)if((*it)->type==AETHER_NODE_ELEMENT)scoped.push_back(*it);
  }
  if(owners.empty())fail(tt::HtmlTextError::unsupported,"NO_TEXT_OWNERS");
  out->work.text_runs=owners.size();out->work.measure_ns=ns(start);start=Clock::now();
  status(aether_layout_create(nullptr,nullptr,&engine),"LAYOUT_CREATE");if(!aether::layout::RealSimpleTextAccess::install_selected(*engine,doc,map,owners,products))fail(tt::HtmlTextError::invalid_input,"TEXT_INSTALL");status(aether::layout::layout_selected_capture(engine,doc,map,authority,&layout),"SELECTED_LAYOUT_FAILED");check();out->work.layout_ns=ns(start);start=Clock::now();
  std::vector<aether_layout_box_s*> textboxes(owners.size());std::vector<aether_layout_box_s*> boxes{layout->root};std::size_t ordinal=0;
  while(!boxes.empty()){check();auto* b=boxes.back();boxes.pop_back();if(!b||ordinal>=128)fail(tt::HtmlTextError::limit_exceeded,"LAYOUT_BOX_LIMIT");out->boxes.push_back({ordinal++,b->element?b->element->tag_name:"",b->element?attr(b->element,"id"):"",b->border_box,b->content_box});if(b->kind==LAYOUT_BOX_TEXT){auto it=std::find(owners.begin(),owners.end(),b->element);if(it==owners.end())fail(tt::HtmlTextError::preparation_failed,"UNBOUND_TEXT");auto i=std::size_t(it-owners.begin());if(textboxes[i]||b->native_text.run!=products[i]->run)fail(tt::HtmlTextError::preparation_failed,"TEXT_IDENTITY");textboxes[i]=b;}for(auto it=b->children.rbegin();it!=b->children.rend();++it)boxes.push_back(*it);}
  status(aether_display_list_builder_create(nullptr,nullptr,&builder),"PAINT_CREATE");std::unique_ptr<NativePaintList> list;status(build_native_opacity(builder,layout,map,nullptr,NativeScrollInput{},list),"PAINT_FAILED");if(!list||!list->images.empty()||list->text_runs.size()!=owners.size())fail(tt::HtmlTextError::preparation_failed,"PAINT_COVERAGE");
  auto& commands=NativeListAccess::list(*list)->commands;tt::RealHtmlMaskRegistry registry;std::vector<bool> seen(owners.size());
  for(auto& entry:list->text_runs){check();std::size_t i=0;while(i<products.size()&&products[i]->run!=entry.placement.run)++i;if(i==products.size()||seen[i]||!textboxes[i]||entry.command_index>=commands.size())fail(tt::HtmlTextError::preparation_failed,"TEXT_SIDECAR_IDENTITY");seen[i]=true;auto& command=commands[entry.command_index];auto& p=*products[i];auto* b=textboxes[i];auto& s=ownerstyles[i];
   if(command.tag!=AETHER_PAINT_DRAW_TEXT||!command.data.draw_text.text||std::string(command.data.draw_text.text)!=p.run->source||entry.placement.run->font!=p.binding->font()||command.data.draw_text.rect.x!=b->content_box.x||entry.placement.baseline!=b->content_box.y+p.run->metrics.baseline||command.data.draw_text.font_size!=s.font_size)fail(tt::HtmlTextError::preparation_failed,"TEXT_COMMAND_MISMATCH");
   auto x=text::selected_raster_coordinate(b->content_box.x),y=text::selected_raster_coordinate(entry.placement.baseline);const float logicalbaseline=entry.placement.baseline;
   // Only private paint placement is quantized. Logical box, width/height, run
   // metrics, clips and nontext paint commands are never rewritten.
   command.data.draw_text.rect.x=float(x.value26_6)/64.f;entry.placement.baseline=float(y.value26_6)/64.f;
   // Preserve the unchanged validator's relation to the immutable logical run.
   // This is only a private text paint rectangle; layout boxes/metrics stay exact.
   command.data.draw_text.rect.y=entry.placement.baseline-p.run->metrics.baseline;
   if(!std::isfinite(command.data.draw_text.rect.y)||std::abs(command.data.draw_text.rect.y)>65536||command.data.draw_text.rect.y+p.run->metrics.baseline!=entry.placement.baseline)fail(tt::HtmlTextError::preparation_failed,"TEXT_PAINT_RECOMPOSITION");
   auto raster=tt::rasterize_budgeted(p.outline,p.binding->font(),x.phase,y.phase,budget.raster,budget.masks,cancel);check();if(auto* e=std::get_if<tt::RasterError>(&raster))fail(*e==tt::RasterError::out_of_memory?tt::HtmlTextError::out_of_memory:*e==tt::RasterError::cancelled?tt::HtmlTextError::cancelled:*e==tt::RasterError::budget_exceeded?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::render_failed,"GLYPH_RASTER_FAILED");auto mask=std::get<tt::RasterOutput>(raster);registry.add(mask.mask,[&]{check();});out->work.mask_bytes+=mask.mask->bytes.size();
   out->text.push_back({i,p.run->source,std::string(s.font_family.data(),strnlen(s.font_family.data(),s.font_family.size())),p.vertical.policy==tt::VerticalMetricPolicy::hhea?"hhea":"os2_typo",s.color,b->content_box,logicalbaseline,entry.placement.baseline,p.run->metrics.width,x.value26_6,y.value26_6,x.phase,y.phase});
  }
  out->work.glyph_commands=list->text_runs.size();Adapter adapter(*this);auto snapshot=preparation::snapshot_native_opacity(*list,&adapter);adapter.rethrow();if(auto* e=std::get_if<preparation::Error>(&snapshot))prep(*e);auto prepared=preparation::prepare(std::get<preparation::Snapshot>(snapshot),&adapter);adapter.rethrow();if(auto* e=std::get_if<preparation::Error>(&prepared))prep(*e);out->work.paint_ns=ns(start);start=Clock::now();
  FrameStore store;auto resized=store.resize({int(w),int(h),1});if(resized!=FrameStatus::ok)fail(resized==FrameStatus::allocation_failed?tt::HtmlTextError::out_of_memory:tt::HtmlTextError::render_failed,"FRAME_FAILED");auto rendered=software::render_a8(store,std::get<preparation::PreparedFrame>(prepared),registry.masks_,&adapter);adapter.rethrow();if(rendered.status!=software::Status::ok)fail(rendered.status==software::Status::allocation_failed?tt::HtmlTextError::out_of_memory:rendered.status==software::Status::cancelled?tt::HtmlTextError::cancelled:rendered.status==software::Status::limit_exceeded?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::render_failed,"SOFTWARE_FAILED");std::optional<Frame> frame;if(store.copy_confirmed(frame)!=FrameStatus::ok||!frame)fail(tt::HtmlTextError::out_of_memory,"FRAME_COPY_FAILED");out->frame=std::move(*frame);out->work.raster_ns=ns(start);out->work.outline_reserved=256000000-budget.outline.remaining;out->work.cmap_reserved=4352-budget.cmap.remaining;out->work.raster_reserved=512000000-budget.raster.remaining;out->work.known_bytes=out->work.mask_bytes+font->bytes.size()+source.size()+cssbytes;check();return out;
 }
};
SelectedCaptureResult capture_selected_html(std::string_view raw,std::string_view id,std::string_view selector,std::shared_ptr<const text::FontSource> font,std::uint32_t vw,std::uint32_t vh,std::uint32_t w,std::uint32_t h,tt::Cancellation* cancel)noexcept{
 try{SelectedCaptureOperation op;return op.run(raw,id,selector,std::move(font),vw,vh,w,h,cancel);}catch(const SelectedCaptureFailure& e){return e;}catch(const std::bad_alloc&){return SelectedCaptureFailure{tt::HtmlTextError::out_of_memory,"OUT_OF_MEMORY"};}catch(const std::length_error&){return SelectedCaptureFailure{tt::HtmlTextError::limit_exceeded,"LIMIT_EXCEEDED"};}catch(const std::invalid_argument&){return SelectedCaptureFailure{tt::HtmlTextError::unsupported,"TEXT_OR_SCOPE_UNSUPPORTED"};}catch(...){return SelectedCaptureFailure{tt::HtmlTextError::invalid_input,"INTERNAL_FAILURE"};}
}
}
