#pragma once
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
namespace aether::layout {struct DocumentTextBudget;class DocumentTextTestAccess;}
namespace aether::rendering {class DocumentCaptureOperation;}
namespace aether::style {
class EmbeddedStyleSource;
class DocumentEmbeddedStyleAccess final {
 friend class aether::rendering::DocumentCaptureOperation;
 friend class aether::layout::DocumentTextTestAccess;
 static void validate(aether_style_runtime_t,aether_doc_t,aether::layout::DocumentTextBudget&);
 static aether_status_t add(aether_style_runtime_t,aether_doc_t,aether_node_t,
  int32_t,aether::layout::DocumentTextBudget&,int64_t*);
};
}
