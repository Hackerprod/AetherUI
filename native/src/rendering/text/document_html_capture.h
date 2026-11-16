#pragma once
#include "selected_html_capture.h"
#include "document_text_paint.h"
#include "../document_viewport_capture.h"
namespace aether::rendering {
struct DocumentCaptureWork {
 std::uint64_t parse_ns{},styles_ns{},layout_ns{},paint_ns{},raster_ns{},nodes{},elements{},boxes{};
 aether::layout::DocumentTextBudget budget;
 text::DocumentProviderWork provider;
 text::DocumentPaintWork paint;
 std::uint64_t outline_reserved{},cmap_reserved{},raster_reserved{},shadow_commands{},software_work{};
};
struct DocumentCaptureFrame {
 Frame frame;bool coverage_complete=false;
 std::vector<std::uint8_t> request_manifest;
 std::size_t accepted_requests{},scroll_registry_count{};
 std::string source_hash,regular_hash,bold_hash;
 DocumentViewportDiagnostics viewport;
 DocumentCaptureWork work;
 // A capped sample only: neither rendering nor totals are truncated to this size.
 std::vector<text::DocumentPaintSample> samples;
};
using DocumentCaptureResult=std::variant<std::shared_ptr<const DocumentCaptureFrame>,SelectedCaptureFailure>;
DocumentCaptureResult capture_document_html(std::string_view,text::DocumentFontCatalog,
 std::uint32_t width,std::uint32_t height,text::truetype::ViewportOffset offset={},text::truetype::Cancellation* cancel=nullptr)noexcept;
// Explicit request-aware API, not yet used by CLI. Exclusive caller budget/input
// ownership required during synchronous execution (no reentrant mutation).
DocumentCaptureResult capture_document_html_requests(std::string_view,text::DocumentFontCatalog,
 std::uint32_t width,std::uint32_t height,const DocumentScrollRequests&,aether::layout::DocumentTextBudget&,
 text::truetype::ViewportOffset offset={},text::truetype::Cancellation* cancel=nullptr)noexcept;

// Private diagnostic drawing entry, deliberately not exposed by public CLI.
// Uses the same authenticated request/budget path; no successful source-coverage
// claim is made by this experimental rendering result.
DocumentCaptureResult capture_document_drawing_diagnostic(std::string_view,text::DocumentFontCatalog,
 std::uint32_t,std::uint32_t,const DocumentScrollRequests&,aether::layout::DocumentTextBudget&,
 text::truetype::ViewportOffset={},text::truetype::Cancellation* =nullptr)noexcept;

}
