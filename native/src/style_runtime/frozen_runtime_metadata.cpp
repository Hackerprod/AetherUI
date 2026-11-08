#include "frozen_runtime_metadata.h"
#include "style_property_registry_internal.h"
#include <stdexcept>
namespace aether::style {
struct FrozenScopeControl {
    FrozenScopeControl* next = nullptr;
    const FrozenCustomPropertyScope value;
    FrozenScopeControl(ImmutableArray<FrozenCustomProperty> local,
        std::unordered_map<std::string_view,size_t> index, FrozenScopeRef parent) noexcept
        : value(std::move(local), std::move(index), std::move(parent)) {}
};
namespace {
struct ScopeDelete {
    FrozenScopeControl* control;
    void operator()(const FrozenCustomPropertyScope*) const noexcept {
        static thread_local FrozenScopeControl* pending = nullptr;
        static thread_local bool draining = false;
        control->next = pending;
        pending = control;
        if (draining) return;
        draining = true;
        while (pending) {
            auto* dead = pending;
            pending = dead->next;
            delete dead;
        }
        draining = false;
    }
};
}
FrozenCustomPropertyScope::FrozenCustomPropertyScope(ImmutableArray<FrozenCustomProperty> local,
    Index index, FrozenScopeRef parent) noexcept
    : local_(std::move(local)), index_(std::move(index)), parent_(std::move(parent)) {}
const FrozenCustomProperty* FrozenCustomPropertyScope::find_local(std::string_view name) const noexcept {
    auto found = index_.find(name);
    return found == index_.end() ? nullptr : &local_.view()[found->second];
}
aether_status_t freeze_custom_scope(const CustomPropertyInput& input, FrozenScopeRef parent, FrozenScopeRef& output) noexcept {
    try {
        for (const auto& [name, entry] : input) {
            (void)name;
            switch(entry.status) {
            case custom_property_status::valid: case custom_property_status::invalid:
            case custom_property_status::work_exhausted: break;
            default: return AETHER_ERROR_INVALID_STATE;
            }
        }
        auto cursor = input.begin();
        auto local = ImmutableArray<FrozenCustomProperty>::generate(input.size(), [&](size_t) {
            const auto& entry = *cursor++;
            return FrozenCustomProperty(entry.first, entry.second);
        });
        FrozenCustomPropertyScope::Index index;
        index.reserve(local.size());
        for(size_t i=0; i<local.size(); ++i) index.emplace(local.view()[i].name(),i);
        auto guard = std::make_unique<FrozenScopeControl>(std::move(local), std::move(index), std::move(parent));
        auto* control = guard.release();
        // shared_ptr invokes ScopeDelete itself if allocating its control block fails.
        FrozenScopeRef prepared(&control->value, ScopeDelete{control});
        output = std::move(prepared);
        return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}
aether_status_t freeze_color_metadata(const color_resolution_state_s& input, FrozenColorRef& output) noexcept {
    try {
        std::array<bool,4> border{};
        for(size_t i=0;i<4;++i) border[i]=input.border_uses_current_color[i];
        auto text = ImmutableArray<uint8_t>::generate(input.text_shadow_uses_current_color.size(),
            [&](size_t i) { return uint8_t(input.text_shadow_uses_current_color[i]); });
        auto box = ImmutableArray<uint8_t>::generate(input.box_shadow_uses_current_color.size(),
            [&](size_t i) { return uint8_t(input.box_shadow_uses_current_color[i]); });
        auto prepared=std::make_shared<const FrozenColorMetadata>(border,std::move(text),std::move(box),input.background_uses_current_color);
        output=std::move(prepared); return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}
bool ColorMetadataReadView::border(size_t i) const {
    if(i>=4) throw std::out_of_range("border flag");
    return frozen_ ? frozen_->border(i) : builder_->border_uses_current_color[i];
}
bool ColorMetadataReadView::background() const noexcept { return frozen_ ? frozen_->background() : builder_->background_uses_current_color; }
size_t ColorMetadataReadView::text_shadow_count() const noexcept { return frozen_ ? frozen_->text_shadow_count() : builder_->text_shadow_uses_current_color.size(); }
bool ColorMetadataReadView::text_shadow_at(size_t i) const { return frozen_ ? frozen_->text_shadow_at(i) : bool(builder_->text_shadow_uses_current_color.at(i)); }
size_t ColorMetadataReadView::box_shadow_count() const noexcept { return frozen_ ? frozen_->box_shadow_count() : builder_->box_shadow_uses_current_color.size(); }
bool ColorMetadataReadView::box_shadow_at(size_t i) const { return frozen_ ? frozen_->box_shadow_at(i) : bool(builder_->box_shadow_uses_current_color.at(i)); }
}
