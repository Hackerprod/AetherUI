#pragma once
#include <aether/aether.h>
#include <aether/aether_dom_style_binding.h>
#include <memory>
#include <string>
struct DomStyleBinding;
struct DomStyleSlotStage;
using DomStyleBindingRef = std::shared_ptr<DomStyleBinding>;
using DomStyleSlotStageRef = std::shared_ptr<DomStyleSlotStage>;
// Prepare owns every allocation. DOM preflight validates ALL participants before
// any commit. Commit is owner-thread-only, callback-free, allocation-free, and
// requires a successful validation with no intervening mutation.
aether_status_t aether_dom_style_prepare_slot(const DomStyleBindingRef& binding,
    const std::string& source, DomStyleSlotStageRef& out) noexcept;
bool aether_dom_style_validate_slot(const DomStyleSlotStageRef& stage) noexcept;
void aether_dom_style_commit_slot(DomStyleSlotStageRef& stage) noexcept;
#ifdef AETHER_DOM_STYLE_TESTING
void aether_dom_style_test_set_limits(aether_style_runtime_t runtime,
    uint64_t epoch, int64_t next_handle, int32_t next_rule) noexcept;
#endif
#include <vector>
struct DomStyleSlotBatch;
using DomStyleSlotBatchRef = std::shared_ptr<DomStyleSlotBatch>;
// Finalization groups shared runtimes and reserves one non-overlapping ID range.
// Grouped stages cannot be published separately, including after batch abort.
aether_status_t aether_dom_style_finalize_slots(const std::vector<DomStyleSlotStageRef>& stages,
    DomStyleSlotBatchRef& out) noexcept;
bool aether_dom_style_validate_batch(const DomStyleSlotBatchRef& batch) noexcept;
void aether_dom_style_commit_batch(DomStyleSlotBatchRef& batch) noexcept;

// Authenticate an external binding once, then validate retained authority without
// borrowing an external handle that its owner may have destroyed.
aether_status_t aether_dom_style_binding_get_ref(aether_dom_style_binding_t handle,
    aether_doc_t document, DomStyleBindingRef& out) noexcept;
bool aether_dom_style_binding_current(const DomStyleBindingRef& binding,
    aether_doc_t document) noexcept;
