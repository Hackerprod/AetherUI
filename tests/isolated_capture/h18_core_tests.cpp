#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include <aether/aether_contextual_style.h>
#include "../../native/src/css/computed_style_handle.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <unordered_map>
static long alloc_budget=-1; static bool injected=false;
void* operator new(size_t n){if(alloc_budget==0){injected=true;throw std::bad_alloc();}if(alloc_budget>0)--alloc_budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static size_t checks=0;
void ck(bool b){if(!b)std::cerr<<"FAIL check "<<checks<<"\n";assert(b);++checks;}
static aether_text_metrics_t no_text(const char*,const aether_computed_style_t*,float,void*){return {};}
static bool near(float a,float b){return std::abs(a-b)<.01f;}
static const char* base="body{display:block;margin:0}div,section,span{display:block}#p,#q{display:flex;gap:5px;width:160px;height:100px;align-items:flex-start}#w,#v{display:contents}#w{padding:20px;border:7px solid red;margin:9px;width:500px;height:500px;order:-99;color:red} .item{width:30px;height:20px;flex-shrink:0;background:blue}#b{order:-1}#c{order:1}";
static const char* html="<body><div id='p'><div id='a' class='item'></div><section id='w'><span id='v'> \n<!--x--><div id='b' class='item'></div><div id='c' class='item'></div></span><span id='empty' style='display:contents'></span></section><div id='d' class='item'></div></div><div id='q'></div></body>";
struct Scene {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t result{}; std::string css;
 Scene(std::string extra="",const char* h=html):css(std::string(base)+extra){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,h)==AETHER_OK);new_runtime();ck(aether_layout_create(no_text,nullptr,&engine)==AETHER_OK);resolve();}
 void new_runtime(){aether_style_environment_t env{400,300,1,16,0,0,0};ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&rt)==AETHER_OK);int64_t sh{};int32_t nr{};ck(aether_style_runtime_add_stylesheet(rt,css.c_str(),1,"h18",&sh,&nr)==AETHER_OK);}
 void resolve(){aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(rt,aether_doc_get_body(doc),&u);ck(n>=0);aether_style_updates_free(u,n);aether_style_map_destroy(map);map=nullptr;ck(aether_style_runtime_create_resolved_style_map(rt,doc,&map)==AETHER_OK);aether_layout_result_destroy(result);result=nullptr;ck(aether_layout_layout(engine,doc,map,400,300,nullptr,nullptr,&result)==AETHER_OK);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}aether_layout_box_s* box(const char* id){return result->find_primary_box(node(id));}
 void fresh_equal(){std::unordered_map<aether_node_t,aether_rectf> old;for(auto* b:result->allocated_boxes)if(b->element)old.emplace(b->element,b->border_box);aether_style_runtime_destroy(rt);rt=nullptr;new_runtime();resolve();for(auto* b:result->allocated_boxes)if(b->element){ck(old.count(b->element));auto r=old.at(b->element);ck(near(r.x,b->border_box.x)&&near(r.y,b->border_box.y)&&near(r.width,b->border_box.width)&&near(r.height,b->border_box.height));}ck(old.size()==result->live_box_count);}
 ~Scene(){aether_layout_result_destroy(result);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
};
int main(){
 static_assert(AETHER_DISPLAY_TABLE_CELL==8&&AETHER_DISPLAY_CONTENTS==9);
 {Scene s;ck(!s.box("w")&&!s.box("v")&&!s.box("empty"));ck(s.node("b")->parent==s.node("v")&&s.node("v")->parent==s.node("w"));auto p=s.box("p");ck(p->children.size()==4);ck(p->children[0]->element==s.node("b")&&p->children[1]->element==s.node("a")&&p->children[2]->element==s.node("d")&&p->children[3]->element==s.node("c"));ck(near(s.box("b")->border_box.x,0)&&near(s.box("a")->border_box.x,35)&&near(s.box("d")->border_box.x,70)&&near(s.box("c")->border_box.x,105));ck(aether_layout_result_hit_test(s.result,5,5)==s.node("b"));ck(aether_layout_result_find_primary_box(s.result,s.node("w"))==nullptr);}
 {Scene s("#w > #v > .item{width:40px}#v > div:first-of-type{height:25px}#v{color:green}");ck(near(s.box("b")->border_box.width,40)&&near(s.box("c")->border_box.width,40)&&near(s.box("b")->border_box.height,25));ck(near(s.box("b")->style.color.g,128.f/255.f));}
 for(auto dir:{"row","column","row-reverse","column-reverse"})for(auto wrap:{"nowrap","wrap"}) {Scene s(std::string("#p{width:80px;height:60px;flex-direction:")+dir+";flex-wrap:"+wrap+"}");ck(!s.box("w")&&s.box("p")->children.size()==4);s.fresh_equal();}
 {Scene s("#a{display:none}#b,#c,#d{order:0}");ck(s.box("p")->children.size()==3);ck(s.box("p")->children[0]->element==s.node("b"));}
 {Scene s;
  // Change all nested wrappers together so the interim unsupported non-flex
  // context is never used for layout.
  auto change=[&](const char* value){for(auto id:{"w","v","empty"})ck(aether_element_set_attribute(s.node(id),"style",value)==AETHER_OK);s.resolve();s.fresh_equal();};
  change("display:block");ck(s.box("w")&&s.box("v"));change("display:contents");ck(!s.box("w"));change("display:none");ck(s.box("p")->children.size()==2);change("display:contents");
  ck(aether_node_append_child(s.node("q"),s.node("w"))==AETHER_OK);s.resolve();s.fresh_equal();ck(s.box("q")->children.size()==2);ck(aether_node_append_child(s.node("v"),s.node("a"))==AETHER_OK);s.resolve();s.fresh_equal();ck(s.box("q")->children.size()==3);
  auto detached=s.node("b");ck(aether_node_remove_child(s.node("v"),detached)==AETHER_OK);s.resolve();s.fresh_equal();ck(s.box("q")->children.size()==2);ck(aether_node_append_child(s.node("v"),detached)==AETHER_OK);s.resolve();s.fresh_equal();
 }
 // Owning clone and retained box tree remain valid after style providers go away.
 {Scene s;aether_style_revision_t revision{};ck(aether_style_runtime_retain_revision(s.rt,&revision)==AETHER_OK);aether_computed_style_handle_t saved{};
  for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t node{};aether_computed_style_handle_t h{};uint32_t impact{};ck(aether_style_revision_get_style(revision,i,&node,&h,&impact)==AETHER_OK);if(node==s.node("w"))ck(aether_computed_style_handle_clone(h,&saved)==AETHER_OK);aether_computed_style_handle_destroy(h);}
  aether_style_revision_destroy(revision);aether_style_map_destroy(s.map);s.map=nullptr;aether_style_runtime_destroy(s.rt);s.rt=nullptr;
  ck(saved&&saved->style.display==AETHER_DISPLAY_CONTENTS);ck(!s.box("w")&&s.box("p")->children.size()==4);ck(aether_layout_result_hit_test(s.result,5,5)==s.node("b"));aether_computed_style_handle_destroy(saved);
 }
 // Unsupported resolved contexts fail closed and leave prior geometry usable.
 for(auto bad:{"#p{display:block}","#w{position:relative}","#w{opacity:.5}","#w{transform:translateX(1px)}","#w{overflow:hidden}","#b{position:absolute}"}) {
  Scene s;auto* retained=s.result;auto before=s.box("c")->border_box;
  s.css+=bad;aether_style_runtime_destroy(s.rt);s.rt=nullptr;s.new_runtime();
  aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(s.rt,aether_doc_get_body(s.doc),&u);ck(n>=0);aether_style_updates_free(u,n);
  aether_style_map_destroy(s.map);s.map=nullptr;ck(aether_style_runtime_create_resolved_style_map(s.rt,s.doc,&s.map)==AETHER_OK);
  aether_layout_result_t out=nullptr;ck(aether_layout_layout(s.engine,s.doc,s.map,400,300,nullptr,nullptr,&out)==AETHER_ERROR_INVALID_STATE);ck(!out&&s.result==retained&&near(s.box("c")->border_box.x,before.x));
 }
 // Layout allocation failure never replaces the retained result or modifies DOM.
 size_t oom=0,fallback=0;bool done=false;{Scene s;auto* old=s.result;auto before=s.box("c")->border_box;
 for(long n=0;n<5000;++n){aether_layout_result_t out=nullptr;injected=false;alloc_budget=n;auto status=aether_layout_layout(s.engine,s.doc,s.map,400,300,nullptr,nullptr,&out);alloc_budget=-1;
  ck(s.result==old&&near(s.box("c")->border_box.x,before.x)&&!s.box("w")&&s.node("b")->parent==s.node("v"));
  if(status==AETHER_OK){ck(out&&out->find_primary_box(s.node("w"))==nullptr);auto b=out->find_primary_box(s.node("c"));ck(b&&near(b->border_box.x,before.x));aether_layout_result_destroy(out);if(!injected){done=true;break;}++fallback;}
  else {ck(status==AETHER_ERROR_OUT_OF_MEMORY&&out==nullptr);++oom;}
 }ck(done&&oom>0);s.resolve();}
 std::cout<<"H18 checks="<<checks<<" OOM="<<oom<<" fallbacks="<<fallback<<"\n";
}
