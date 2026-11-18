#pragma once
#include "../text/shaped_run.h"
#include "control_presentation.h"
#include "b2_inline_transport.h"
#include <stdexcept>
#include <span>
#include "../../layout/b3_width_evidence.h"
struct aether_layout_box_s;
struct aether_layout_result_s;
namespace aether::layout { class BoxStyleSnapshot; }
struct aether_node_s;
namespace aether::rendering::html_ltr {
class HtmlLtrSession;
class InteractionNativeDocument;
class HtmlLtrMeasureBridge {
public:
 virtual ~HtmlLtrMeasureBridge()=default;
 // Explicit opt-in only. Production B0/B1 retains its existing admission.
 virtual bool b3_policy() const noexcept { return false; }
 virtual void b3_begin_result(aether_layout_result_s*,uint64_t) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_box(aether_layout_result_s*,aether_layout_box_s*,uint64_t,uint64_t,uint64_t) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_complete(aether_layout_box_s*) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_replace(aether_layout_result_s*,aether_layout_box_s*) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_offset(aether_layout_box_s*,float,float) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_discard(aether_layout_result_s*,aether_layout_box_s*,const B2InlineResult&,std::span<aether_node_s* const>) { throw std::logic_error("B3 unavailable"); }
 virtual void b3_width(aether_layout_result_s*,aether_layout_box_s*,aether_layout_box_s*,float,float,B3WidthReason,bool,bool) { throw std::logic_error("B3 unavailable"); }
 virtual bool b2_inline_policy() const noexcept { return false; }
 virtual bool b2_inline_owner(aether_node_s*) const noexcept { return b2_inline_policy(); }
 virtual std::pmr::memory_resource* b2_resource() { throw std::logic_error("B2 resource unavailable"); }
 virtual bool b2_begin_collection(const B2InlineResult&) { throw std::logic_error("B2 collection unavailable"); }
 virtual void b2_work(B2Work,size_t) { throw std::logic_error("B2 work accounting unavailable"); }
 // Called on each collection, before measuring. The session counts logical
 // units/visible scalars once per frozen context; visits remain cumulative.
 virtual void b2_preflight(const B2InlineResult&,size_t,size_t) { throw std::logic_error("B2 preflight unavailable"); }
 virtual void b2_inline_result(const B2InlineResult&) { throw std::logic_error("B2 provenance unavailable"); }
 virtual std::shared_ptr<const ControlPresentation> control(aether_node_s*,const aether::layout::BoxStyleSnapshot&) { return {}; }
 virtual std::uint64_t occurrence(const text::ShapedRun*) const { return 0; }
 virtual text::Measurement measure(const char*,const aether::layout::BoxStyleSnapshot&,float,aether_node_s*,bool transport)=0;
};
}
