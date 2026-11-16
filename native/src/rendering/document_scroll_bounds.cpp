#include "document_scroll_bounds.h"
#include <map>
namespace aether::rendering {
namespace {
void validate(const layout_scroll_bounds_s& b){
 if(b.empty)return;
 for(double x:{b.left,b.top,b.right,b.bottom})aether_check_scroll_coordinate(x);
 if(b.left>b.right||b.top>b.bottom)throw std::invalid_argument("DOCUMENT_SCROLL_BOUNDS");
}
}
DocumentScrollBoundsResult DocumentScrollBounds::compute(const Graph& graph,
 aether::layout::DocumentTextBudget& budget,const std::function<void()>& check){
 if(!graph.root||graph.nodes.empty())throw std::invalid_argument("DOCUMENT_SCROLL_GRAPH");
 if(graph.nodes.size()>65536)throw std::length_error("DOCUMENT_SCROLL_TREE_LIMIT");
 struct Frame {std::size_t index,next;layout_scroll_bounds_s bounds;};
 DocumentScrollBoundsResult result;std::vector<Frame> stack;
 std::map<const aether_layout_box_s*,std::size_t> index;
 budget.bytes(graph.nodes.size(),sizeof(std::uint8_t));std::vector<std::uint8_t> seen(graph.nodes.size(),0);
 // Index preparation is a separate charged pass, not hidden inside tree counts.
 for(std::size_t i=0;i<graph.nodes.size();++i){
  budget.normalized(1);++result.index_nodes;if(check)check();const auto& n=graph.nodes[i];
  budget.bytes(1,sizeof(decltype(index)::value_type)+4*sizeof(void*));
  if(!n.key||!index.emplace(n.key,i).second)throw std::invalid_argument("DOCUMENT_SCROLL_DUPLICATE");
 }
 const auto event=[&]{budget.normalized(1);if(check)check();};
 const auto enter=[&](const aether_layout_box_s* key){
  event();if(result.nodes>=65536||stack.size()>=256)throw std::length_error("DOCUMENT_SCROLL_TREE_LIMIT");
  const auto found=index.find(key);if(found==index.end())throw std::invalid_argument("DOCUMENT_SCROLL_OWNER");
  const auto i=found->second;if(seen[i]++)throw std::invalid_argument("DOCUMENT_SCROLL_DUPLICATE");
  const auto& n=graph.nodes[i];validate(n.own);
  const auto& p=n.padding;
  for(double x:{double(p.x),double(p.y),double(p.width),double(p.height),double(p.x)+p.width,double(p.y)+p.height})aether_check_scroll_coordinate(x);
  if(p.width<0||p.height<0)throw std::invalid_argument("DOCUMENT_SCROLL_CLIP");
  budget.bytes(1,sizeof(Frame));stack.push_back({i,0,n.own});++result.nodes;
 };
 enter(graph.root);
 while(!stack.empty()){
  auto& frame=stack.back();const auto& node=graph.nodes[frame.index];
  if(frame.next<node.children.size()){
   event();if(result.edges>=65535)throw std::length_error("DOCUMENT_SCROLL_EDGE_LIMIT");++result.edges;
   // children belongs to const Graph, not mutable live layout adjacency.
   const auto* key=node.children[frame.next++];enter(key);continue;
  }
  const auto complete=frame.bounds;stack.pop_back();
  if(stack.empty()){result.bounds=complete;break;}
  event();++result.combines;const auto& parent=graph.nodes[stack.back().index];
  auto contribution=aether_scroll_clip_descendant(complete,parent.padding,parent.overflow_x,parent.overflow_y);
  validate(contribution);aether_scroll_unite(stack.back().bounds,contribution);
 }
 if(result.nodes!=graph.nodes.size()||result.edges+1!=result.nodes||result.combines!=result.edges)throw std::invalid_argument("DOCUMENT_SCROLL_TREE");
 if(check)check();return result;
}
}
