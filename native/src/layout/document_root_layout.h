#pragma once
#include "layout_types.h"
namespace aether::rendering { class DocumentCaptureOperation; }
namespace aether::layout {
class DocumentTextTestAccess;
struct DocumentRootGeometryUnsupported {};
// Private document-only entry. Live owner-thread DOM/map handles remain owned
// by the caller throughout layout; no global mode and no retained raw callback.
class DocumentRootLayoutAccess final {
 friend class aether::rendering::DocumentCaptureOperation;
 friend class DocumentTextTestAccess;
 static aether_status_t layout(aether_layout_engine_t,aether_doc_t,aether_style_map_t,
                              float,float,aether_layout_result_t*) noexcept;
};
}
