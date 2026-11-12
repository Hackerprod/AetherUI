#pragma once
// Private synchronous, borrowed audit hook for the isolated validator. Not a
// second resolver or public ABI: values are observed at native cascade points.
#include <aether/aether.h>
#include <string>
namespace aether::style { class FrozenCustomPropertyScope; }
struct css_variable_budget;
struct style_value_audit_s {
 css_variable_budget* work=nullptr;
 void* context=nullptr;
 void (*scope)(void*,const aether::style::FrozenCustomPropertyScope*)=nullptr;
 void (*value)(void*,aether_node_t,const std::string&,const std::string&,const std::string*)=nullptr;
};
void style_runtime_set_value_audit(aether_style_runtime_t,const style_value_audit_s*);
