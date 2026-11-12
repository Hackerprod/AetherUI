#pragma once
#include "../src/rendering/software/software_renderer.h"
namespace aether::rendering::r8_reference {
using software::Result; using software::Status;
namespace prep=aether::rendering::preparation;
Result render(FrameStore&,const prep::PreparedFrame&,prep::Cancellation* = nullptr) noexcept;
}
