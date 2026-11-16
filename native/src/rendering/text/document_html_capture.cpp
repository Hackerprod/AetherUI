#include "../../style_runtime/document_embedded_style.h"
#include "document_html_capture.h"
#include "real_html_style_transition.h"
#include "../../layout/document_root_layout.h"
#include "../document_shadow_plan.h"
#include "../document_scope_plan.h"
#include "../../style_runtime/ua_stylesheet.h"
#include "../../style_runtime/style_runtime_types.h"
#include "../preparation/native_command_source.h"
#include <chrono>
namespace aether::rendering {
namespace tt=text::truetype;
namespace {
using Clock=std::chrono::steady_clock;
std::uint64_t ns(Clock::time_point t){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-t).count();}
[[noreturn]] void fail(tt::HtmlTextError e,const char* reason){throw SelectedCaptureFailure{e,reason};}
void status(aether_status_t s,const char* reason){if(s!=AETHER_OK)fail(s==AETHER_ERROR_OUT_OF_MEMORY?tt::HtmlTextError::out_of_memory:tt::HtmlTextError::preparation_failed,reason);}
}
class DocumentCaptureOperation final {
 const std::thread::id thread=std::this_thread::get_id();
 const DocumentCaptureThread capture_thread;
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_style_transaction_t transaction{};aether_style_revision_t revision{};
 aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 std::unique_ptr<aether::layout::CallbackEpochGuard> epochs;tt::Cancellation* cancel{};

