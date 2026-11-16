#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace aether::rendering::preparation {
enum class ImageError { InvalidInput, LimitExceeded, OutOfMemory };
class ImageResource;
using ImageRef = std::shared_ptr<const ImageResource>;
using ImageResult = std::variant<ImageRef, ImageError>;
// Input must remain valid and unmodified for the synchronous copy call.
// Canonical tightly packed BGRA8 premultiplied pixels. No external mutable alias.
class ImageResource final {
 std::uint32_t width_, height_;
 std::vector<std::uint8_t> pixels_;
 ImageResource(std::uint32_t w, std::uint32_t h, std::span<const std::uint8_t> p)
  : width_(w), height_(h), pixels_(p.begin(),p.end()) {}
 public:
 ImageResource(const ImageResource&) = delete;
 ImageResource& operator=(const ImageResource&) = delete;
 static ImageResult copy(std::uint32_t width, std::uint32_t height,
                         std::span<const std::uint8_t> bgra) noexcept;
 std::uint32_t width() const noexcept { return width_; }
 std::uint32_t height() const noexcept { return height_; }
 std::size_t stride() const noexcept { return std::size_t(width_)*4; }
 std::span<const std::uint8_t> pixels() const noexcept { return pixels_; }
};
class ImageSet;
using ImageSetResult = std::variant<std::shared_ptr<const ImageSet>, ImageError>;
// Budget applies only to unique pixel buffers retained by this set. Not process RAM,
// decoder scratch, allocator overhead, nor resources retained by other frames.
class ImageSet final {
 std::vector<ImageRef> resources_;
 std::size_t bytes_{};
 ImageSet(std::vector<ImageRef>&& refs, std::size_t bytes)
  : resources_(std::move(refs)), bytes_(bytes) {}
 public:
 ImageSet(const ImageSet&) = delete;
 ImageSet& operator=(const ImageSet&) = delete;
 static ImageSetResult create(std::span<const ImageRef> refs,
   std::size_t max_bytes, std::size_t max_unique) noexcept;
 std::span<const ImageRef> resources() const noexcept { return resources_; }
 std::size_t bytes() const noexcept { return bytes_; }
};
}
