#pragma once
#include <cstdint>
#include <string>
struct css_variable_budget;
struct custom_property_scope_s;
// Private injection point for deterministic work-limit regression tests. Does
// not change the installed C ABI or the production default budget.
bool variable_resolve_with_budget(const char* source, const custom_property_scope_s* scope,
                                 int32_t max_depth, int32_t max_expansion_length,
                                 css_variable_budget& budget, std::string& out_value);
