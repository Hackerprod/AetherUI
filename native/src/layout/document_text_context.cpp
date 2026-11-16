#include "document_attempt_layout.h"
#include <set>
#include "document_text_context.h"
#include "document_attempt_capsule.h"
#include "layout_types.h"
#include "native_text_transaction.h"
namespace aether::layout {
namespace {
void charge_lookup(DocumentTextBudget& budget,std::size_t count){std::uint64_t work=1;for(;count;count>>=1)work+=2;budget.normalized(work);}
std::uint32_t scalar(const std::string& s,std::size_t& i){
 if(i>=s.size())throw std::invalid_argument("DOCUMENT_UTF8");const auto c=static_cast<unsigned char>(s[i++]);if(c<128)return c;
 unsigned tail;std::uint32_t cp,min;
 if(c>=0xc2&&c<=0xdf){tail=1;cp=c&31;min=128;}else if(c>=0xe0&&c<=0xef){tail=2;cp=c&15;min=2048;}else if(c>=0xf0&&c<=0xf4){tail=3;cp=c&7;min=65536;}else throw std::invalid_argument("DOCUMENT_UTF8");
 if(tail>s.size()-i)throw std::invalid_argument("DOCUMENT_UTF8");while(tail--){const auto d=static_cast<unsigned char>(s[i++]);if((d&192)!=128)throw std::invalid_argument("DOCUMENT_UTF8");cp=(cp<<6)|(d&63);}
 if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::invalid_argument("DOCUMENT_UTF8");return cp;
}
bool collapsible(std::uint32_t cp){return cp==32||cp==9||cp==10||cp==13;}
bool supported_scalar(std::uint32_t cp){return collapsible(cp)||(cp>=32&&cp<127)||(cp>=160&&cp<=255&&cp!=173)||cp==0x2013||cp==0x2014||cp==0x2018||cp==0x2019||cp==0x201c||cp==0x201d||cp==0x2022||cp==0x2026||(cp>=0x2190&&cp<=0x2193);}
bool same_text_style(const BoxStyleSnapshot& a,const BoxStyleSnapshot& b){return a.style_owner()==b.style_owner()&&a.measures_owner()==b.measures_owner()&&native_text_detail::scalars_equal(a.scalars(),b.scalars());}
}
DocumentTextSource::DocumentTextSource(std::shared_ptr<const int> op,aether::style::FrozenNodeRef owner,std::shared_ptr<node_identity_s> layout_owner,std::shared_ptr<node_identity_s> token,const aether_node_s* node,std::uint64_t id,std::size_t dom_id,int source_kind,std::string bytes)
 :operation_(std::move(op)),owner_(std::move(owner)),layout_owner_(std::move(layout_owner)),text_identity_(std::move(token)),text_node_(node),ordinal(id),dom_ordinal(dom_id),kind(static_cast<Kind>(source_kind)),decoded(std::move(bytes)){}
DocumentTextFragment::DocumentTextFragment(std::shared_ptr<const int> op,aether::style::FrozenNodeRef owner,std::shared_ptr<node_identity_s> layout_owner,BoxStyleSnapshot style,std::vector<std::shared_ptr<const DocumentTextSource>> sources,std::uint64_t id,std::uint64_t pass,std::string text,std::vector<DocumentTextRange> mapping)
 :operation_(std::move(op)),owner_(std::move(owner)),layout_owner_(std::move(layout_owner)),style_(std::move(style)),sources_(std::move(sources)),occurrence(id),normalization_pass(pass),normalized(std::move(text)),ranges(std::move(mapping)){}
DocumentTextTransaction::DocumentTextTransaction(aether_doc_s* doc,aether_style_map_s* map,std::shared_ptr<DocumentTextBackend> backend,std::function<bool()> cancel,DocumentTextBudget initial)
 :document_(doc?doc->identity:nullptr),version_(aether::style::pin_style_map(map)),epochs_(document_),nonstate_(document_?document_->non_state_epoch:0),backend_(std::move(backend)),cancelled_(std::move(cancel)),budget(initial){
 if(!document_||!version_||!version_->contextual()||version_->document_lifetime()!=document_||!backend_)throw std::invalid_argument("DOCUMENT_TEXT_BINDING");valid();
}
DocumentTextTransaction::~DocumentTextTransaction()noexcept{if(attempt_capsule_)attempt_capsule_->terminate();attempt_layout_.reset();attempt_capsule_.reset();}
// Inactive factory: no engine/provider callsites in this delta. There is no
// caller-supplied operation token, thread, budget, layout pointer or mode flag.
void DocumentTextAccess::initialize_attempt_topology(aether_layout_engine_s* engine){
 if(!active(engine))throw std::invalid_argument("DOCUMENT_ATTEMPT_MISSING");
 auto& t=*engine->document_text_transaction_;
 t.valid(); // thread/document/map/epochs before any mutable state or callbacks
 if(!engine->layout_in_progress||!t.active_||!t.active_layout_||!t.styles_||
    !t.final_geometry_mode_||t.finalizing_||t.pass_active_||t.slice_auditing_||t.final_issuing_||
    t.layout_completed_||t.attempt_capsule_||!t.passes_.empty()||!t.sources_.empty()||
    !t.fragments_.empty()||!t.dropped_.empty())
  throw std::invalid_argument("DOCUMENT_ATTEMPT_INITIALIZATION");
 if(t.failure_)std::rethrow_exception(t.failure_);
 if(t.cancelled_latched_)throw DocumentTextCancelled{};
 try{
  auto candidate=DocumentAttemptCapsule::create(t.operation_,t.thread_,t.budget);
  t.attempt_capsule_=std::move(candidate); // noexcept publication; no callbacks
 }catch(...){
  if(!t.failure_)t.failure_=std::current_exception();
  t.invalidate_final_ready();throw;
 }
}
std::pair<std::shared_ptr<DocumentAttemptCapsule>,DocumentTextBudget*> DocumentTextAccess::attempt_control_entry(aether_layout_engine_s* engine){
 if(!active(engine))throw std::invalid_argument("DOCUMENT_ATTEMPT_MISSING");
 auto& t=*engine->document_text_transaction_;
 t.valid(); // owner thread/DOM/map before any mutation; never calls cancellation
 try{
  if(!engine->layout_in_progress||!t.active_||!t.active_layout_||!t.styles_||
     !t.final_geometry_mode_||t.finalizing_||t.pass_active_||t.slice_auditing_||t.final_issuing_||
     t.layout_completed_||t.final_ready_||!t.attempt_capsule_)
   throw std::invalid_argument("DOCUMENT_ATTEMPT_CONTROL_STATE");
  if(t.failure_)std::rethrow_exception(t.failure_);
  if(t.cancelled_latched_)throw DocumentTextCancelled{};
  t.attempt_capsule_->valid_owner(t.operation_);
  return {t.attempt_capsule_,&t.budget}; // owner-only synchronous borrow, no callback
 }catch(...){
  if(!t.failure_)t.failure_=std::current_exception();
  t.invalidate_final_ready();t.layout_completed_=false;
  if(t.attempt_capsule_)t.attempt_capsule_->terminate();
  throw;
 }
}
std::shared_ptr<DocumentAttemptCapsule> DocumentTextAccess::attempt_selection_entry(const aether_layout_engine_s* engine){
 // Read-only finalization boundary; unlike attempt_control_entry this permits
 // finalizing/ready, but never terminal/completed transactions. Paint must use
 // the certificate produced by the successful no-throw final commit instead.
 if(!active(engine))throw std::invalid_argument("DOCUMENT_ATTEMPT_MISSING");
 auto& t=*engine->document_text_transaction_;
 t.valid(); // owner thread before all non-atomic state, no callback
 if(!engine->layout_in_progress||!t.active_||!t.active_layout_||!t.styles_||
    !t.final_geometry_mode_||t.pass_active_||t.slice_auditing_||t.final_issuing_||
    t.layout_completed_||!t.attempt_capsule_)
  throw std::invalid_argument("DOCUMENT_ATTEMPT_SELECTION_STATE");
 if(t.failure_)std::rethrow_exception(t.failure_);
 if(t.cancelled_latched_)throw DocumentTextCancelled{};
 t.attempt_capsule_->valid_owner(t.operation_);
 return t.attempt_capsule_; // exact live owner; no mutation, budget or callback
}
bool DocumentTextAccess::drawing_mode(const aether_layout_engine_s* engine){
 if(!active(engine))return false;
 auto& t=*engine->document_text_transaction_;t.valid();return t.drawing_mode_;
}
void DocumentTextAccess::begin_drawing_attempts(aether_layout_engine_s* engine){
 if(!drawing_mode(engine))throw std::invalid_argument("DOCUMENT_DRAWING_MODE");
 auto& t=*engine->document_text_transaction_;
 if(t.attempt_layout_)throw std::invalid_argument("DOCUMENT_DRAWING_REENTRY");
 t.budget.bytes(1,sizeof(DocumentAttemptLayout));
 auto state=std::unique_ptr<DocumentAttemptLayout>(new DocumentAttemptLayout(engine));
 t.attempt_layout_=std::move(state);
}
DocumentAttemptLayout& DocumentTextAccess::drawing_attempts(const aether_layout_engine_s* engine){
 (void)attempt_control_entry(const_cast<aether_layout_engine_s*>(engine));
 auto& t=*engine->document_text_transaction_;
 if(!t.drawing_mode_||!t.attempt_layout_)throw std::invalid_argument("DOCUMENT_DRAWING_STATE");
 return *t.attempt_layout_;
}
aether_layout_result_s* DocumentTextAccess::construction_layout(const aether_layout_engine_s* engine){
 (void)attempt_control_entry(const_cast<aether_layout_engine_s*>(engine));
 return engine->document_text_transaction_->active_layout_;
}
void DocumentTextAccess::discard_drawing_geometry(const aether_layout_engine_s* engine,const aether_layout_box_s* box)noexcept{
 if(!engine||!engine->document_text_transaction_)return;
 auto& t=*engine->document_text_transaction_;
 if(t.thread_!=std::this_thread::get_id())return;
 if(t.attempt_layout_)t.attempt_layout_->discard_before_delete(box);
}
DocumentTextBudget& DocumentTextAccess::drawing_budget(const aether_layout_engine_s* engine){
 (void)drawing_attempts(engine);return engine->document_text_transaction_->budget;
}
DocumentTextBackend& DocumentTextAccess::drawing_backend(const aether_layout_engine_s* engine){
 (void)drawing_attempts(engine);auto& t=*engine->document_text_transaction_;return *t.backend_;
}
void DocumentTextAccess::bind_latest_construction_pass(const aether_layout_engine_s* engine){
 (void)drawing_attempts(engine);const auto& t=*engine->document_text_transaction_;
 if(t.passes_.empty())throw std::invalid_argument("DOCUMENT_CONSTRUCTION_PASS");
 bind_construction_pass(engine,t.passes_.size()-1);
}
void DocumentTextAccess::register_normalized_unit(const aether_layout_engine_s* e,const std::shared_ptr<const DocumentTextFragment>& f,std::uint64_t ordinal,bool separator){
 auto& t=*e->document_text_transaction_;t.valid();
 if(!t.drawing_mode_||!t.pass_active_||!f||f->operation_!=t.operation_||(t.passes_.empty()||f->normalization_pass!=t.passes_.size()-1)||ordinal>=65536||t.fragments_.empty()||t.fragments_.back()!=f)
  throw std::invalid_argument("DOCUMENT_NORMALIZED_UNIT");
 charge_lookup(t.budget,t.normalized_units_.size());t.budget.bytes(1,sizeof(decltype(t.normalized_units_)::value_type)+4*sizeof(void*));
 if(!t.normalized_units_.emplace(f->occurrence,DocumentTextTransaction::NormalizedUnit{ordinal,separator}).second)throw std::invalid_argument("DOCUMENT_NORMALIZED_UNIT_DUPLICATE");
}
std::shared_ptr<const DocumentAtomicInlineEvent> DocumentTextAccess::bind_atomic_unit(const aether_layout_engine_s* e,aether_layout_box_s* box,std::uint64_t ordinal,double width,double height,double baseline){
 auto& state=drawing_attempts(e);auto& t=*e->document_text_transaction_;t.valid();
 if(t.pass_active_||t.passes_.empty()||ordinal>=65536||!t.active_layout_)throw std::invalid_argument("DOCUMENT_ATOMIC_PASS");
 bool member=false;for(auto* b:t.active_layout_->allocated_boxes){t.budget.normalized(1);if(b==box)member=true;}
 if(!member||box->owner!=t.active_layout_||box->kind!=LAYOUT_BOX_ELEMENT||box->style.display!=AETHER_DISPLAY_INLINE_BLOCK||!box->document_has_last_line||!box->document_layout_attempt||box->document_atomic_inline)
  throw std::invalid_argument("DOCUMENT_ATOMIC_BOX");
 const auto record=t.styles_->lookup_record_owner(box->element);const auto token=record?record->node_lifetime():nullptr;
 if(!record||record->node()!=box->element||!token||!token->alive||!token->attached||token->owner!=box->element||record->document_lifetime()!=t.document_||!same_text_style(BoxStyleSnapshot(record->style(),record->measures()),box->style))throw std::invalid_argument("DOCUMENT_ATOMIC_OWNER");
 for(double v:{width,height,baseline})if(!std::isfinite(v)||v<0||v>65536)throw std::invalid_argument("DOCUMENT_ATOMIC_METRICS");
 if(box->document_atomic_widths){const auto& logical=*box->document_atomic_widths;
  const auto& style=box->style;
  const double edge=double(style.padding.left.value)+style.padding.right.value+box->used_border.left+box->used_border.right;
  const double margins=double(style.margin.left.value)+style.margin.right.value;
  for(double value:{logical.content,logical.border,logical.margin})if(!std::isfinite(value)||value<0||value>65536)throw std::invalid_argument("DOCUMENT_ATOMIC_LOGICAL_WIDTH");
  if(style.width.unit!=AETHER_CSS_UNIT_AUTO||logical.border!=logical.content+edge||logical.margin!=logical.border+margins||
     box->border_box.width!=float(logical.border)||box->margin_box.width!=float(logical.border)+float(margins))throw std::invalid_argument("DOCUMENT_ATOMIC_LOGICAL_PROJECTION");
 }
 if(width!=(box->document_atomic_widths?box->document_atomic_widths->margin:double(box->margin_box.width))||height!=double(box->margin_box.height)||baseline!=double(box->content_box.y)-box->margin_box.y+box->document_last_line_baseline)throw std::invalid_argument("DOCUMENT_ATOMIC_PROJECTION");
 const auto pass=t.passes_.size()-1,attempt=state.active_id(const_cast<aether_layout_engine_s*>(e));
 if(!t.passes_[pass].complete||t.passes_[pass].construction_attempt!=attempt)throw std::invalid_argument("DOCUMENT_ATOMIC_ATTEMPT");
 if(t.atomic_units_.size()>=65536)throw std::length_error("DOCUMENT_ATOMIC_CAP");
 charge_lookup(t.budget,t.atomic_units_.size());t.budget.bytes(1,sizeof(DocumentAtomicInlineEvent)+2*sizeof(void*)+sizeof(decltype(t.atomic_units_)::value_type)+4*sizeof(void*));
 auto out=std::shared_ptr<DocumentAtomicInlineEvent>(new DocumentAtomicInlineEvent);
 out->operation_=t.operation_;out->owner_token_=token;out->owner_=box->element;out->box_=box;out->pass_=pass;out->ordinal_=ordinal;out->parent_attempt_=attempt;out->child_attempt_=box->document_layout_attempt;out->projected_width_=box->margin_box.width;out->width_=width;out->height_=height;out->baseline_=baseline;
 if(!t.atomic_units_.emplace(std::make_pair(pass,ordinal),out).second)throw std::invalid_argument("DOCUMENT_ATOMIC_DUPLICATE");
 box->document_atomic_inline=out;return out;
}
void DocumentTextAccess::validate_atomic_event(const aether_layout_engine_s* e,const std::shared_ptr<const DocumentAtomicInlineEvent>& event){
 auto& t=*e->document_text_transaction_;t.valid();
 if(!event||event->operation_!=t.operation_||event->pass_>=t.passes_.size()||!event->owner_token_||!event->owner_token_->alive||!event->owner_token_->attached||event->owner_token_->owner!=event->owner_||t.passes_[event->pass_].construction_attempt!=event->parent_attempt_)throw std::invalid_argument("DOCUMENT_ATOMIC_STALE");
 charge_lookup(t.budget,t.atomic_units_.size());auto i=t.atomic_units_.find({event->pass_,event->ordinal_});if(i==t.atomic_units_.end()||i->second!=event)throw std::invalid_argument("DOCUMENT_ATOMIC_IDENTITY");
}
void DocumentTextAccess::seal_soft_wrap_plan(const aether_layout_engine_s* e,const std::shared_ptr<DocumentSoftWrapPlan>& plan){
 auto& state=drawing_attempts(e);auto& t=*e->document_text_transaction_;
 if(!plan||plan->operation_||plan->pending_.empty()||!plan->pending_.front())throw std::invalid_argument("DOCUMENT_SOFT_WRAP_PLAN");
 plan->operation_=t.operation_;plan->attempt_=state.active_id(const_cast<aether_layout_engine_s*>(e));plan->pass_=plan->pending_.front()->fragment_->normalization_pass;
 validate_soft_wrap_plan(e,plan);
}
void DocumentTextAccess::validate_soft_wrap_plan(const aether_layout_engine_s* e,const std::shared_ptr<const DocumentSoftWrapPlan>& plan){
 auto& t=*e->document_text_transaction_;t.valid();
 if(!plan||plan->operation_!=t.operation_||plan->reason_!=DocumentSoftWrapPlan::Reason::normal_pending_soft_wrap||plan->pass_>=t.passes_.size()||
    !plan->attempt_||t.passes_[plan->pass_].construction_attempt!=plan->attempt_||!t.passes_[plan->pass_].complete||
    plan->line_number_>=65536||(plan->line_.empty()&&plan->line_atoms_.empty())||plan->pending_.empty()||(plan->next_.empty()&&!plan->next_atom_)||(!plan->next_.empty()&&plan->next_atom_)||
    !std::isfinite(plan->available_)||plan->available_<0||plan->available_>65536)
  throw std::invalid_argument("DOCUMENT_SOFT_WRAP_PLAN");
 auto kind=[&](const std::shared_ptr<const DocumentConstructionInterval>& interval){
  t.budget.normalized(1);if(!interval||!interval->fragment_)throw std::invalid_argument("DOCUMENT_SOFT_WRAP_INTERVAL");
  const auto& f=interval->fragment_;
  if(f->operation_!=t.operation_||f->normalization_pass!=plan->pass_||f->style_.white_space!=AETHER_WHITE_SPACE_NORMAL||
     interval->byte_begin_>=interval->byte_end_||interval->byte_end_>f->normalized.size()||!std::isfinite(interval->exact_width_)||interval->exact_width_<0)
   throw std::invalid_argument("DOCUMENT_SOFT_WRAP_SOURCE");
  DocumentTextBudget* budget=nullptr;(void)slice_token(e,f,t.backend_.get(),budget);
  charge_lookup(t.budget,t.normalized_units_.size());auto found=t.normalized_units_.find(f->occurrence);
  if(found==t.normalized_units_.end())throw std::invalid_argument("DOCUMENT_SOFT_WRAP_UNIT_MISSING");return found->second;
 };
 const auto separator=kind(plan->pending_.front());
 if(plan->next_atom_)validate_atomic_event(e,plan->next_atom_);
 const auto next=plan->next_atom_?DocumentTextTransaction::NormalizedUnit{plan->next_atom_->ordinal_,false}:kind(plan->next_.front());
 if(plan->next_atom_&&(plan->next_atom_->pass_!=plan->pass_||plan->next_atom_->parent_attempt_!=plan->attempt_))throw std::invalid_argument("DOCUMENT_ATOMIC_PLAN");
 if(!separator.separator||next.separator||separator.ordinal==65535||next.ordinal!=separator.ordinal+1)throw std::invalid_argument("DOCUMENT_SOFT_WRAP_UNIT_KIND");
 auto sum=[&](const auto& intervals,int category){double result=0;std::uint64_t previous=0;std::size_t end=0;bool first=true;
  for(const auto& interval:intervals){const auto unit=kind(interval);const auto& f=interval->fragment_;
   if(!first&&(f->occurrence<previous||(f->occurrence==previous&&interval->byte_begin_!=end)))throw std::invalid_argument("DOCUMENT_SOFT_WRAP_ORDER");
   if(category==0){if(unit.ordinal>=separator.ordinal)throw std::invalid_argument("DOCUMENT_SOFT_WRAP_LINE");}
   else{
    if(unit.ordinal!=(category==1?separator.ordinal:next.ordinal)||unit.separator!=(category==1)||interval->byte_begin_!=0||interval->byte_end_!=f->normalized.size()||(!first&&f->occurrence!=previous+1))throw std::invalid_argument("DOCUMENT_SOFT_WRAP_WHOLE_UNIT");
    if(category==1)for(unsigned char c:f->normalized){t.budget.normalized(1);if(c!=0x20)throw std::invalid_argument("DOCUMENT_SOFT_WRAP_VISIBLE");}
   }
   result+=interval->exact_width_;if(!std::isfinite(result)||result>65536)throw std::length_error("DOCUMENT_SOFT_WRAP_WIDTH");
   previous=f->occurrence;end=interval->byte_end_;first=false;
  }
  if(category){const auto start=intervals.front()->fragment_->occurrence,finish=intervals.back()->fragment_->occurrence;const auto& pass=t.passes_[plan->pass_];
   for(auto adjacent:{start?start-1:start,finish+1}){if(adjacent>=pass.fragment_begin&&adjacent<pass.fragment_end&&adjacent!=start){charge_lookup(t.budget,t.normalized_units_.size());auto found=t.normalized_units_.find(adjacent);if(found!=t.normalized_units_.end()&&found->second.ordinal==(category==1?separator.ordinal:next.ordinal))throw std::invalid_argument("DOCUMENT_SOFT_WRAP_UNIT_OMISSION");}}
  }
  return result;
 };
 (void)sum(plan->line_,0);double used=0;std::uint64_t last_atom=0;bool first_atom=true;
 for(const auto& atom:plan->line_atoms_){validate_atomic_event(e,atom);
  if(atom->pass_!=plan->pass_||atom->parent_attempt_!=plan->attempt_||atom->ordinal_>=separator.ordinal||(!first_atom&&atom->ordinal_<=last_atom))throw std::invalid_argument("DOCUMENT_ATOMIC_PLAN_ORDER");
  last_atom=atom->ordinal_;first_atom=false;
 }
 if(plan->line_order_.empty()) {if(!plan->line_atoms_.empty())throw std::invalid_argument("DOCUMENT_ATOMIC_PLAN_ORDER");for(const auto& f:plan->line_)used+=f->exact_width_;}
 else {std::size_t text=0,atom=0;std::uint64_t last=0;bool first=true;
  for(auto tag:plan->line_order_){t.budget.normalized(1);std::uint64_t ordinal;double width;
   if(tag==0){if(text>=plan->line_.size())throw std::invalid_argument("DOCUMENT_PLAN_ORDER");const auto& f=plan->line_[text++];ordinal=kind(f).ordinal;width=f->exact_width_;}
   else if(tag==1){if(atom>=plan->line_atoms_.size())throw std::invalid_argument("DOCUMENT_PLAN_ORDER");const auto& f=plan->line_atoms_[atom++];ordinal=f->ordinal_;width=f->width_;}
   else throw std::invalid_argument("DOCUMENT_PLAN_ORDER");
   if(!first&&ordinal<last)throw std::invalid_argument("DOCUMENT_PLAN_ORDER");last=ordinal;first=false;used+=width;
  }if(text!=plan->line_.size()||atom!=plan->line_atoms_.size())throw std::invalid_argument("DOCUMENT_PLAN_ORDER");
 }
 const double pending=sum(plan->pending_,1),following=plan->next_atom_?plan->next_atom_->width_:sum(plan->next_,2);
 // Match needs_break's exact association, no epsilon or rounded widths.
 const double unit=pending+following,required=used+unit;
 if(!std::isfinite(unit)||!std::isfinite(required)||!(required>plan->available_))throw std::invalid_argument("DOCUMENT_SOFT_WRAP_FITS");
}
void DocumentTextAccess::suppress_normal_separator(const aether_layout_engine_s* engine,
 const std::shared_ptr<const DocumentTextFragment>& fragment,const std::shared_ptr<const DocumentSoftWrapPlan>& plan){
 validate_soft_wrap_plan(engine,plan);const auto line=plan->line_number_;
 bool member=false;for(const auto& interval:plan->pending_)if(interval->fragment_==fragment)member=true;
 if(!member)throw std::invalid_argument("DOCUMENT_SEPARATOR_NOT_PENDING");
 auto& state=drawing_attempts(engine);auto& t=*engine->document_text_transaction_;
 const auto attempt=state.active_id(const_cast<aether_layout_engine_s*>(engine));
 if(!fragment||fragment->operation_!=t.operation_||fragment->normalization_pass>=t.passes_.size()||
    fragment->style_.white_space!=AETHER_WHITE_SPACE_NORMAL||fragment->normalized.empty()||line>=65536)
  throw std::invalid_argument("DOCUMENT_SEPARATOR_SUPPRESSION");
 const auto& pass=t.passes_[fragment->normalization_pass];
 if(!pass.complete||pass.construction_attempt!=attempt||fragment->occurrence<pass.fragment_begin||
    fragment->occurrence>=pass.fragment_end||t.fragments_[fragment->occurrence]!=fragment)
  throw std::invalid_argument("DOCUMENT_SEPARATOR_PASS");
 for(unsigned char c:fragment->normalized){t.budget.normalized(1);if(c!=0x20)throw std::invalid_argument("DOCUMENT_SEPARATOR_VISIBLE");}
 std::uint64_t work=1;for(auto n=t.suppressed_separators_.size();n;n>>=1)work+=2;t.budget.normalized(2*work);
 if(t.suppressed_separators_.contains(fragment->occurrence))throw std::invalid_argument("DOCUMENT_SEPARATOR_DUPLICATE");
 t.budget.retained();t.budget.bytes(1,sizeof(DocumentSuppressedSeparator)+4*sizeof(void*));
 t.budget.bytes(1,sizeof(decltype(t.suppressed_separators_)::value_type)+4*sizeof(void*));
 auto event=std::shared_ptr<const DocumentSuppressedSeparator>(new DocumentSuppressedSeparator(t.operation_,fragment,attempt,line,plan));
 t.suppressed_separators_.emplace(fragment->occurrence,std::move(event));
}
void DocumentTextAccess::bind_construction_pass(const aether_layout_engine_s* engine,std::uint64_t index){
 auto& state=drawing_attempts(engine);auto& t=*engine->document_text_transaction_;
 if(index>=t.passes_.size()||index+1!=t.passes_.size())throw std::invalid_argument("DOCUMENT_CONSTRUCTION_PASS");
 auto& pass=t.passes_[index];
 if(!pass.complete||pass.final_generation||pass.construction_attempt||t.pass_active_)
  throw std::invalid_argument("DOCUMENT_CONSTRUCTION_ROLE");
 t.budget.normalized(1);
 pass.construction_attempt=state.active_id(const_cast<aether_layout_engine_s*>(engine));
}
void DocumentTextAccess::promote_selected_passes(aether_layout_engine_s* engine){
 auto& t=*engine->document_text_transaction_;t.valid();
 if(!t.drawing_mode_||!t.active_||!t.attempt_layout_||t.promotions_committed_||t.pass_active_)
  throw std::invalid_argument("DOCUMENT_PROMOTION_STATE");
 const auto selection=t.attempt_layout_->selected_identity(engine);
 decltype(t.promoted_passes_) staged;
 for(std::size_t index=0;index<t.passes_.size();++index){
  t.budget.normalized(1);const auto& pass=t.passes_[index];
  if(!pass.complete)throw std::invalid_argument("DOCUMENT_PROMOTION_INCOMPLETE");
  if(!pass.construction_attempt)continue; // intrinsic/height, never geometry
  if(!t.attempt_layout_->selected_attempt(engine,pass.construction_attempt,t.budget))continue;
  if(pass.final_generation||pass.fragment_end<pass.fragment_begin||pass.fragment_end>t.fragments_.size())
   throw std::invalid_argument("DOCUMENT_PROMOTION_PASS");
  if(t.next_final_generation_==std::numeric_limits<std::uint64_t>::max())
   throw std::length_error("DOCUMENT_FINAL_GENERATION");
  const auto generation=t.next_final_generation_++; // failed attempts never reuse
  const auto count=pass.fragment_end-pass.fragment_begin;
  t.budget.bytes(1,sizeof(DocumentFinalPass)+2*sizeof(void*));
  t.budget.bytes(count,sizeof(std::shared_ptr<const DocumentTextFragment>));
  t.budget.bytes(1,sizeof(decltype(staged)::value_type)+4*sizeof(void*));
  auto proof=std::shared_ptr<DocumentFinalPass>(new DocumentFinalPass);
  proof->operation_=t.operation_;proof->pass_=index;proof->generation_=generation;
  proof->promoted_attempt_=pass.construction_attempt;proof->selected_nonce_=selection;
  proof->fragments_.reserve(count);
  t.budget.bytes(count,sizeof(std::shared_ptr<const DocumentSuppressedSeparator>));proof->suppressed_.reserve(count);
  for(auto i=pass.fragment_begin;i<pass.fragment_end;++i){
   t.budget.normalized(1);const auto& fragment=t.fragments_[i];
   if(!fragment||fragment->normalization_pass!=index)throw std::invalid_argument("DOCUMENT_PROMOTION_FRAGMENT");
   DocumentTextBudget* budget=nullptr;(void)slice_token(engine,fragment,t.backend_.get(),budget);
   proof->fragments_.push_back(fragment);
   std::uint64_t work=1;for(auto n=t.suppressed_separators_.size();n;n>>=1)work+=2;t.budget.normalized(work);
   if(auto found=t.suppressed_separators_.find(fragment->occurrence);found!=t.suppressed_separators_.end()){
    if(found->second->attempt_!=pass.construction_attempt)throw std::invalid_argument("DOCUMENT_SEPARATOR_ATTEMPT");
    proof->suppressed_.push_back(found->second);
   }

  }
  staged.emplace(index,std::move(proof));
 }
 // No callback anywhere in this stage. An exception leaves the prior inventory
 // unchanged; the enclosing operation rejects output. Geometry replacement is
 // still a separate staged step in this same integrated pipeline.
 t.attempt_layout_->audit_selection(engine);
 t.promoted_passes_.swap(staged);t.promotions_committed_=true;
}
void DocumentTextTransaction::valid()const{
 if(std::this_thread::get_id()!=thread_)throw std::invalid_argument("DOCUMENT_TEXT_THREAD");
 if(!document_||!document_->alive||!epochs_.valid()||document_->non_state_epoch!=nonstate_||!aether::style::inputs_current(version_.get()))throw std::invalid_argument("DOCUMENT_TEXT_STALE");
}
void DocumentTextTransaction::check()const{valid();invalidate_final_ready();slice_phase_.reset();if(cancelled_latched_)throw DocumentTextCancelled{};const bool stop=cancelled_&&cancelled_();valid();if(stop){cancelled_latched_=true;throw DocumentTextCancelled{};}}
void DocumentTextTransaction::invalidate_final_ready()const noexcept{
 final_ready_selection_.reset();final_ready_geometry_=nullptr;
 final_ready_.reset();final_ready_layout_=nullptr;final_ready_assets_.reset();final_ready_product_=nullptr;final_ready_revision_=0;
}
void DocumentTextAccess::enable_drawing(aether_layout_engine_s* engine){
 enable_final_geometry(engine);engine->document_text_transaction_->drawing_mode_=true;
}
void DocumentTextAccess::enable_final_geometry(aether_layout_engine_s* engine){
 if(!active(engine))throw std::invalid_argument("DOCUMENT_FINAL_MISSING");
 auto& t=*engine->document_text_transaction_;t.valid();
 if(engine->layout_in_progress||t.begun_once_||t.active_||t.layout_completed_)throw std::invalid_argument("DOCUMENT_FINAL_MODE");
 t.final_geometry_mode_=true;
}
bool DocumentTextAccess::final_geometry_mode(const aether_layout_engine_s* engine){
 if(!active(engine))return false;
 auto& t=*engine->document_text_transaction_;t.valid();return t.final_geometry_mode_;
}
void DocumentTextAccess::collector_entry(const aether_layout_engine_s* engine){
 if(!active(engine))return;
 auto& t=*engine->document_text_transaction_;t.valid();
 if(t.finalizing_)throw std::invalid_argument("DOCUMENT_FINAL_COLLECTOR_REENTRANT");
}
bool DocumentTextAccess::install(aether_layout_engine_s& e,aether_doc_s* doc,aether_style_map_s* map,std::shared_ptr<DocumentTextBackend> backend,std::function<bool()> cancel,DocumentTextBudget initial){
 DocumentTextBudget* current=&initial;
 return install_tracked(e,doc,map,std::move(backend),std::move(cancel),initial,current);
}
bool DocumentTextAccess::install_tracked(aether_layout_engine_s& e,aether_doc_s* doc,aether_style_map_s* map,std::shared_ptr<DocumentTextBackend> backend,std::function<bool()> cancel,DocumentTextBudget& initial,DocumentTextBudget*& current){
 if(e.layout_in_progress||e.document_text_transaction_||e.real_text_policy_||e.synthetic_text_policy_||e.native_text_transaction_||e.text_selection_||e.text_measurer||e.contextual_text_measurer)return false;
 initial.bytes(1,sizeof(DocumentTextTransaction)+sizeof(int)+4*sizeof(void*));auto candidate=std::shared_ptr<DocumentTextTransaction>(new DocumentTextTransaction(doc,map,std::move(backend),std::move(cancel),initial));e.document_text_transaction_=std::move(candidate);current=&e.document_text_transaction_->budget;return true;
}
bool DocumentTextAccess::active(const aether_layout_engine_s* e)noexcept{return e&&bool(e->document_text_transaction_);}
void DocumentTextAccess::begin(aether_layout_engine_s* e,aether_doc_s* doc,const StyleLookup& styles,aether_layout_result_s* layout){
 collector_entry(e);if(!active(e))return;auto& t=*e->document_text_transaction_;t.check();if(!doc||t.begun_once_||t.active_||e->real_text_policy_||e->synthetic_text_policy_||e->native_text_transaction_||e->text_selection_||e->text_measurer||e->contextual_text_measurer||doc->identity!=t.document_||styles.base_version()!=t.version_||styles.selected_version()!=t.version_||!styles.structure_valid())throw std::invalid_argument("DOCUMENT_TEXT_BEGIN");t.begun_once_=true;
 // The complete original DOM index is separate from normalization passes.
 struct Visit {aether_node_s* node;std::size_t depth;};
 t.budget.bytes(1,sizeof(Visit));std::vector<Visit> stack{{doc,0}};
 while(!stack.empty()){t.check();t.budget.normalized(1);const auto v=stack.back();stack.pop_back();
  if(!v.node||v.depth>64||t.dom_order_.size()>=16384||!v.node->identity||!v.node->identity->alive||!v.node->identity->attached)throw std::length_error("DOCUMENT_TEXT_DOM_BOUND");
  t.budget.bytes(1,sizeof(std::pair<const aether_node_s*,std::size_t>)+4*sizeof(void*));
  if(!t.dom_order_.emplace(v.node,t.dom_order_.size()).second)throw std::invalid_argument("DOCUMENT_TEXT_DOM_ORDER");
  if(v.node->children.size()>16384-stack.size())throw std::length_error("DOCUMENT_TEXT_DOM_BOUND");
  for(std::size_t i=v.node->children.size();i>0;--i){if((i&255)==0)t.check();t.budget.normalized(1);auto* child=v.node->children[i-1];if(!child||child->parent!=v.node)throw std::invalid_argument("DOCUMENT_TEXT_DOM_PARENT");t.budget.bytes(1,sizeof(Visit));stack.push_back({child,v.depth+1});}
 }
 t.active_layout_=layout;t.styles_=&styles;t.active_=true;
}
void DocumentTextAccess::finish(aether_layout_engine_s* e)noexcept{if(active(e)&&std::this_thread::get_id()==e->document_text_transaction_->thread_){e->document_text_transaction_->invalidate_final_ready();if(e->document_text_transaction_->attempt_capsule_)e->document_text_transaction_->attempt_capsule_->terminate();e->document_text_transaction_->slice_phase_.reset();e->document_text_transaction_->active_=false;e->document_text_transaction_->pass_active_=false;e->document_text_transaction_->styles_=nullptr;}}

void DocumentTextAccess::capture_failure(aether_layout_engine_s* e) noexcept{
 if(active(e)&&std::this_thread::get_id()==e->document_text_transaction_->thread_){auto& t=*e->document_text_transaction_;t.invalidate_final_ready();if(t.attempt_capsule_)t.attempt_capsule_->terminate();if(!t.failure_)t.failure_=std::current_exception();t.layout_completed_=false;}
}
DocumentTextBudget& DocumentTextAccess::paint_budget(const aether_layout_engine_s* engine,const aether_layout_result_s* layout,const DocumentTextBackend* backend){
 (void)paint_token(engine,layout,backend);return engine->document_text_transaction_->budget;
}
void DocumentTextAccess::rethrow_failure(const aether_layout_engine_s* e){
 if(!active(e))throw std::invalid_argument("DOCUMENT_TEXT_MISSING");const auto& t=*e->document_text_transaction_;
 if(std::this_thread::get_id()!=t.thread_)throw std::invalid_argument("DOCUMENT_TEXT_THREAD");
 if(t.failure_)std::rethrow_exception(t.failure_);if(t.cancelled_latched_)throw DocumentTextCancelled{};t.valid();
}
void DocumentTextAccess::complete_layout(aether_layout_engine_s* e,const aether_layout_result_s* layout){
 if(!active(e))return;rethrow_failure(e);auto& t=*e->document_text_transaction_;
 if(t.final_geometry_mode_)throw std::invalid_argument("DOCUMENT_FINAL_LEGACY_COMPLETE");
 if(!t.begun_once_||t.active_||t.pass_active_||!layout||!layout->assets||layout->assets->base!=t.version_||layout->assets->document_lifetime!=t.document_)throw std::invalid_argument("DOCUMENT_TEXT_LAYOUT_FINISH");
 for(const auto& pass:t.passes_){t.check();if(!pass.complete)throw std::invalid_argument("DOCUMENT_TEXT_INCOMPLETE_PASS");}
 t.completed_assets_=layout->assets;t.completed_layout_=layout;t.layout_completed_=true;
}
std::shared_ptr<const int> DocumentTextAccess::paint_token(const aether_layout_engine_s* e,const aether_layout_result_s* layout,const DocumentTextBackend* backend){
 rethrow_failure(e);const auto& t=*e->document_text_transaction_;
 if(t.final_geometry_mode_)throw std::invalid_argument("DOCUMENT_FINAL_PAINT_INACTIVE");
 if(!t.layout_completed_||t.active_||t.pass_active_||!layout||t.completed_layout_!=layout||layout->assets!=t.completed_assets_||t.backend_.get()!=backend||!layout->assets->structure_valid())throw std::invalid_argument("DOCUMENT_TEXT_PAINT_BINDING");return t.operation_;
}

void DocumentTextAccess::check(const aether_layout_engine_s* e){if(active(e)){auto& t=*e->document_text_transaction_;if(std::this_thread::get_id()!=t.thread_)throw std::invalid_argument("DOCUMENT_TEXT_THREAD");if(t.final_issuing_)throw std::invalid_argument("DOCUMENT_FINAL_REENTRANT");}if(active(e)&&e->document_text_transaction_->slice_auditing_)throw std::invalid_argument("DOCUMENT_SLICE_REENTRANT");if(!active(e))throw std::invalid_argument("DOCUMENT_TEXT_MISSING");e->document_text_transaction_->check();}
std::shared_ptr<const DocumentTextSource> DocumentTextAccess::source(const aether_layout_engine_s* e,aether_node_s* owner,aether_node_s* node,const BoxStyleSnapshot& style){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||!owner||!node||(node->type!=AETHER_NODE_TEXT&&node->type!=AETHER_NODE_COMMENT&&!(node==owner&&node->type==AETHER_NODE_ELEMENT&&node->tag_name=="br")))throw std::invalid_argument("DOCUMENT_TEXT_SOURCE");
 auto* actual=owner->anonymous_flex_owner?owner->anonymous_flex_owner:owner;auto record=t.styles_?t.styles_->lookup_record_owner(actual):aether::style::FrozenNodeRef{};
 if(!record||!record->node_lifetime()->alive||!record->node_lifetime()->attached||record->node_lifetime()->owner!=actual||(node!=owner&&node->parent!=actual)||!node->identity||!node->identity->alive||!node->identity->attached)throw std::invalid_argument("DOCUMENT_TEXT_SOURCE_IDENTITY");
 if(owner->anonymous_flex_owner){if(owner->parent!=actual||owner->style_origin!=actual||t.styles_->lookup_record_owner(owner))throw std::invalid_argument("DOCUMENT_TEXT_ANONYMOUS_OWNER");bool member=false;for(std::size_t i=0;i<owner->children.size();++i){if((i&255)==0)t.check();t.budget.normalized(1);if(owner->children[i]==node)member=true;}if(!member)throw std::invalid_argument("DOCUMENT_TEXT_ANONYMOUS_SOURCE");}
 auto style_ref=t.styles_->lookup_node_owner(owner);auto measures_ref=t.styles_->lookup_measures_owner(owner);if(!style_ref||!measures_ref||!owner->identity||!owner->identity->alive||owner->identity->owner!=owner)throw std::invalid_argument("DOCUMENT_TEXT_EFFECTIVE_OWNER");BoxStyleSnapshot frozen(style_ref,measures_ref);if(!same_text_style(frozen,style))throw std::invalid_argument("DOCUMENT_TEXT_SOURCE_STYLE");t.backend_->validate_style(record,style);t.check();
 t.budget.bytes(1,sizeof(inline_item_s));if(node->type==AETHER_NODE_TEXT)t.budget.bytes(node->text.size());
 if(auto found=t.sources_.find({owner,node});found!=t.sources_.end()){if(node->type==AETHER_NODE_TEXT&&found->second->decoded!=node->text)throw std::invalid_argument("DOCUMENT_TEXT_SOURCE_MUTATION");return found->second;}
 std::uint64_t count=0;for(std::size_t i=0;node->type==AETHER_NODE_TEXT&&i<node->text.size();){if((count&255)==0)t.check();t.budget.normalized(1);auto cp=scalar(node->text,i);if(!supported_scalar(cp))throw std::invalid_argument("DOCUMENT_TEXT_SCALAR_UNSUPPORTED");if((style.white_space==AETHER_WHITE_SPACE_PRE||style.white_space==AETHER_WHITE_SPACE_PRE_WRAP)&&cp==9)throw std::invalid_argument("TAB_PRESERVATION_UNSUPPORTED");++count;}
 if(!t.decoded_nodes_.count(node)){t.budget.decoded(count);t.budget.bytes(1,sizeof(void*)*5);t.decoded_nodes_.insert(node);}t.budget.source();t.budget.bytes(1,sizeof(DocumentTextSource)+sizeof(void*)*10);if(node->type==AETHER_NODE_TEXT)t.budget.bytes(node->text.size());auto order=t.dom_order_.find(node);if(order==t.dom_order_.end())throw std::invalid_argument("DOCUMENT_TEXT_ORIGINAL_SOURCE");
 auto result=std::shared_ptr<const DocumentTextSource>(new DocumentTextSource(t.operation_,record,owner->identity,node->identity,node,t.next_source_++,order->second,node->type==AETHER_NODE_TEXT?0:node->type==AETHER_NODE_COMMENT?1:2,node->type==AETHER_NODE_TEXT?node->text:std::string{}));t.sources_by_ordinal_.push_back(result);t.sources_.emplace(std::make_pair(owner,node),result);return result;
}
std::shared_ptr<const DocumentTextFragment> DocumentTextAccess::fragment(const aether_layout_engine_s* e,const std::vector<std::shared_ptr<const DocumentTextSource>>& sources,aether_node_s* owner,const BoxStyleSnapshot& style,const std::string& text,const std::vector<DocumentTextRange>& ranges){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||!t.pass_active_||text.empty()||sources.empty()||!owner)throw std::invalid_argument("DOCUMENT_TEXT_FRAGMENT");
 auto* actual=owner->anonymous_flex_owner?owner->anonymous_flex_owner:owner;auto record=t.styles_?t.styles_->lookup_record_owner(actual):aether::style::FrozenNodeRef{};if(!record)throw std::invalid_argument("DOCUMENT_TEXT_FRAGMENT_OWNER");auto style_ref=t.styles_->lookup_node_owner(owner);auto measures_ref=t.styles_->lookup_measures_owner(owner);if(!style_ref||!measures_ref||!owner->identity||!owner->identity->alive||owner->identity->owner!=owner)throw std::invalid_argument("DOCUMENT_TEXT_EFFECTIVE_OWNER");BoxStyleSnapshot frozen(style_ref,measures_ref);if(!same_text_style(frozen,style))throw std::invalid_argument("DOCUMENT_TEXT_FRAGMENT_STYLE");t.backend_->validate_style(record,style);t.check();
 t.budget.retained();t.budget.bytes(1,sizeof(DocumentTextFragment)+sizeof(void*)*2);t.budget.bytes(text.size(),2); // retained text plus temporary coverage bits conservative bytes
 t.budget.bytes(ranges.size(),sizeof(DocumentTextRange));t.budget.bytes(sources.size(),sizeof(std::shared_ptr<const DocumentTextSource>)+sizeof(std::pair<std::uint64_t,std::size_t>)+4*sizeof(void*));
 std::vector<std::uint8_t> covered(text.size());std::map<std::uint64_t,std::size_t> positions;std::size_t last_output=0,last_source=0,last_input=0;bool first=true;
 for(std::size_t i=0;i<sources.size();++i){t.check();t.budget.normalized(1);const auto& s=sources[i];if(!s||s->operation_!=t.operation_||!s->layout_owner_->alive||!s->layout_owner_->attached||!s->text_identity_->alive||!s->text_identity_->attached||s->text_identity_->owner!=s->text_node_||!positions.emplace(s->ordinal,i).second)throw std::invalid_argument("DOCUMENT_TEXT_FRAGMENT_SOURCE");}
 for(const auto& r:ranges){t.check();t.budget.normalized(1);auto found=positions.find(r.source_ordinal);if(found==positions.end())throw std::invalid_argument("DOCUMENT_TEXT_RANGE_SOURCE");const auto source_index=found->second;const auto it=sources.begin()+source_index;if(!first&&(source_index<last_source||(source_index==last_source&&r.input_begin<last_input)))throw std::invalid_argument("DOCUMENT_TEXT_INPUT_ORDER");first=false;last_source=source_index;const auto& raw=(*it)->decoded;if(r.input_begin>raw.size()||r.input_length>raw.size()-r.input_begin||r.output_begin>text.size()||r.output_length>text.size()-r.output_begin||r.output_begin<last_output)throw std::invalid_argument("DOCUMENT_TEXT_RANGE");last_input=r.input_begin+r.input_length;last_output=r.output_begin;
  std::size_t cursor=r.input_begin;const auto cp=scalar(raw,cursor);if(cursor-r.input_begin!=r.input_length)throw std::invalid_argument("DOCUMENT_TEXT_SCALAR_RANGE");
  if(r.output_length){if(r.input_length==r.output_length&&raw.compare(r.input_begin,r.input_length,text,r.output_begin,r.output_length)==0){if(!collapsible(cp)&&(*it)->layout_owner_!=owner->identity)throw std::invalid_argument("DOCUMENT_TEXT_CROSS_OWNER");}else if((style.white_space==AETHER_WHITE_SPACE_NORMAL||style.white_space==AETHER_WHITE_SPACE_NOWRAP)&&collapsible(cp)&&r.output_length==1&&text[r.output_begin]==' '){}else throw std::invalid_argument("DOCUMENT_TEXT_NORMALIZATION");const bool collapsed=(style.white_space==AETHER_WHITE_SPACE_NORMAL||style.white_space==AETHER_WHITE_SPACE_NOWRAP)&&collapsible(cp)&&r.output_length==1&&text[r.output_begin]==' ';for(std::size_t j=0;j<r.output_length;++j){auto& coverage=covered[r.output_begin+j];if(coverage&&!(collapsed&&coverage==2))throw std::invalid_argument("DOCUMENT_TEXT_OUTPUT_OVERLAP");coverage=collapsed?2:1;}}
  else if(!collapsible(cp))throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_SCALAR");
 }
 if(std::find(covered.begin(),covered.end(),false)!=covered.end())throw std::invalid_argument("DOCUMENT_TEXT_OUTPUT_COVERAGE");for(std::size_t i=0;i<text.size();)scalar(text,i);
 auto result=std::shared_ptr<const DocumentTextFragment>(new DocumentTextFragment(t.operation_,record,owner->identity,style,sources,t.next_fragment_++,t.passes_.size()-1,text,ranges));t.fragments_.push_back(result);return result;
}
void DocumentTextAccess::dropped(const aether_layout_engine_s* e,const std::vector<DocumentTextRange>& ranges){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||!t.pass_active_)throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_OPERATION");
 for(const auto& r:ranges){t.check();t.budget.normalized(1);if(r.output_length)throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_OUTPUT");const DocumentTextSource* source=r.source_ordinal<t.sources_by_ordinal_.size()?t.sources_by_ordinal_[r.source_ordinal].get():nullptr;if(source&&source->decoded.empty()){if(r.input_begin||r.input_length||r.output_begin)throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_EVENT");continue;}if(!source||r.input_begin>=source->decoded.size())throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_SOURCE");std::size_t i=r.input_begin;const auto cp=scalar(source->decoded,i);if(i-r.input_begin!=r.input_length||!collapsible(cp))throw std::invalid_argument("DOCUMENT_TEXT_DROPPED_INPUT");}
 t.budget.bytes(ranges.size(),sizeof(DocumentTextRange));t.dropped_.insert(t.dropped_.end(),ranges.begin(),ranges.end());
}

