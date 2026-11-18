#pragma once
#include "owned_allocator.h"
#include "owned_font_transport.h"
#include "../preparation/text_fields_view.h"
#include <span>
#include <variant>

namespace aether::rendering::html_ltr {
namespace prep=preparation;
struct InteractionNativeText {
 std::shared_ptr<const owned_text::Run> run;
 float baseline{};
};
// Borrowed fields for the native pipeline only, never an application callback.
// Shared run ownership remains charged by its allocator, not by each reference.
struct InteractionTextView {
 std::string_view text;prep::Rect rect{};std::string_view font_family;float font_size{};
 std::int32_t font_weight{},text_align{};prep::Color color{};aether_bool_t wrap{};
 std::optional<InteractionNativeText> native;
};
struct InteractionText {
 OwnedString text;prep::Rect rect{};OwnedString font_family;float font_size{};
 std::int32_t font_weight{},text_align{};prep::Color color{};aether_bool_t wrap{};
 std::optional<InteractionNativeText> native;
 InteractionText(OwnedBudget::Handle,const InteractionTextView&);
};
struct InteractionGradient {
 prep::Rect rect{},gradient_rect{};prep::Radii radii{};float direction_x{},direction_y{},opacity{1};
 OwnedVector<prep::GradientStop> stops;
 explicit InteractionGradient(OwnedBudget::Handle domain):stops(OwnedAllocator<prep::GradientStop>(domain,OwnedCategory::paint)){}
};
using InteractionCommand=std::variant<prep::Fill,prep::Stroke,InteractionText,prep::PushClip,prep::PopClip,prep::PushTransform,
 prep::PopTransform,prep::BeginChunk,prep::EndChunk,prep::BoxShadow,prep::BorderGroup,prep::BeginOpacity,prep::EndOpacity,prep::PushScroll,prep::PopScroll,prep::ScrollChrome,InteractionGradient>;
// Views include every POD scope/BorderGroup used by the existing kernel. The
// gradient alternative is deliberately absent: profile2 never admits gradients.
using InteractionCommandView=std::variant<prep::Fill,prep::Stroke,InteractionTextView,prep::PushClip,prep::PopClip,prep::PushTransform,
 prep::PopTransform,prep::BeginChunk,prep::EndChunk,prep::BoxShadow,prep::BorderGroup,prep::BeginOpacity,prep::EndOpacity,prep::PushScroll,prep::PopScroll,prep::ScrollChrome>;
class InteractionSnapshot {
 OwnedVector<InteractionCommand> commands_;
 bool native_text_required_;
public:
 explicit InteractionSnapshot(OwnedBudget::Handle domain,bool native_required=true):commands_(OwnedAllocator<InteractionCommand>(domain,OwnedCategory::paint)),native_text_required_(native_required){}
 InteractionSnapshot(const InteractionSnapshot&)=delete;
 InteractionSnapshot& operator=(const InteractionSnapshot&)=delete;
 InteractionSnapshot(InteractionSnapshot&&)noexcept=default;
 InteractionSnapshot& operator=(InteractionSnapshot&&)=delete;
 const OwnedVector<InteractionCommand>& commands()const noexcept{return commands_;}
 bool native_text_required()const noexcept{return native_text_required_;}
 OwnedBudget::Handle domain()const noexcept{return commands_.get_allocator().domain();}
 void append(const InteractionCommandView&);
 void reserve(std::size_t);
};
struct InteractionPreparedEvent {
 InteractionCommand command;prep::Matrix matrix{prep::identity};std::size_t transform_depth{};double scroll_x{},scroll_y{};std::size_t scroll_depth{};
};
class InteractionPreparedFrame {
 OwnedVector<InteractionPreparedEvent> events_;
 explicit InteractionPreparedFrame(OwnedVector<InteractionPreparedEvent>&& events)noexcept:events_(std::move(events)){}
 friend std::variant<InteractionPreparedFrame,prep::Error> interaction_prepare(OwnedBudget&,const InteractionSnapshot&,prep::Cancellation*)noexcept;
public:
 InteractionPreparedFrame(const InteractionPreparedFrame&)=delete;
 InteractionPreparedFrame& operator=(const InteractionPreparedFrame&)=delete;
 InteractionPreparedFrame(InteractionPreparedFrame&&)noexcept=default;
 InteractionPreparedFrame& operator=(InteractionPreparedFrame&&)=delete;
 const OwnedVector<InteractionPreparedEvent>& events()const noexcept{return events_;}
 OwnedBudget::Handle domain()const noexcept{return events_.get_allocator().domain();}
};
using InteractionSnapshotResult=std::variant<InteractionSnapshot,prep::Error>;
using InteractionPrepareResult=std::variant<InteractionPreparedFrame,prep::Error>;
InteractionSnapshotResult interaction_snapshot(OwnedBudget&,std::span<const InteractionCommandView>,bool native_text_required=true)noexcept;
InteractionPrepareResult interaction_prepare(OwnedBudget&,const InteractionSnapshot&,prep::Cancellation* =nullptr)noexcept;
// Pure validation uses the same text field and nontext command validators as
// legacy preparation, with owned immutable run views instead of legacy copies.
std::optional<prep::ErrorCode> validate_interaction_text(const InteractionText&,std::size_t&)noexcept;
}
