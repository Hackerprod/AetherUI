#pragma once
#include "interaction_state.h"
#include "../native_frame.h"
#include "../text/font_source.h"
#include "../text/glyph_mask_a8.h"
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <memory>
#include <vector>
struct aether_layout_engine_s;
namespace aether::rendering::html_ltr {
class HtmlLtrMeasureBridge;
class InteractionNativeDocument;
class InteractionNativeScene final {
 struct State;
 std::shared_ptr<State> state_;
 friend class InteractionNativeDocument;
public:
 // State is private; only the native document can supply this constructor.
 explicit InteractionNativeScene(std::shared_ptr<State>);
 ~InteractionNativeScene();
 InteractionNativeScene(const InteractionNativeScene&)=delete;
 std::uint32_t hit(float x,float y)const;
 const preparation::PreparedFrame& prepared()const noexcept;
 const std::vector<std::shared_ptr<const text::GlyphMaskA8>>& masks()const noexcept;
};
class InteractionNativeDocument final:public NativeStateValidator {
 struct State;
 std::shared_ptr<State> state_;
 friend class InteractionNativeScene;
 static void install(aether_layout_engine_s*,std::shared_ptr<HtmlLtrMeasureBridge>);
public:
 explicit InteractionNativeDocument(std::shared_ptr<State>);
 static std::shared_ptr<InteractionNativeDocument> create(OwnedBudget&,std::string_view html,
  std::shared_ptr<const text::FontSource> regular,std::shared_ptr<const text::FontSource> bold,
  std::int32_t width,std::int32_t height);
 ~InteractionNativeDocument();
 InteractionNativeDocument(const InteractionNativeDocument&)=delete;
 std::span<const InteractionControlSeed> seeds()const noexcept;
 std::size_t immutable_scalar_count()const noexcept;
 void validate(const InteractionCandidateView&)override;
 std::shared_ptr<InteractionNativeScene> prepare(const InteractionCandidateView&);
};
}