void DocumentTextAccess::validate_source(const aether_layout_engine_s* e,const std::shared_ptr<const DocumentTextSource>& s){
 check(e);auto& t=*e->document_text_transaction_;
 if(!t.active_||!s||s->operation_!=t.operation_||!s->layout_owner_->alive||!s->layout_owner_->attached||!s->text_identity_->alive||!s->text_identity_->attached||s->text_identity_->owner!=s->text_node_)throw std::invalid_argument("DOCUMENT_TEXT_PASS_SOURCE");
}
void DocumentTextAccess::begin_pass(const aether_layout_engine_s* e,const std::vector<std::shared_ptr<const DocumentTextSource>>& sources){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||t.pass_active_)throw std::invalid_argument("DOCUMENT_TEXT_PASS_STATE");
 bool first=true;std::size_t previous=0;
 for(const auto& s:sources){validate_source(e,s);t.budget.normalized(1);if(!first&&s->dom_ordinal<=previous)throw std::invalid_argument("DOCUMENT_TEXT_PASS_ORDER");first=false;previous=s->dom_ordinal;}
 t.budget.bytes(1,sizeof(DocumentTextTransaction::Pass));t.budget.bytes(sources.size(),sizeof(std::shared_ptr<const DocumentTextSource>));
 DocumentTextTransaction::Pass pass;pass.sources=sources;pass.fragment_begin=t.fragments_.size();pass.dropped_begin=t.dropped_.size();t.passes_.push_back(std::move(pass));t.pass_active_=true;
}
void DocumentTextAccess::begin_final_pass(const aether_layout_engine_s* e,
 const std::vector<std::shared_ptr<const DocumentTextSource>>& sources){
 collector_entry(e);
 if(!active(e))throw std::invalid_argument("DOCUMENT_FINAL_OPERATION");
 auto& t=*e->document_text_transaction_;t.valid(); // thread before counters/callbacks
 if(!t.active_||t.pass_active_||t.slice_auditing_||t.final_issuing_)throw std::invalid_argument("DOCUMENT_FINAL_STATE");
 if(t.next_final_generation_==std::numeric_limits<std::uint64_t>::max())throw std::length_error("DOCUMENT_FINAL_GENERATION");
 const auto generation=t.next_final_generation_++; // nonrefundable even if emission fails
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{t.final_issuing_};t.final_issuing_=true;
 const auto old_passes=t.passes_.size(),old_fragments=t.fragments_.size(),old_dropped=t.dropped_.size();
 const auto* styles=t.styles_;
 t.budget.bytes(sources.size(),sizeof(std::shared_ptr<const DocumentTextSource>));
 std::vector<std::shared_ptr<const DocumentTextSource>> owned_sources=sources; // before any callback
 auto ready=[&]{t.valid();if(!t.active_||t.pass_active_||t.styles_!=styles||!styles||
   t.passes_.size()!=old_passes||t.fragments_.size()!=old_fragments||t.dropped_.size()!=old_dropped)
    throw std::invalid_argument("DOCUMENT_FINAL_EMISSION_ABORTED");};
 // No public entry is allowed to start another pass from these callbacks.
 // finish/abort stay noexcept and terminate the operation; ready detects that.
 t.check();ready();
 bool first=true;std::size_t previous=0;
 for(const auto& source:owned_sources){
  t.check();ready();
  if(!source||source->operation_!=t.operation_||!source->layout_owner_||!source->layout_owner_->alive||
     !source->layout_owner_->attached||!source->text_identity_||!source->text_identity_->alive||
     !source->text_identity_->attached||source->text_identity_->owner!=source->text_node_)
   throw std::invalid_argument("DOCUMENT_TEXT_PASS_SOURCE");
  t.budget.normalized(1);
  if(!first&&source->dom_ordinal<=previous)throw std::invalid_argument("DOCUMENT_TEXT_PASS_ORDER");
  first=false;previous=source->dom_ordinal;
 }
 t.budget.bytes(1,sizeof(DocumentTextTransaction::Pass));
 DocumentTextTransaction::Pass pass;pass.sources=std::move(owned_sources);pass.fragment_begin=old_fragments;
 pass.dropped_begin=old_dropped;pass.final_generation=generation;
 ready(); // final callback-free state check before publication
 t.passes_.push_back(std::move(pass));t.pass_active_=true;
}
void DocumentTextAccess::abort_pass(const aether_layout_engine_s* e) noexcept{
 if(active(e)&&std::this_thread::get_id()==e->document_text_transaction_->thread_){auto& t=*e->document_text_transaction_;t.invalidate_final_ready();if(t.attempt_capsule_)t.attempt_capsule_->terminate();t.slice_phase_.reset();t.active_=false;t.pass_active_=false;t.styles_=nullptr;}
}
void DocumentTextAccess::finish_pass(const aether_layout_engine_s* e){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||!t.pass_active_||t.passes_.empty())throw std::invalid_argument("DOCUMENT_TEXT_PASS_STATE");auto& pass=t.passes_.back();
 struct Span {const DocumentTextSource* source;std::size_t start;};
 t.budget.bytes(pass.sources.size(),sizeof(std::pair<const std::uint64_t,Span>)+4*sizeof(void*));std::map<std::uint64_t,Span> positions;std::size_t size=0;
 for(const auto& s:pass.sources){validate_source(e,s);t.budget.normalized(1);const auto length=std::max(std::size_t{1},s->decoded.size());t.budget.bytes(length);if(length>std::numeric_limits<std::size_t>::max()-size)throw std::length_error("DOCUMENT_TEXT_PASS_SIZE");positions.emplace(s->ordinal,Span{s.get(),size});size+=length;}
 std::vector<unsigned char> coverage(size,0);bool first=true;std::size_t previous_order=0,previous_end=0;
 auto cover=[&](const DocumentTextRange& r,bool emitted){
  t.check();t.budget.normalized(1);auto it=positions.find(r.source_ordinal);if(it==positions.end())throw std::invalid_argument("DOCUMENT_TEXT_PASS_FOREIGN_SOURCE");const auto& span=it->second;const auto& raw=span.source->decoded;
  std::size_t length=r.input_length;
  if(raw.empty()){if(r.input_begin||r.input_length||r.output_length)throw std::invalid_argument("DOCUMENT_TEXT_PASS_EVENT");length=1;}
  else {if(r.input_begin>=raw.size()||r.input_length>raw.size()-r.input_begin)throw std::invalid_argument("DOCUMENT_TEXT_PASS_RANGE");std::size_t end=r.input_begin;scalar(raw,end);if(end-r.input_begin!=r.input_length)throw std::invalid_argument("DOCUMENT_TEXT_PASS_SCALAR");}
  if(emitted&&r.output_length){if(!first&&(span.source->dom_ordinal<previous_order||(span.source->dom_ordinal==previous_order&&r.input_begin<previous_end)))throw std::invalid_argument("DOCUMENT_TEXT_PASS_EMISSION_ORDER");first=false;previous_order=span.source->dom_ordinal;previous_end=r.input_begin+r.input_length;}
  for(std::size_t i=0;i<length;++i){if((i&255)==0)t.check();t.budget.normalized(1);auto& byte=coverage[span.start+r.input_begin+i];if(byte++)throw std::invalid_argument("DOCUMENT_TEXT_PASS_DUPLICATE_INPUT");}
 };
 for(std::size_t i=pass.fragment_begin;i<t.fragments_.size();++i){t.check();for(const auto& r:t.fragments_[i]->ranges)cover(r,true);}
 for(std::size_t i=pass.dropped_begin;i<t.dropped_.size();++i)cover(t.dropped_[i],false);
 for(std::size_t i=0;i<coverage.size();++i){if((i&255)==0)t.check();t.budget.normalized(1);if(coverage[i]!=1)throw std::invalid_argument("DOCUMENT_TEXT_PASS_MISSING_INPUT");}
 pass.fragment_end=t.fragments_.size();pass.dropped_end=t.dropped_.size();pass.complete=true;t.pass_active_=false;
}

