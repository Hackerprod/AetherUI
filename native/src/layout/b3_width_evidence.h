#pragma once
// Private construction-time evidence. No numeric CSS solver lives here: the
// layout branches supply decisions they already made, including forced size
// semantics. A finite number alone never certifies a reference.
struct aether_layout_box_s;
namespace aether::rendering::html_ltr {
enum class B3WidthReason { indefinite, viewport, px, block_fill, flex_final };
}
using aether::rendering::html_ltr::B3WidthReason;
struct B3WidthFrame {
    aether_layout_box_s* box = nullptr;
    float content_width = 0.f;
    B3WidthReason reason = B3WidthReason::indefinite;
    bool definite = false;
};
enum class forced_width_kind_e { none, definite, indefinite };
// Captures the selected layout_element branch, not an alternate sizing pass.
inline B3WidthReason b3_selected_width_reason(bool intrinsic, bool forced,
    forced_width_kind_e forced_kind, bool specified_px, bool specified_percentage,
    bool auto_fill, bool reference_definite,
    B3WidthReason reference_reason = B3WidthReason::indefinite) noexcept {
    if (intrinsic) return B3WidthReason::indefinite;
    if (forced) return forced_kind == forced_width_kind_e::definite && reference_definite
        ? B3WidthReason::flex_final : B3WidthReason::indefinite;
    if (specified_px) return B3WidthReason::px;
    // A percentage propagates the reference's permitted basis, rather than
    // minting a fourth kind of definiteness. The separate percentage flag in
    // the journal still requires an authenticated dependency, even for px.
    if (specified_percentage) return reference_definite
        ? (reference_reason == B3WidthReason::viewport ? B3WidthReason::block_fill : reference_reason)
        : B3WidthReason::indefinite;
    if (auto_fill && reference_definite) return B3WidthReason::block_fill;
    return B3WidthReason::indefinite;
}
