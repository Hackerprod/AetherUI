// Private protocol driver: no renderer, no claim of public B3 admission.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/rendering/html_ltr/b2_session_provenance.h"
#include "../src/rendering/html_ltr/session_validation.h"
#include "../src/layout/layout_internal.h"
#include <cassert>
#include <cstdio>
#include <functional>
using namespace aether::rendering::html_ltr;
namespace b2=aether::layout::b2;
struct F {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
 aether_layout_result_s result;
 std::unique_ptr<B2Provenance> provenance;
 aether_node_s* body{};
 F(bool flex=true,int nontrivia=0){
  assert(aether_doc_create(&doc)==AETHER_OK);
  assert(aether_doc_parse_html(doc,"<html><body> \t\n<!--keep-->\f <!---->x</body></html>")==AETHER_OK);
  auto* source_body=aether_doc_get_body(doc);assert(source_body->children.size()==5);source_body->children.front()->text+='\r';source_body->children.back()->text.clear();if(nontrivia)source_body->children.front()->text=nontrivia==1?"\xc2\xa0":"not trivia";
  aether_style_runtime_options_t options{};aether_style_environment_t env{400,200,1,16,0,0,0};
  assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  const std::string css="html,body{display:block;margin:0;padding:0;font-family:AetherB0;font-size:16px;line-height:24px}"+std::string(flex?"body{display:flex}":"");
  int64_t sheet{};assert(aether_style_runtime_add_stylesheet(runtime,css.c_str(),0,"probe",&sheet,nullptr)==AETHER_OK);
  aether_style_update_t* updates{};auto n=aether_style_runtime_resolve_subtree(runtime,aether_doc_get_document_element(doc),&updates);assert(n>=0);aether_style_updates_free(updates,n);
  assert(aether_style_runtime_create_resolved_style_map(runtime,doc,&map)==AETHER_OK);body=aether_doc_get_body(doc);
  auto font=std::make_shared<const aether::rendering::text::FontSource>(std::vector<uint8_t>{1});
  provenance=std::make_unique<B2Provenance>(doc,map,font,font,true);
  result.b3_result_id=1;result.viewport={400,200};provenance->b3_begin_result(&result,1);
  result.root=make(1,1);
 }
 aether_layout_box_s* make(uint64_t id,uint64_t gen){
  auto* box=result.create_box();box->element=body;box->kind=LAYOUT_BOX_ELEMENT;
  auto record=map->current->backing()->find_owner(body);box->style=aether::layout::BoxStyleSnapshot(record->style(),record->measures());
  box->b3_box_id=id;box->b3_generation_id=gen;box->b3_parent_generation_id=0;result.b3_next_generation_id=gen;
  try{provenance->b3_box(&result,box,id,gen,0);box->content_box=box->padding_box=box->border_box=box->margin_box={0,0,400,200};provenance->b3_complete(box);return box;}catch(...){delete box;throw;}
 }
 B2InlineResult receipt(aether_layout_box_s* anchor){B2InlineResult r(provenance->resource());r.anchor_box=anchor;if(anchor){r.result_id=result.b3_result_id;r.generation_id=anchor->b3_generation_id;r.anchor_box_id=anchor->b3_box_id;}r.block_owner=body;auto record=map->current->backing()->find_owner(body);for(auto* child:body->children){r.participants.push_back({child,body,record->style().get()});if(child->type==AETHER_NODE_TEXT&&!child->text.empty())r.ranges.push_back({child,0,child->text.size(),B2Disposition::flex_whitespace});}return r;}
 void discard(aether_layout_box_s* anchor){auto r=receipt(anchor);provenance->b3_discard(&result,anchor,r,{});}
 void move(float dx,float dy,bool receipt=true,bool geometry=true){auto* box=result.root;if(receipt)provenance->b3_offset(box,dx,dy);if(geometry)for(auto* r:{&box->content_box,&box->padding_box,&box->border_box,&box->margin_box}){r->x+=dx;r->y+=dy;}}
 ~F(){provenance.reset();delete result.root;result.root=nullptr;aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static void internal(const std::function<void()>& f){bool failed=false;try{f();}catch(const Error& e){assert(e.status==AETHER_ERROR_INTERNAL);failed=true;}assert(failed);}
struct FailResource:std::pmr::memory_resource {
 size_t remaining=SIZE_MAX;
 void* do_allocate(size_t n,size_t a)override {if(remaining!=SIZE_MAX){if(!remaining)throw std::bad_alloc();--remaining;}return std::pmr::new_delete_resource()->allocate(n,a);}
 void do_deallocate(void* p,size_t n,size_t a)override{std::pmr::new_delete_resource()->deallocate(p,n,a);}
 bool do_is_equal(const std::pmr::memory_resource& x)const noexcept override{return this==&x;}
};
static void limited(const std::function<void()>& run){bool hit=false;try{run();}catch(const b2::LimitExceeded&){hit=true;}assert(hit);}
static void oom_phase(int phase){size_t failures=0;bool success=false;for(size_t at=0;at<128&&!success;++at){FailResource resource;auto* previous=std::pmr::set_default_resource(&resource);{
 F f;std::pmr::set_default_resource(previous);if(phase!=0)f.discard(f.result.root);resource.remaining=at;
 try{if(phase==0)f.discard(f.result.root);if(phase==1)f.move(1,0);if(phase==2)f.provenance->b3_replace(&f.result,f.result.root);if(phase==3)f.provenance->validate_final(&f.result);if(phase==4){auto* extra=f.make(2,2);delete extra;}success=true;}
 catch(const std::bad_alloc&){++failures;}
 resource.remaining=SIZE_MAX;
 } }assert(success&&failures>0);std::printf("OOM phase %d: %zu allocation failures, success reached\n",phase,failures);}
int main(){
 {F f;f.discard(nullptr);auto before=f.provenance->counters().used(b2::Quota::token_visits);f.discard(f.result.root);f.discard(f.result.root);f.discard(f.result.root);assert(f.provenance->counters().used(b2::Quota::candidates)==1);assert(f.provenance->counters().used(b2::Quota::token_visits)>before);f.move(80.25f,60.125f);f.move(10.5f,2.25f);f.provenance->validate_final(&f.result);assert(f.provenance->ledger().placements().empty());assert(f.provenance->ledger().lines().empty());assert(f.provenance->ledger().spans().size()==2);internal([&]{f.move(1,0);});}
 {F f;f.discard(f.result.root);f.move(1,0,false,true);internal([&]{f.provenance->validate_final(&f.result);});}
 {F f;f.discard(f.result.root);f.move(1,0,true,false);internal([&]{f.provenance->validate_final(&f.result);});}
 {F f;internal([&]{f.provenance->validate_final(&f.result);});}
 {F f;auto r=f.receipt(f.result.root);r.participants.pop_back();internal([&]{f.provenance->b3_discard(&f.result,f.result.root,r,{});});}
 {F f;auto r=f.receipt(f.result.root);r.ranges.front().end--;internal([&]{f.provenance->b3_discard(&f.result,f.result.root,r,{});});}
 {F f;auto r=f.receipt(f.result.root);r.participants.front().node->text="x";internal([&]{f.provenance->b3_discard(&f.result,f.result.root,r,{});});}
 {F f;f.discard(f.result.root);auto stale=f.receipt(f.result.root);auto* old=f.result.root;f.provenance->b3_replace(&f.result,old);delete old;f.result.root=f.make(2,2);stale.anchor_box=f.result.root;internal([&]{f.provenance->b3_discard(&f.result,f.result.root,stale,{});});f.discard(f.result.root);f.provenance->validate_final(&f.result);assert(f.provenance->counters().used(b2::Quota::candidates)==2);}
 {F f;f.discard(f.result.root);auto* extra=f.make(2,2);internal([&]{f.discard(extra);});delete extra;}
 {F f;f.discard(f.result.root);auto* extra=f.make(2,2);delete extra;internal([&]{f.provenance->validate_final(&f.result);});}
 {F f(false);auto r=f.receipt(f.result.root);r.minimum_line_height=24;r.available_width=400;for(auto& range:r.ranges)range.disposition=B2Disposition::empty_context;f.provenance->begin_collection(r);f.provenance->preflight(r,0,0);f.provenance->inline_result(r);internal([&]{f.provenance->inline_result(r);});}
 for(auto q:{b2::Quota::candidates,b2::Quota::generations,b2::Quota::bindings,b2::Quota::events,b2::Quota::final_visits}){b2::Counters counters;counters.charge(q,b2::limits[size_t(q)]);bool hit=false;try{counters.charge(q);}catch(const b2::LimitExceeded&){hit=true;}assert(hit);}
 {F f(true,1);internal([&]{f.discard(f.result.root);});}
 {F f(true,2);internal([&]{f.discard(f.result.root);});}
 {F f;f.provenance->counters().charge(b2::Quota::generations,4095);limited([&]{(void)f.make(2,2);});}
 {F f;f.provenance->counters().charge(b2::Quota::candidates,4096);limited([&]{f.discard(f.result.root);});}
 {F f;f.discard(f.result.root);f.provenance->counters().charge(b2::Quota::events,65536);limited([&]{f.move(1,0);});}
 {F f;f.discard(f.result.root);f.provenance->counters().charge(b2::Quota::final_visits,65536);limited([&]{f.provenance->validate_final(&f.result);});}
 {F f;limited([&]{(void)f.provenance->resource()->allocate(4*1024*1024);});}
 for(int phase=0;phase!=5;++phase)oom_phase(phase);
 std::puts("B3 private provenance checks PASS (not public integration gate)");
}
