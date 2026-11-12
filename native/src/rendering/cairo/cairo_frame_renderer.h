#pragma once
#include "cairo_geometry.h"
namespace aether::rendering::cairo_adapter {
enum class RenderStatus { ok, suspended, wrong_thread, preflight_failed,
 incompatible_platform, frame_failed, cairo_failed };
// Preparation errors carry source index/tag; Cairo runtime status is global
// (no claim that sticky backend errors identify a specific command).
struct RenderResult {
 RenderStatus status{RenderStatus::ok};
 std::optional<Error> preparation_error;
 FrameStatus frame_status{FrameStatus::ok};
 int cairo_status{};
};
// Synchronous and owner-thread only. No core, callbacks, window or presentation.
// Never retain a FrameStore pixel borrow across this operation.
RenderResult render_cairo(FrameStore&,const prep::PreparedFrame&,prep::Color clear) noexcept;
}
