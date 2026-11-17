#pragma once
#include "../text/shaped_run.h"
namespace aether::layout { class BoxStyleSnapshot; }
struct aether_node_s;
namespace aether::rendering::html_ltr {
class HtmlLtrSession;
class HtmlLtrMeasureBridge {
public:
 virtual ~HtmlLtrMeasureBridge()=default;
 virtual text::Measurement measure(const char*,const aether::layout::BoxStyleSnapshot&,float,aether_node_s*,bool transport)=0;
};
}