 void check(){if(std::this_thread::get_id()!=thread)fail(tt::HtmlTextError::invalid_input,"THREAD_MISMATCH");auto valid=[&]{if(epochs&&!epochs->valid())fail(tt::HtmlTextError::invalid_input,"STALE_DOCUMENT");if(map){auto v=aether::style::pin_style_map(map);if(!v||!aether::style::inputs_current(v.get()))fail(tt::HtmlTextError::invalid_input,"STALE_STYLE");}};valid();bool stop=cancel&&cancel->cancelled();valid();if(stop)throw aether::layout::DocumentTextCancelled{};}
 struct Adapter final:tt::Cancellation,preparation::Cancellation {
  DocumentCaptureOperation& o;std::exception_ptr error;
  explicit Adapter(DocumentCaptureOperation& op):o(op){}
  bool cancelled()noexcept override {if(error)return true;try{o.check();return false;}catch(...){error=std::current_exception();return true;}}
  void rethrow(){if(error)std::rethrow_exception(error);}
 };
 static void prep(const preparation::Error& e){fail(e.code==preparation::ErrorCode::OUT_OF_MEMORY?tt::HtmlTextError::out_of_memory:e.code==preparation::ErrorCode::CANCELLED?tt::HtmlTextError::cancelled:e.code==preparation::ErrorCode::LIMIT_EXCEEDED?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::preparation_failed,"PREPARATION_FAILED");}
public:
 ~DocumentCaptureOperation(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_transaction_destroy(transaction);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 std::shared_ptr<const DocumentCaptureFrame> run(std::string_view raw,text::DocumentFontCatalog fonts,std::uint32_t w,std::uint32_t h,tt::ViewportOffset offset,tt::Cancellation* c,const DocumentScrollRequests* requests=nullptr,aether::layout::DocumentTextBudget* caller_budget=nullptr,bool drawing=false){
  aether::layout::DocumentTextBudget initial=caller_budget?*caller_budget:aether::layout::DocumentTextBudget{};
  auto* current_budget=&initial;
  struct Mirror {aether::layout::DocumentTextBudget* caller; aether::layout::DocumentTextBudget*& current; ~Mirror()noexcept{if(caller)*caller=*current;}} mirror{caller_budget,current_budget};
  std::optional<DocumentScrollRequests> owned_requests;
  if(requests){
   initial.bytes(requests->entries().size(),sizeof(DocumentScrollRequestInput));initial.normalized(requests->entries().size());
   std::vector<DocumentScrollRequestInput> input;input.reserve(requests->entries().size());
   for(const auto& r:requests->entries())input.push_back({r.id,r.x,r.y});
   owned_requests.emplace(DocumentScrollRequests::prepare(input,initial));requests=&*owned_requests;
  }
  cancel=c;check();if(!fonts.regular||!w||!h||w>2048||h>2048)fail(tt::HtmlTextError::invalid_input,"INVALID_INPUT");
  if(raw.size()>512*1024)fail(tt::HtmlTextError::limit_exceeded,"INPUT_LIMIT");if(raw.find('\0')!=raw.npos)fail(tt::HtmlTextError::invalid_input,"EMBEDDED_NUL");
  initial.bytes(1,sizeof(DocumentCaptureFrame)+sizeof(Adapter));initial.bytes(raw.size());initial.bytes(192);
  auto out=std::make_shared<DocumentCaptureFrame>();out->source_hash=text::detail::sha256(raw);out->regular_hash=fonts.regular->digest;if(fonts.bold)out->bold_hash=fonts.bold->digest;
  auto start=Clock::now();status(aether_doc_create(&doc),"DOCUMENT_CREATE");std::string source(raw);status(aether_doc_parse_html(doc,source.c_str()),"PARSE_FAILED");epochs=std::make_unique<aether::layout::CallbackEpochGuard>(doc->identity);check();
  // This inventory is post-parse. The synchronous parser is bounded by process
  // limits, not by an invented pre-allocation DOM guarantee.
  initial.bytes(1,sizeof(std::pair<aether_node_s*,unsigned>));std::vector<std::pair<aether_node_s*,unsigned>> todo{{doc,0}};std::vector<std::string> sheets;std::vector<aether_node_s*> embedded_nodes;std::size_t cssbytes=0;aether_node_s* body=nullptr;aether_node_s* html=nullptr;
  if(drawing){initial.bytes(16384,sizeof(aether_node_s*));embedded_nodes.reserve(16384);}
  while(!todo.empty()){check();auto [n,d]=todo.back();todo.pop_back();if(!n||d>64||out->work.nodes>=16384)fail(tt::HtmlTextError::limit_exceeded,"DOM_LIMIT");++out->work.nodes;
   if(n->type==AETHER_NODE_ELEMENT){++out->work.elements;auto inline_style=n->attributes.find("style");if(inline_style!=n->attributes.end()){if(inline_style->second.size()>128*1024-cssbytes)fail(tt::HtmlTextError::limit_exceeded,"CSS_LIMIT");cssbytes+=inline_style->second.size();}if(n->tag_name=="html"){if(html)fail(tt::HtmlTextError::unsupported,"MULTIPLE_HTML");html=n;}if(n->tag_name=="body"){if(body)fail(tt::HtmlTextError::unsupported,"MULTIPLE_BODY");body=n;}if(n->tag_name=="style"){initial.bytes(1,sizeof(std::string));std::string sheet;for(auto* child:n->children){check();if(child->type!=AETHER_NODE_TEXT)fail(tt::HtmlTextError::unsupported,"STYLE_SOURCE_UNSUPPORTED");if(child->text.size()>128*1024-cssbytes)fail(tt::HtmlTextError::limit_exceeded,"CSS_LIMIT");cssbytes+=child->text.size();initial.bytes(child->text.size());sheet+=child->text;}sheets.push_back(std::move(sheet));if(drawing)embedded_nodes.push_back(n);}}
   if(n->children.size()>16384-out->work.nodes-todo.size())fail(tt::HtmlTextError::limit_exceeded,"DOM_LIMIT");initial.bytes(n->children.size(),sizeof(decltype(todo)::value_type));for(auto it=n->children.rbegin();it!=n->children.rend();++it){check();todo.emplace_back(*it,d+1);}
  }
  if(!body)fail(tt::HtmlTextError::unsupported,"BODY_MISSING");out->work.parse_ns=ns(start);start=Clock::now();
  aether_style_environment_t env{float(w),float(h),1,16,0,0,0};status(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"STYLE_CREATE");std::int64_t sheetid{};
  status(aether_style_runtime_add_stylesheet(runtime,aether::style::web_capture_defaults_css(),0,"document-web-ua",&sheetid,nullptr),"UA_FAILED");
  for(std::size_t i=0;i<sheets.size();++i){check();
   status(drawing?aether::style::DocumentEmbeddedStyleAccess::add(runtime,doc,embedded_nodes[i],2,initial,&sheetid):aether_style_runtime_add_stylesheet(runtime,sheets[i].c_str(),2,"document-author",&sheetid,nullptr),"AUTHOR_STYLE_FAILED");
  }
  // No scoped matcher is misused as a whole-document CSS completeness proof.
  // Compiler/typography coverage remains explicitly unproven in every result.
  check();const tt::RealHtmlStyleTransition transition(doc);
  status(aether_style_runtime_prepare_revision(runtime,doc,&env,&transaction),"STYLE_PREPARE");status(aether_style_runtime_commit_revision(runtime,transaction,&revision),"STYLE_COMMIT");status(aether_style_revision_create_map(revision,&map),"STYLE_MAP");if(!transition.accepts(doc,map))fail(tt::HtmlTextError::invalid_input,"STYLE_TRANSITION");epochs=std::make_unique<aether::layout::CallbackEpochGuard>(doc->identity);check();
  if(drawing)aether::style::DocumentEmbeddedStyleAccess::validate(runtime,doc,initial);
  auto version=aether::style::pin_style_map(map);Adapter adapter(*this);auto provider=text::DocumentTextProvider::create(fonts,initial,&adapter);provider->drawing_=drawing;adapter.rethrow();
  auto policy_coverage=[&](const aether::style::FrozenNodeRef& rec){if(!drawing)return;const auto& r=rec->typography();if(!r||!r->inputs||!r->inputs->inputs_accounted||r->inputs->external_pending||r->inputs->losses||!r->evidence.break_policy.fields[0].known||!r->evidence.break_policy.fields[1].known)fail(tt::HtmlTextError::unsupported,"DOCUMENT_BREAK_POLICY_UNPROVEN");};
  if(html){auto rec=version->backing()->find_owner(html);if(!rec||!rec->measures())fail(tt::HtmlTextError::unsupported,"HTML_STYLE_MISSING");aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());auto view=s.view();
   if(s.display!=AETHER_DISPLAY_BLOCK||s.position!=AETHER_POSITION_STATIC||s.opacity!=1||(s.overflow_x!=AETHER_OVERFLOW_VISIBLE&&s.overflow_x!=AETHER_OVERFLOW_AUTO)||(s.overflow_y!=AETHER_OVERFLOW_VISIBLE&&s.overflow_y!=AETHER_OVERFLOW_AUTO)||!view.transforms().empty()||!view.backdrop_filters().empty()||!view.box_shadows().empty()||!view.text_shadows().empty()||!view.background_layers().empty()||s.has_text_gradient)fail(tt::HtmlTextError::unsupported,"HTML_EFFECT_UNSUPPORTED");
   policy_coverage(rec);provider->validate_style(rec,s);adapter.rethrow();
  }
  initial.bytes(1,sizeof(aether_node_s*));std::vector<aether_node_s*> visible{body};
  while(!visible.empty()){check();auto* n=visible.back();visible.pop_back();if(n->type!=AETHER_NODE_ELEMENT)continue;auto rec=version->backing()->find_owner(n);if(!rec||!rec->measures())fail(tt::HtmlTextError::unsupported,"DOCUMENT_STYLE_MISSING");aether::layout::BoxStyleSnapshot s(rec->style(),rec->measures());auto view=s.view();if(s.display==AETHER_DISPLAY_NONE)continue;
   const bool rectangular_hidden=drawing&&requests&&n!=body&&s.overflow_x==AETHER_OVERFLOW_HIDDEN&&s.overflow_y==AETHER_OVERFLOW_HIDDEN;
   const bool overflow_supported=rectangular_hidden||(requests?((s.overflow_x==AETHER_OVERFLOW_VISIBLE&&s.overflow_y==AETHER_OVERFLOW_VISIBLE)||(s.overflow_x==AETHER_OVERFLOW_AUTO&&s.overflow_y==AETHER_OVERFLOW_AUTO)||(s.overflow_x==AETHER_OVERFLOW_SCROLL&&s.overflow_y==AETHER_OVERFLOW_SCROLL)):(s.overflow_x==AETHER_OVERFLOW_VISIBLE&&s.overflow_y==AETHER_OVERFLOW_VISIBLE));
   if(s.position!=AETHER_POSITION_STATIC||s.opacity!=1||!overflow_supported||!view.transforms().empty()||!view.backdrop_filters().empty()||!view.text_shadows().empty()||!view.background_layers().empty()||s.has_text_gradient)fail(tt::HtmlTextError::unsupported,"DOCUMENT_EFFECT_UNSUPPORTED");
   if(!view.box_shadows().empty()) {
    if(s.display!=AETHER_DISPLAY_BLOCK&&s.display!=AETHER_DISPLAY_FLEX)throw DocumentShadowUnsupported{};
    for(const auto& shadow:view.box_shadows()){check();if(shadow.inset)throw DocumentShadowUnsupported{};}
   }
   if(s.display==AETHER_DISPLAY_CONTENTS||n->tag_name=="img"||n->tag_name=="svg"||n->tag_name=="canvas"||n->tag_name=="video"||n->tag_name=="input"||n->tag_name=="textarea"||n->tag_name=="select"||n->tag_name=="button")fail(tt::HtmlTextError::unsupported,"DOCUMENT_ELEMENT_UNSUPPORTED");
   if(s.font_weight==700&&!fonts.bold)fail(tt::HtmlTextError::unsupported,"BOLD_FONT_MISSING");policy_coverage(rec);provider->validate_style(rec,s);adapter.rethrow();
   initial.bytes(n->children.size(),sizeof(aether_node_s*));for(auto* child:n->children){check();visible.push_back(child);}
  }
  out->work.styles_ns=ns(start);start=Clock::now();status(aether_layout_create(nullptr,nullptr,&engine),"LAYOUT_CREATE");
  if(!(requests?aether::layout::DocumentTextAccess::install_tracked(*engine,doc,map,provider,[&]{return adapter.cancelled();},initial,current_budget):aether::layout::DocumentTextAccess::install(*engine,doc,map,provider,[&]{return adapter.cancelled();},initial)))fail(tt::HtmlTextError::invalid_input,"TEXT_INSTALL");
  if(drawing)aether::layout::DocumentTextAccess::enable_drawing(engine);
  const auto laid=aether::layout::DocumentRootLayoutAccess::layout(engine,doc,map,float(w),float(h),&layout);
  adapter.rethrow();provider->rethrow_failure();aether::layout::DocumentTextAccess::rethrow_failure(engine);status(laid,"DOCUMENT_LAYOUT_FAILED");check();
  auto& budget=drawing?aether::layout::DocumentTextAccess::drawing_paint_budget(engine,layout,provider.get()):aether::layout::DocumentTextAccess::paint_budget(engine,layout,provider.get());out->work.layout_ns=ns(start);start=Clock::now();
  auto authority=requests?DocumentViewportAuthority::create_registry_requests(doc,layout,map,w,h,offset,*requests,capture_thread,budget,[&]{check();}):DocumentViewportAuthority::create(doc,layout,map,w,h,offset,budget,[&]{check();});out->viewport=authority.diagnostics();for(const auto* box:layout->allocated_boxes){(void)box;check();++out->work.boxes;}
  DocumentShadowPlan shadow_plan(layout,map,authority,budget,[&]{check();});
  const auto finish=[&](DocumentScopePlan* scopes)->std::shared_ptr<const DocumentCaptureFrame>{
  status(aether_display_list_builder_create(nullptr,nullptr,&builder),"PAINT_CREATE");std::unique_ptr<NativePaintList> list;status(scopes?build_native_document_requests_checked(builder,layout,map,authority,*scopes,shadow_plan,list):build_native_document_capture(builder,layout,map,authority,list,&shadow_plan),"PAINT_FAILED");check();if(!list||!list->images.empty())fail(tt::HtmlTextError::preparation_failed,"PAINT_COVERAGE");
  shadow_plan.verify(*list,[&]{check();});out->work.shadow_commands=shadow_plan.size();
  auto painted=drawing?text::DocumentTextPainter::finalize_drawing(engine,layout,*list,*provider):text::DocumentTextPainter::finalize(engine,layout,*list,*provider);adapter.rethrow();if(scopes){shadow_plan.verify(*list,[&]{check();});scopes->verify(*list);}
  if(!(scopes?authority.accepts_checked(layout,map):authority.accepts(layout,map)))fail(tt::HtmlTextError::preparation_failed,"GEOMETRY_CHANGED");
  auto snapshot=preparation::snapshot_native_opacity(*list,&adapter);adapter.rethrow();if(auto* e=std::get_if<preparation::Error>(&snapshot))prep(*e);auto prepared=preparation::prepare(std::get<preparation::Snapshot>(snapshot),&adapter);adapter.rethrow();if(auto* e=std::get_if<preparation::Error>(&prepared))prep(*e);out->work.paint_ns=ns(start);start=Clock::now();
  FrameStore store;auto resized=store.resize({int(w),int(h),1});if(resized!=FrameStatus::ok)fail(resized==FrameStatus::allocation_failed?tt::HtmlTextError::out_of_memory:tt::HtmlTextError::render_failed,"FRAME_FAILED");auto rendered=software::render_a8(store,std::get<preparation::PreparedFrame>(prepared),painted.masks,&adapter);adapter.rethrow();if(rendered.status!=software::Status::ok)fail(rendered.status==software::Status::allocation_failed?tt::HtmlTextError::out_of_memory:rendered.status==software::Status::cancelled?tt::HtmlTextError::cancelled:rendered.status==software::Status::limit_exceeded?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::render_failed,"SOFTWARE_FAILED");std::optional<Frame> frame;if(store.copy_confirmed(frame)!=FrameStatus::ok||!frame)fail(tt::HtmlTextError::out_of_memory,"FRAME_COPY_FAILED");
  out->work.software_work=rendered.pixel_budget;out->frame=std::move(*frame);out->samples=std::move(painted.samples);out->work.paint=painted.work;out->work.provider=provider->work;out->work.budget=budget;out->work.raster_ns=ns(start);out->work.outline_reserved=256000000-provider->font_budget.outline.remaining;out->work.cmap_reserved=4352-provider->font_budget.cmap.remaining;out->work.raster_reserved=512000000-provider->font_budget.raster.remaining;check();
  if(scopes){
   shadow_plan.verify(*list,[&]{check();});scopes->verify(*list);
   if(!authority.accepts_checked(layout,map))fail(tt::HtmlTextError::preparation_failed,"GEOMETRY_CHANGED");
   const auto manifest=authority.request_manifest();budget.bytes(manifest.size());budget.normalized(manifest.size());
   out->request_manifest.assign(manifest.begin(),manifest.end());out->accepted_requests=authority.request_count();out->scroll_registry_count=authority.registry().size();out->work.budget=budget;
  }
  if(drawing){(void)aether::layout::DocumentTextAccess::drawing_paint_token(engine,layout,provider.get());aether::style::DocumentEmbeddedStyleAccess::validate(runtime,doc,budget);out->work.budget=budget;}
  return out;
  };
  if(requests){DocumentScopePlan scopes(layout,map,authority,budget,[&]{check();});return finish(&scopes);}
  return finish(nullptr);
 }
};
static DocumentCaptureResult capture_document_impl(std::string_view raw,text::DocumentFontCatalog fonts,std::uint32_t w,std::uint32_t h,tt::ViewportOffset offset,tt::Cancellation* cancel,const DocumentScrollRequests* requests,aether::layout::DocumentTextBudget* budget,bool drawing=false)noexcept{
 try{DocumentCaptureOperation operation;return operation.run(raw,std::move(fonts),w,h,offset,cancel,requests,budget,drawing);}catch(const SelectedCaptureFailure& e){return e;}catch(const DocumentScrollResolutionError& e){return SelectedCaptureFailure{e==DocumentScrollResolutionError::stale?tt::HtmlTextError::preparation_failed:tt::HtmlTextError::invalid_input,e==DocumentScrollResolutionError::missing?"SCROLL_TARGET_MISSING":e==DocumentScrollResolutionError::ambiguous?"SCROLL_TARGET_AMBIGUOUS":"SCROLL_TARGET_STALE"};}catch(const DocumentScrollBindingError& e){return SelectedCaptureFailure{e==DocumentScrollBindingError::out_of_range||e==DocumentScrollBindingError::root?tt::HtmlTextError::invalid_input:tt::HtmlTextError::unsupported,e==DocumentScrollBindingError::root?"SCROLL_TARGET_ROOT":e==DocumentScrollBindingError::noncontainer?"SCROLL_TARGET_NONCONTAINER":e==DocumentScrollBindingError::inactive?"SCROLL_TARGET_INACTIVE":"SCROLL_OFFSET_OUT_OF_RANGE"};}catch(const DocumentShadowUnsupported&){return SelectedCaptureFailure{tt::HtmlTextError::unsupported,"DOCUMENT_SHADOW_UNSUPPORTED"};}catch(const aether::layout::DocumentRootGeometryUnsupported&){return SelectedCaptureFailure{tt::HtmlTextError::unsupported,"DOCUMENT_HTML_ROOT_GEOMETRY_UNSUPPORTED"};}catch(const aether::layout::DocumentTextCancelled&){return SelectedCaptureFailure{tt::HtmlTextError::cancelled,"CANCELLED"};}catch(const tt::HtmlTextError& e){return SelectedCaptureFailure{e,"DOCUMENT_VIEWPORT_UNSUPPORTED"};}catch(const tt::RunError& e){return SelectedCaptureFailure{e==tt::RunError::out_of_memory?tt::HtmlTextError::out_of_memory:e==tt::RunError::cancelled?tt::HtmlTextError::cancelled:e==tt::RunError::limit_exceeded?tt::HtmlTextError::limit_exceeded:tt::HtmlTextError::unsupported,e==tt::RunError::missing_glyph?"MISSING_GLYPH":e==tt::RunError::limit_exceeded?"DOCUMENT_WORK_LIMIT":"DOCUMENT_TEXT_UNSUPPORTED"};}catch(const std::bad_alloc&){return SelectedCaptureFailure{tt::HtmlTextError::out_of_memory,"OUT_OF_MEMORY"};}catch(const std::length_error&){return SelectedCaptureFailure{tt::HtmlTextError::limit_exceeded,"LIMIT_EXCEEDED"};}catch(const std::invalid_argument& error){const std::string_view reason(error.what());return SelectedCaptureFailure{tt::HtmlTextError::unsupported,drawing&&reason.starts_with("DOCUMENT_")&&reason.size()<128?std::string(reason):"DOCUMENT_TEXT_UNSUPPORTED"};}catch(...){return SelectedCaptureFailure{tt::HtmlTextError::preparation_failed,"INTERNAL_FAILURE"};}
}
DocumentCaptureResult capture_document_html(std::string_view raw,text::DocumentFontCatalog fonts,std::uint32_t w,std::uint32_t h,tt::ViewportOffset offset,tt::Cancellation* cancel)noexcept{
 return capture_document_impl(raw,std::move(fonts),w,h,offset,cancel,nullptr,nullptr);
}
DocumentCaptureResult capture_document_html_requests(std::string_view raw,text::DocumentFontCatalog fonts,std::uint32_t w,std::uint32_t h,const DocumentScrollRequests& requests,aether::layout::DocumentTextBudget& budget,tt::ViewportOffset offset,tt::Cancellation* cancel)noexcept{
 return capture_document_impl(raw,std::move(fonts),w,h,offset,cancel,&requests,&budget);
}

DocumentCaptureResult capture_document_drawing_diagnostic(std::string_view raw,text::DocumentFontCatalog fonts,
 std::uint32_t w,std::uint32_t h,const DocumentScrollRequests& requests,aether::layout::DocumentTextBudget& budget,
 tt::ViewportOffset offset,tt::Cancellation* cancel)noexcept{
 return capture_document_impl(raw,std::move(fonts),w,h,offset,cancel,&requests,&budget,true);
}

}
