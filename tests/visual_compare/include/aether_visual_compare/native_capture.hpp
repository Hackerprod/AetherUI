#pragma once
#include "resource_validation.hpp"
#include <span>
namespace avc {
struct NativeCaptureResult {
 bool captured=false; int exit_code=14; std::string reason_code="NATIVE_INTERNAL";
 std::int32_t public_status=0; std::uint32_t abi_version=0,profile=0,limitations=0;
 std::uint64_t capabilities=0,sequence=0; unsigned width=0,height=0,stride=0;
 std::string runtime_version,regular_sha256,bold_sha256,png;
};
void apply_native_capture(Report&,const NativeCaptureResult&);
NativeCaptureResult capture_native(const Request&,const ValidatedInputs&,const CancellationToken&);
// Dependency-free PNG RGBA8 encoder: filter 0, zlib stored DEFLATE blocks.
// Input BGRA8 is premultiplied. Output channels: min(255,(c*255+a/2)/a).
// a=0 => RGB=0. Rejects malformed dimensions/size; maximum 2048 x 2048.
std::string encode_native_png(unsigned width,unsigned height,std::span<const std::uint8_t> bgra);
}
