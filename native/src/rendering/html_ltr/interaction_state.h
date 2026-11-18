#pragma once
#include "owned_allocator.h"
#include <span>
#include <thread>

namespace aether::rendering::html_ltr {
enum class InteractionControlKind : std::uint32_t { text=1, password=2, button=3 };
enum class InteractionEventKind : std::uint32_t { pointer_down=1,pointer_up=2,pointer_cancel=3,key_down=4,key_up=5,text=6,blur=7 };
enum class InteractionKey : std::uint32_t { none=0,tab=1,enter=2,space=3,backspace=4 };
enum class InteractionArm : std::uint32_t { none=0,pointer=1,space=2 };
inline constexpr std::uint32_t interaction_shift=1,interaction_enter_down=1,interaction_space_down=2;
struct InteractionEvent {
 std::uint32_t kind{},key{},modifiers{};
 float x{},y{};
 const std::uint8_t* text{};
 std::size_t text_length{};
};
// Allocation-free full event-shape/payload validation. Call before native hit-test.
// Returns TEXT scalar count, zero for other event kinds.
std::size_t validate_interaction_event(const InteractionEvent&);
struct InteractionMetadata {
 std::int32_t width{},height{};
 std::uint32_t focus_id{},arm_id{},keys_down{};
 InteractionArm arm{InteractionArm::none};
 bool pointer_primary_down{};
 std::uint64_t state_version{1};
 bool operator==(const InteractionMetadata&)const=default;
};
struct InteractionStateResult {bool changed{};std::uint64_t state_version{};std::uint32_t activated_control_id{};};
struct InteractionControlSeed {
 InteractionControlKind kind;
 std::string_view initial_value,placeholder;
};
struct InteractionPresentation {
 InteractionControlKind kind;
 OwnedString text;
 bool shows_placeholder{};
 explicit InteractionPresentation(OwnedBudget::Handle,InteractionControlKind);
};
// Private native-only view. It has no plaintext-value or password-length getter.
struct InteractionCandidateView {
 const InteractionMetadata& metadata;
 std::span<const InteractionPresentation> presentations;
};
class NativeStateValidator {
public:
 virtual ~NativeStateValidator()=default;
 // Validate every native candidate presentation before commit. Throws Error or
 // bad_alloc on failure; no external side effects or application callbacks.
 virtual void validate(const InteractionCandidateView&)=0;
};
// The ABI adapter obtains this from its confirmed native layout. A public event
// never carries a control ID. It must not fabricate freshness from an old frame.
struct InteractionPointerContext {bool frame_current{};std::uint32_t hit_id{};};
class InteractionStateMachine final {
 struct Definition {InteractionControlKind kind;OwnedString placeholder;Definition(OwnedBudget::Handle,InteractionControlKind,std::string_view);};
 struct Data {
  InteractionMetadata metadata;
  OwnedVector<OwnedString> values;
  OwnedVector<std::uint16_t> scalar_counts;
  OwnedVector<InteractionPresentation> presentations;
  std::size_t value_scalars{};
  explicit Data(OwnedBudget::Handle);
  void swap(Data&)noexcept;
 };
 OwnedBudget::Handle domain_;
 OwnedVector<Definition> definitions_;
 std::size_t immutable_scalars_{};
 Data data_;
 std::thread::id owner_{std::this_thread::get_id()};
 bool busy_{};
 void owner()const;
 void present(Data&,std::size_t)const;
 bool input(std::uint32_t)const noexcept;
 bool button(std::uint32_t)const noexcept;
 InteractionStateResult commit(Data&,std::uint32_t,NativeStateValidator&);
 friend struct InteractionStateTestAccess;
public:
 // immutable_scalars = exact R3 inventory scalar_count minus initial input
 // values. It includes static labels and all placeholders, even hidden ones.
 InteractionStateMachine(OwnedBudget&,std::span<const InteractionControlSeed>,std::size_t immutable_scalars,
                         std::int32_t width,std::int32_t height,NativeStateValidator&);
 InteractionStateMachine(const InteractionStateMachine&)=delete;
 InteractionStateMachine& operator=(const InteractionStateMachine&)=delete;
 InteractionMetadata metadata()const;
 std::uint32_t control_count()const;
 InteractionControlKind control_kind(std::uint32_t index)const;
 InteractionCandidateView candidate_view()const;
 InteractionStateResult dispatch(const InteractionEvent&,InteractionPointerContext,NativeStateValidator&);
 InteractionStateResult resize(std::int32_t width,std::int32_t height,NativeStateValidator&);
};
}
