#pragma once
#include "../layout/layout_scroll_bounds.h"
#include "../layout/document_text_context.h"
namespace aether::rendering {
class DocumentViewportAuthority;
struct DocumentScrollBoundsResult {layout_scroll_bounds_s bounds;std::uint64_t index_nodes{},nodes{},edges{},combines{};};
// Pure traversal of private, immutable authenticated snapshots. Pointer values
// are identity keys ONLY: no layout/DOM pointer is dereferenced during callbacks.
// The issuer validates membership/static geometry/proxy provenance and charges
// Graph/Node/children copies before constructing Graph, then checks its complete
// live authority evidence after compute. This result is not itself authority.
class DocumentScrollBounds final {
 friend class DocumentViewportAuthority;
 friend class aether::layout::DocumentTextTestAccess;
 struct Node {
  const aether_layout_box_s* key{};
  layout_scroll_bounds_s own;
  aether_rectf padding{};
  int overflow_x{},overflow_y{};
  std::vector<const aether_layout_box_s*> children;
 };
 struct Graph {
  const aether_layout_box_s* const root;
  const std::vector<Node> nodes;
  Graph(const aether_layout_box_s* r,std::vector<Node>&& n):root(r),nodes(std::move(n)){}
 };
 static DocumentScrollBoundsResult compute(const Graph&,
     aether::layout::DocumentTextBudget&,const std::function<void()>&);
};
}