DocumentTextBudget& DocumentTextAccess::normalization_budget(const aether_layout_engine_s* e){collector_entry(e);check(e);auto& t=*e->document_text_transaction_;if(!t.active_)throw std::invalid_argument("DOCUMENT_TEXT_PASS");return t.budget;}
void DocumentTextAccess::final_pass_identity(const aether_layout_engine_s* e,
 const std::shared_ptr<const DocumentFinalPass>& proof,const DocumentTextBackend* backend){
 (void)slice_entry(e,backend);
 auto& t=*e->document_text_transaction_;
 if(!proof||proof->operation_!=t.operation_||!proof->generation_||proof->pass_>=t.passes_.size())
  throw std::invalid_argument("DOCUMENT_FINAL_IDENTITY");
 const auto& pass=t.passes_[proof->pass_];
 bool role=pass.final_generation==proof->generation_;
 if(proof->promoted_attempt_){
  charge_lookup(t.budget,t.promoted_passes_.size());
  const auto found=t.promoted_passes_.find(proof->pass_);
  role=t.drawing_mode_&&found!=t.promoted_passes_.end()&&found->second==proof&&
   pass.construction_attempt==proof->promoted_attempt_&&proof->selected_nonce_&&t.attempt_layout_&&
   t.attempt_layout_->selected_identity(e)==proof->selected_nonce_;
 }
 if(!pass.complete||!role||pass.fragment_end<pass.fragment_begin||
    pass.fragment_end>t.fragments_.size()||proof->fragments_.size()!=pass.fragment_end-pass.fragment_begin)
  throw std::invalid_argument("DOCUMENT_FINAL_INVENTORY");
}
void DocumentTextAccess::validate_final_inventory(const aether_layout_engine_s* e,
 const std::shared_ptr<const DocumentFinalPass>& proof,const DocumentTextBackend* backend){
 final_pass_identity(e,proof,backend);
 auto& t=*e->document_text_transaction_;const auto begin=t.passes_[proof->pass_].fragment_begin;
 // Callback-free: never use a stale A phase as final source authority.
 for(std::size_t i=0;i<proof->fragments_.size();++i){
  t.budget.normalized(1);const auto& f=proof->fragments_[i];
  if(f!=t.fragments_[begin+i]||f->normalization_pass!=proof->pass_)
   throw std::invalid_argument("DOCUMENT_FINAL_FRAGMENT");
  DocumentTextBudget* budget=nullptr;(void)slice_token(e,f,backend,budget);
 }
 std::uint64_t previous=0;bool any=false;
 for(const auto& event:proof->suppressed_){
  t.budget.normalized(1);
  if(!proof->promoted_attempt_||!event||event->operation_!=t.operation_||event->attempt_!=proof->promoted_attempt_||
     event->line_>=65536||!event->fragment_||event->fragment_->normalization_pass!=proof->pass_||
     event->fragment_->style_.white_space!=AETHER_WHITE_SPACE_NORMAL||event->fragment_->normalized.empty())
   throw std::invalid_argument("DOCUMENT_FINAL_SUPPRESSION");
  validate_soft_wrap_plan(e,event->plan_);
  if(event->plan_->attempt_!=event->attempt_||event->plan_->line_number_!=event->line_)throw std::invalid_argument("DOCUMENT_FINAL_SUPPRESSION_PLAN");
  const auto id=event->fragment_->occurrence;
  if(any&&id<=previous)throw std::invalid_argument("DOCUMENT_FINAL_SUPPRESSION_ORDER");any=true;previous=id;
  std::uint64_t work=1;for(auto n=t.suppressed_separators_.size();n;n>>=1)work+=2;t.budget.normalized(work);
  const auto found=t.suppressed_separators_.find(id);
  if(found==t.suppressed_separators_.end()||found->second!=event||id<begin||id-begin>=proof->fragments_.size()||proof->fragments_[id-begin]!=event->fragment_)
   throw std::invalid_argument("DOCUMENT_FINAL_SUPPRESSION_IDENTITY");
  for(unsigned char c:event->fragment_->normalized){t.budget.normalized(1);if(c!=0x20)throw std::invalid_argument("DOCUMENT_FINAL_SUPPRESSION_VISIBLE");}
 }
}
std::shared_ptr<const DocumentFinalPass> DocumentTextAccess::final_inventory(
 const aether_layout_engine_s* e,std::uint64_t pass_index,const DocumentTextBackend* backend){
 auto& budget=slice_entry(e,backend);auto& t=*e->document_text_transaction_;
 if(pass_index>=t.passes_.size()||!t.passes_[pass_index].complete)
  throw std::invalid_argument("DOCUMENT_FINAL_ROLE");
 charge_lookup(budget,t.promoted_passes_.size());
 if(auto found=t.promoted_passes_.find(pass_index);found!=t.promoted_passes_.end()){
  validate_final_inventory(e,found->second,backend);return found->second;
 }
 if(!t.passes_[pass_index].final_generation)throw std::invalid_argument("DOCUMENT_FINAL_ROLE");
 // Callback occurs before inventory capture; no provider/caller subset is used.
 {struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{t.slice_auditing_};
  t.slice_auditing_=true;t.check();}
 (void)slice_entry(e,backend);
 const auto& pass=t.passes_[pass_index];const auto count=pass.fragment_end-pass.fragment_begin;
 budget.bytes(1,sizeof(DocumentFinalPass)+2*sizeof(void*));
 budget.bytes(count,sizeof(std::shared_ptr<const DocumentTextFragment>));
 auto proof=std::shared_ptr<DocumentFinalPass>(new DocumentFinalPass);
 proof->operation_=t.operation_;proof->pass_=pass_index;proof->generation_=pass.final_generation;
 proof->fragments_.reserve(count);
 for(std::size_t i=pass.fragment_begin;i<pass.fragment_end;++i){budget.normalized(1);proof->fragments_.push_back(t.fragments_[i]);}
 validate_final_inventory(e,proof,backend);return proof;
}
std::uint64_t DocumentTextAccess::reserve_placement(const aether_layout_engine_s* e,
 const std::shared_ptr<const DocumentFinalPass>& proof,const DocumentTextBackend* backend,std::uint64_t glyphs){
 final_pass_identity(e,proof,backend);auto& t=*e->document_text_transaction_;
 if(!glyphs)throw std::invalid_argument("DOCUMENT_FINAL_EMPTY_PLACEMENT");
 if(t.next_placement_==std::numeric_limits<std::uint64_t>::max())throw std::length_error("DOCUMENT_FINAL_PLACEMENT_ID");
 const auto id=t.next_placement_++; // no reuse even if a subsequent budget/allocation fails
 t.budget.normalized(1);t.budget.placed(glyphs);return id;
}

