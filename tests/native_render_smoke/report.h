#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace aether::smoke {
enum class Status { pass,skip,fail };
enum class Provenance { measured,not_measured,synthetic_test };
enum class Phase:std::size_t { build,geometry_render,geometry_png,geometry_verify,
 text_layout,text_draw,text_png,text_verify,count };
struct PhaseResult {Status status{Status::skip};std::string reason{"not_run"};std::optional<std::int64_t> duration_ns;};
struct Report {
 std::string run_id;
 Provenance provenance{Provenance::not_measured};
 std::array<PhaseResult,static_cast<std::size_t>(Phase::count)> phases;
 std::string cairo_version{"unknown"},pango_version{"unknown"};
 std::optional<double> text_width,text_height,text_baseline;
};
bool valid_run_id(std::string_view) noexcept;
bool coherent(const Report&) noexcept;
Status overall(const Report&) noexcept;
// Acceptance still requires successful final report.log installation by runner.
bool accepts_real_smoke(const Report&) noexcept;
std::string serialize(const Report&);
}
