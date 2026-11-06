#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

struct css_variable_budget;

// Sources exist only while computing one element, never in runtime node state.
struct custom_property_source_s {
    std::string source;
    bool initial = false;
};
using custom_property_sources_s = std::unordered_map<std::string, custom_property_source_s>;

enum class custom_property_status { valid, invalid, work_exhausted };
struct computed_custom_property_s {
    custom_property_status status = custom_property_status::invalid;
    std::string value;
};

class computed_custom_property_scope_s;
using computed_custom_property_scope_ptr = std::shared_ptr<const computed_custom_property_scope_s>;
class computed_custom_property_scope_s {
public:
    std::unordered_map<std::string, computed_custom_property_s> local;
    explicit computed_custom_property_scope_s(computed_custom_property_scope_ptr parent = {})
        : parent_(std::move(parent)) {}
    ~computed_custom_property_scope_s();
    computed_custom_property_scope_s(const computed_custom_property_scope_s&) = delete;
    computed_custom_property_scope_s& operator=(const computed_custom_property_scope_s&) = delete;
    const computed_custom_property_scope_s* parent() const { return parent_.get(); }
private:
    // Published handles are const. Only exclusive destruction detaches this link,
    // avoiding recursive shared_ptr destruction on arbitrarily deep scope chains.
    mutable computed_custom_property_scope_ptr parent_;
};

computed_custom_property_scope_ptr compute_custom_property_scope(
    const custom_property_sources_s& sources, computed_custom_property_scope_ptr parent,
    int32_t max_depth, int32_t max_expansion_length);

// Explicit named root: a decoded key is never reinterpreted as CSS source.
computed_custom_property_s resolve_custom_property_root_with_budget(
    const std::string& name, const custom_property_sources_s& sources,
    const computed_custom_property_scope_s* parent, int32_t max_depth,
    int32_t max_expansion_length, css_variable_budget& budget);

bool resolve_computed_value_with_budget(const char* source,
    const computed_custom_property_scope_s* scope, int32_t max_depth,
    int32_t max_expansion_length, css_variable_budget& budget, std::string& out_value);
bool resolve_computed_value(const char* source,
    const computed_custom_property_scope_s* scope, int32_t max_depth,
    int32_t max_expansion_length, std::string& out_value);
