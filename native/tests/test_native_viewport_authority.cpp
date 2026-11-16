#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/native_viewport_capture.h"
#include "../src/layout/layout_internal.h"
#include "../src/css/style_map_types.h"
#include <cassert>
#include <iostream>
#include <thread>
namespace aether::layout {
class RealHtmlTextTestAccess {
public:
 static auto create(aether_doc_s* d,aether_layout_result_s* l,aether_style_map_s* m,unsigned w,unsigned h,aether::rendering::text::truetype::ViewportOffset offset={}){
  return aether::rendering::NativeViewportAuthority::create(d,l,m,w,h,offset);
 }
};
}
using A=aether::rendering::NativeViewportAuthority;
using E=aether::rendering::text::truetype::HtmlTextError;
using Access=aether::layout::RealHtmlTextTestAccess;
struct Context {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};
 explicit Context(bool overflow=true){
  assert(aether_doc_create(&doc)==AETHER_OK);
  const char* raw=overflow?"<html><head></head><body style='margin:0px;padding:0px;border-width:0px;width:auto;overflow:auto'><div style='width:200px;height:200px'></div></body></html>":"<html><head></head><body style='margin:0px;padding:0px;border-width:0px;width:auto;overflow:auto'></body></html>";
  assert(aether_doc_parse_html(doc,raw)==AETHER_OK);
  aether_style_environment_t env{100,80,1,16,0,0,0};
  assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  assert(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);
  assert(aether_style_runtime_commit_revision(runtime,tx,&rev)==AETHER_OK&&aether_style_revision_create_map(rev,&map)==AETHER_OK);
  assert(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK&&aether_layout_layout(engine,doc,map,100,80,nullptr,nullptr,&layout)==AETHER_OK);
 }
 ~Context(){aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
int main(){
 unsigned checks=0;
 Context c;auto result=Access::create(c.doc,c.layout,c.map,100,80);assert(std::holds_alternative<A>(result));
 auto authority=std::move(std::get<A>(result));assert(authority.accepts(c.layout,c.map));++checks;
 assert(!std::get<A>(result).accepts(c.layout,c.map));++checks;
 bool accepted=true;std::thread other([&]{accepted=authority.accepts(c.layout,c.map);});other.join();assert(!accepted);++checks;
 auto othermap=aether_style_map_snapshot(c.map);assert(!authority.accepts(c.layout,othermap.get()));++checks;
 {Context foreign;assert(!authority.accepts(foreign.layout,c.map));++checks;}
 auto& token=*c.doc->identity;
 for(auto epoch:{&token.structure_epoch,&token.mutation_epoch,&token.style_tree_epoch,&token.non_state_epoch}){++*epoch;assert(!authority.accepts(c.layout,c.map));--*epoch;assert(authority.accepts(c.layout,c.map));++checks;}
 auto root_token=c.layout->root->element->identity;root_token->alive=false;assert(!authority.accepts(c.layout,c.map));root_token->alive=true;++checks;
 root_token->attached=false;assert(!authority.accepts(c.layout,c.map));root_token->attached=true;++checks;
 auto saved=c.layout->root->scroll.scrollable_overflow_x;
 c.layout->root->scroll.scrollable_overflow_x=65436;auto cap=Access::create(c.doc,c.layout,c.map,100,80);assert(std::holds_alternative<A>(cap));++checks;
 c.layout->root->scroll.scrollable_overflow_x=65437;auto over=Access::create(c.doc,c.layout,c.map,100,80);assert(std::get<E>(over)==E::limit_exceeded);++checks;
 c.layout->root->scroll.scrollable_overflow_x=saved;
 {Context empty(false);auto zero=Access::create(empty.doc,empty.layout,empty.map,100,80);assert(std::holds_alternative<A>(zero)&&!std::get<A>(zero).scrolling());++checks;}
 aether_doc_destroy(c.doc);c.doc=nullptr;assert(!authority.accepts(c.layout,c.map));++checks;
 std::cout<<"Q2g authority internal PASS: "<<checks<<" controls/negative/lifetime gates. Factory assumes live same-thread handles; no freed-map test.\n";
}
