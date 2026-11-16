#pragma once
#include "document_attempt_capsule.h"
namespace aether::layout {
class DocumentRootLayoutAccess;
// Inactive authorized control surface. No provider/normalizer/painter friendship;
// only the genuine root layout implementation may later propagate these values.
class DocumentAttemptControl final {
 friend class DocumentRootLayoutAccess;
 friend class DocumentAttemptLayout;
 friend class DocumentTextTestAccess;
 using Capability=DocumentAttemptCapsule::Capability;
 using Selected=DocumentAttemptCapsule::Selected;
 template<class Function>
 static auto with_owner(aether_layout_engine_s* engine,Function&& function){
  auto context=DocumentTextAccess::attempt_control_entry(engine);
  // Only fixed internal lambdas below, never user callbacks. Context/budget
  // borrow is confined to this synchronous call and cannot escape in a result.
  try{return function(context.first,*context.second);}
  catch(...){DocumentTextAccess::capture_failure(engine);throw;}
 }
 static Capability initialize_root(aether_layout_engine_s* engine){
  DocumentTextAccess::initialize_attempt_topology(engine);
  return with_owner(engine,[](const auto& capsule,auto& budget){return capsule->open_root(capsule,budget);});
 }
 static Capability open_child(aether_layout_engine_s* engine,Capability& parent){
  return with_owner(engine,[&](const auto& capsule,auto& budget){return capsule->open_child(parent,budget);});
 }
 static Capability close_scope(aether_layout_engine_s* engine,Capability& scope){
  return with_owner(engine,[&](const auto& capsule,auto& budget){return capsule->close_scope(scope,budget);});
 }
 static void adopt(aether_layout_engine_s* engine,Capability& parent,Capability& ticket){
  with_owner(engine,[&](const auto& capsule,auto& budget){capsule->adopt(parent,ticket,budget);});
 }
 static void discard(aether_layout_engine_s* engine,Capability& ticket){
  with_owner(engine,[&](const auto& capsule,auto&){capsule->discard(ticket);});
 }
 static std::shared_ptr<const Selected> select(aether_layout_engine_s* engine,Capability& root){
  return with_owner(engine,[&](const auto& capsule,auto& budget){return capsule->select(root,budget);});
 }
 static std::uint64_t active_id(aether_layout_engine_s* engine,Capability& scope){
  return with_owner(engine,[&](const auto& capsule,auto&){return capsule->scope_id(scope);});
 }
 static std::shared_ptr<const int> selected_identity(const aether_layout_engine_s* engine,const std::shared_ptr<const Selected>& selected){
  const auto capsule=DocumentTextAccess::attempt_selection_entry(engine);
  return capsule->selection_identity(selected);
 }
 static bool selected_attempt(const aether_layout_engine_s* engine,const std::shared_ptr<const Selected>& selected,
  std::uint64_t id,DocumentTextBudget& budget){
  const auto capsule=DocumentTextAccess::attempt_selection_entry(engine);
  return capsule->selected_attempt(selected,id,budget);
 }
 // Only before final commit/finish. No mutation or callbacks: the complete
 // finalization audit may safely use this after all callback-bearing work.
 static void audit_selected_for_publication(const aether_layout_engine_s* engine,
  const std::shared_ptr<const Selected>& selected){
  const auto capsule=DocumentTextAccess::attempt_selection_entry(engine);
  capsule->validate_selected(selected);
 }
 static void validate_selected(aether_layout_engine_s* engine,const std::shared_ptr<const Selected>& selected){
  with_owner(engine,[&](const auto& capsule,auto&){capsule->validate_selected(selected);});
 }
};
}
