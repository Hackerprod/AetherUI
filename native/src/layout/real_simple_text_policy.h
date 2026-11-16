#pragma once
#include "native_text_transaction.h"
#include "../rendering/text/real_html_normalization.h"
#include "../rendering/text/real_html_blocks.h"
#include "../rendering/text/real_html_blocks_images.h"
#include "../rendering/text/real_html_viewport.h"
#include "../rendering/text/selected_text_product.h"
namespace aether::rendering::text::truetype {
enum class HtmlTextError; struct HtmlTextFrame;
using HtmlTextResult=std::variant<std::shared_ptr<const HtmlTextFrame>,HtmlTextError>;
HtmlTextResult render_real_html_text(std::string_view,std::shared_ptr<const RealFontBinding>,LineBox,std::uint32_t,std::uint32_t,Cancellation*) noexcept;
}
namespace aether::layout {
// Distinct real-font channel. No synthetic face authority is reused.
class RealHtmlTextTestAccess; // Defined only in independent test code.
class RealSimpleTextPolicy final {
 friend class RealHtmlTextTestAccess;
 friend class RealSimpleTextAccess;
 const std::thread::id thread_=std::this_thread::get_id();
 aether_layout_engine_s* engine_;
 std::shared_ptr<node_identity_s> document_;
 MapVersionRef version_;
 CallbackEpochGuard epochs_;
 struct Record {
 FrozenNodeRef owner_;
 BoxStyleSnapshot style_;
 std::vector<std::shared_ptr<node_identity_s>> participants_;
 std::shared_ptr<const aether::rendering::text::truetype::PreparedSimpleRun> prepared_;
 std::shared_ptr<const aether::rendering::text::truetype::HtmlNormalizationProof> normalization_;
 std::shared_ptr<const aether::rendering::text::SelectedTextProduct> selected_;
 aether::rendering::text::Measurement measured_;
 Record(FrozenNodeRef owner,
  std::shared_ptr<const aether::rendering::text::truetype::PreparedSimpleRun> prepared,
  std::shared_ptr<const aether::rendering::text::truetype::HtmlNormalizationProof> normalization)
 :owner_(std::move(owner)),style_(owner_->style(),owner_->measures()),prepared_(std::move(prepared)),normalization_(std::move(normalization)) {
  using namespace aether::rendering::text;
  if(!normalization_ || normalization_->binding!=prepared_->binding ||
     normalization_->normalized_output!=prepared_->source_text ||
     style_.white_space!=(normalization_->profile==truetype::HtmlWhiteSpaceProfile::pre?AETHER_WHITE_SPACE_PRE:AETHER_WHITE_SPACE_NOWRAP))
   throw std::invalid_argument("real normalization identity");
  std::string text;std::size_t ordinal=0;
  for(auto* n:owner_->node()->children) {
   if(n->type!=AETHER_NODE_TEXT && n->type!=AETHER_NODE_COMMENT) throw std::invalid_argument("real owner children");
   const std::string piece=n->type==AETHER_NODE_TEXT?n->text:std::string{};
   if(ordinal>=normalization_->pieces.size()||normalization_->pieces[ordinal++]!=piece)throw std::invalid_argument("real decoded pieces identity");
   participants_.push_back(n->identity);text+=piece;
  }
  if(ordinal!=normalization_->pieces.size()||text!=normalization_->decoded_input) throw std::invalid_argument("real source identity");
  measured_.metrics={float(prepared_->width_26_6)/64.f,float(prepared_->linebox.height26_6)/64.f,float(prepared_->linebox.baseline26_6)/64.f};
  if(!prepared_->source_text.empty()) {
   std::vector<Glyph> glyphs;
   for(const auto& g:prepared_->glyphs) glyphs.push_back({g.glyph_id,static_cast<std::uint32_t>(g.cluster_byte),static_cast<std::int32_t>(g.advance_26_6),0,0,0});
   measured_.run=std::make_shared<const ShapedRun>(prepared_->binding->font(),prepared_->source_text,std::move(glyphs),measured_.metrics);
  }
 }
 Record(FrozenNodeRef owner,std::shared_ptr<const aether::rendering::text::SelectedTextProduct> product)
 :owner_(std::move(owner)),style_(owner_->style(),owner_->measures()),selected_(std::move(product)) {
  if(!selected_||!owner_->measures()||style_.white_space!=AETHER_WHITE_SPACE_NORMAL||
     style_.font_size!=float(selected_->binding->font()->size_26_6)/64.f)
   throw std::invalid_argument("selected text style identity");
  const aether::style::MeasuresReadView measures(*owner_->measures());
  if(measures.direction_rtl()||measures.writing_mode()!=0||
     aether::style::measure_used_line_height(&measures,style_.scalars())!=selected_->run->metrics.height)
   throw std::invalid_argument("selected text logical metrics");
  std::size_t ordinal=0;
  for(auto* node:owner_->node()->children) {
   if(!node||(node->type!=AETHER_NODE_TEXT&&node->type!=AETHER_NODE_COMMENT))throw std::invalid_argument("selected text children");
   const std::string piece=node->type==AETHER_NODE_TEXT?node->text:std::string{};
   if(ordinal>=selected_->pieces.size()||selected_->pieces[ordinal++]!=piece)throw std::invalid_argument("selected text source identity");
   participants_.push_back(node->identity);
  }
  if(ordinal!=selected_->pieces.size())throw std::invalid_argument("selected text source identity");
  measured_={selected_->run->metrics,selected_->run};
 }
 };
 std::vector<Record> records_;
 bool begun_=false;
 RealSimpleTextPolicy(aether_layout_engine_s* e,MapVersionRef v)
 :engine_(e),document_(v->document_lifetime()),version_(std::move(v)),epochs_(document_) {}
 void valid() const {
  if(std::this_thread::get_id()!=thread_) throw std::runtime_error("real text wrong thread");
  if(!epochs_.valid() || !aether::style::inputs_current(version_.get())) throw std::runtime_error("real text retired");
  for(const auto& r:records_) {
   if(!r.owner_->node_lifetime()->alive || !r.owner_->node_lifetime()->attached)throw std::runtime_error("real text owner retired");
   for(const auto& p:r.participants_)if(!p->alive || !p->attached)throw std::runtime_error("real text child retired");
  }
 }
};
class RealSimpleTextAccess final {
 friend class aether::rendering::SelectedCaptureOperation;
 friend class SelectedCaptureTestAccess;
 friend class RealHtmlTextTestAccess;
 friend aether::rendering::text::truetype::HtmlTextResult aether::rendering::text::truetype::render_real_html_text(std::string_view,std::shared_ptr<const aether::rendering::text::truetype::RealFontBinding>,aether::rendering::text::truetype::LineBox,std::uint32_t,std::uint32_t,aether::rendering::text::truetype::Cancellation*) noexcept;
 friend aether::rendering::text::truetype::HtmlBlocksResult aether::rendering::text::truetype::render_real_html_blocks(std::string_view,const std::vector<std::string>&,std::shared_ptr<const aether::rendering::text::truetype::RealFontBinding>,aether::rendering::text::truetype::LineBox,std::uint32_t,std::uint32_t,aether::rendering::text::truetype::Cancellation*) noexcept;
 friend aether::rendering::text::truetype::HtmlBlocksImagesResult aether::rendering::text::truetype::render_real_html_blocks_images(std::string_view,const std::vector<std::string>&,std::shared_ptr<const aether::rendering::text::truetype::RealFontBinding>,aether::rendering::text::truetype::LineBox,std::uint32_t,std::uint32_t,const aether::rendering::NativeImageCatalog&,aether::rendering::text::truetype::Cancellation*) noexcept;
 friend aether::rendering::text::truetype::HtmlViewportResult aether::rendering::text::truetype::render_real_html_viewport(std::string_view,const std::vector<std::string>&,std::shared_ptr<const aether::rendering::text::truetype::RealFontBinding>,aether::rendering::text::truetype::LineBox,std::uint32_t,std::uint32_t,const aether::rendering::NativeImageCatalog&,aether::rendering::text::truetype::ViewportOffset,aether::rendering::text::truetype::Cancellation*) noexcept;
public:
 static bool active(const aether_layout_engine_s* e){return e && bool(e->real_text_policy_);}
private:
 static bool install_selected(aether_layout_engine_s& e,aether_doc_s* doc,aether_style_map_s* map,
  const std::vector<aether_node_s*>& owners,
  const std::vector<std::shared_ptr<const aether::rendering::text::SelectedTextProduct>>& products) {
  if(e.layout_in_progress||e.document_text_transaction_||e.real_text_policy_||e.synthetic_text_policy_||e.text_selection_||e.text_measurer||e.contextual_text_measurer||
     !doc||!map||owners.empty()||owners.size()>8||owners.size()!=products.size())return false;
  auto version=aether::style::pin_style_map(map);
  if(!version||!version->contextual()||version->document_lifetime()!=doc->identity||!aether::style::inputs_current(version.get()))return false;
  auto policy=std::shared_ptr<RealSimpleTextPolicy>(new RealSimpleTextPolicy(&e,version));
  policy->records_.reserve(owners.size());
  for(std::size_t i=0;i<owners.size();++i) {
   if(!owners[i]||!products[i])return false;
   for(std::size_t j=0;j<i;++j)if(owners[i]==owners[j])return false;
   const auto owner=version->backing()->find_owner(owners[i]);
   if(!owner||!owner->measures())return false;
   policy->records_.emplace_back(owner,products[i]);
  }
  policy->valid();e.real_text_policy_=std::move(policy);return true;
 }
 static bool install(aether_layout_engine_s& e,aether_doc_s* doc,aether_style_map_s* map,aether_node_s* owner,
 std::shared_ptr<const aether::rendering::text::truetype::PreparedSimpleRun> run,
 std::shared_ptr<const aether::rendering::text::truetype::HtmlNormalizationProof> normalization) {
  return install_blocks(e,doc,map,{owner},{std::move(run)},{std::move(normalization)});
 }
 static bool install_blocks(aether_layout_engine_s& e,aether_doc_s* doc,aether_style_map_s* map,
  const std::vector<aether_node_s*>& owners,
  const std::vector<std::shared_ptr<const aether::rendering::text::truetype::PreparedSimpleRun>>& runs,
  const std::vector<std::shared_ptr<const aether::rendering::text::truetype::HtmlNormalizationProof>>& normalizations) {
  if(e.layout_in_progress || e.document_text_transaction_ || e.real_text_policy_ || e.synthetic_text_policy_ || e.text_selection_ || e.text_measurer || e.contextual_text_measurer || !doc || !map || owners.empty() || owners.size()>8 || owners.size()!=runs.size() || owners.size()!=normalizations.size())return false;
  auto v=aether::style::pin_style_map(map);
  if(!v || !v->contextual() || v->document_lifetime()!=doc->identity || !aether::style::inputs_current(v.get()))return false;
  auto p=std::shared_ptr<RealSimpleTextPolicy>(new RealSimpleTextPolicy(&e,v));p->records_.reserve(owners.size());
  for(std::size_t i=0;i<owners.size();++i) {
   if(!owners[i] || !runs[i] || !normalizations[i])return false;
   if(i && (runs[i]->binding!=runs[0]->binding || runs[i]->linebox.baseline26_6!=runs[0]->linebox.baseline26_6 || runs[i]->linebox.height26_6!=runs[0]->linebox.height26_6))return false;
   for(std::size_t j=0;j<i;++j)if(owners[i]==owners[j])return false;
   auto record=v->backing()->find_owner(owners[i]);if(!record || !record->measures())return false;
   p->records_.emplace_back(record,runs[i],normalizations[i]);
  }
  p->valid();e.real_text_policy_=std::move(p);return true;
 }

public:
 static void begin(aether_layout_engine_s* e,aether_doc_s* doc,const StyleLookup& styles) {
  if(!active(e)) return;
  auto& p=*e->real_text_policy_;p.valid();
  if(e->document_text_transaction_ || e->synthetic_text_policy_ || e->text_selection_ || e->text_measurer || e->contextual_text_measurer ||
     !e->layout_in_progress || p.document_!=doc->identity || styles.base_version()!=p.version_ || styles.selected_version()!=p.version_ || !styles.structure_valid()) throw std::runtime_error("real policy conflict");
  p.begun_=true;
 }
 static bool owns(const aether_layout_engine_s* e,aether_node_s* owner) {
  if(!active(e))return false;
  auto& p=*e->real_text_policy_;p.valid();
  if(!p.begun_)return false;
  for(const auto& r:p.records_)if(r.owner_->node()==owner)return true;
  return false;
 }
 static aether::rendering::text::Measurement measure(const aether_layout_engine_s* e,aether_node_s* owner,const BoxStyleSnapshot& style) {
  if(!owns(e,owner))throw std::invalid_argument("real owner missing");
  auto& p=*e->real_text_policy_;
  for(const auto& r:p.records_)if(r.owner_->node()==owner) {
   if(style.style_owner()!=r.style_.style_owner() || style.measures_owner()!=r.style_.measures_owner() || !native_text_detail::scalars_equal(style.scalars(),r.style_.scalars()))throw std::runtime_error("real style changed");
   return r.measured_;
  }
  throw std::invalid_argument("real owner missing");
 }
 static void finish(const aether_layout_engine_s* e){if(active(e))e->real_text_policy_->valid();}
 static void clear(aether_layout_engine_s* e) noexcept {if(active(e))e->real_text_policy_->begun_=false;}
};
}