std::shared_ptr<const int> DocumentTextAccess::final_fragment_identity(const aether_layout_engine_s* engine,
 const std::shared_ptr<const DocumentTextFragment>& fragment,const DocumentTextBackend* backend){
 auto& budget=slice_entry(engine,backend);auto& t=*engine->document_text_transaction_;
 budget.normalized(1);
 if(!fragment||fragment->operation_!=t.operation_||fragment->normalization_pass>=t.passes_.size())
  throw std::invalid_argument("DOCUMENT_INTERVAL_FINAL_ROLE");
 const auto& pass=t.passes_[fragment->normalization_pass];
 bool role=pass.final_generation!=0;
 if(!role&&t.drawing_mode_){
  charge_lookup(t.budget,t.promoted_passes_.size());
  const auto found=t.promoted_passes_.find(fragment->normalization_pass);
  if(found!=t.promoted_passes_.end()){final_pass_identity(engine,found->second,backend);role=true;}
 }
 if(!pass.complete||!role||fragment->occurrence<pass.fragment_begin||
    fragment->occurrence>=pass.fragment_end||fragment->occurrence>=t.fragments_.size()||
    t.fragments_[fragment->occurrence]!=fragment)
  throw std::invalid_argument("DOCUMENT_INTERVAL_FINAL_ROLE");
 return t.operation_; // identity only; the live A phase supplies source audit
}
DocumentTextBudget& DocumentTextAccess::slice_entry(const aether_layout_engine_s* e,const DocumentTextBackend* backend){
 if(!active(e))throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 auto& t=*e->document_text_transaction_;t.valid(); // thread first; no callback or mutation
 if(!t.active_||t.pass_active_||t.slice_auditing_||t.final_issuing_||!t.operation_||t.backend_.get()!=backend)
  throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 return t.budget;
}
std::shared_ptr<const int> DocumentTextAccess::slice_token(const aether_layout_engine_s* e,
 const std::shared_ptr<const DocumentTextFragment>& f,const DocumentTextBackend* backend,DocumentTextBudget*& budget){
 if(!active(e))throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 auto& t=*e->document_text_transaction_;t.valid();
 if(!t.active_||t.pass_active_||!f||t.backend_.get()!=backend||f->operation_!=t.operation_||
    f->normalization_pass>=t.passes_.size()||!t.passes_[f->normalization_pass].complete)
  throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 const auto& pass=t.passes_[f->normalization_pass];
 if(f->occurrence<pass.fragment_begin||f->occurrence>=pass.fragment_end||
    f->occurrence>=t.fragments_.size()||t.fragments_[f->occurrence]!=f)
  throw std::invalid_argument("DOCUMENT_SLICE_FRAGMENT");
 const auto& token=f->layout_owner_;
 if(!token||!token->alive||!token->attached||!token->owner)throw std::invalid_argument("DOCUMENT_SLICE_OWNER");
 auto* owner=static_cast<const aether_node_s*>(token->owner);
 auto* actual=owner->anonymous_flex_owner?owner->anonymous_flex_owner:owner;
 if(!t.styles_||owner->identity!=token||t.styles_->lookup_record_owner(actual)!=f->owner_||
    t.styles_->lookup_node_owner(owner)!=f->style_.style_owner()||t.styles_->lookup_measures_owner(owner)!=f->style_.measures_owner())
  throw std::invalid_argument("DOCUMENT_SLICE_STYLE");
 const auto& anchor=f->owner_;
 if(!anchor||!anchor->node_lifetime()||!anchor->node_lifetime()->alive||!anchor->node_lifetime()->attached||anchor->node_lifetime()->owner!=actual)
  throw std::invalid_argument("DOCUMENT_SLICE_ANCHOR");
 if(owner->anonymous_flex_owner&&(owner->parent!=actual||owner->style_origin!=actual||t.styles_->lookup_record_owner(owner)))
  throw std::invalid_argument("DOCUMENT_SLICE_ANONYMOUS_OWNER");
 // No callbacks in this full validation: never follow replacement pointers.
 for(const auto& source:f->sources_){
  t.budget.normalized(1);
  if(!source||source->operation_!=t.operation_||!source->layout_owner_||!source->layout_owner_->alive||!source->layout_owner_->attached||!source->layout_owner_->owner||
     source->ordinal>=t.sources_by_ordinal_.size()||t.sources_by_ordinal_[source->ordinal]!=source||
     !source->text_identity_||!source->text_identity_->alive||!source->text_identity_->attached||
     source->text_identity_->owner!=source->text_node_)throw std::invalid_argument("DOCUMENT_SLICE_SOURCE");
  const auto* node=source->text_node_;
  const auto* source_owner=static_cast<const aether_node_s*>(source->layout_owner_->owner);
  const auto* source_actual=source_owner->anonymous_flex_owner?source_owner->anonymous_flex_owner:source_owner;
  if(source_owner->identity!=source->layout_owner_||node->identity!=source->text_identity_||node->parent!=source_actual||node->type!=AETHER_NODE_TEXT)
   throw std::invalid_argument("DOCUMENT_SLICE_SOURCE");
  const auto& source_anchor=source->owner_;
  if(!source_anchor||!source_anchor->node_lifetime()||!source_anchor->node_lifetime()->alive||!source_anchor->node_lifetime()->attached||
     source_anchor->node_lifetime()->owner!=source_actual||t.styles_->lookup_record_owner(source_actual)!=source_anchor)
   throw std::invalid_argument("DOCUMENT_SLICE_SOURCE_ANCHOR");
  if(source_owner->anonymous_flex_owner){
   if(source_owner->parent!=source_actual||source_owner->style_origin!=source_actual||t.styles_->lookup_record_owner(source_owner))
    throw std::invalid_argument("DOCUMENT_SLICE_ANONYMOUS_OWNER");
   bool member=false;
   for(const auto* key:source_owner->children){t.budget.normalized(1);if(key==node)member=true;} // keys only, no new-pointer dereference
   if(!member)throw std::invalid_argument("DOCUMENT_SLICE_ANONYMOUS_SOURCE");
  }
  t.budget.normalized(source->decoded.size());
  if(node->text!=source->decoded)throw std::invalid_argument("DOCUMENT_SLICE_SOURCE_CHANGED");
 }
 budget=&t.budget;return t.operation_;
}

