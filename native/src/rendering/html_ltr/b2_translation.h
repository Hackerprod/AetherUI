#pragma once
#include <aether/aether.h>
#include <cstdint>
struct aether_layout_result_s;
struct aether_node_s;
namespace aether::rendering::text { class ShapedRun; }
namespace aether::rendering::html_ltr {
class B2TranslationSink;
// Construction-only receipt. Never stored in a box, snapshot or shaped product.
struct B2Translation {
 const B2TranslationSink* session;
 aether_layout_result_s* layout;
 uint64_t operation;
 const text::ShapedRun* run;
 aether_node_s* owner;
 const void* style;
 aether_rectf before;
 float baseline,dx,dy;
};
class B2TranslationSink {
public:
 virtual ~B2TranslationSink()=default;
 virtual void begin(aether_layout_result_s*)=0;
 virtual void end(aether_layout_result_s*) noexcept=0;
 virtual uint64_t operation(aether_layout_result_s*)=0;
 virtual void visit(aether_layout_result_s*)=0;
 virtual void translate(const B2Translation&)=0;
};
#ifdef AETHER_HTML_LTR_TESTING
namespace testing {
// Test-only seams, default inert for all existing test consumers.
inline void (*translation_operation)(uint64_t&)=nullptr;
inline bool (*translation_event)(B2TranslationSink*,B2Translation&)=nullptr;
inline void (*translation_applied)(B2TranslationSink*,const B2Translation&)=nullptr;
}
#endif
}
