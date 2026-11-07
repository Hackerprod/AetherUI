#include "custom_property_scope_internal.h"
#include "css_variable_tokens.h"

computed_custom_property_scope_s::~computed_custom_property_scope_s() {
    auto current = std::move(parent_);
    while (current && current.use_count() == 1) {
        auto next = std::move(current->parent_);
        current.reset(); // Its parent is now empty, so destruction cannot recurse.
        current = std::move(next);
    }
}

computed_custom_property_scope_ptr compute_custom_property_scope(
    const custom_property_sources_s& sources, computed_custom_property_scope_ptr parent,
    int32_t max_depth, int32_t max_expansion_length) {
    // Do not publish a partial map, nor replace sources in place. As in the
    // surrounding native core, allocation failures propagate; there is no OOM
    // recovery contract or successful return with the previous generation.
    auto scope = std::make_shared<computed_custom_property_scope_s>(std::move(parent));
    for (const auto& entry : sources) {
        css_variable_budget budget;
        auto value = resolve_custom_property_root_with_budget(entry.first, sources,
            scope->parent(), max_depth, max_expansion_length, budget);
        scope->local.emplace(entry.first, std::move(value));
    }
    return scope;
}

#include "frozen_runtime_metadata.h"
#include <stdexcept>
namespace aether::style {
FrozenScopeRef compute_frozen_custom_property_scope(const custom_property_sources_s& sources, FrozenScopeRef parent,
    int32_t max_depth, int32_t max_expansion_length) {
    CustomPropertyInput local;
    for(const auto& entry:sources) {
        css_variable_budget budget;
        auto value=resolve_frozen_custom_property_root_with_budget(entry.first,sources,parent.get(),max_depth,max_expansion_length,budget);
        local.emplace(entry.first,std::move(value));
    }
    FrozenScopeRef result;
    auto status=freeze_custom_scope(local,std::move(parent),result);
    if(status==AETHER_ERROR_OUT_OF_MEMORY)throw std::bad_alloc();
    if(status!=AETHER_OK)throw std::invalid_argument("custom scope freeze");
    return result;
}
}
