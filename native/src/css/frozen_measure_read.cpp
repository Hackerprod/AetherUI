#include "frozen_measure_read.h"
namespace aether::style {
namespace {
FrozenLineHeightKind freeze_kind(measure_line_height_kind_e kind) {
    switch(kind) {
    case measure_line_height_kind_e::normal: return FrozenLineHeightKind::normal;
    case measure_line_height_kind_e::number: return FrozenLineHeightKind::number;
    case measure_line_height_kind_e::absolute_css_px: return FrozenLineHeightKind::absolute_css_px;
    }
    throw std::invalid_argument("line height kind");
}
measure_line_height_kind_e read_kind(FrozenLineHeightKind kind) {
    switch(kind) {
    case FrozenLineHeightKind::normal: return measure_line_height_kind_e::normal;
    case FrozenLineHeightKind::number: return measure_line_height_kind_e::number;
    case FrozenLineHeightKind::absolute_css_px: return measure_line_height_kind_e::absolute_css_px;
    }
    throw std::invalid_argument("frozen line height kind");
}
}
float MeasuresReadView::parent_font_size() const { return frozen_ ? frozen_->scalars().parent_font_size : builder_.value().parent_font_size(); }
float MeasuresReadView::self_font_size() const { return frozen_ ? frozen_->scalars().self_font_size : builder_.value().self_font_size(); }
measure_line_height_s MeasuresReadView::line_height() const {
    if(!frozen_) return builder_.value().line_height();
    return {read_kind(frozen_->scalars().line_height_kind), frozen_->scalars().line_height_value};
}
int32_t MeasuresReadView::writing_mode() const { return frozen_ ? frozen_->scalars().writing_mode : builder_.value().writing_mode(); }
bool MeasuresReadView::direction_rtl() const { return frozen_ ? frozen_->scalars().direction_rtl : builder_.value().direction_rtl(); }
int32_t MeasuresReadView::flex_order() const { return frozen_ ? frozen_->scalars().flex_order : builder_.value().flex_order(); }
aether_css_length MeasuresReadView::row_gap() const { return frozen_ ? frozen_->scalars().row_gap : builder_.value().row_gap(); }
aether_css_length MeasuresReadView::column_gap() const { return frozen_ ? frozen_->scalars().column_gap : builder_.value().column_gap(); }
measure_inputs_s MeasuresReadView::inputs() const { return inputs_with_self_font(self_font_size()); }
measure_inputs_s MeasuresReadView::inputs_with_self_font(float font) const {
    if(!frozen_) return builder_.value().inputs_with_self_font(font);
    const auto& d=frozen_->document().values();
    return {font,d.viewport_width,d.viewport_height,d.initial_font_size,d.root_font_size};
}
aether::css::intrinsic_size_keyword MeasuresReadView::intrinsic_keyword(const std::string& name) const {
    return frozen_ ? frozen_->intrinsic_keyword(name) : builder_ ? builder_->intrinsic_keyword(name) : aether::css::intrinsic_size_keyword::none;
}
aether::css::math_tree MeasuresReadView::contextual_tree(const std::string& name) const {
    return frozen_ ? frozen_->contextual_tree(name) : builder_ ? builder_->tree(name) : nullptr;
}
uint32_t MeasuresReadView::dependency(const std::string& name) const {
    return frozen_ ? frozen_->dependency(name) : builder_ ? builder_->property_dependencies(name) : 0;
}
size_t MeasuresReadView::position_count() const noexcept { return frozen_ ? frozen_->background_positions().size() : builder_ ? builder_->background_positions().size() : 0; }
MeasureBackgroundPosition MeasuresReadView::position_at(size_t i) const {
    if(frozen_) return frozen_->background_positions().at(i);
    auto values=builder_.value().background_positions();
    if(i>=values.size()) throw std::out_of_range("measure position");
    return {values[i].x, values[i].y};
}
size_t MeasuresReadView::size_count() const noexcept { return frozen_ ? frozen_->background_sizes().size() : builder_ ? builder_->background_sizes().size() : 0; }
MeasureBackgroundSize MeasuresReadView::size_at(size_t i) const {
    if(frozen_) return frozen_->background_sizes().at(i);
    auto values=builder_.value().background_sizes();
    if(i>=values.size()) throw std::out_of_range("measure size");
    return {values[i].kind, values[i].x, values[i].y};
}
aether_status_t freeze_runtime_measures(const measure_state_s& source, FrozenDocumentRef document, FrozenMeasuresRef& output) noexcept {
    try {
        if(!document || source.background_positions.empty() || source.background_sizes.empty()) return AETHER_ERROR_INVALID_STATE;
        MeasureScalarValues scalars{source.parent_font_size,source.self_font_size,freeze_kind(source.line_height.kind),
            source.line_height.value,source.row_gap,source.column_gap,source.dependencies,source.flex_order,source.direction_rtl,source.writing_mode};
        std::vector<MeasureBackgroundPosition> positions;
        positions.reserve(source.background_positions.size());
        for(const auto& p:source.background_positions) positions.push_back({p.x,p.y});
        std::vector<MeasureBackgroundSize> sizes;
        sizes.reserve(source.background_sizes.size());
        for(const auto& p:source.background_sizes) sizes.push_back({p.kind,p.x,p.y});
        std::vector<PropertyDependencyInput> dependencies;
        dependencies.reserve(source.property_dependencies.size());
        for(const auto& [name, flags]:source.property_dependencies) if(flags) dependencies.push_back({name,flags});
        return freeze_measures(scalars,std::move(document),positions,sizes,dependencies,output,source.contextual_lengths,source.intrinsic_keywords);
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}
float measure_used_line_height(const MeasuresReadView* view, const StyleScalars& flat) {
    if(!view || !*view) return flat.font_size * (flat.line_height > 0 ? flat.line_height : 1.2f);
    const auto line=view->line_height();
    return line.kind==measure_line_height_kind_e::absolute_css_px ? line.value :
        view->self_font_size() * (line.kind==measure_line_height_kind_e::normal ? 1.2f : line.value);
}
bool measure_has_percentage(const MeasuresReadView* view, const std::string& property, aether_css_length value) {
    if(value.unit==AETHER_CSS_UNIT_PERCENT || (value.unit==AETHER_CSS_UNIT_LINEAR && value.linear_percent!=0)) return true;
    return view && *view && ((view->dependency(property)&MEASURE_PERCENTAGE) ||
        aether::css::math_has_percentage(view->contextual_tree(property)));
}
}
