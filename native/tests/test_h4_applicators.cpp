#include "../src/style_runtime/style_property_registry_internal.h"
#include <cassert>
#include <cstring>
#include <iostream>
extern void builtin_properties_register(style_property_registry_s*);
int main(){
 style_property_registry_s registry;builtin_properties_register(&registry);registry.seal();
 for(auto name:{"padding-left","flex-basis"}) {
  auto* d=registry.by_name.at(name);auto flat=aether_computed_style_default();auto before=flat;
  assert(!d->apply_checked(&flat,"max(20px,50%)").success);assert(!std::memcmp(&before,&flat,sizeof flat));
  assert(!d->apply_checked(&flat,"max(20px,garbage)").success);assert(!std::memcmp(&before,&flat,sizeof flat));
  bool threw=false;try{d->apply(&flat,"max(20px,50%)");}catch(const std::invalid_argument&){threw=true;}assert(threw);assert(!std::memcmp(&before,&flat,sizeof flat));
  measure_state_s state;state.document=std::make_shared<const document_measure_context_s>();
  measure_property_delta_s delta;std::string property=name;measure_apply_state_s probe(state,delta,property);
  property_apply_context_s ctx{&flat,nullptr,&probe};assert(d->apply_with_context_checked(ctx,"max(20px,50%)").success);
  assert(!state.contextual_lengths.size());assert(probe.read().tree(property));
  assert(d->apply_with_context_checked(ctx,"7px").success);assert(!probe.read().tree(property));assert(probe.property_dependencies()==0);
  assert(!d->apply_with_context_checked(ctx,"max(1px,wat)").success);assert(!probe.read().tree(property));
  aether_computed_style_free(&flat);
 }
 std::cout<<"PASS H4 checked/void flat reject no mutation, contextual probes set/erase/syntax atomic\n";
}