std::shared_ptr<const int> DocumentTextAccess::seal_slice_phase(const aether_layout_engine_s* e,
 std::span<const std::shared_ptr<const DocumentTextFragment>> fragments,const DocumentTextBackend* backend){
 if(!active(e))throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 auto& t=*e->document_text_transaction_;t.valid();
 if(t.slice_auditing_||fragments.empty()||fragments.size()>65536)throw std::invalid_argument("DOCUMENT_SLICE_PHASE");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}}restore{t.slice_auditing_};t.slice_auditing_=true;
 t.check(); // invalidates previous certification before invoking callback
 t.budget.bytes(1,sizeof(int)+2*sizeof(void*));auto phase=std::make_shared<const int>(0);
 for(const auto& f:fragments){DocumentTextBudget* budget=nullptr;(void)slice_token(e,f,backend,budget);}
 // Full audit above is callback-free, and this publication cannot throw.
 t.slice_phase_=phase;return phase;
}
void DocumentTextAccess::validate_slice_phase(const aether_layout_engine_s* e,const std::shared_ptr<const int>& phase,
 const std::shared_ptr<const DocumentTextFragment>& f,const DocumentTextBackend* backend,DocumentTextBudget*& budget){
 if(!active(e))throw std::invalid_argument("DOCUMENT_SLICE_OPERATION");
 auto& t=*e->document_text_transaction_;t.valid();
 if(t.slice_auditing_||!phase||t.slice_phase_!=phase||!t.active_||t.pass_active_||t.backend_.get()!=backend||
    !f||f->operation_!=t.operation_||f->normalization_pass>=t.passes_.size()||!t.passes_[f->normalization_pass].complete||
    f->occurrence>=t.fragments_.size()||t.fragments_[f->occurrence]!=f)
  throw std::invalid_argument("DOCUMENT_SLICE_PHASE_EXPIRED");
 const auto& token=f->layout_owner_;
 if(!token||!token->alive||!token->attached||!token->owner)throw std::invalid_argument("DOCUMENT_SLICE_OWNER");
 const auto* owner=static_cast<const aether_node_s*>(token->owner);
 if(owner->identity!=token||!t.styles_||t.styles_->lookup_node_owner(owner)!=f->style_.style_owner()||
    t.styles_->lookup_measures_owner(owner)!=f->style_.measures_owner())throw std::invalid_argument("DOCUMENT_SLICE_STYLE");
 t.budget.normalized(1);budget=&t.budget;
}

