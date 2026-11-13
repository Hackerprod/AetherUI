#pragma once
#include "style_freeze.h"
#include "measure_state.h"
#include <optional>
namespace aether::style {
using FrozenDocumentRef = std::shared_ptr<const FrozenDocumentMeasureContext>;
using FrozenMeasuresRef = std::shared_ptr<const FrozenMeasures>;
// Borrowed, nullable descriptor. No vector/map/document construction on reads.
class MeasuresReadView {
    const FrozenMeasures* frozen_ = nullptr;
    const std::optional<measure_read_view_s> builder_;
public:
    MeasuresReadView() noexcept = default;
    explicit MeasuresReadView(const FrozenMeasures& value) noexcept : frozen_(&value) {}
    explicit MeasuresReadView(measure_read_view_s value) noexcept : builder_(value) {}
    explicit MeasuresReadView(const measure_state_s& value) noexcept : builder_(measure_read_view_s(value)) {}
    explicit operator bool() const noexcept { return frozen_ || builder_.has_value(); }
    float parent_font_size() const;
    float self_font_size() const;
    measure_line_height_s line_height() const;
    int32_t flex_order() const;
    bool direction_rtl() const;
    int32_t writing_mode() const;
    aether::css::intrinsic_size_keyword intrinsic_keyword(const std::string& name) const;
    aether_css_length row_gap() const;
    aether_css_length column_gap() const;
    measure_inputs_s inputs() const;
    measure_inputs_s inputs_with_self_font(float font) const;
    // Stage1 builder only accepts string keys; this signature avoids allocating
    // a temporary string for a string_view. Caller supplies its existing stable
    // string key; Frozen lookup uses its bytes directly.
    aether::css::math_tree contextual_tree(const std::string& name) const;
    uint32_t dependency(const std::string& name) const;
    size_t position_count() const noexcept;
    MeasureBackgroundPosition position_at(size_t i) const;
    size_t size_count() const noexcept;
    MeasureBackgroundSize size_at(size_t i) const;
};
// document is explicitly supplied AFTER root bootstrap; mutable state.document
// is never retained or silently frozen by this factory.
aether_status_t freeze_runtime_measures(const measure_state_s&, FrozenDocumentRef document, FrozenMeasuresRef&) noexcept;
float measure_used_line_height(const MeasuresReadView*, const StyleScalars&);
bool measure_has_percentage(const MeasuresReadView*, const std::string&, aether_css_length);
}
