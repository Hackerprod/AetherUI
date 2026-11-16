#pragma once
#include "document_text_provider.h"
#include "../native_paint_list.h"
namespace aether::rendering::text {
struct DocumentPaintSample {
 std::uint64_t occurrence{},command_index{},glyphs{};
 aether_rectf logical_rect{};float logical_baseline{},raster_baseline{};
 std::int64_t first_x26_6{},last_x26_6{},baseline26_6{};
};
struct DocumentPaintWork {std::uint64_t commands{},placed_glyphs{},mask_hits{},mask_misses{},mask_bytes{},raster_work{},raster_reserved{},edge_scan_pairs{},active_pairs{},prepass_work{},prepass_reserved{},main_reserved{};};
struct DocumentPaintProduct {
 std::vector<std::shared_ptr<const GlyphMaskA8>> masks;
 std::vector<DocumentPaintSample> samples;
 DocumentPaintWork work;
};
// Only the capture operation can replace private paint transports. These
// products never rewrite logical layout or authorize caller-supplied runs.
class DocumentTextPainter final {
 friend class aether::rendering::DocumentCaptureOperation;
 friend class aether::layout::DocumentTextTestAccess;
 static DocumentPaintProduct finalize_drawing(aether_layout_engine_s*,aether_layout_result_s*,NativePaintList&,DocumentTextProvider&);
 static DocumentPaintProduct finalize(aether_layout_engine_s*,aether_layout_result_s*,NativePaintList&,
     DocumentTextProvider&);
};
}
