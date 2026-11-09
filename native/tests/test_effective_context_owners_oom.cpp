#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/css/computed_style_handle.h"
#include "../src/css/measure_state.h"
#include "../src/style_runtime/frozen_runtime_metadata.h"
#include "../src/css/style_map_types.h"
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <cmath>
#include <string>
static long fail_after = -1;
static bool fault_delivered = false;
static size_t ordinary_test_calls = 0, ordinary_test_failures = 0;
void* operator new(std::size_t n) {
    ++ordinary_test_calls; if (fail_after >= 0 && fail_after-- == 0) { fault_delivered = true; ++ordinary_test_failures; throw std::bad_alloc(); } if (auto p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
// Route nothrow and array allocations through the same fault allocator. This
// keeps ASan allocation families matched instead of suppressing its checks.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* value, const std::nothrow_t&) noexcept { std::free(value); }
void operator delete[](void* value, const std::nothrow_t&) noexcept { std::free(value); }

void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#include "fault_inject_aligned.h"
#include "fault_inject_pmr_contract.h"


#define main d2_baseline_main
#include "test_effective_context_owners.cpp"
#undef main
static void sweep_context(bool compact,bool legacy=false) {
 unsigned rejected=0;bool completed=false;
 for(long failure=0;failure<5000;++failure) {
  Fixture f;
  if(legacy) {aether_transform_operation operation{};auto style=transformed(operation,10);install_legacy(f,unbound_record(f.node,style));}
  const auto old_layout=f.layout->effective_context_owners;const auto old_map=f.map->current;
  aether_style_transaction_t tx{};
  if(compact) {ok(aether_element_set_attribute(f.node,"style","transform:translateX(10px);backdrop-filter:blur(2px)"));
   ok(aether_style_runtime_prepare_revision(f.styles,f.doc,nullptr,&tx));}
  else if(!legacy) {auto before=aether_computed_style_default();aether_transform_operation operation{};auto after=transformed(operation,20);
   ok(aether_transition_runtime_observe(f.transitions,f.node,&before,&after,0));}
  aether_style_map_t staged=nullptr;aether_bool_t changed=0;
  fault_delivered=false;fail_after=failure;
  const auto status=compact?aether_style_transaction_preview_compact_map(tx,f.map,&staged):
   aether_layout_prepare_transition_context(f.engine,f.transitions,f.map,f.layout,&changed);
  fail_after=-1;
  CHECK(f.map->current==old_map && f.layout->effective_context_owners==old_layout);
  if(status==AETHER_OK){CHECK(!fault_delivered);completed=true;}
  else {CHECK(status==AETHER_ERROR_OUT_OF_MEMORY && fault_delivered);++rejected;
   if(compact){CHECK(!staged);ok(aether_style_transaction_preview_compact_map(tx,f.map,&staged));}
   else {CHECK(!f.engine->transition_context_prepared);f.prepare(true);}}
  if(compact) {CHECK(staged->current->backing()!=old_map->backing());aether_style_map_destroy(staged);aether_style_transaction_destroy(tx);}
  else {CHECK(f.engine->prepared_context_owners->owners.size()==1);f.build();}
  if(completed)break;
 }
 CHECK(completed && rejected);std::printf("PASS D2 %s OOM rejected=%u\n",compact?"compact staging":legacy?"legacy full context":"context preparation",rejected);
}
int main(){sweep_context(true);sweep_context(false);sweep_context(false,true);std::printf("PASS D2 OOM %u checks\n",checks);}
