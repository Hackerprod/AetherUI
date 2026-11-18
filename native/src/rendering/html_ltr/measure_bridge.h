#pragma once
#include "../text/shaped_run.h"
#include "control_presentation.h"
namespace aether::layout { class BoxStyleSnapshot; }
struct aether_node_s;
namespace aether::rendering::html_ltr {
class HtmlLtrSession;
class InteractionNativeDocument;
class HtmlLtrMeasureBridge {
public:
 virtual ~HtmlLtrMeasureBridge()=default;
 virtual std::shared_ptr<const ControlPresentation> control(aether_node_s*,const aether::layout::BoxStyleSnapshot&) { return {}; }
 virtual std::uint64_t occurrence(const text::ShapedRun*) const { return 0; }
 virtual text::Measurement measure(const char*,const aether::layout::BoxStyleSnapshot&,float,aether_node_s*,bool transport)=0;
};
}
