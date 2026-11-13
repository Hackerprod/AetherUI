#pragma once
#include "operation_pin.h"
#include "../rendering/text/native_inline_paragraph.h"
#include <atomic>
#include <aether/aether_contextual_style.h>
#include <thread>
struct aether_layout_engine_s;
namespace aether::layout {
class SyntheticTextTestIssuer;
class NativeTextAccess;
class NativeTextTransaction;
// Proof of a closed test fixture, never issued from arbitrary application CSS.
class SyntheticFixtureAuthority final {
 friend class SyntheticTextTestIssuer;
 const MapVersionRef version_;
 const std::shared_ptr<node_identity_s> document_;
 const CallbackEpochGuard epochs_;
 SyntheticFixtureAuthority(MapVersionRef version, std::shared_ptr<node_identity_s> document)
 :version_(std::move(version)),document_(std::move(document)),epochs_(document_) {}
};
// Definition is identical in every TU. Only tests link the issuer implementation.
// This prevents accidental API use; it is not a hostile in-process security boundary.
class SyntheticTextPolicy final {
 friend class SyntheticTextTestIssuer;
 friend class NativeTextAccess;
 friend class NativeTextTransaction;
 const aether_layout_engine_s* const engine_;
 const std::shared_ptr<const SyntheticFixtureAuthority> fixture_;
 const std::shared_ptr<const int> session_;
 const std::shared_ptr<node_identity_s> document_;
 const MapVersionRef version_;
 const CallbackEpochGuard epochs_;
 const std::vector<FrozenNodeRef> owners_;
 const std::shared_ptr<const aether::rendering::text::SyntheticFaceSeal> seal_;
 const std::shared_ptr<const aether::rendering::text::ContextualMeasurementProvider> provider_;
 const std::shared_ptr<const std::atomic_bool> cancelled_;
 const std::thread::id thread_;
 SyntheticTextPolicy(const aether_layout_engine_s* engine, std::shared_ptr<const SyntheticFixtureAuthority> fixture, std::shared_ptr<const int> session, std::shared_ptr<node_identity_s> doc,
   MapVersionRef version, std::vector<FrozenNodeRef> owners,
   std::shared_ptr<const aether::rendering::text::SyntheticFaceSeal> seal,
   std::shared_ptr<const aether::rendering::text::ContextualMeasurementProvider> provider,
   std::shared_ptr<const std::atomic_bool> cancelled)
 : engine_(engine),fixture_(std::move(fixture)),session_(std::move(session)),document_(std::move(doc)),version_(std::move(version)),
   epochs_(document_),owners_(std::move(owners)),seal_(std::move(seal)),provider_(std::move(provider)),
   cancelled_(std::move(cancelled)),thread_(std::this_thread::get_id()) {}
};
class SyntheticTextTestIssuer final {
public:
 // Both functions are implemented in tests/isolated_capture/t17_test_issuer.cpp ONLY.
 // Creates a fresh built-in runtime with no external stylesheet/custom property
 // inputs, after strict whole-DOM fixture-source admission. Outputs unchanged on failure.
 static bool create_fixture_styles(aether_doc_s*, const aether_style_environment_t&,
   aether_style_runtime_t*, aether_style_map_t*, std::shared_ptr<const SyntheticFixtureAuthority>&);
 // Arbitrary contextual maps have no authority. Exact issued version required.
 static bool install(aether_layout_engine_s&, aether_doc_s*, aether_style_map_s*,
   std::shared_ptr<const SyntheticFixtureAuthority>,
   const std::vector<aether_node_s*>&,
   std::shared_ptr<const aether::rendering::text::SyntheticFaceSeal>,
   std::shared_ptr<const aether::rendering::text::ContextualMeasurementProvider>,
   std::shared_ptr<const std::atomic_bool> cancelled = {});
};
}