aether::rendering::text::Measurement DocumentTextAccess::measure(const aether_layout_engine_s* e,const std::shared_ptr<const DocumentTextFragment>& f,const BoxStyleSnapshot& style,aether_node_s* owner){
 collector_entry(e);
 check(e);auto& t=*e->document_text_transaction_;if(!t.active_||!f||f->normalization_pass>=t.passes_.size()||!t.passes_[f->normalization_pass].complete||f->operation_!=t.operation_||!owner||!same_text_style(f->style_,style))throw std::invalid_argument("DOCUMENT_TEXT_MEASURE_IDENTITY");if(!f->layout_owner_||!f->layout_owner_->alive||!f->layout_owner_->attached||f->layout_owner_->owner!=owner)throw std::invalid_argument("DOCUMENT_TEXT_MEASURE_OWNER");auto* actual=owner->anonymous_flex_owner?owner->anonymous_flex_owner:owner;if(!t.styles_||f->layout_owner_!=owner->identity||t.styles_->lookup_record_owner(actual)!=f->owner_||t.styles_->lookup_node_owner(owner)!=f->style_.style_owner()||t.styles_->lookup_measures_owner(owner)!=f->style_.measures_owner())throw std::invalid_argument("DOCUMENT_TEXT_MEASURE_OWNER");
 t.budget.request();auto result=t.backend_->measure(f,t.budget);t.check();if(!result.run||result.run->source!=f->normalized||result.metrics.width!=result.run->metrics.width||result.metrics.height!=result.run->metrics.height||result.metrics.baseline!=result.run->metrics.baseline)throw std::invalid_argument("DOCUMENT_TEXT_MEASURE_PRODUCT");return result;
}
namespace {
float interval_projection(double width){
 if(!std::isfinite(width)||width<0.||width>65536.)throw std::invalid_argument("DOCUMENT_INTERVAL_WIDTH");
 const float projected=static_cast<float>(width);
 if(!std::isfinite(projected))throw std::invalid_argument("DOCUMENT_INTERVAL_WIDTH");
 return projected;
}
}
DocumentConstructionInterval::DocumentConstructionInterval(std::shared_ptr<const DocumentTextFragment> fragment,
 std::shared_ptr<const aether::rendering::text::ShapedRun> run,
 std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable> table,
 std::size_t glyph_begin,std::size_t glyph_end,std::size_t byte_begin,std::size_t byte_end,double width)
 :fragment_(std::move(fragment)),run_(std::move(run)),table_(std::move(table)),
  glyph_begin_(glyph_begin),glyph_end_(glyph_end),byte_begin_(byte_begin),byte_end_(byte_end),
  exact_width_(width),projected_width_(interval_projection(width)){
 if(!fragment_||!run_||!table_||glyph_begin_>=glyph_end_||glyph_end_>run_->glyphs.size()||
    byte_begin_>=byte_end_||byte_end_>fragment_->normalized.size()||run_->source!=fragment_->normalized||
    !std::isfinite(run_->metrics.height)||run_->metrics.height<=0.f||run_->metrics.height>65536.f||
    !std::isfinite(run_->metrics.baseline)||run_->metrics.baseline<0.f||run_->metrics.baseline>run_->metrics.height)
  throw std::invalid_argument("DOCUMENT_INTERVAL_SOURCE");
}
DocumentTextInterval::DocumentTextInterval(std::shared_ptr<const DocumentTextFragment> fragment,
 std::shared_ptr<const aether::rendering::text::ShapedRun> run,
 std::shared_ptr<const aether::rendering::text::DocumentBoundaryTable> table,
 std::size_t glyph_begin,std::size_t glyph_end,std::size_t byte_begin,std::size_t byte_end,double width)
 :fragment_(std::move(fragment)),run_(std::move(run)),table_(std::move(table)),
  glyph_begin_(glyph_begin),glyph_end_(glyph_end),byte_begin_(byte_begin),byte_end_(byte_end),
  exact_width_(width),projected_width_(interval_projection(width)){
 if(!fragment_||!run_||!table_||glyph_begin_>=glyph_end_||glyph_end_>run_->glyphs.size()||
    byte_begin_>=byte_end_||byte_end_>fragment_->normalized.size()||run_->source!=fragment_->normalized||
    !std::isfinite(run_->metrics.height)||run_->metrics.height<=0.f||run_->metrics.height>65536.f||
    !std::isfinite(run_->metrics.baseline)||run_->metrics.baseline<0.f||run_->metrics.baseline>run_->metrics.height)
  throw std::invalid_argument("DOCUMENT_INTERVAL_SOURCE");
}
bool DocumentTextInterval::matches_source(const std::shared_ptr<const aether::rendering::text::ShapedRun>& run,std::string_view text)const noexcept{
 return run==run_&&fragment_&&byte_begin_<=byte_end_&&byte_end_<=fragment_->normalized.size()&&
  text==std::string_view(fragment_->normalized).substr(byte_begin_,byte_end_-byte_begin_);
}
std::pair<float,float> DocumentTextAccess::validate_admission_marker(const aether_layout_engine_s* engine,aether_node_s* owner,const BoxStyleSnapshot& style,bool parent_redundant){
 collector_entry(engine);auto& t=*engine->document_text_transaction_;t.budget.normalized(1);
 const auto record=t.styles_?t.styles_->lookup_record_owner(owner):aether::style::FrozenNodeRef{};
 if(!record||record->node()!=owner)throw std::invalid_argument("DOCUMENT_MARKER_OWNER");
 const auto token=record->node_lifetime();
 auto valid=[&]{return token&&token->alive&&token->attached&&token->owner==owner&&record->document_lifetime()==t.document_&&
  t.styles_&&t.styles_->lookup_record_owner(owner)==record&&t.styles_->lookup_node_owner(owner)==style.style_owner()&&t.styles_->lookup_measures_owner(owner)==style.measures_owner()&&
  style.style_owner()&&style.measures_owner()&&same_text_style(BoxStyleSnapshot(style.style_owner(),style.measures_owner()),style);};
 if(!valid())throw std::invalid_argument("DOCUMENT_MARKER_STYLE");
 // Capture the parent only after authenticating owner. A raw lookup precedes
 // any parent dereference, and both identities/relationship survive callbacks.
 auto* parent=parent_redundant?owner->parent:nullptr;
 const auto parent_record=parent?t.styles_->lookup_record_owner(parent):aether::style::FrozenNodeRef{};
 const auto parent_token=parent_record?parent_record->node_lifetime():std::shared_ptr<node_identity_s>{};
 const auto parent_style=parent?t.styles_->lookup_node_owner(parent):aether::style::FrozenStyleRef{};
 const auto parent_measures=parent?t.styles_->lookup_measures_owner(parent):aether::style::FrozenMeasuresRef{};
 auto parent_valid=[&]{return !parent_redundant||(parent_record&&parent_record->node()==parent&&parent_token&&parent_token->alive&&parent_token->attached&&parent_token->owner==parent&&
  parent_record->document_lifetime()==t.document_&&owner->parent==parent&&parent_style&&parent_measures&&t.styles_->lookup_record_owner(parent)==parent_record&&t.styles_->lookup_node_owner(parent)==parent_style&&t.styles_->lookup_measures_owner(parent)==parent_measures);};
 if(!parent_valid())throw std::invalid_argument("DOCUMENT_MARKER_PARENT");
 t.backend_->validate_style(record,style);t.check();
 if(!valid()||!parent_valid())throw std::invalid_argument("DOCUMENT_MARKER_STALE");
 const auto metrics=t.backend_->admission_metrics(style,t.budget);t.check();
 if(!valid()||!parent_valid())throw std::invalid_argument("DOCUMENT_MARKER_STALE");
 if(parent_redundant){
  BoxStyleSnapshot frozen_parent(parent_style,parent_measures);
  t.backend_->validate_style(parent_record,frozen_parent);t.check();
  if(!valid()||!parent_valid())throw std::invalid_argument("DOCUMENT_MARKER_STALE");
  const auto parent_metrics=t.backend_->admission_metrics(frozen_parent,t.budget);t.check();
  if(!valid()||!parent_valid())throw std::invalid_argument("DOCUMENT_MARKER_STALE");
  if(metrics!=parent_metrics)throw std::invalid_argument("DOCUMENT_MARKER_STRUT_UNSUPPORTED");
 }
 return metrics;
}
void DocumentTextAccess::validate_interval_geometry(const aether_layout_engine_s* engine,
 const aether_layout_box_s* box,const DocumentTextBackend* backend){
 // Caller has already authenticated registry membership/revision for box. This
 // full source audit has no callbacks and charges each traversed source/member.
 const auto& interval=box->document_interval;if(!interval)return;
 auto& budget=slice_entry(engine,backend);const auto& f=interval->fragment_;
 DocumentTextBudget* audited=nullptr;(void)slice_token(engine,f,backend,audited);
 auto& t=*engine->document_text_transaction_;
 (void)final_fragment_identity(engine,f,backend); // exact legacy or immutable promoted proof
 if(box->kind!=LAYOUT_BOX_TEXT||
    f->layout_owner_->owner!=box->element||!same_text_style(box->style,f->style_)||
    box->native_text.run!=interval->run_)
  throw std::invalid_argument("DOCUMENT_INTERVAL_IDENTITY");
 budget.normalized(box->text.size()+1);
 const auto source=std::string_view(f->normalized).substr(interval->byte_begin_,interval->byte_end_-interval->byte_begin_);
 if(box->text!=source)throw std::invalid_argument("DOCUMENT_INTERVAL_TEXT");
 const auto& r=box->content_box;
 auto bounded=[](float v){return std::isfinite(v)&&std::abs(double(v))<=65536.;};
 if(!bounded(r.x)||!bounded(r.y)||r.width!=interval->projected_width_||
    r.height!=interval->run_->metrics.height||!bounded(box->native_text.baseline)||
    box->native_text.baseline!=interval->run_->metrics.baseline)
  throw std::invalid_argument("DOCUMENT_INTERVAL_GEOMETRY");
 for(const auto rect:{box->margin_box,box->border_box,box->padding_box})
  if(rect.x!=r.x||rect.y!=r.y||rect.width!=r.width||rect.height!=r.height)
   throw std::invalid_argument("DOCUMENT_INTERVAL_TEXT_RECT");
 // No right-left reconstruction or Q: C3 uses exact_width_ for ledger endpoints
 // and checks raster domain separately after final absolute placement.
}
struct DocumentGeometryCertificate::Values {
 std::shared_ptr<const int> operation;
 LayoutAssetsRef assets;
 aether_layout_result_s* layout{};
 const aether_layout_box_s* root{};
 std::uint64_t revision{};
 aether_sizef viewport{};float root_font{};int overflow_x{},overflow_y{};
 bool records{},effective{},table_cells{};std::vector<const aether_node_s*> fixed;
 std::vector<const aether_layout_box_s*> allocations,paint;
 std::vector<std::pair<const aether_node_s*,const aether_layout_box_s*>> primary;
 struct Box {
  const aether_layout_box_s* key{};
  std::shared_ptr<node_identity_s> element_token,presentation_token,visual_token;
  aether_node_s* element{};
  aether_node_s* presentation_owner{};
  layout_box_kind_t kind{};
  BoxStyleSnapshot style{};
  std::vector<aether_layout_box_s*> children{};
  std::string text{};
  std::shared_ptr<const DocumentTextSource> document_source{};
  aether::rendering::text::Placement native_text{};
  std::shared_ptr<const DocumentTextInterval> document_interval;
  std::shared_ptr<const DocumentConstructionInterval> document_construction;
  std::optional<document_atomic_widths_s> document_atomic_widths;
  bool document_has_last_line{};
  double document_last_line_baseline{};
  std::uint64_t document_layout_attempt{};
  std::shared_ptr<const DocumentAtomicInlineEvent> document_atomic_inline;
  bool is_atomic_control{};
  bool is_initial_canvas{};
  std::string control_display_text{};
  bool control_shows_placeholder{};
  aether_scroll_metrics_t scroll{};
  bool visual_sample_valid{};
  bool visual_has_transform{};
  aether_transform_matrix visual_transform{};
  float visual_opacity{};
  int64_t visual_timestamp{};
  float collapsed_margin_before{};
  float collapsed_margin_after{};
  float own_relative_dx{};
  float own_relative_dy{};
  aether_thicknessf used_border{};
  aether_rectf margin_box{};
  aether_rectf border_box{};
  aether_rectf padding_box{};
  aether_rectf content_box{};
  layout_scroll_proxy_s scroll_proxy{};
  float scroll_flow_height{};
  float scroll_base_height{};
  uint8_t scroll_completion_flags{};
 };
 std::vector<Box> boxes;
 std::uint64_t captured_bytes{};
};
struct DocumentLayoutSnapshot::State : DocumentGeometryCertificate::Values {
 std::unique_ptr<LayoutOperationPin> pin;
};
DocumentGeometryCertificate::DocumentGeometryCertificate()=default;
DocumentGeometryCertificate::~DocumentGeometryCertificate()=default;
DocumentLayoutSnapshot::DocumentLayoutSnapshot()=default;
DocumentLayoutSnapshot::~DocumentLayoutSnapshot()=default;
std::shared_ptr<const DocumentLayoutSnapshot> DocumentTextAccess::capture_final_geometry(const aether_layout_engine_s* engine,aether_layout_result_s* layout,const DocumentTextBackend* backend){return capture_geometry(engine,layout,backend,false);}
std::shared_ptr<const DocumentLayoutSnapshot> DocumentTextAccess::capture_construction_geometry(const aether_layout_engine_s* engine,aether_layout_result_s* layout,const DocumentTextBackend* backend){
 auto& budget=slice_entry(engine,backend);(void)budget;auto& t=*engine->document_text_transaction_;
 if(!t.drawing_mode_||t.finalizing_||t.final_ready_||t.layout_completed_)throw std::invalid_argument("DOCUMENT_CONSTRUCTION_SNAPSHOT_STATE");
 return capture_geometry(engine,layout,backend,true);
}
std::shared_ptr<const DocumentLayoutSnapshot> DocumentTextAccess::capture_geometry(
 const aether_layout_engine_s* engine,aether_layout_result_s* layout,const DocumentTextBackend* backend,bool construction){
 auto& budget=slice_entry(engine,backend);auto& t=*engine->document_text_transaction_;
 const auto capture_start=budget.metadata_bytes;
 if(!layout||layout!=t.active_layout_||!layout->assets||layout->assets->base!=t.version_||layout->assets->document_lifetime!=t.document_)
  throw std::invalid_argument("DOCUMENT_GEOMETRY_LAYOUT");
 if(!layout->allocated_boxes.allocation_revision_valid())throw std::invalid_argument("DOCUMENT_GEOMETRY_REVISION");
 const auto count=layout->live_box_count;
 if(count>65536)throw std::length_error("DOCUMENT_GEOMETRY_COUNT");
 budget.bytes(1,sizeof(DocumentLayoutSnapshot)+2*sizeof(void*)+sizeof(DocumentLayoutSnapshot::State)+sizeof(LayoutOperationPin));
 auto out=std::shared_ptr<DocumentLayoutSnapshot>(new DocumentLayoutSnapshot);
 out->state_=std::make_unique<DocumentLayoutSnapshot::State>();auto& st=*out->state_;
 st.pin=std::make_unique<LayoutOperationPin>(layout);st.layout=layout;st.assets=layout->assets;st.operation=t.operation_;
 st.revision=layout->allocated_boxes.allocation_revision();st.root=layout->root;st.viewport=layout->viewport;
 st.root_font=layout->root_font_size;st.overflow_x=layout->viewport_overflow_x;st.overflow_y=layout->viewport_overflow_y;
 st.records=layout->records_boxes;st.effective=layout->effective_context_enabled;st.table_cells=layout->has_finalized_table_cells;
 // No callback in capture, including budget methods. Registry nodes are the
 // trusted allocation index; graph children are never dereferenced for lookup.
 budget.bytes(count,sizeof(const aether_layout_box_s*)+sizeof(DocumentLayoutSnapshot::State::Box));
 st.allocations.reserve(count);st.boxes.reserve(count);
 using Index=std::map<const aether_layout_box_s*,std::size_t>;Index index;
 budget.bytes(count,sizeof(Index::value_type)+4*sizeof(void*));
 auto lookup_cost=[&]{std::uint64_t work=1;for(auto n=count;n;n>>=1)work+=2;budget.normalized(work);};
 for(const auto* box:layout->allocated_boxes){
  if(st.allocations.size()>=count||!box)throw std::invalid_argument("DOCUMENT_GEOMETRY_REGISTRY");
  lookup_cost();if(!index.emplace(box,st.allocations.size()).second)throw std::invalid_argument("DOCUMENT_GEOMETRY_DUPLICATE");
  st.allocations.push_back(box);
 }
 if(st.allocations.size()!=count)throw std::invalid_argument("DOCUMENT_GEOMETRY_COUNT");
 auto member=[&](const aether_layout_box_s* key){lookup_cost();auto it=index.find(key);if(it==index.end())throw std::invalid_argument("DOCUMENT_GEOMETRY_FOREIGN_BOX");return it->second;};
 if(st.root)(void)member(st.root);else if(count&&!construction)throw std::invalid_argument("DOCUMENT_GEOMETRY_ROOT");
 auto node_token=[&](const aether_node_s* key)->std::shared_ptr<node_identity_s>{
  if(!key)return {};
  budget.normalized(1);
  if(auto record=t.version_->backing()->find_owner(key)){
   const auto token=record->node_lifetime();
   if(record->node()!=key||record->document_lifetime()!=t.document_||!token||!token->alive||!token->attached||token->owner!=key)
    throw std::invalid_argument("DOCUMENT_GEOMETRY_NODE");
   if(key->identity!=token)throw std::invalid_argument("DOCUMENT_GEOMETRY_NODE");return token;
  }
  if(st.assets->synthetic)if(const auto* entry=st.assets->synthetic->find(key)){
   if(entry->node()!=key)throw std::invalid_argument("DOCUMENT_GEOMETRY_SYNTHETIC");
   const auto token=entry->node()->identity;
   if(!token||!token->alive||!token->attached||token->owner!=key)throw std::invalid_argument("DOCUMENT_GEOMETRY_SYNTHETIC");return token;
  }
  throw std::invalid_argument("DOCUMENT_GEOMETRY_NODE");
 };
 std::size_t edges=0;
 for(const auto* box:st.allocations){
  // Membership is already proven by registry collection, before first deref.
  budget.normalized(1);if(box->owner!=layout||!box->block_static_anchors.empty())throw std::invalid_argument("DOCUMENT_GEOMETRY_OWNER");
  if(box->children.size()>65535-edges)throw std::length_error("DOCUMENT_GEOMETRY_EDGES");edges+=box->children.size();
  budget.bytes(box->children.size(),sizeof(aether_layout_box_s*));budget.bytes(box->text.size());budget.bytes(box->control_display_text.size());
  DocumentLayoutSnapshot::State::Box r;r.key=box;r.element_token=node_token(box->element);r.presentation_token=node_token(box->presentation_owner);r.visual_token=box->visual_identity.lock();
  for(const auto* child:box->children)(void)member(child);
  r.element=box->element;
  r.presentation_owner=box->presentation_owner;
  r.kind=box->kind;
  r.style=box->style;
  r.children=box->children;
  r.text=box->text;
  r.document_source=box->document_source;
  r.native_text=box->native_text;
  validate_interval_geometry(engine,box,backend);
  r.document_interval=box->document_interval;r.document_construction=box->document_construction;
  r.document_atomic_widths=box->document_atomic_widths;
  r.document_has_last_line=box->document_has_last_line;r.document_last_line_baseline=box->document_last_line_baseline;
  r.document_layout_attempt=box->document_layout_attempt;r.document_atomic_inline=box->document_atomic_inline;
  r.is_atomic_control=box->is_atomic_control;
  r.is_initial_canvas=box->is_initial_canvas;
  r.control_display_text=box->control_display_text;
  r.control_shows_placeholder=box->control_shows_placeholder;
  r.scroll=box->scroll;
  r.visual_sample_valid=box->visual_sample_valid;
  r.visual_has_transform=box->visual_has_transform;
  r.visual_transform=box->visual_transform;
  r.visual_opacity=box->visual_opacity;
  r.visual_timestamp=box->visual_timestamp;
  r.collapsed_margin_before=box->collapsed_margin_before;
  r.collapsed_margin_after=box->collapsed_margin_after;
  r.own_relative_dx=box->own_relative_dx;
  r.own_relative_dy=box->own_relative_dy;
  r.used_border=box->used_border;
  r.margin_box=box->margin_box;
  r.border_box=box->border_box;
  r.padding_box=box->padding_box;
  r.content_box=box->content_box;
  r.scroll_proxy=box->scroll_proxy;
  r.scroll_flow_height=box->scroll_flow_height;
  r.scroll_base_height=box->scroll_base_height;
  r.scroll_completion_flags=box->scroll_completion_flags;
  st.boxes.push_back(std::move(r));
 }
 // Validate the frozen adjacency, not live children. Each node has <=1 parent;
 // all components (including detached provisional boxes) must be acyclic.
 budget.bytes(count,sizeof(std::size_t)+sizeof(unsigned char));std::vector<std::size_t> parents(count);std::vector<unsigned char> color(count);
 for(const auto& r:st.boxes)for(auto* child:r.children){budget.normalized(1);if(++parents[member(child)]>1)throw std::invalid_argument("DOCUMENT_GEOMETRY_MULTIPARENT");}
 if(st.root&&parents[member(st.root)])throw std::invalid_argument("DOCUMENT_GEOMETRY_ROOT_PARENT");
 struct Walk{std::size_t index,next,depth;};budget.bytes(count,sizeof(Walk));std::vector<Walk> stack;stack.reserve(count);
 for(std::size_t i=0;i<count;++i){if(parents[i])continue;if(color[i])throw std::invalid_argument("DOCUMENT_GEOMETRY_DUPLICATE_ROOT");color[i]=1;stack.push_back({i,0,1});
  while(!stack.empty()){budget.normalized(1);auto& frame=stack.back();const auto& children=st.boxes[frame.index].children;
   if(frame.depth>256)throw std::length_error("DOCUMENT_GEOMETRY_DEPTH");
   if(frame.next==children.size()){color[frame.index]=2;stack.pop_back();continue;}
   const auto child=member(children[frame.next++]);if(color[child]==1)throw std::invalid_argument("DOCUMENT_GEOMETRY_CYCLE");
   if(color[child]==0){color[child]=1;stack.push_back({child,0,frame.depth+1});}
  }
 }
 for(const auto visited:color){budget.normalized(1);if(visited!=2)throw std::invalid_argument("DOCUMENT_GEOMETRY_CYCLE");}
 if(layout->paint_order.size()>count||layout->primary_boxes.size()>count)throw std::length_error("DOCUMENT_GEOMETRY_INDEX");
 budget.bytes(layout->paint_order.size(),sizeof(const aether_layout_box_s*));st.paint.reserve(layout->paint_order.size());
 budget.normalized(count);std::fill(color.begin(),color.end(),0);
 for(const auto* box:layout->paint_order){const auto index=member(box);if(color[index]++)throw std::invalid_argument("DOCUMENT_GEOMETRY_DUPLICATE_PAINT");st.paint.push_back(box);}
 budget.bytes(layout->primary_boxes.size(),sizeof(decltype(st.primary)::value_type));st.primary.reserve(layout->primary_boxes.size());
 for(const auto& pair:layout->primary_boxes){(void)node_token(pair.first);(void)member(pair.second);st.primary.push_back(pair);}
 if(layout->effective_fixed_container_nodes.size()>16384)throw std::length_error("DOCUMENT_GEOMETRY_FIXED_COUNT");
 budget.bytes(layout->effective_fixed_container_nodes.size(),sizeof(const aether_node_s*));st.fixed.reserve(layout->effective_fixed_container_nodes.size());
 for(const auto* key:layout->effective_fixed_container_nodes){(void)node_token(key);st.fixed.push_back(key);}
 st.captured_bytes=budget.metadata_bytes-capture_start;
 validate_final_geometry(engine,out,backend);return out;
}
void DocumentTextAccess::validate_final_geometry(const aether_layout_engine_s* engine,
 const std::shared_ptr<const DocumentLayoutSnapshot>& snapshot,const DocumentTextBackend* backend){
 (void)slice_entry(engine,backend);
 if(!snapshot)throw std::invalid_argument("DOCUMENT_GEOMETRY_SNAPSHOT");
 validate_final_geometry(engine,*snapshot,backend);
}
void DocumentTextAccess::validate_final_geometry(const aether_layout_engine_s* engine,
 const DocumentLayoutSnapshot& snapshot,const DocumentTextBackend* backend){
 auto& budget=slice_entry(engine,backend);auto& t=*engine->document_text_transaction_;
 if(!snapshot.state_)throw std::invalid_argument("DOCUMENT_GEOMETRY_SNAPSHOT");
 const auto& st=*snapshot.state_;auto* layout=t.active_layout_;
 if(st.operation!=t.operation_||!layout||layout!=st.layout||!st.pin||st.pin->geometry()!=layout||layout->assets!=st.assets)
  throw std::invalid_argument("DOCUMENT_GEOMETRY_OPERATION");
 compare_geometry_values(engine,st,backend,budget,true);
}
void DocumentTextAccess::compare_geometry_values(const aether_layout_engine_s* engine,
 const DocumentGeometryCertificate::Values& st,const DocumentTextBackend* backend,DocumentTextBudget& budget,bool source_audit){
 auto* layout=st.layout;
 // Sticky revision and pinned result identity precede ALL saved-box dereferences.
 if(!layout->allocated_boxes.allocation_revision_valid()||layout->allocated_boxes.allocation_revision()!=st.revision)
  throw std::invalid_argument("DOCUMENT_GEOMETRY_REVISION");
 if(layout->root!=st.root||layout->live_box_count!=st.allocations.size()||layout->viewport.width!=st.viewport.width||layout->viewport.height!=st.viewport.height||
    layout->root_font_size!=st.root_font||layout->viewport_overflow_x!=st.overflow_x||layout->viewport_overflow_y!=st.overflow_y||layout->primary_boxes.size()!=st.primary.size()||layout->paint_order.size()!=st.paint.size()||layout->records_boxes!=st.records||layout->effective_context_enabled!=st.effective||layout->has_finalized_table_cells!=st.table_cells||layout->effective_fixed_container_nodes.size()!=st.fixed.size())
  throw std::invalid_argument("DOCUMENT_GEOMETRY_LAYOUT_CHANGED");
 std::size_t i=0;for(const auto* key:layout->allocated_boxes){budget.normalized(1);if(i>=st.allocations.size()||st.allocations[i++]!=key)throw std::invalid_argument("DOCUMENT_GEOMETRY_REGISTRY_CHANGED");}
 if(i!=st.allocations.size())throw std::invalid_argument("DOCUMENT_GEOMETRY_REGISTRY_CHANGED");
 auto node_same=[](const aether_node_s* key,const std::shared_ptr<node_identity_s>& token){return !key?!token:token&&token->alive&&token->attached&&token->owner==key;};
 auto rect=[](aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;};
 auto real=[](float a,float b){return a==b||(std::isnan(a)&&std::isnan(b));};
 for(const auto& r:st.boxes){
  budget.normalized(1+r.children.size()+r.text.size()+r.control_display_text.size());const auto* box=r.key;
  if(box->owner!=layout||!box->block_static_anchors.empty()||!node_same(r.element,r.element_token)||!node_same(r.presentation_owner,r.presentation_token))throw std::invalid_argument("DOCUMENT_GEOMETRY_LIFETIME");
  if((r.element&&r.element->identity!=r.element_token)||(r.presentation_owner&&r.presentation_owner->identity!=r.presentation_token))throw std::invalid_argument("DOCUMENT_GEOMETRY_NODE_CHANGED");
  if(box->element!=r.element)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->presentation_owner!=r.presentation_owner)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->kind!=r.kind)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->children!=r.children)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->text!=r.text)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->document_source!=r.document_source)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->document_construction!=r.document_construction)throw std::invalid_argument("DOCUMENT_CONSTRUCTION_CHANGED");
  if(box->document_atomic_widths!=r.document_atomic_widths||box->document_has_last_line!=r.document_has_last_line||box->document_last_line_baseline!=r.document_last_line_baseline||
     box->document_layout_attempt!=r.document_layout_attempt||box->document_atomic_inline!=r.document_atomic_inline)
   throw std::invalid_argument("DOCUMENT_ATOMIC_GEOMETRY_CHANGED");
  if(box->document_interval!=r.document_interval)throw std::invalid_argument("DOCUMENT_GEOMETRY_INTERVAL_CHANGED");
  if(source_audit){validate_interval_geometry(engine,box,backend);if(r.document_atomic_inline)validate_atomic_event(engine,r.document_atomic_inline);}
  if(box->is_atomic_control!=r.is_atomic_control)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->is_initial_canvas!=r.is_initial_canvas)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->control_display_text!=r.control_display_text)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->control_shows_placeholder!=r.control_shows_placeholder)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_sample_valid!=r.visual_sample_valid)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_has_transform!=r.visual_has_transform)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_opacity!=r.visual_opacity)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->visual_timestamp!=r.visual_timestamp)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->collapsed_margin_before!=r.collapsed_margin_before)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->collapsed_margin_after!=r.collapsed_margin_after)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->own_relative_dx!=r.own_relative_dx)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->own_relative_dy!=r.own_relative_dy)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->scroll_flow_height!=r.scroll_flow_height)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->scroll_completion_flags!=r.scroll_completion_flags)throw std::invalid_argument("DOCUMENT_GEOMETRY_CHANGED");
  if(box->style.style_owner()!=r.style.style_owner()||box->style.measures_owner()!=r.style.measures_owner()||!native_text_detail::scalars_equal(box->style.scalars(),r.style.scalars())||box->native_text.run!=r.native_text.run||box->native_text.baseline!=r.native_text.baseline||box->visual_identity.lock()!=r.visual_token)throw std::invalid_argument("DOCUMENT_GEOMETRY_STYLE");
  for(auto pair:{std::pair{box->margin_box,r.margin_box},std::pair{box->border_box,r.border_box},std::pair{box->padding_box,r.padding_box},std::pair{box->content_box,r.content_box}})if(!rect(pair.first,pair.second))throw std::invalid_argument("DOCUMENT_GEOMETRY_RECT");
  const auto& a=box->visual_transform;const auto& b=r.visual_transform;
  if(a.m11!=b.m11||a.m12!=b.m12||a.m21!=b.m21||a.m22!=b.m22||a.dx!=b.dx||a.dy!=b.dy||
     box->scroll.scrollable_overflow_x!=r.scroll.scrollable_overflow_x||box->scroll.scrollable_overflow_y!=r.scroll.scrollable_overflow_y||box->scroll.scroll_offset_x!=r.scroll.scroll_offset_x||box->scroll.scroll_offset_y!=r.scroll.scroll_offset_y||
     box->scroll_proxy.left!=r.scroll_proxy.left||box->scroll_proxy.right!=r.scroll_proxy.right||box->scroll_proxy.reference_content_x!=r.scroll_proxy.reference_content_x||box->scroll_proxy.valid!=r.scroll_proxy.valid||
     box->used_border.left!=r.used_border.left||box->used_border.top!=r.used_border.top||box->used_border.right!=r.used_border.right||box->used_border.bottom!=r.used_border.bottom||!real(box->scroll_base_height,r.scroll_base_height))throw std::invalid_argument("DOCUMENT_GEOMETRY_PROXY");
 }
 for(std::size_t n=0;n<st.paint.size();++n){budget.normalized(1);if(layout->paint_order[n]!=st.paint[n])throw std::invalid_argument("DOCUMENT_GEOMETRY_PAINT_ORDER");}
 for(const auto* key:st.fixed){budget.normalized(1);if(!layout->effective_fixed_container_nodes.count(const_cast<aether_node_s*>(key)))throw std::invalid_argument("DOCUMENT_GEOMETRY_FIXED");}
 for(const auto& pair:st.primary){budget.normalized(1);auto found=layout->primary_boxes.find(const_cast<aether_node_s*>(pair.first));if(found==layout->primary_boxes.end()||found->second!=pair.second)throw std::invalid_argument("DOCUMENT_GEOMETRY_PRIMARY");}
}

