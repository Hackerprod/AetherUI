#pragma once
#include <aether/rendering/preparation/paint_snapshot.hpp>
#include <string_view>
namespace aether::rendering::text {
namespace prep = aether::rendering::preparation;
enum class Purpose { intrinsic, paint };
struct IntrinsicInput {
 std::string_view text, family;
 float font_size{};
 std::int32_t weight{};
 float used_line_height{}; // DIPs, not a multiplier
 std::optional<float> max_width; // zero is distinct from unconstrained
};
class Request;
using Result = std::variant<Request,prep::ErrorCode>;
class Request {
 std::string text_,family_;
 Purpose purpose_;
 float size_;
 std::int32_t weight_;
 bool wrap_;
 std::optional<float> width_,height_,used_line_height_;
 std::optional<std::int32_t> alignment_;
 Request(std::string_view,std::string_view,Purpose,float,std::int32_t,bool,
         std::optional<float>,std::optional<float>,std::optional<float>,std::optional<std::int32_t>);
 friend Result intrinsic(const IntrinsicInput&) noexcept;
 friend Result paint(const prep::Text&) noexcept;
public:
 // Value requests are intentionally copy-only: moving copies and leaves the
 // source valid. Copies can allocate/throw; factories catch bad_alloc.
 Request(const Request&)=default;
 Request& operator=(const Request&)=delete;
 const std::string& text() const noexcept {return text_;}
 const std::string& family() const noexcept {return family_;}
 Purpose purpose() const noexcept {return purpose_;}
 float font_size() const noexcept {return size_;}
 std::int32_t weight() const noexcept {return weight_;}
 bool wrap() const noexcept {return wrap_;}
 std::optional<float> width() const noexcept {return width_;}
 std::optional<float> height() const noexcept {return height_;}
 std::optional<float> used_line_height() const noexcept {return used_line_height_;}
 std::optional<std::int32_t> alignment() const noexcept {return alignment_;}
 // Exact request equality only. NOT provider/layout equivalence or cache key.
 bool operator==(const Request&) const = default;
};
Result intrinsic(const IntrinsicInput&) noexcept;
Result paint(const prep::Text&) noexcept;
}
