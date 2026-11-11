#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace avc {
struct CancellationToken { std::function<bool()> requested; bool cancelled() const { return requested && requested(); } };
enum class Command { help, version, inspect, capture };
struct Viewport { unsigned width=1, height=1, scale_quarters=4, pixel_width=1, pixel_height=1; };
struct Request { Command command=Command::help; std::string html, resource_root, manifest, out; Viewport viewport; };
struct Error { int code; std::string stage, detail_code; std::optional<std::string> resource_label; bool operator==(const Error&) const = default; };
struct Stage { std::string name, status="not_run", reason_code="NOT_REQUESTED"; };
struct Report {
 std::string run_id; Request request;
 std::optional<std::string> input_sha256, manifest_sha256;
 std::optional<std::uint64_t> input_size;
 std::optional<double> host_inspection_wall_ms;
 std::array<Stage,10> stages{{{"arguments"},{"output_prepare"},{"input_read"},{"manifest_parse"},{"resource_validation"},{"aether_capture"},{"webview2_capture"},{"comparison"},{"coverage"},{"output_commit"}}};
 std::vector<Error> errors;
};
struct Resource { std::string path, mime; std::uint64_t size_bytes=0; std::string sha256, role; };
struct Manifest { std::vector<Resource> entries; };
std::string requested_scope(const Request&);
void add_error(Report&, Error);
int exit_code(const Report&);
std::string overall_status(const Report&);
std::string primary_error(const Report&);
}