std::shared_ptr<const int> DocumentTextAccess::drawing_paint_token(const aether_layout_engine_s* engine,
 const aether_layout_result_s* layout,const DocumentTextBackend* backend){
 rethrow_failure(engine);auto& t=*engine->document_text_transaction_;
 if(!t.drawing_mode_||!t.final_geometry_mode_||!t.layout_completed_||t.active_||t.pass_active_||
    !layout||layout!=t.completed_layout_||layout->assets!=t.completed_assets_||!layout->assets->structure_valid()||
    t.backend_.get()!=backend||!t.completed_product_||!t.completed_selection_||!t.completed_geometry_)
  throw std::invalid_argument("DOCUMENT_DRAWING_PAINT_CERTIFICATE");
 const auto& values=*t.completed_geometry_->values_;
 if(values.operation!=t.operation_||values.layout!=layout||values.assets!=t.completed_assets_)
  throw std::invalid_argument("DOCUMENT_DRAWING_PAINT_IDENTITY");
 compare_geometry_values(engine,values,backend,t.budget,false);
 return t.operation_;
}
DocumentTextBudget& DocumentTextAccess::drawing_paint_budget(const aether_layout_engine_s* engine,
 const aether_layout_result_s* layout,const DocumentTextBackend* backend){
 (void)drawing_paint_token(engine,layout,backend);return engine->document_text_transaction_->budget;
}
std::shared_ptr<const DocumentFinalizationProduct> DocumentTextAccess::drawing_product(const aether_layout_engine_s* engine,
 const aether_layout_result_s* layout,const DocumentTextBackend* backend){
 (void)drawing_paint_token(engine,layout,backend);return engine->document_text_transaction_->completed_product_;
}

DocumentTextBudget& DocumentTextAccess::final_stage_entry(const aether_layout_engine_s* engine,
 const DocumentLayoutSnapshot& snapshot,const DocumentTextBackend* backend){
 auto& budget=slice_entry(engine,backend);const auto& t=*engine->document_text_transaction_;
 if(!t.final_geometry_mode_||!t.finalizing_)throw std::invalid_argument("DOCUMENT_FINAL_STAGE_NOT_ACTIVE");
 validate_final_geometry(engine,snapshot,backend);return budget;
}
std::vector<DocumentFinalIntervalPlacement> DocumentTextAccess::final_snapshot_intervals(
 const aether_layout_engine_s* engine,const DocumentLayoutSnapshot& snapshot,const DocumentTextBackend* backend){
 auto& budget=final_stage_entry(engine,snapshot,backend);const auto& st=*snapshot.state_;
 const auto work=[](std::size_t n){std::uint64_t w=1;for(;n;n>>=1)w+=2;return w;};
 budget.bytes(st.boxes.size(),sizeof(std::pair<const aether_layout_box_s* const,std::size_t>)+4*sizeof(void*)+sizeof(std::size_t)+sizeof(DocumentFinalIntervalPlacement));
 std::map<const aether_layout_box_s*,std::size_t> index;
 for(std::size_t i=0;i<st.boxes.size();++i){budget.normalized(work(st.boxes.size()));index.emplace(st.boxes[i].key,i);}
 std::vector<std::size_t> stack;stack.reserve(st.boxes.size());
 std::vector<DocumentFinalIntervalPlacement> out;out.reserve(st.boxes.size());
 auto member=[&](const aether_layout_box_s* key){budget.normalized(work(index.size()));auto it=index.find(key);if(it==index.end())throw std::invalid_argument("DOCUMENT_FINAL_GRAPH");return it->second;};
 if(st.root)stack.push_back(member(st.root));
 while(!stack.empty()){
  const auto i=stack.back();stack.pop_back();const auto& b=st.boxes[i];budget.normalized(1+b.children.size());
  for(const auto* child:b.children)stack.push_back(member(child)); // frozen keys only
  if(b.kind!=LAYOUT_BOX_TEXT||b.text.empty())continue;
  if(!b.document_interval)throw std::invalid_argument("DOCUMENT_FINAL_MISSING_INTERVAL");
  DocumentFinalIntervalPlacement r{b.key,b.document_interval,double(b.content_box.x),double(b.content_box.y),
   b.document_interval->exact_width_,double(b.content_box.height),double(b.content_box.y)+double(b.native_text.baseline)};
  for(double v:{r.x,r.y,r.width,r.height,r.baseline})if(!std::isfinite(v)||std::abs(v)>65536.)throw std::invalid_argument("DOCUMENT_FINAL_PLACEMENT_BOUNDS");
  out.push_back(std::move(r));
 }
 return out;
}

void DocumentTextAccess::promote_drawing_geometry(aether_layout_engine_s* engine,aether_layout_result_s* layout){
 if(!drawing_mode(engine))throw std::invalid_argument("DOCUMENT_DRAWING_MODE");
 auto& t=*engine->document_text_transaction_;
 if(t.finalizing_||t.final_ready_||t.layout_completed_)throw std::invalid_argument("DOCUMENT_PROMOTION_REENTRY");
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{t.finalizing_};t.finalizing_=true;
 // Shared snapshot machinery freezes construction identity as well as geometry;
 // this local snapshot is NOT a final interval/coverage certificate. Its pin dies
 // before every caller-side delete on error. Final snapshot is taken separately.
 const auto snapshot=capture_final_geometry(engine,layout,t.backend_.get());
 const auto& st=*snapshot->state_;auto& budget=t.budget;
 const auto lookup=[](std::size_t n){std::uint64_t w=1;for(;n;n>>=1)w+=2;return w;};
 budget.bytes(st.boxes.size(),sizeof(std::pair<const aether_layout_box_s* const,std::size_t>)+4*sizeof(void*));
 std::map<const aether_layout_box_s*,std::size_t> index;
 for(std::size_t i=0;i<st.boxes.size();++i){budget.normalized(lookup(st.boxes.size()));index.emplace(st.boxes[i].key,i);}
 budget.bytes(st.boxes.size(),2*sizeof(std::size_t)+sizeof(std::shared_ptr<const DocumentConstructionInterval>));
 std::vector<std::size_t> pending,selected;pending.reserve(st.boxes.size());selected.reserve(st.boxes.size());
 std::vector<std::shared_ptr<const DocumentConstructionInterval>> sources;sources.reserve(st.boxes.size());
 const auto member=[&](const aether_layout_box_s* key){budget.normalized(lookup(index.size()));auto i=index.find(key);if(i==index.end())throw std::invalid_argument("DOCUMENT_PROMOTION_GRAPH");return i->second;};
 budget.bytes(t.atomic_units_.size(),sizeof(const DocumentAtomicInlineEvent*)+4*sizeof(void*));
 std::set<const DocumentAtomicInlineEvent*> seen_atoms;
 if(st.root)pending.push_back(member(st.root));
 while(!pending.empty()){
  const auto i=pending.back();pending.pop_back();const auto& box=st.boxes[i];budget.normalized(1+box.children.size());
  for(auto* child:box.children)pending.push_back(member(child));
  if(box.kind==LAYOUT_BOX_ELEMENT&&box.style.display==AETHER_DISPLAY_INLINE_BLOCK){
   const auto& event=box.document_atomic_inline;validate_atomic_event(engine,event);
   if(event->box_!=box.key||event->owner_!=box.element||event->child_attempt_!=box.document_layout_attempt||
      !box.document_has_last_line||event->projected_width_!=box.margin_box.width||event->width_!=(box.document_atomic_widths?box.document_atomic_widths->margin:double(box.margin_box.width))||event->height_!=double(box.margin_box.height)||
      event->baseline_!=double(box.content_box.y)-box.margin_box.y+box.document_last_line_baseline||
      !t.attempt_layout_->selected_attempt(engine,event->parent_attempt_,budget)||!t.attempt_layout_->selected_attempt(engine,event->child_attempt_,budget))
    throw std::invalid_argument("DOCUMENT_ATOMIC_FINAL_GEOMETRY");
   charge_lookup(budget,seen_atoms.size());if(!seen_atoms.insert(event.get()).second)throw std::invalid_argument("DOCUMENT_ATOMIC_FINAL_DUPLICATE");
  }else if(box.document_atomic_inline)throw std::invalid_argument("DOCUMENT_ATOMIC_FINAL_KIND");
  if(box.kind!=LAYOUT_BOX_TEXT||box.text.empty())continue;
  if(!box.document_construction||box.document_interval)throw std::invalid_argument("DOCUMENT_PROMOTION_TRANSPORT");
  const auto& transport=box.document_construction;
  if(box.native_text.run!=transport->run_||box.content_box.width!=transport->projected_width_||
     box.text.size()!=transport->byte_end_-transport->byte_begin_||
     transport->fragment_->normalized.compare(transport->byte_begin_,box.text.size(),box.text)!=0)
   throw std::invalid_argument("DOCUMENT_PROMOTION_GEOMETRY");
  budget.normalized(box.text.size()+1);selected.push_back(i);sources.push_back(transport);
 }
 for(const auto& [key,event]:t.atomic_units_){budget.normalized(1);if(t.attempt_layout_->selected_attempt(engine,event->parent_attempt_,budget)){
  validate_atomic_event(engine,event);charge_lookup(budget,seen_atoms.size());if(!seen_atoms.contains(event.get()))throw std::invalid_argument("DOCUMENT_ATOMIC_FINAL_OMISSION");
 }}
 promote_selected_passes(engine);
 auto intervals=t.backend_->promote_constructions(engine,sources);
 validate_final_geometry(engine,snapshot,t.backend_.get());
 if(intervals.size()!=sources.size())throw std::invalid_argument("DOCUMENT_PROMOTION_COUNT");
 for(std::size_t i=0;i<sources.size();++i){budget.normalized(1);
  const auto& a=sources[i];const auto& b=intervals[i];
  if(!b||a->fragment_!=b->fragment_||a->run_!=b->run_||a->table_!=b->table_||
     a->glyph_begin_!=b->glyph_begin_||a->glyph_end_!=b->glyph_end_||a->byte_begin_!=b->byte_begin_||
     a->byte_end_!=b->byte_end_||a->exact_width_!=b->exact_width_||a->projected_width_!=b->projected_width_)
   throw std::invalid_argument("DOCUMENT_PROMOTION_MAPPING");
  (void)final_fragment_identity(engine,b->fragment_,t.backend_.get());
 }
 // Last full audit after all backend callbacks/allocation. No callback, throw,
 // allocation or new pointer traversal after it and before all replacements.
 validate_final_geometry(engine,snapshot,t.backend_.get());t.attempt_layout_->audit_selection(engine);
 for(std::size_t i=0;i<selected.size();++i){
  auto* box=const_cast<aether_layout_box_s*>(st.boxes[selected[i]].key);
  box->document_interval=std::move(intervals[i]);box->document_construction.reset();
 }
}

