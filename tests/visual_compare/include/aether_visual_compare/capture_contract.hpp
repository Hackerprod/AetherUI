#pragma once
#include "model.hpp"
#include <memory>
namespace avc {
struct ApprovedAsset { std::string label, mime, sha256; std::shared_ptr<const std::vector<std::uint8_t>> bytes; };
struct CaptureRequest { std::string run_id; std::uint64_t generation; Viewport viewport; std::vector<ApprovedAsset> assets; CancellationToken cancellation; };
struct CapturedPixels { unsigned width, height, stride; std::vector<std::uint8_t> bgra_premultiplied; };
enum class CaptureState { not_implemented, blocked, cancelled, failed, captured };
struct CaptureOutcome {
 std::string run_id; std::uint64_t generation; CaptureState state;
 std::string reason_code; std::unique_ptr<CapturedPixels> pixels;
 // Ownership transfers with the outcome. A consumer must reject stale generations
 // and require pixels only for captured; no concrete backend exists in this phase.
};
struct CaptureCapabilities { bool implemented=false; bool offscreen=false; };
// No factories, runtime loading, callbacks or platform objects in this increment.
}
