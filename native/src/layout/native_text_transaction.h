#pragma once
#include "layout_types.h"
#include <type_traits>
namespace aether::layout {
namespace native_text_detail {
template<class T> requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
inline bool eq(T a,T b) { return a==b; }
inline bool eq(float a,float b) {return a==b || (std::isnan(a)&&std::isnan(b));}
inline bool eq(aether_css_length a,aether_css_length b) {return eq(a.value,b.value) && a.unit==b.unit && eq(a.linear_percent,b.linear_percent) && eq(a.linear_em,b.linear_em) && eq(a.linear_rem,b.linear_rem) && eq(a.linear_vw,b.linear_vw) && eq(a.linear_vh,b.linear_vh);}
inline bool eq(aether_flex_basis a,aether_flex_basis b){return a.kind==b.kind && eq(a.length,b.length);}
inline bool eq(aether_css_edges a,aether_css_edges b){return eq(a.left,b.left)&&eq(a.top,b.top)&&eq(a.right,b.right)&&eq(a.bottom,b.bottom);}
inline bool eq(aether_color_rgba a,aether_color_rgba b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
inline bool eq(aether_color_edges a,aether_color_edges b){return eq(a.left,b.left)&&eq(a.top,b.top)&&eq(a.right,b.right)&&eq(a.bottom,b.bottom);}
inline bool eq(aether_border_style_edges a,aether_border_style_edges b){return a.left==b.left&&a.top==b.top&&a.right==b.right&&a.bottom==b.bottom;}
inline bool eq(aether_transform_origin a,aether_transform_origin b){return eq(a.x,b.x)&&eq(a.y,b.y);}
inline bool eq(aether_corner_radius a,aether_corner_radius b){return eq(a.horizontal,b.horizontal)&&eq(a.vertical,b.vertical);}
inline bool eq(aether_border_radii a,aether_border_radii b){return eq(a.top_left,b.top_left)&&eq(a.top_right,b.top_right)&&eq(a.bottom_left,b.bottom_left)&&eq(a.bottom_right,b.bottom_right);}
inline bool scalars_equal(const StyleScalars& a,const StyleScalars& b) {
#define T17_COMPARE(type,name) if(!eq(a.name,b.name)) return false;
 AETHER_STYLE_SCALAR_FIELDS(T17_COMPARE)
#undef T17_COMPARE
 return a.font_family==b.font_family;
}
}
// This object is never published. All raw DOM use stays on the issuing thread,
// after epoch checks; published paragraphs use only ordinal source provenance.
class NativeTextTransaction final {
 friend class NativeTextAccess;
 using Paragraph=aether::rendering::text::NativeInlineParagraph;
 using Measurement=aether::rendering::text::Measurement;
 struct Entry {
  FrozenNodeRef record;
  BoxStyleSnapshot style;
  std::vector<std::shared_ptr<node_identity_s>> participants;
  std::shared_ptr<const Paragraph> paragraph;
  Measurement measurement;
  bool measured=false;
 };
 const std::shared_ptr<const SyntheticTextPolicy> policy_;
 const std::shared_ptr<const int> operation_=std::make_shared<const int>(0);
 const StyleLookup* const styles_;
 const CallbackEpochGuard epochs_;
 std::unordered_map<aether_node_s*,Entry> entries_;
 void valid() const {
  // thread check comes before any non-atomic epoch/DOM access.
  if(std::this_thread::get_id()!=policy_->thread_ ||
     (policy_->cancelled_ && policy_->cancelled_->load(std::memory_order_relaxed)))
   throw std::runtime_error("synthetic layout cancelled or wrong thread");
  if(policy_->engine_->synthetic_session_!=policy_->session_ ||
     policy_->engine_->synthetic_text_policy_!=policy_ || !policy_->engine_->layout_in_progress ||
     policy_->engine_->text_selection() || policy_->engine_->text_measurer || policy_->engine_->contextual_text_measurer ||
     !policy_->epochs_.valid() || !epochs_.valid() || !styles_->structure_valid() ||
     styles_->document_lifetime()!=policy_->document_ || styles_->base_version()!=policy_->version_ ||
     styles_->selected_version()!=policy_->version_ || !aether::style::inputs_current(policy_->version_.get()))
   throw std::runtime_error("synthetic operation binding changed");
  for(const auto& [owner,e]:entries_) {
   if(!e.record->node_lifetime()->alive || !e.record->node_lifetime()->attached ||
      e.record->node_lifetime()->owner!=owner || styles_->lookup_record_owner(owner)!=e.record)
    throw std::runtime_error("synthetic owner retired");
   for(const auto& p:e.participants) if(!p->alive || !p->attached)
    throw std::runtime_error("synthetic participant retired");
  }
 }
 NativeTextTransaction(std::shared_ptr<const SyntheticTextPolicy> p,const StyleLookup& styles)
 :policy_(std::move(p)),styles_(&styles),epochs_(policy_->document_) {}
 void prepare(aether_doc_s* doc) {
  using namespace aether::rendering::text;
  valid();
  if(!policy_->seal_ || !policy_->provider_ || policy_->owners_.empty())
   throw std::invalid_argument("synthetic policy incomplete");
  for(const auto& record:policy_->owners_) {
   valid();
   if(!record || !record->node_lifetime()->alive || !record->node_lifetime()->attached ||
      record->document_lifetime()!=policy_->document_ || styles_->lookup_record_owner(record->node())!=record ||
      !record->measures()) throw std::invalid_argument("unbound synthetic owner/style");
   auto* owner=record->node();
   Entry e{record,BoxStyleSnapshot(record->style(),record->measures()),{}, {}, {},false};
   const auto& s=e.style;
   const MeasuresReadView measures(*record->measures());
   if(owner->type!=AETHER_NODE_ELEMENT || owner->pseudo_element!=aether_pseudo_element_t::none ||
      owner->anonymous_flex_owner || s.display!=AETHER_DISPLAY_BLOCK || s.white_space!=AETHER_WHITE_SPACE_NOWRAP ||
      s.text_transform!=AETHER_TEXT_TRANSFORM_NONE || s.font_weight!=400 ||
      s.font_size!=float(policy_->seal_->font->size_26_6)/64.f ||
      std::string(s.font_family.data(),strnlen(s.font_family.data(),s.font_family.size()))!="SyntheticTest" ||
      measures.direction_rtl() || measures.writing_mode()!=0 || s.has_text_gradient ||
      !s.view().text_shadows().empty() || !s.view().transforms().empty() ||
      (s.text_align!=AETHER_TEXT_ALIGN_START && s.text_align!=AETHER_TEXT_ALIGN_LEFT))
    throw std::invalid_argument("outside synthetic nowrap profile");
   std::vector<std::string> pieces;
   for(auto* child:owner->children) {
    if(!child || (child->type!=AETHER_NODE_TEXT && child->type!=AETHER_NODE_COMMENT))
     throw std::invalid_argument("synthetic owner has non-text child");
    e.participants.push_back(child->identity);
    // Comments remain empty provenance pieces; no fake child geometry.
    pieces.push_back(child->type==AETHER_NODE_TEXT ? child->text : std::string{});
   }
   e.paragraph=std::make_shared<const Paragraph>(std::move(pieces),policy_->seal_);
   if(!entries_.emplace(owner,std::move(e)).second) throw std::invalid_argument("duplicate synthetic owner");
  }
  // Check the WHOLE tree before the first provider call, including unsupported
  // siblings. No partial prefix is measured and there is no legacy fallback.
  std::vector<aether_node_s*> stack{doc};
  while(!stack.empty()) {
   auto* n=stack.back();stack.pop_back();
   if(n->type==AETHER_NODE_TEXT && !n->text.empty() && entries_.find(n->parent)==entries_.end())
    throw std::invalid_argument("text outside synthetic owners");
   for(auto* c:n->children) stack.push_back(c);
  }
  valid();
 }
 Measurement measure(aether_node_s* owner,const BoxStyleSnapshot& style) {
  valid();
  auto it=entries_.find(owner);
  if(it==entries_.end()) throw std::invalid_argument("unbound synthetic paragraph");
  auto& e=it->second;
  if(style.style_owner()!=e.style.style_owner() || style.measures_owner()!=e.style.measures_owner() ||
     !native_text_detail::scalars_equal(style.scalars(),e.style.scalars()))
   throw std::invalid_argument("effective synthetic style changed");
  if(e.measured) return e.measurement;
  if(e.paragraph->text.empty()) {e.measured=true;return {};}
  auto response=policy_->provider_->measure(e.paragraph,0,e.paragraph->text.size(),0.f);
  // Never inspect any DOM after callback before validating all epochs.
  valid();
  aether::rendering::text::validate_contextual_measurement(e.paragraph,response);
  e.measurement=std::move(response.measured);e.measured=true;
  return e.measurement;
 }
};
class NativeTextAccess final {
public:
 static bool active(const aether_layout_engine_s* e) {return e && bool(e->synthetic_text_policy_);}
 static void clear(aether_layout_engine_s* e) noexcept {e->native_text_transaction_.reset();}
 static void begin(aether_layout_engine_s* e,aether_doc_s* doc,const StyleLookup& styles) {
  if(!active(e)) return;
  const auto& p=e->synthetic_text_policy_;
  if(std::this_thread::get_id()!=p->thread_) throw std::runtime_error("synthetic owner thread");
  if(p->engine_!=e || !e->layout_in_progress || !p->session_ || p->session_!=e->synthetic_session_ ||
     p->document_!=doc->identity || e->text_selection() || e->text_measurer || e->contextual_text_measurer)
   throw std::invalid_argument("synthetic policy/session conflict");
  auto transaction=std::shared_ptr<NativeTextTransaction>(new NativeTextTransaction(p,styles));
  transaction->prepare(doc);
  e->native_text_transaction_=std::move(transaction);
 }
 static bool owns(const aether_layout_engine_s* e,aether_node_s* owner) {
  return e->native_text_transaction_ && e->native_text_transaction_->entries_.contains(owner);
 }
 static aether::rendering::text::Measurement measure(const aether_layout_engine_s* e,aether_node_s* owner,const BoxStyleSnapshot& s) {
  if(!e->native_text_transaction_) throw std::invalid_argument("missing synthetic transaction");
  const auto pinned=e->native_text_transaction_;
  return pinned->measure(owner,s);
 }
 static void finish(const aether_layout_engine_s* e) {if(e->native_text_transaction_) e->native_text_transaction_->valid();}
};
}