std::unique_ptr<DocumentFinalizationPending> DocumentTextAccess::prepare_final_layout(
 aether_layout_engine_s* engine,aether_layout_result_s* layout){
 if(!active(engine))throw std::invalid_argument("DOCUMENT_FINAL_MISSING");
 auto& t=*engine->document_text_transaction_;t.valid();
 if(!t.final_geometry_mode_||t.finalizing_||t.final_ready_||t.layout_completed_)
  throw std::invalid_argument("DOCUMENT_FINAL_STAGE_STATE");
 auto& budget=slice_entry(engine,t.backend_.get());
 struct Restore{bool& flag;~Restore()noexcept{flag=false;}} restore{t.finalizing_};t.finalizing_=true;
 // The snapshot and pin are local to this helper, destroyed on every return or
 // unwind BEFORE engine deletes result. Pending/product cannot retain them.
 const auto snapshot=capture_final_geometry(engine,layout,t.backend_.get());
 const auto& st=*snapshot->state_;
 budget.bytes(t.passes_.size(),sizeof(std::shared_ptr<const DocumentFinalPass>));
 std::vector<std::shared_ptr<const DocumentFinalPass>> proofs;proofs.reserve(t.passes_.size());
 const auto pass_count=t.passes_.size();
 for(std::size_t i=0;i<pass_count;++i){
  // Former complete_layout callbacks occur here, before final certification.
  t.check();validate_final_geometry(engine,snapshot,t.backend_.get());
  budget.normalized(1);
  if(t.passes_.size()!=pass_count||!t.passes_[i].complete)throw std::invalid_argument("DOCUMENT_FINAL_INCOMPLETE_PASS");
  charge_lookup(t.budget,t.promoted_passes_.size());
  if(t.passes_[i].final_generation||t.promoted_passes_.count(i)){
   auto proof=final_inventory(engine,i,t.backend_.get());
   validate_final_geometry(engine,snapshot,t.backend_.get());proofs.push_back(std::move(proof));
  }
 }
 // Backend borrows only a core-owned immutable vector/snapshot. Reentry into
 // collector mutation is blocked; terminal finish/abort makes validation fail.
 auto product=t.backend_->stage_final_geometry(engine,*snapshot,proofs,budget);
 validate_final_geometry(engine,snapshot,t.backend_.get());
 if(!product||product->passes_.size()!=proofs.size())throw std::invalid_argument("DOCUMENT_FINAL_PRODUCT_INVENTORY");
 for(std::size_t i=0;i<proofs.size();++i){budget.normalized(1);
  if(product->passes_[i].proof!=proofs[i]||!product->passes_[i].partition)
   throw std::invalid_argument("DOCUMENT_FINAL_PRODUCT_PROOF");
 }
 // This checks core inventory and actual text coverage; the separate provider
 // issuer must also authenticate opaque partitions against these EXACT proofs.
 std::size_t fragments=0;
 for(const auto& proof:proofs){
  if(proof->fragments_.size()>65536-fragments)throw std::length_error("DOCUMENT_FINAL_FRAGMENT_CAP");
  fragments+=proof->fragments_.size();
 }
 const auto lookup_work=[](std::size_t count){std::uint64_t n=1;for(;count;count>>=1)n+=2;return n;};
 budget.bytes(fragments,sizeof(std::pair<const std::uint64_t,std::shared_ptr<const DocumentTextFragment>>)+4*sizeof(void*));
 std::map<std::uint64_t,std::shared_ptr<const DocumentTextFragment>> required;
 for(const auto& proof:proofs)for(const auto& f:proof->fragments_){
  budget.normalized(lookup_work(fragments));
  if(!f||!required.emplace(f->occurrence,f).second)throw std::invalid_argument("DOCUMENT_FINAL_DUPLICATE_FRAGMENT");
 }
 budget.bytes(st.boxes.size(),sizeof(std::pair<const aether_layout_box_s* const,std::size_t>)+4*sizeof(void*));
 std::map<const aether_layout_box_s*,std::size_t> index;
 for(std::size_t i=0;i<st.boxes.size();++i){budget.normalized(lookup_work(st.boxes.size()));index.emplace(st.boxes[i].key,i);}
 struct Range{std::uint64_t fragment;std::size_t begin,end;};
 std::size_t disposition_count=0;
 for(const auto& proof:proofs){if(proof->suppressed_.size()>65536-disposition_count)throw std::length_error("DOCUMENT_DISPOSITION_CAP");disposition_count+=proof->suppressed_.size();}
 budget.bytes(st.boxes.size(),sizeof(std::size_t));budget.bytes(st.boxes.size()+disposition_count,sizeof(Range));
 std::vector<std::size_t> stack;stack.reserve(st.boxes.size());
 std::vector<Range> ranges;ranges.reserve(st.boxes.size()+disposition_count);
 auto node_index=[&](const aether_layout_box_s* key){budget.normalized(lookup_work(index.size()));auto i=index.find(key);if(i==index.end())throw std::invalid_argument("DOCUMENT_FINAL_GRAPH");return i->second;};
 if(st.root)stack.push_back(node_index(st.root));
 while(!stack.empty()){
  const auto i=stack.back();stack.pop_back();const auto& box=st.boxes[i];budget.normalized(1+box.children.size());
  // Owned forest was checked by snapshot. This traversal follows keys in the
  // frozen index, never new pointers or callback-mutated live children.
  for(const auto* child:box.children)stack.push_back(node_index(child));
  if(box.kind!=LAYOUT_BOX_TEXT)continue;
  if(box.text.empty()){if(box.document_interval)throw std::invalid_argument("DOCUMENT_FINAL_EMPTY_INTERVAL");continue;}
  const auto& interval=box.document_interval;
  if(!interval)throw std::invalid_argument("DOCUMENT_FINAL_MISSING_INTERVAL"); // clones cannot fall back
  budget.normalized(lookup_work(required.size()));const auto f=required.find(interval->fragment_->occurrence);
  if(f==required.end()||f->second!=interval->fragment_)throw std::invalid_argument("DOCUMENT_FINAL_FOREIGN_INTERVAL");
  ranges.push_back({f->first,interval->byte_begin_,interval->byte_end_});
 }
 for(const auto& proof:proofs)for(const auto& event:proof->suppressed_){
  budget.normalized(1);
  ranges.push_back({event->fragment_->occurrence,0,event->fragment_->normalized.size()});
 }
 // Fixed upper bounds and upfront sort-work charge; no user callbacks here.
 budget.normalized(ranges.size()*lookup_work(ranges.size()));
 std::sort(ranges.begin(),ranges.end(),[](const Range& a,const Range& b){return std::tie(a.fragment,a.begin,a.end)<std::tie(b.fragment,b.begin,b.end);});
 std::size_t at=0;
 for(const auto& [id,f]:required){
  std::size_t end=0;budget.normalized(1);
  while(at<ranges.size()&&ranges[at].fragment==id){budget.normalized(1);const auto& r=ranges[at++];
   if(r.begin!=end||r.end<=r.begin||r.end>f->normalized.size())throw std::invalid_argument("DOCUMENT_FINAL_INTERVAL_PARTITION");end=r.end;
  }
  if(end!=f->normalized.size())throw std::invalid_argument("DOCUMENT_FINAL_MISSING_SUFFIX");
 }
 if(at!=ranges.size())throw std::invalid_argument("DOCUMENT_FINAL_EXTRA_INTERVAL");
 // Full box->placement bijection, independent of source-byte coverage above.
 const auto expected=final_snapshot_intervals(engine,*snapshot,t.backend_.get());
 if(product->bindings_.size()!=expected.size())throw std::invalid_argument("DOCUMENT_FINAL_BINDING_COUNT");
 budget.bytes(expected.size(),sizeof(std::pair<const aether_layout_box_s* const,std::size_t>)+sizeof(std::uint64_t)+9*sizeof(void*));
 std::map<const aether_layout_box_s*,std::size_t> unmatched;
 std::set<std::uint64_t> ids;
 for(std::size_t i=0;i<expected.size();++i){budget.normalized(lookup_work(expected.size()));if(!unmatched.emplace(expected[i].box,i).second)throw std::invalid_argument("DOCUMENT_FINAL_DUPLICATE_BOX");}
 budget.bytes(proofs.size(),sizeof(std::pair<const std::uint64_t,const DocumentFinalPass*>)+4*sizeof(void*));
 std::map<std::uint64_t,const DocumentFinalPass*> proof_index;
 for(const auto& proof:proofs){budget.normalized(lookup_work(proofs.size()));proof_index.emplace(proof->pass_,proof.get());}
 for(const auto& binding:product->bindings_){
  budget.normalized(lookup_work(expected.size())*2+lookup_work(proofs.size()));
  const auto it=unmatched.find(binding.placement.box);
  if(it==unmatched.end()||!binding.id||!ids.insert(binding.id).second)throw std::invalid_argument("DOCUMENT_FINAL_BINDING_IDENTITY");
  const auto& e=expected[it->second];const auto& b=binding.placement;
  const auto proof=proof_index.find(e.interval->fragment_->normalization_pass);
  if(proof==proof_index.end()||binding.proof.get()!=proof->second||b.interval!=e.interval||
     b.x!=e.x||b.y!=e.y||b.width!=e.width||b.height!=e.height||b.baseline!=e.baseline)
   throw std::invalid_argument("DOCUMENT_FINAL_BINDING_GEOMETRY");
  unmatched.erase(it);
 }
 if(!unmatched.empty())throw std::invalid_argument("DOCUMENT_FINAL_MISSING_BINDING");

 budget.bytes(1,sizeof(DocumentFinalizationPending)+sizeof(int)+2*sizeof(void*));
 auto nonce=std::make_shared<const int>(0);
 auto pending=std::unique_ptr<DocumentFinalizationPending>(new DocumentFinalizationPending(product));
 pending->nonce_=nonce;
 if(t.drawing_mode_){
  budget.bytes(1,sizeof(DocumentGeometryCertificate)+2*sizeof(void*));
  budget.bytes(st.captured_bytes); // conservative complete captured storage, including temporary/index overhead
  auto certificate=std::shared_ptr<DocumentGeometryCertificate>(new DocumentGeometryCertificate);
  certificate->values_=std::make_unique<const DocumentGeometryCertificate::Values>(static_cast<const DocumentGeometryCertificate::Values&>(st));
  pending->geometry_=std::move(certificate);
  pending->selected_nonce_=t.attempt_layout_->selected_identity(engine);
 }
 // Last callback-free full audit, after EVERY allocation and backend callback.
 // Repeated source audits are charged per pass/box, not claimed globally linear.
 validate_final_geometry(engine,snapshot,t.backend_.get());
 for(const auto& proof:proofs)validate_final_inventory(engine,proof,t.backend_.get());
 if(t.passes_.size()!=pass_count||t.failure_||t.cancelled_latched_) {
  if(t.failure_)std::rethrow_exception(t.failure_);if(t.cancelled_latched_)throw DocumentTextCancelled{};
  throw std::invalid_argument("DOCUMENT_FINAL_CHANGED_PASSES");
 }
 for(const auto& pass:t.passes_){budget.normalized(1);if(!pass.complete)throw std::invalid_argument("DOCUMENT_FINAL_INCOMPLETE_PASS");}
 t.valid();
 if(t.drawing_mode_)t.attempt_layout_->audit_selection(engine);
 // Pin-free certificate binding. Shared-pointer assignment is noexcept; no
 // callback/allocation/throwing work follows publication of this private nonce.
 t.final_ready_layout_=layout;t.final_ready_assets_=st.assets;t.final_ready_product_=product.get();
 t.final_ready_revision_=st.revision;t.final_ready_selection_=pending->selected_nonce_;t.final_ready_geometry_=pending->geometry_.get();t.final_ready_=std::move(nonce);
 return pending;
}
bool DocumentTextAccess::commit_final_layout(aether_layout_engine_s* engine,aether_layout_result_s* layout,
 const std::unique_ptr<DocumentFinalizationPending>& pending)noexcept{
 if(!active(engine))return false;
 auto& t=*engine->document_text_transaction_;
 if(std::this_thread::get_id()!=t.thread_)return false; // no cross-thread mutation
 const bool valid=t.final_geometry_mode_&&!t.finalizing_&&t.active_&&!t.pass_active_&&!t.layout_completed_&&
  !t.failure_&&!t.cancelled_latched_&&pending&&pending->product_&&pending->nonce_&&pending->nonce_==t.final_ready_&&
  t.operation_&&t.document_&&t.document_->alive&&t.epochs_.valid()&&t.document_->non_state_epoch==t.nonstate_&&
  aether::style::inputs_current(t.version_.get())&&layout&&layout==t.active_layout_&&layout==t.final_ready_layout_&&
  layout->assets==t.final_ready_assets_&&pending->product_.get()==t.final_ready_product_&&
  layout->allocated_boxes.allocation_revision_valid()&&layout->allocated_boxes.allocation_revision()==t.final_ready_revision_&&
  (!t.drawing_mode_||(pending->selected_nonce_&&pending->selected_nonce_==t.final_ready_selection_&&pending->geometry_&&pending->geometry_.get()==t.final_ready_geometry_));
 if(!valid){t.invalidate_final_ready();return false;}
 const auto product=pending->product_;const auto selected=pending->selected_nonce_;const auto geometry=pending->geometry_; // noexcept retain before finish
 finish(engine);pending->nonce_.reset(); // single use, terminal callbacks invalidate too
 t.completed_assets_=layout->assets;t.completed_layout_=layout;t.completed_product_=product;t.completed_selection_=selected;t.completed_geometry_=geometry;t.layout_completed_=true;
 return true;
}

}
