#include "frozen_style.h"
namespace aether::style {
ReadOnlyArrayView<FrozenBackgroundLayer> ComputedStyleReadView::background_layers() const noexcept { return payload_->background_layers(); }
ReadOnlyArrayView<aether_backdrop_filter> ComputedStyleReadView::backdrop_filters() const noexcept { return payload_->backdrop_filters(); }
ReadOnlyArrayView<aether_transform_operation> ComputedStyleReadView::transforms() const noexcept { return payload_->transforms(); }
ReadOnlyArrayView<aether_shadow> ComputedStyleReadView::text_shadows() const noexcept { return payload_->text_shadows(); }
ReadOnlyArrayView<aether_shadow> ComputedStyleReadView::box_shadows() const noexcept { return payload_->box_shadows(); }
GradientReadView ComputedStyleReadView::text_gradient() const noexcept { return payload_->text_gradient(); }
} // namespace aether::style
