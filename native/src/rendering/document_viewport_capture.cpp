#include "document_viewport_capture.h"
#include "document_scroll_bounds.h"
#include "../layout/scrollbar_geometry.h"
#include "../css/border_radii.h"
#include "../layout/layout_internal.h"
#include "../layout/layout_scroll_bounds.h"
#include "../layout/native_text_transaction.h"
#include "../css/style_map_types.h"
#include <set>
namespace aether::rendering {
namespace {
using E=text::truetype::HtmlTextError;
bool rect_same(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool matrix_same(aether_transform_matrix a,aether_transform_matrix b){return a.m11==b.m11&&a.m12==b.m12&&a.m21==b.m21&&a.m22==b.m22&&a.dx==b.dx&&a.dy==b.dy;}
void bounded(double x){if(!std::isfinite(x)||x<0)throw E::unsupported;if(x>65536)throw E::limit_exceeded;}
void signed_bound(double x){if(!std::isfinite(x))throw E::unsupported;if(std::abs(x)>65536)throw E::limit_exceeded;}
void signed_rect_bound(aether_rectf r){bounded(r.width);bounded(r.height);for(double v:{double(r.x),double(r.y),double(r.x)+r.width,double(r.y)+r.height})signed_bound(v);}
void rect_bound(aether_rectf r){for(double v:{double(r.x),double(r.y),double(r.width),double(r.height),double(r.x)+r.width,double(r.y)+r.height})bounded(v);}
// A negative abstract normal-flow margin ledger is not a negative painted box.
// Preserve the engine's stored float arithmetic, including collapsed margins.
bool abstract_margin_top(const aether_layout_box_s* b,const aether_layout_box_s* parent){
 if(b->margin_box.y>=0)return false;
 if(b->kind!=LAYOUT_BOX_ELEMENT||!b->element||b->style.display!=AETHER_DISPLAY_BLOCK||b->style.position!=AETHER_POSITION_STATIC||b->is_atomic_control||b->is_initial_canvas||b->scroll_proxy.valid||(b->scroll_completion_flags&LAYOUT_SCROLL_TABLE_CELL)||b->visual_has_transform||b->own_relative_dx!=0||b->own_relative_dy!=0||(parent&&parent->style.display!=AETHER_DISPLAY_BLOCK))throw E::unsupported;
 for(const auto v:{b->style.margin.top,b->style.margin.right,b->style.margin.bottom,b->style.margin.left})if(v.unit!=AETHER_CSS_UNIT_PX||!std::isfinite(v.value)||v.value<0||v.value>65536||v.linear_percent||v.linear_em||v.linear_rem||v.linear_vw||v.linear_vh)throw E::unsupported;
 const float before=b->collapsed_margin_before,after=b->collapsed_margin_after;
 bounded(before);bounded(after);if(before<=0)throw E::unsupported;
 const float y=b->border_box.y-before,intermediate=b->border_box.height+before,height=intermediate+after;
 if(!std::isfinite(y)||std::abs(y)>65536||!std::isfinite(intermediate)||!std::isfinite(height)||b->margin_box.y!=y||b->margin_box.height!=height)throw E::unsupported;
 for(double v:{double(b->margin_box.x),double(b->margin_box.width),double(b->margin_box.height),double(b->margin_box.x)+b->margin_box.width,double(b->margin_box.y)+b->margin_box.height})bounded(v);
 return true;
}
// Admission must not treat a small real curve as a rectangular clip.
bool clip_radii_exact_zero(aether_corner_radii r){return r.top_left_x==0&&r.top_left_y==0&&r.top_right_x==0&&r.top_right_y==0&&r.bottom_right_x==0&&r.bottom_right_y==0&&r.bottom_left_x==0&&r.bottom_left_y==0;}
bool circular(aether_corner_radii r){const float v=r.top_left_x;return std::isfinite(v)&&v>=0&&v==r.top_left_y&&v==r.top_right_x&&v==r.top_right_y&&v==r.bottom_right_x&&v==r.bottom_right_y&&v==r.bottom_left_x&&v==r.bottom_left_y;}
void rounded_owner(const aether_layout_box_s* b,aether_corner_radii outer,aether_corner_radii inner){
 if(!circular(outer)||!circular(inner))throw E::unsupported;
 const auto raw=b->style.border_radii;const auto first=raw.top_left.horizontal.value;
 for(auto c:{raw.top_left,raw.top_right,raw.bottom_right,raw.bottom_left})for(auto v:{c.horizontal,c.vertical})
  if(v.unit!=AETHER_CSS_UNIT_PX||!std::isfinite(v.value)||v.value<0||v.value!=first||v.linear_percent||v.linear_em||v.linear_rem||v.linear_vw||v.linear_vh)throw E::unsupported;
 if(clip_radii_exact_zero(outer))return;
 const float width=b->used_border.top;
 if(!std::isfinite(width)||width<0||width!=b->used_border.left||width!=b->used_border.right||width!=b->used_border.bottom)throw E::unsupported;
 if(width>0){
  for(auto style:{b->style.border_styles.top,b->style.border_styles.left,b->style.border_styles.right,b->style.border_styles.bottom})if(style!=AETHER_BORDER_STYLE_SOLID)throw E::unsupported;
  const auto color=b->style.border_colors.top;
  for(auto c:{b->style.border_colors.left,b->style.border_colors.right,b->style.border_colors.bottom})if(c.r!=color.r||c.g!=color.g||c.b!=color.b||c.a!=color.a)throw E::unsupported;
  // Existing centered-stroke primitive cannot reconstruct a curved outer
  // shape after its centerline radius collapses to zero. Reject, not square.
  if(outer.top_left_x<=width/2||b->border_box.width<=width||b->border_box.height<=width)throw E::unsupported;
 }
 if(!b->style.style_owner()->view().box_shadows().empty())throw E::unsupported;
}
aether_rectf thumb(aether_rectf track,bool horizontal,double range,double offset){const double length=horizontal?track.width:track.height;const double size=std::min(length,std::max(std::min(24.,length),length*length/(length+range)));const double position=(horizontal?track.x:track.y)+(length-size)*(range?offset/range:0);return horizontal?aether_rectf{float(position),track.y,float(size),track.height}:aether_rectf{track.x,float(position),track.width,float(size)};}
aether_scrollbar_geometry_t chrome(std::uint32_t w,std::uint32_t h,double rx,double ry,text::truetype::ViewportOffset offset){
 const bool x=rx>0,y=ry>0;if((x||y)&&(w<8||h<8))throw E::unsupported;
 aether_scrollbar_geometry_t out{};out.horizontal_visible=x;out.vertical_visible=y;
 if(y){out.vertical_track={float(w)-6,0,4,float(h)-(x?6:0)};out.vertical_thumb=thumb(out.vertical_track,false,ry,offset.y);}
 if(x){out.horizontal_track={0,float(h)-6,float(w)-(y?6:0),4};out.horizontal_thumb=thumb(out.horizontal_track,true,rx,offset.x);}
 if(x&&y)out.corner={float(w)-6,float(h)-6,4,4};return out;
}
}
DocumentViewportAuthority DocumentViewportAuthority::create(aether_doc_s* doc,aether_layout_result_s* l,aether_style_map_s* map,std::uint32_t w,std::uint32_t h,text::truetype::ViewportOffset offset,aether::layout::DocumentTextBudget& budget,const std::function<void()>& callback){return create_impl(doc,l,map,w,h,offset,budget,callback,false);}
DocumentViewportAuthority DocumentViewportAuthority::create_registry_candidate(aether_doc_s* doc,aether_layout_result_s* l,aether_style_map_s* map,std::uint32_t w,std::uint32_t h,text::truetype::ViewportOffset offset,aether::layout::DocumentTextBudget& budget,const std::function<void()>& callback){return create_impl(doc,l,map,w,h,offset,budget,callback,true);}
DocumentViewportAuthority DocumentViewportAuthority::create_registry_requests(aether_doc_s* doc,aether_layout_result_s* l,aether_style_map_s* map,std::uint32_t w,std::uint32_t h,text::truetype::ViewportOffset offset,const DocumentScrollRequests& requests,const DocumentCaptureThread& thread,aether::layout::DocumentTextBudget& budget,const std::function<void()>& callback){
 if(thread.owner_!=std::this_thread::get_id())throw E::invalid_input;
 return create_impl(doc,l,map,w,h,offset,budget,callback,true,&requests,&thread);
}
DocumentViewportAuthority DocumentViewportAuthority::create_impl(aether_doc_s* doc,aether_layout_result_s* l,aether_style_map_s* map,std::uint32_t w,std::uint32_t h,text::truetype::ViewportOffset offset,aether::layout::DocumentTextBudget& budget,const std::function<void()>& callback,bool registry_candidate,const DocumentScrollRequests* requests,const DocumentCaptureThread* thread){
 if(requests&&(!registry_candidate||!thread||thread->owner_!=std::this_thread::get_id()))throw E::invalid_input;
 if(!doc||!l||!map||!w||!h)throw E::invalid_input;
 if(w>2048||h>2048||offset.x>65536||offset.y>65536)throw E::limit_exceeded;
 auto token=doc->identity;auto version=aether::style::pin_style_map(map);
 if(!token||!token->alive||token->owner!=doc||!version||!version->contextual()||version->document_lifetime()!=token||!aether::style::inputs_current(version.get())||version->style_tree_epoch()!=token->style_tree_epoch)throw E::preparation_failed;
 budget.bytes(1,sizeof(DocumentViewportAuthority)+sizeof(aether::layout::LayoutOperationPin));
 auto layout_pin=std::make_unique<aether::layout::LayoutOperationPin>(l);
 if(!l->assets||l->assets->base!=version)throw E::preparation_failed;
 const auto captured_assets=l->assets;
 std::unique_ptr<DocumentScrollResolution> resolution;
 if(requests)resolution=DocumentScrollResolution::capture(doc,map,*requests,budget,thread->owner_);
 const CallbackEpochGuard epochs(token);const auto nonstate=token->non_state_epoch;
 // The first callback has no saved box/primary pointers. Pin already owns l.
 auto epoch_check=[&]{if(!epochs.valid()||token->non_state_epoch!=nonstate||!aether::style::inputs_current(version.get()))throw E::preparation_failed;};
 if(callback)callback();epoch_check();
 if(resolution&&!resolution->accepts())throw E::preparation_failed;
 if(l->assets!=captured_assets)throw E::preparation_failed;
 if(!l->allocated_boxes.allocation_revision_valid())throw E::preparation_failed;
 const auto allocation_revision=l->allocated_boxes.allocation_revision();
 auto check=[&]{if(callback)callback();epoch_check();if(!l->allocated_boxes.allocation_revision_valid()||l->allocated_boxes.allocation_revision()!=allocation_revision||l->assets!=captured_assets)throw E::preparation_failed;};
 // Capture is callback-free. Membership is authenticated before tree pointers.
 std::set<const aether_layout_box_s*> allocated;
 std::vector<const aether_layout_box_s*> allocation_order;
 for(const auto* box:l->allocated_boxes){
  if(allocated.size()>=65536)throw E::limit_exceeded;
  budget.normalized(1);budget.bytes(1,2*sizeof(box)+4*sizeof(void*));
  if(!box||!allocated.insert(box).second)throw E::preparation_failed;
  allocation_order.push_back(box);
 }
 if(allocated.size()!=l->live_box_count)throw E::preparation_failed;

 // Index lookup never dereferences the supplied key. Only retained immutable
 // owners can authorize a subsequent node read. No raw-key identity fallback.
 auto authenticate_node=[&](const aether_node_s* key,bool dom_only=false)->std::shared_ptr<node_identity_s>{
  if(!key)return {};
  budget.normalized(1);
  if(version->backing())if(auto record=version->backing()->find_owner(key)){
   const auto& identity=record->node_lifetime();
   if(record->node()!=key||record->document_lifetime()!=token||!token->alive||token->owner!=doc||!identity||!identity->alive||!identity->attached||identity->owner!=key)throw E::preparation_failed;
   if(key->identity!=identity)throw E::preparation_failed;
   return identity;
  }
  if(!dom_only&&captured_assets->synthetic)if(const auto* entry=captured_assets->synthetic->find(key)){
   if(entry->node()!=key||captured_assets->document_lifetime!=token||!token->alive||token->owner!=doc)throw E::preparation_failed;
   // SyntheticEntry owns the actual node through shared_ptr, not merely a token.
   auto identity=entry->node()->identity;
   if(!identity||!identity->alive||!identity->attached||identity->owner!=key)throw E::preparation_failed;
   return identity;
  }
  throw E::preparation_failed;
 };
 aether::layout::StyleOperationPin styles(l->assets,version,true);
 if(styles.document_lifetime()!=token||!styles.structure_valid()||!styles.inputs_current()||!styles.accepts_override())throw E::preparation_failed;
 auto* root=l->root;if(!root||!allocated.count(root))throw E::preparation_failed;const auto root_identity=authenticate_node(root->element,true);if(!root_identity||!root->element||root->element->tag_name!="body"||l->find_primary_box(root->element)!=root||root->owner!=l||root->element->style_node_id<=0)throw E::unsupported;
 if(styles.lookup().lookup_node_owner(root->element)!=root->style.style_owner()||styles.lookup().lookup_measures_owner(root->element)!=root->style.measures_owner())throw E::preparation_failed;
 if(l->viewport.width!=w||l->viewport.height!=h||l->viewport_overflow_x!=AETHER_OVERFLOW_AUTO||l->viewport_overflow_y!=AETHER_OVERFLOW_AUTO||root->style.width.unit!=AETHER_CSS_UNIT_AUTO)throw E::unsupported;
 if(!l->effective_fixed_container_nodes.empty())throw E::unsupported;
 const auto captured_root_id=root->element->style_node_id;
 DocumentViewportAuthority out;out.requests_=std::move(resolution);out.registry_candidate_=registry_candidate;out.document_=token;out.root_identity_=root_identity;out.structure_=token->structure_epoch;out.mutation_=token->mutation_epoch;out.style_=token->style_tree_epoch;out.nonstate_=nonstate;out.doc_=doc;out.map_=map;out.layout_=l;out.root_=root;out.version_=version;out.pin_=std::move(layout_pin);out.assets_=l->assets;out.root_font_size_=l->root_font_size;out.allocation_revision_=allocation_revision;out.allocations_=std::move(allocation_order);
 struct Pending {const aether_layout_box_s* box;const aether_layout_box_s* parent;std::size_t depth;bool hidden_ancestor,rounded_ancestor;};std::vector<Pending> pending;std::set<const aether_layout_box_s*> seen;
 budget.bytes(1,sizeof(Pending));pending.push_back({root,nullptr,1,false,false});double right=w,bottom=h;std::size_t edges=0;
 while(!pending.empty()){budget.normalized(1);auto [b,parent,depth,hidden_ancestor,rounded_ancestor]=pending.back();pending.pop_back();if(depth>256)throw E::limit_exceeded;if(!allocated.count(b))throw E::preparation_failed;budget.bytes(1,sizeof(aether_layout_box_s*)+4*sizeof(void*));if(!b||b->owner!=l||!seen.insert(b).second)throw E::preparation_failed;
  const auto element_identity=authenticate_node(b->element);
  const auto presentation_identity=authenticate_node(b->presentation_owner);
  if(!b->style.style_owner()||b->style.position!=AETHER_POSITION_STATIC||b->visual_has_transform||b->visual_opacity!=1||!b->style.style_owner()->view().transforms().empty()||b->is_atomic_control)throw E::unsupported;
  // Existing shadow raster has no joint curved-clip coverage path. Keep this
  // combination explicitly unsupported, rather than silently square-clip it.
  if(rounded_ancestor&&!b->style.style_owner()->view().box_shadows().empty())throw E::unsupported;
  const int ox=layout_scroll_overflow_axis(l,b,true),oy=layout_scroll_overflow_axis(l,b,false);
  if(b!=root){
   if(!registry_candidate&&(ox!=AETHER_OVERFLOW_VISIBLE||oy!=AETHER_OVERFLOW_VISIBLE))throw E::unsupported;
   if(registry_candidate&& !((ox==AETHER_OVERFLOW_VISIBLE&&oy==AETHER_OVERFLOW_VISIBLE)||(ox==AETHER_OVERFLOW_AUTO&&oy==AETHER_OVERFLOW_AUTO)||(ox==AETHER_OVERFLOW_SCROLL&&oy==AETHER_OVERFLOW_SCROLL)||(ox==AETHER_OVERFLOW_HIDDEN&&oy==AETHER_OVERFLOW_HIDDEN)))throw E::unsupported;
  }
  for(auto r:{b->border_box,b->padding_box,b->content_box}){if(hidden_ancestor)signed_rect_bound(r);else rect_bound(r);}
  const bool abstract_top=!hidden_ancestor&&abstract_margin_top(b,parent);if(hidden_ancestor)signed_rect_bound(b->margin_box);else if(!abstract_top)rect_bound(b->margin_box);
  const auto bounds=aether_scroll_own_bounds(b);for(double v:{bounds.left,bounds.right,bounds.bottom}){if(hidden_ancestor)signed_bound(v);else bounded(v);}if(hidden_ancestor)signed_bound(bounds.top);else if(!abstract_top)bounded(bounds.top);else if(bounds.top!=b->margin_box.y)throw E::unsupported;if(bounds.left>bounds.right||bounds.top>bounds.bottom)throw E::unsupported;
  right=std::max(right,bounds.right);bottom=std::max(bottom,bounds.bottom);
  if(b==root){right=std::max(right,double(b->border_box.x)+b->border_box.width);bottom=std::max(bottom,double(b->border_box.y)+b->border_box.height);}
  if(b->children.size()>65535-edges)throw E::limit_exceeded;edges+=b->children.size();
  budget.normalized(b->children.size());budget.bytes(1,sizeof(BoxEvidence));budget.bytes(b->children.size(),sizeof(aether_layout_box_s*));budget.bytes(b->text.size());
  BoxEvidence evidence;
  const auto effective_owner=styles.lookup().lookup_node_owner(b->element);
  evidence.effective_style=effective_owner?BoxStyleSnapshot(effective_owner,styles.lookup().lookup_measures_owner(b->element)):b->style;
  evidence.html_element=b->element&&b->element->tag_name=="html";
  evidence.placement=b->native_text;evidence.text=b->text;evidence.presentation_owner=b->presentation_owner;evidence.box=b;evidence.parent=parent;evidence.abstract_margin_top=abstract_top;evidence.hidden_ancestor=hidden_ancestor;evidence.identity=element_identity;evidence.presentation_identity=presentation_identity;evidence.style=b->style;evidence.margin=b->margin_box;evidence.border=b->border_box;evidence.padding=b->padding_box;evidence.content=b->content_box;evidence.proxy=b->scroll_proxy;evidence.scroll=b->scroll;evidence.sampled=b->visual_sample_valid;evidence.transformed=b->visual_has_transform;evidence.transform=b->visual_transform;evidence.opacity=b->visual_opacity;evidence.timestamp=b->visual_timestamp;evidence.visual_identity=b->visual_identity;evidence.used_border=b->used_border;evidence.before=b->collapsed_margin_before;evidence.after=b->collapsed_margin_after;evidence.relative_x=b->own_relative_dx;evidence.relative_y=b->own_relative_dy;evidence.atomic=b->is_atomic_control;evidence.canvas=b->is_initial_canvas;evidence.kind=b->kind;evidence.completion_flags=b->scroll_completion_flags;
  evidence.node_id=element_identity?b->element->style_node_id:0;evidence.overflow_x=ox;evidence.overflow_y=oy;
  evidence.visible=styles.lookup().presentation(*b,aether::layout::BoxStylePolicy::effective_map).visible;
  evidence.primary=b->element&&l->find_primary_box(b->element)==b;
  const bool scroll_candidate=b!=root&&b->kind==LAYOUT_BOX_ELEMENT&&(ox==AETHER_OVERFLOW_AUTO||ox==AETHER_OVERFLOW_SCROLL||ox==AETHER_OVERFLOW_HIDDEN);
  if(registry_candidate&&scroll_candidate){
   if(!evidence.primary||!element_identity||evidence.node_id<=0||(b->style.display!=AETHER_DISPLAY_BLOCK&&b->style.display!=AETHER_DISPLAY_FLEX))throw E::unsupported;
   bounded(b->scroll.scrollable_overflow_x);bounded(b->scroll.scrollable_overflow_y);
   for(double v:{double(b->padding_box.x)-offset.x,double(b->padding_box.y)-offset.y,double(b->padding_box.x)+b->padding_box.width-offset.x,double(b->padding_box.y)+b->padding_box.height-offset.y})if(!std::isfinite(v)||std::abs(v)>65536||std::floor(v)!=v)throw E::unsupported;
   const auto outer=aether_border_radii_resolve(b->style.border_radii,b->border_box,b->style.font_size,l->root_font_size,l->viewport.width,l->viewport.height);
   const auto clip=aether_corner_radii_inset(outer,b->padding_box.x-b->border_box.x,b->padding_box.y-b->border_box.y,b->border_box.x+b->border_box.width-(b->padding_box.x+b->padding_box.width),b->border_box.y+b->border_box.height-(b->padding_box.y+b->padding_box.height));
   if(ox==AETHER_OVERFLOW_HIDDEN)rounded_owner(b,outer,clip);
   else if(!clip_radii_exact_zero(clip))throw E::unsupported;
   evidence.clip_radii=clip;
   evidence.chrome=aether_resolve_scrollbar_geometry(*b,{0,0});
   if(ox==AETHER_OVERFLOW_HIDDEN&&(b->scroll.scroll_offset_x!=0||b->scroll.scroll_offset_y!=0||evidence.chrome.horizontal_visible||evidence.chrome.vertical_visible))throw E::unsupported;
   evidence.push_expected=layout_box_has_dynamic_scroll(l,b);
   evidence.chrome_expected=evidence.visible&&b->border_box.width>0&&b->border_box.height>0&&(evidence.chrome.horizontal_visible||evidence.chrome.vertical_visible);
  }
  evidence.children.reserve(b->children.size());for(auto* child:b->children){if(!allocated.count(child))throw E::preparation_failed;evidence.children.push_back(child);budget.bytes(1,sizeof(Pending));pending.push_back({child,b,depth+1,hidden_ancestor||(registry_candidate&&b!=root&&b->kind==LAYOUT_BOX_ELEMENT&&ox==AETHER_OVERFLOW_HIDDEN&&oy==AETHER_OVERFLOW_HIDDEN),rounded_ancestor||!clip_radii_exact_zero(evidence.clip_radii)});}out.boxes_.push_back(std::move(evidence));
 }
 if(seen.size()!=allocated.size())throw E::preparation_failed;
 if(l->primary_boxes.size()>65536)throw E::limit_exceeded;
 budget.normalized(l->primary_boxes.size());budget.bytes(l->primary_boxes.size(),sizeof(decltype(out.primary_)::value_type));out.primary_.reserve(l->primary_boxes.size());
 for(const auto& [node,box]:l->primary_boxes){if(!node||!box||!allocated.count(box)||box->element!=node||box->owner!=l||!seen.count(box))throw E::preparation_failed;out.primary_.push_back({node,box});}
 // Registry A is inactive: offsets are zero for nested records, root uses
 // existing input. Resolve ancestry once, never one subtree traversal per state.
 if(registry_candidate){
  budget.bytes(1,sizeof(int)+2*sizeof(void*));out.registry_operation_=std::make_shared<const int>(0);
  std::map<const aether_layout_box_s*,std::pair<std::size_t,std::size_t>> ancestry;
  std::set<std::int64_t> ids;
  std::size_t consumed_requests=0;
  for(const auto& e:out.boxes_){
   budget.normalized(1);std::size_t parent_scope=0,scope_depth=3;
   if(e.parent){const auto p=ancestry.find(e.parent);if(p==ancestry.end())throw E::preparation_failed;parent_scope=p->second.first;scope_depth=p->second.second;}
   const auto* request=out.requests_&&e.primary&&e.kind==LAYOUT_BOX_ELEMENT&&e.identity?out.requests_->find(static_cast<const aether_node_s*>(e.identity->owner)):nullptr;
   if(request&&request->identity!=e.identity)throw E::preparation_failed;
   if(request&&e.box==root)throw DocumentScrollBindingError::root;
   // Hidden retains its native range and owner but only implicit zero is
   // admitted. An explicit zero request is not an identity bypass.
   if(request&&e.overflow_x==AETHER_OVERFLOW_HIDDEN)throw DocumentScrollBindingError::noncontainer;
   const bool container=e.box==root||(e.kind==LAYOUT_BOX_ELEMENT&&(e.overflow_x==AETHER_OVERFLOW_AUTO||e.overflow_x==AETHER_OVERFLOW_SCROLL||e.overflow_x==AETHER_OVERFLOW_HIDDEN));
   if(request&&(!container||!e.primary))throw DocumentScrollBindingError::noncontainer;
   std::size_t descendant_scope=parent_scope;
   if(container){
    if(out.registry_.size()>=256)throw E::limit_exceeded;
    budget.bytes(1,sizeof(DocumentScrollRecord)+sizeof(decltype(out.registry_index_)::value_type)+sizeof(std::int64_t)+8*sizeof(void*));
    if(e.node_id<=0||!ids.insert(e.node_id).second)throw E::preparation_failed;
    descendant_scope=out.registry_.size();
    DocumentScrollRecord record;record.operation=out.registry_operation_;record.box=e.box;record.identity=e.identity;record.root=e.box==root;record.parent_scope=parent_scope;record.visible=e.visible;record.overflow_x=e.overflow_x;record.overflow_y=e.overflow_y;record.scrollport=e.padding;record.clip_radii=e.clip_radii;
    record.ancestor_x=offset.x;record.ancestor_y=offset.y;
    record.push_expected=record.root||e.push_expected;record.chrome_expected=record.root||e.chrome_expected;
    if(!record.root){record.state={e.node_id,0,0,e.scroll.scrollable_overflow_x,e.scroll.scrollable_overflow_y,e.chrome};scope_depth+=1+(record.push_expected?1:0);}
    if(request){
     if(!e.visible||(!record.push_expected&&!record.chrome_expected))throw DocumentScrollBindingError::inactive;
     if(request->x>record.state.range_x||request->y>record.state.range_y)throw DocumentScrollBindingError::out_of_range;
     record.state.x=request->x;record.state.y=request->y;
     record.state.chrome=aether_resolve_scrollbar_geometry(*e.box,{float(request->x),float(request->y)});
     ++consumed_requests;
    }
    if(scope_depth>256)throw E::limit_exceeded;
    out.registry_index_.emplace(e.box,descendant_scope);out.registry_.push_back(std::move(record));
   }
   budget.bytes(1,sizeof(decltype(ancestry)::value_type)+4*sizeof(void*));ancestry.emplace(e.box,std::make_pair(descendant_scope,scope_depth));
  }
  if(out.requests_&&consumed_requests!=out.requests_->size())throw DocumentScrollBindingError::noncontainer;
 }
 // One ancestry pass produces document-space owner context and identified
 // screen-space clip chains. No command geometry is rewritten.
 if(!out.registry_operation_){budget.bytes(1,sizeof(int)+2*sizeof(void*));out.registry_operation_=std::make_shared<const int>(0);}
 const auto add_clip=[&](const aether_layout_box_s* owner,DocumentClipKind kind,std::size_t parent,aether_rectf screen,aether_corner_radii radii=aether_corner_radii{}){
  budget.normalized(1);budget.bytes(1,sizeof(DocumentClipEvidence));
  DocumentClipEvidence clip;clip.owner=owner;clip.kind=kind;clip.parent=parent;clip.screen=screen;clip.intersection=screen;clip.radii=radii;
  for(double value:{double(screen.x),double(screen.y),double(screen.width),double(screen.height),double(screen.x)+screen.width,double(screen.y)+screen.height})if(!std::isfinite(value)||std::abs(value)>65536)throw E::unsupported;
  if(screen.width<0||screen.height<0)throw E::unsupported;
  clip.empty=screen.width==0||screen.height==0;
  if(parent!=std::numeric_limits<std::size_t>::max()){
   if(parent>=out.clip_chains_.size())throw E::preparation_failed;
   const auto& prior=out.clip_chains_[parent];
   const double left=std::max(double(prior.intersection.x),double(screen.x)),top=std::max(double(prior.intersection.y),double(screen.y));
   const double right=std::min(double(prior.intersection.x)+prior.intersection.width,double(screen.x)+screen.width),bottom=std::min(double(prior.intersection.y)+prior.intersection.height,double(screen.y)+screen.height);
   clip.empty=clip.empty||prior.empty||right<=left||bottom<=top;
   clip.intersection={float(left),float(top),float(std::max(0.,right-left)),float(std::max(0.,bottom-top))};
  }
  const auto index=out.clip_chains_.size();out.clip_chains_.push_back(clip);return index;
 };
 const auto outer_clip=add_clip(root,DocumentClipKind::viewport_outer,std::numeric_limits<std::size_t>::max(),{0,0,float(w),float(h)});
 const auto content_clip=add_clip(root,DocumentClipKind::viewport_content,outer_clip,{0,0,float(w),float(h)});
 for(const auto& e:out.boxes_){
  budget.normalized(1);budget.bytes(1,sizeof(DocumentPaintContext)+sizeof(decltype(out.paint_context_index_)::value_type)+4*sizeof(void*));
  DocumentPaintContext context;context.operation=out.registry_operation_;context.box=e.box;context.chain=content_clip;context.x=offset.x;context.y=offset.y;context.painted=true;
  if(e.parent){
   const auto it=out.paint_context_index_.find(e.parent);if(it==out.paint_context_index_.end())throw E::preparation_failed;
   const auto& parent=out.paint_contexts_[it->second];context.chain=parent.child_chain;context.x=parent.child_x;context.y=parent.child_y;context.painted=parent.painted;
  }
  context.painted=context.painted&&e.effective_style.display!=AETHER_DISPLAY_NONE;
  if(registry_candidate){auto found=out.registry_index_.find(e.box);if(found!=out.registry_index_.end()){
   auto& record=out.registry_[found->second];record.ancestor_x=context.x;record.ancestor_y=context.y;
   if(out.requests_&&e.identity&&out.requests_->find(static_cast<const aether_node_s*>(e.identity->owner))&&!context.painted)throw DocumentScrollBindingError::inactive;
  }}
  context.child_chain=context.chain;context.child_x=context.x;context.child_y=context.y;
  if(registry_candidate&&e.box!=root){
   if(const auto* record=out.lookup_scroll(e.box)){
    // Own decoration excludes own content offset. Its descendants include it.
    const aether_rectf screen{float(double(record->scrollport.x)-context.x),float(double(record->scrollport.y)-context.y),record->scrollport.width,record->scrollport.height};
    context.child_chain=add_clip(e.box,DocumentClipKind::nested_padding,context.chain,screen,record->clip_radii);
    context.child_x+=record->state.x;context.child_y+=record->state.y;
   }
  }
  for(double v:{context.x,context.y,context.child_x,context.child_y})if(!std::isfinite(v)||v<0||v>65536||std::floor(v)!=v)throw E::limit_exceeded;
  out.paint_context_index_.emplace(e.box,out.paint_contexts_.size());out.paint_contexts_.push_back(std::move(context));
 }
 // Build graph exclusively from captured evidence. No aliases to live vectors.
 budget.bytes(1,sizeof(DocumentScrollBounds::Graph));budget.bytes(out.boxes_.size(),sizeof(DocumentScrollBounds::Node));
 std::vector<DocumentScrollBounds::Node> graph_nodes;graph_nodes.reserve(out.boxes_.size());
 for(const auto& e:out.boxes_){
  budget.normalized(1+e.children.size());budget.bytes(e.children.size(),sizeof(const aether_layout_box_s*));
  DocumentScrollBounds::Node n;n.key=e.box;n.padding=e.padding;n.overflow_x=registry_candidate&&e.box!=root?e.overflow_x:AETHER_OVERFLOW_VISIBLE;n.overflow_y=registry_candidate&&e.box!=root?e.overflow_y:AETHER_OVERFLOW_VISIBLE;
  if(e.proxy.valid&&e.kind==LAYOUT_BOX_TEXT){const double dx=double(e.content.x)-e.proxy.reference_content_x;n.own={e.proxy.left+dx,e.content.y,e.proxy.right+dx,double(e.content.y)+e.content.height,false};}
  else n.own={e.margin.x,e.margin.y,double(e.margin.x)+e.margin.width,double(e.margin.y)+e.margin.height,false};
  n.children.assign(e.children.begin(),e.children.end());graph_nodes.push_back(std::move(n));
 }
 const DocumentScrollBounds::Graph graph(root,std::move(graph_nodes));
 const auto contextual=DocumentScrollBounds::compute(graph,budget,check);
 right=std::max(double(w),std::max(contextual.bounds.right,double(out.boxes_.front().border.x)+out.boxes_.front().border.width));
 bottom=std::max(double(h),std::max(contextual.bounds.bottom,double(out.boxes_.front().border.y)+out.boxes_.front().border.height));
 const double rx=right-w,ry=bottom-h;bounded(right);bounded(bottom);if(offset.x>rx||offset.y>ry)throw E::invalid_input;
 out.state_={captured_root_id,double(offset.x),double(offset.y),rx,ry,chrome(w,h,rx,ry,offset)};
 if(registry_candidate){out.registry_.front().state=out.state_;out.registry_.front().scrollport={0,0,float(w),float(h)};}
 out.diagnostics_={offset,rx,ry,right,bottom,rx>0||ry>0,{0,0,float(w),float(h)},out.state_.chrome};
 // Charge final comparison before callback. Revision check precedes saved derefs.
 budget.normalized(out.allocations_.size()+out.boxes_.size()+edges+out.primary_.size());check();
 if(!out.accepts_checked(l,map))throw E::preparation_failed;
 return out;
}
bool DocumentViewportAuthority::stamp_current(aether_layout_result_s* l,aether_style_map_s* map)const noexcept{
 if(std::this_thread::get_id()!=thread_||!pin_||!document_||!document_->alive||document_->owner!=doc_||!root_identity_||!root_identity_->alive||!root_identity_->attached)return false;
 if(document_->structure_epoch!=structure_||document_->mutation_epoch!=mutation_||document_->style_tree_epoch!=style_||document_->non_state_epoch!=nonstate_)return false;
 if(!l||l!=layout_||!map||map!=map_||aether::style::pin_style_map(map)!=version_||l->assets!=assets_)return false;
 return l->allocated_boxes.allocation_revision_valid()&&l->allocated_boxes.allocation_revision()==allocation_revision_;
}
bool DocumentViewportAuthority::accepts(aether_layout_result_s* l,aether_style_map_s* map)const noexcept{
 try{return accepts_checked(l,map);}catch(...){return false;}
}
bool DocumentViewportAuthority::accepts_checked(aether_layout_result_s* l,aether_style_map_s* map)const{
 if(!stamp_current(l,map))return false;
 if(requests_&&!requests_->accepts())return false;
 if(std::this_thread::get_id()!=thread_||!document_||!document_->alive||document_->owner!=doc_||!root_identity_||!root_identity_->alive||!root_identity_->attached)return false;
 if(document_->structure_epoch!=structure_||document_->mutation_epoch!=mutation_||document_->style_tree_epoch!=style_||document_->non_state_epoch!=nonstate_)return false;
 if(!pin_||!l||l!=layout_||!map||map!=map_||aether::style::pin_style_map(map)!=version_||!aether::style::inputs_current(version_.get()))return false;
 // Check allocation ABA before root or saved evidence dereference.
 if(!l->allocated_boxes.allocation_revision_valid()||l->allocated_boxes.allocation_revision()!=allocation_revision_)return false;
 std::size_t allocation_index=0;
 for(const auto* box:l->allocated_boxes){if(allocation_index>=allocations_.size()||allocations_[allocation_index++]!=box)return false;}
 if(allocation_index!=allocations_.size()||l->live_box_count!=allocations_.size())return false;
 if(l->assets!=assets_||l->root_font_size!=root_font_size_||l->primary_boxes.size()!=primary_.size()||l->root!=root_||root_identity_->owner!=root_->element||root_->owner!=l||l->find_primary_box(root_->element)!=root_||root_->element->style_node_id!=state_.id||l->viewport.width!=diagnostics_.viewport.width||l->viewport.height!=diagnostics_.viewport.height||l->viewport_overflow_x!=AETHER_OVERFLOW_AUTO||l->viewport_overflow_y!=AETHER_OVERFLOW_AUTO||!l->effective_fixed_container_nodes.empty())return false;
 for(const auto& [node,box]:primary_){auto i=l->primary_boxes.find(const_cast<aether_node_s*>(node));if(i==l->primary_boxes.end()||i->second!=box)return false;}
 for(const auto& e:boxes_){const auto* b=e.box;if(!e.presentation_identity&&b->presentation_owner)return false;if(e.presentation_identity&&(!e.presentation_identity->alive||!e.presentation_identity->attached||e.presentation_identity->owner!=b->presentation_owner))return false;if(!e.identity&&b->element)return false;if(e.identity&&(!e.identity->alive||!e.identity->attached||e.identity->owner!=b->element))return false;try{if(!e.hidden_ancestor&&abstract_margin_top(b,e.parent)!=e.abstract_margin_top)return false;}catch(...){return false;}
  if(e.identity&&b->element->style_node_id!=e.node_id)return false;
  if(layout_scroll_overflow_axis(l,b,true)!=e.overflow_x||layout_scroll_overflow_axis(l,b,false)!=e.overflow_y)return false;
  if(b->native_text.run!=e.placement.run||b->native_text.baseline!=e.placement.baseline||b->text!=e.text||b->presentation_owner!=e.presentation_owner)return false;
  if(b->owner!=l||b->style.style_owner()!=e.style.style_owner()||b->style.measures_owner()!=e.style.measures_owner()||!aether::layout::native_text_detail::scalars_equal(b->style.scalars(),e.style.scalars())||b->children!=e.children||b->kind!=e.kind||b->is_atomic_control!=e.atomic||b->is_initial_canvas!=e.canvas||b->scroll_completion_flags!=e.completion_flags)return false;
  if(!rect_same(b->margin_box,e.margin)||!rect_same(b->border_box,e.border)||!rect_same(b->padding_box,e.padding)||!rect_same(b->content_box,e.content)||b->scroll_proxy.valid!=e.proxy.valid||b->scroll_proxy.left!=e.proxy.left||b->scroll_proxy.right!=e.proxy.right||b->scroll_proxy.reference_content_x!=e.proxy.reference_content_x)return false;
  if(b->scroll.scrollable_overflow_x!=e.scroll.scrollable_overflow_x||b->scroll.scrollable_overflow_y!=e.scroll.scrollable_overflow_y||b->scroll.scroll_offset_x!=e.scroll.scroll_offset_x||b->scroll.scroll_offset_y!=e.scroll.scroll_offset_y||b->visual_sample_valid!=e.sampled||b->visual_has_transform!=e.transformed||!matrix_same(b->visual_transform,e.transform)||b->visual_opacity!=e.opacity||b->visual_timestamp!=e.timestamp||b->visual_identity.lock()!=e.visual_identity.lock())return false;
  if(b->used_border.left!=e.used_border.left||b->used_border.right!=e.used_border.right||b->used_border.top!=e.used_border.top||b->used_border.bottom!=e.used_border.bottom||b->collapsed_margin_before!=e.before||b->collapsed_margin_after!=e.after||b->own_relative_dx!=e.relative_x||b->own_relative_dy!=e.relative_y)return false;
 }
 return true;
}
}
