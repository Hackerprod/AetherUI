#include "document_shadow_plan.h"
#include "display_list_types.h"
#include "../css/border_radii.h"
#include "../layout/style_lookup.h"
#include <algorithm>
namespace aether::rendering {
namespace {
[[noreturn]]void unsupported(){throw DocumentShadowUnsupported{};}
bool rect_eq(aether_rectf a,aether_rectf b){return a.x==b.x&&a.y==b.y&&a.width==b.width&&a.height==b.height;}
bool shadow_eq(aether_shadow a,aether_shadow b){return a.offset_x==b.offset_x&&a.offset_y==b.offset_y&&a.blur_radius==b.blur_radius&&a.spread_radius==b.spread_radius&&a.inset==b.inset&&a.color.r==b.color.r&&a.color.g==b.color.g&&a.color.b==b.color.b&&a.color.a==b.color.a;}
void bounds(aether_rectf r,aether_shadow s,double x,double y){
 const double l=double(r.x)-x,t=double(r.y)-y,right=l+r.width,bottom=t+r.height;
 const auto bounded=[](double v){return std::isfinite(v)&&std::abs(v)<=65536;};
 if(!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<0||r.height<0||r.width>8192||r.height>8192||!bounded(l)||!bounded(t)||!bounded(right)||!bounded(bottom))unsupported();
 for(float v:{s.offset_x,s.offset_y,s.spread_radius})if(!std::isfinite(v)||std::abs(v)>8192)unsupported();
 if(!std::isfinite(s.blur_radius)||s.blur_radius<0||s.blur_radius>64||(s.blur_radius>0&&s.blur_radius<1)||s.inset)unsupported();
 for(float v:{s.color.r,s.color.g,s.color.b,s.color.a})if(!std::isfinite(v)||v<0||v>1)unsupported();
 const double spread=s.spread_radius,support=2*double(s.blur_radius);
 for(double v:{l-spread+s.offset_x,t-spread+s.offset_y,right+spread+s.offset_x,bottom+spread+s.offset_y,
               l-spread+s.offset_x-support,t-spread+s.offset_y-support,right+spread+s.offset_x+support,bottom+spread+s.offset_y+support})if(!bounded(v))unsupported();
}
}
DocumentShadowPlan::DocumentShadowPlan(aether_layout_result_s* l,aether_style_map_s* m,
 const DocumentViewportAuthority& a,aether::layout::DocumentTextBudget& budget,const std::function<void()>& check)
 :budget_(&budget),authority_(&a),layout_(l),map_(m){
 if(!a.accepts_checked(l,m))throw std::invalid_argument("SHADOW_AUTHORITY");
 budget.bytes(1,sizeof(DocumentShadowPlan));
 budget.bytes(a.clip_chains_.size(),sizeof(DocumentClipEvidence));
 budget.normalized(a.clip_chains_.size());clip_chains_=a.clip_chains_;
 struct Source {
  const aether_layout_box_s* box;BoxStyleSnapshot style;
  aether_rectf rect; aether_corner_radii radii;DocumentPaintContext context;bool emitted;
 };
 std::vector<Source> sources;
 // Callback-free bounded copy: later callbacks inspect only owned immutable
 // style resources and value geometry, never live children or box pointers.
 for(const auto& e:a.boxes_){
  budget.normalized(1);
  const auto* context=a.lookup_paint_context(e.box);
  if(!context)throw std::invalid_argument("SHADOW_CONTEXT");
  if(!context->painted||e.kind!=LAYOUT_BOX_ELEMENT)continue;
  const auto view=e.effective_style.view();const auto shadows=view.box_shadows();
  if(shadows.empty())continue;
  const auto& style=view.scalars();
  if((style.display!=AETHER_DISPLAY_BLOCK&&style.display!=AETHER_DISPLAY_FLEX)||!e.identity||e.html_element)unsupported();
  const auto radii=aether_border_radii_resolve(style.border_radii,e.border,style.font_size,a.root_font_size_,a.diagnostics_.viewport.width,a.diagnostics_.viewport.height);
  if(!aether_corner_radii_is_zero(radii))unsupported();
  if(context->chain>=clip_chains_.size())throw std::invalid_argument("SHADOW_CLIP_CHAIN");
  budget.bytes(1,sizeof(Source));sources.push_back({e.box,e.effective_style,e.border,radii,*context,e.visible&&e.border.width>0&&e.border.height>0});
 }
 auto checkpoint=[&]{if(check)check();if(!a.stamp_current(l,m))throw std::invalid_argument("SHADOW_STALE");};
 for(const auto& source:sources){
  checkpoint();const auto shadows=source.style.view().box_shadows();
  for(std::size_t i=0;i<shadows.size();++i){
   budget.normalized(1);checkpoint();bounds(source.rect,shadows[i],0,0);bounds(source.rect,shadows[i],source.context.x,source.context.y);
   // Bounds are checked even if ancestor clips are empty or entirely offscreen.
   // No new culling: preserve the native emitter's layer correspondence.
   if(!source.emitted)continue;
   if(layers_.size()>=65536)throw std::length_error("SHADOW_COMMAND_LIMIT");
   budget.bytes(1,sizeof(Layer)+sizeof(NativeShadowEntry)+sizeof(aether_paint_command_t));
   layers_.push_back({source.box,i,source.style,source.context,source.rect,source.radii,shadows[i]});
  }
 }
 std::size_t comparisons=0;
 std::sort(layers_.begin(),layers_.end(),[&](const Layer&a,const Layer&b){
  if((comparisons++&255u)==0)checkpoint();
  return a.box==b.box?a.declaration<b.declaration:std::less<const aether_layout_box_s*>{}(a.box,b.box);
 });
 checkpoint();if(!accepts_checked(l,m))throw std::invalid_argument("SHADOW_STALE");
 for(std::size_t i=1;i<layers_.size();++i){
  if((i&255u)==1)checkpoint();
  if(layers_[i-1].box==layers_[i].box&&layers_[i-1].declaration==layers_[i].declaration)throw std::invalid_argument("SHADOW_DUPLICATE_BOX");
 }
 checkpoint();if(!accepts_checked(l,m))throw std::invalid_argument("SHADOW_STALE");
}
bool DocumentShadowPlan::accepts(aether_layout_result_s*l,aether_style_map_s*m)const noexcept{
 try{return accepts_checked(l,m);}catch(...){return false;}
}
bool DocumentShadowPlan::accepts_checked(aether_layout_result_s*l,aether_style_map_s*m)const{
 return l==layout_&&m==map_&&authority_&&authority_->accepts_checked(l,m);
}
void DocumentShadowPlan::verify(const NativePaintList& list,const std::function<void()>&check)const{
 if(!accepts_checked(layout_,map_))throw std::invalid_argument("SHADOW_STALE");
 const auto* raw=NativeListAccess::list(list);
 if(!raw||raw->commands.size()>65536||list.shadows.size()!=layers_.size())throw std::invalid_argument("SHADOW_COVERAGE");
 // No callback while reading caller-owned list storage. Own every plain-value
 // shadow command/sidecar used after callbacks. Nonshadow pointer payloads are
 // never retained or dereferenced by this verifier.
 const auto command_count=raw->commands.size();
 budget_->normalized(2*(command_count+list.shadows.size()));
 std::size_t count=0;for(const auto& command:raw->commands)if(command.tag==AETHER_PAINT_DRAW_BOX_SHADOW)++count;
 if(count!=layers_.size())throw std::invalid_argument("SHADOW_EXTRA");
 budget_->bytes(layers_.size(),sizeof(Audit)+sizeof(std::uint8_t));
 std::vector<Audit> audit;audit.reserve(layers_.size());
 for(const auto& entry:list.shadows){
  if(entry.command_index>=raw->commands.size()||raw->commands[entry.command_index].tag!=AETHER_PAINT_DRAW_BOX_SHADOW)throw std::invalid_argument("SHADOW_PAYLOAD");
  audit.push_back({entry,raw->commands[entry.command_index]});
 }
 std::vector<std::uint8_t> seen(layers_.size(),0);
 const auto checkpoint=[&]{if(check)check();if(!authority_->stamp_current(layout_,map_))throw std::invalid_argument("SHADOW_STALE");};
 std::size_t previous_command=0;bool first=true;
 for(const auto& item:audit){
  checkpoint();const auto& entry=item.entry;
  auto it=std::lower_bound(layers_.begin(),layers_.end(),entry,[](const Layer&a,const NativeShadowEntry&b){return a.box==b.box_identity?a.declaration<b.declaration_index:std::less<const aether_layout_box_s*>{}(a.box,b.box_identity);});
  if(it==layers_.end()||it->box!=entry.box_identity||it->declaration!=entry.declaration_index)throw std::invalid_argument("SHADOW_IDENTITY");
  const auto index=std::size_t(it-layers_.begin());if(seen[index]++||(!first&&entry.command_index<=previous_command))throw std::invalid_argument("SHADOW_BIJECTION");
  if(index+1<layers_.size()&&layers_[index+1].box==it->box&&!seen[index+1])throw std::invalid_argument("SHADOW_ORDER");
  const auto& cmd=item.command;
  if(!rect_eq(cmd.data.draw_box_shadow.rect,it->rect)||!aether_corner_radii_is_zero(cmd.data.draw_box_shadow.radii)||!shadow_eq(cmd.data.draw_box_shadow.shadow,it->shadow))throw std::invalid_argument("SHADOW_PAYLOAD");
  // Payload remains in document space. The identified chain is retained for B2
 // observed-stack audit, not substituted by rectangle-intersection equality.
  if(it->context.chain>=clip_chains_.size()||!it->context.operation)throw std::invalid_argument("SHADOW_CLIP_CHAIN");
  bounds(it->rect,it->shadow,it->context.x,it->context.y);
  first=false;previous_command=entry.command_index;
 }
 checkpoint();if(!accepts_checked(layout_,map_))throw std::invalid_argument("SHADOW_STALE");
 // The private caller keeps list alive throughout verify; destruction/reentry
 // of that borrowed object is not supported. No callbacks from here to return.
 // Recheck the complete shadow universe, including insertions and removals.
 const auto* final_raw=NativeListAccess::list(list);
 if(!final_raw||final_raw->commands.size()!=command_count||list.shadows.size()!=audit.size())throw std::invalid_argument("SHADOW_INPUT_CHANGED");
 std::size_t final_count=0;
 for(const auto& command:final_raw->commands)if(command.tag==AETHER_PAINT_DRAW_BOX_SHADOW)++final_count;
 if(final_count!=audit.size())throw std::invalid_argument("SHADOW_INPUT_CHANGED");
 for(std::size_t i=0;i<audit.size();++i){
  const auto& saved=audit[i];const auto& entry=list.shadows[i];
  if(entry.command_index!=saved.entry.command_index||entry.box_identity!=saved.entry.box_identity||entry.declaration_index!=saved.entry.declaration_index||entry.command_index>=final_raw->commands.size())throw std::invalid_argument("SHADOW_INPUT_CHANGED");
  const auto& command=final_raw->commands[entry.command_index];
  if(command.tag!=AETHER_PAINT_DRAW_BOX_SHADOW||!rect_eq(command.data.draw_box_shadow.rect,saved.command.data.draw_box_shadow.rect)||!aether_corner_radii_is_zero(command.data.draw_box_shadow.radii)||!shadow_eq(command.data.draw_box_shadow.shadow,saved.command.data.draw_box_shadow.shadow))throw std::invalid_argument("SHADOW_INPUT_CHANGED");
 }
}
}
