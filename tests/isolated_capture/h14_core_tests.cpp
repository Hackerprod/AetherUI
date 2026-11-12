#define main capture_entry_unused
#include "main.cpp"
#undef main
#include <cassert>
#include "../../native/src/css/computed_style_handle.h"
#include <cmath>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}
static size_t checks=0;
static void check(bool b){if(!b)std::cerr<<"failed "<<checks<<std::endl;assert(b);++checks;}
struct Trio {float g,s,b;};
static Trio computed(const std::string& css,bool audit=false,size_t budget=1048576,bool expect=true) {
 Objects o;check(aether_doc_create(&o.doc)==AETHER_OK);check(aether_doc_parse_html(o.doc,"<body><div id='p'><div id='x'></div></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};options.max_variable_depth=32;options.max_variable_expansion_length=4096;
 aether_style_environment_t env{400,200,1,16,0,0,0};check(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);
 h14::Audit a;a.work.remaining=budget;auto hook=a.hook();if(audit)style_runtime_set_value_audit(o.runtime,&hook);
 int64_t handle;int32_t nr;check(aether_style_runtime_add_stylesheet(o.runtime,css.c_str(),1,"h14",&handle,&nr)==AETHER_OK);
 aether_style_transaction_t tx{};auto status=aether_style_runtime_prepare_revision(o.runtime,aether_doc_get_body(o.doc),&env,&tx);
 check((status==AETHER_OK)==expect);if(status!=AETHER_OK){check(tx==nullptr);return {};}
 aether_style_revision_t revision{};check(aether_style_runtime_commit_revision(o.runtime,tx,&revision)==AETHER_OK);aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);
 check(aether_style_runtime_create_resolved_style_map(o.runtime,o.doc,&o.styles)==AETHER_OK);
 const auto v=aether::style::pin_style_map(o.styles);const auto st=v->backing()->find(aether_doc_get_element_by_id(o.doc,"x"))->style_view().scalars();
 return {st.flex_grow,st.flex_shrink,st.flex_basis.kind==AETHER_FLEX_BASIS_AUTO?NAN:st.flex_basis.length.value};
}
static float revision_basis(aether_style_revision_t revision,aether_node_t target) {
 for(int i=0;i<aether_style_revision_style_count(revision);++i){aether_node_t node{};aether_computed_style_handle_t style{};
  check(aether_style_revision_get_style(revision,i,&node,&style,nullptr)==AETHER_OK);
  const float basis=style->style.flex_basis.length.value;aether_computed_style_handle_destroy(style);if(node==target)return basis;
 }check(false);return 0;
}
static size_t runtime_oom(){
 Objects o;check(aether_doc_create(&o.doc)==AETHER_OK);check(aether_doc_parse_html(o.doc,"<body><div id='x' style='flex:0 0 30px'></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{400,200,1,16,0,0,0};check(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);
 aether_style_transaction_t tx{};check(aether_style_runtime_prepare_revision(o.runtime,aether_doc_get_body(o.doc),&env,&tx)==AETHER_OK);
 aether_style_revision_t old{};check(aether_style_runtime_commit_revision(o.runtime,tx,&old)==AETHER_OK);aether_style_transaction_destroy(tx);
 auto* target=aether_doc_get_element_by_id(o.doc,"x");const auto generation=aether_style_revision_generation(old);
 check(aether_element_set_attribute(target,"style","--x:0 0 80px;flex:var(--x)")==AETHER_OK);
 size_t failures=0;bool done=false;
 for(long i=0;i<10000;++i){h14::Audit a;auto hook=a.hook();style_runtime_set_value_audit(o.runtime,&hook);tx=nullptr;allocation_budget=i;
  auto status=aether_style_runtime_prepare_revision(o.runtime,aether_doc_get_body(o.doc),&env,&tx);allocation_budget=-1;
  check(revision_basis(old,target)==30);
  aether_style_revision_t current{};check(aether_style_runtime_retain_revision(o.runtime,&current)==AETHER_OK);check(aether_style_revision_generation(current)==generation);check(revision_basis(current,target)==30);aether_style_revision_destroy(current);
  if(status==AETHER_OK){aether_style_revision_t next{};check(aether_style_runtime_commit_revision(o.runtime,tx,&next)==AETHER_OK);check(revision_basis(next,target)==80);aether_style_revision_destroy(next);aether_style_transaction_destroy(tx);done=true;style_runtime_set_value_audit(o.runtime,nullptr);break;}
  check(status==AETHER_ERROR_OUT_OF_MEMORY&&tx==nullptr);++failures;style_runtime_set_value_audit(o.runtime,nullptr);
 }
 check(done&&failures>0);check(revision_basis(old,target)==30);aether_style_revision_destroy(old);return failures;
}
int main(){
 style_property_registry_init();
 for(auto s:{"1 1 0","0 0 80px","0 1 20%","auto","none","initial"})check(h14::resolved("flex",s));
 for(auto s:{"1 1 2em","1 1 calc(2px)","1 1 -2px","1 1 1e999px",""})check(!h14::resolved("flex",s));
 for(auto s:{"var(--x)","var(--x,var(--y,1 1 0))",R"(v\61 r(--x))"})check(h14::variable_source("flex",s));
 for(auto s:{"var(--x,calc(2px))","var(--x,url(a))","var(--x,unknown())"})check(!h14::variable_source("flex",s));
 check(!h14::variable_source("width","var(--x)"));
 auto a=computed("#p{--x:0 0 80px}#x{flex:var(--x)}",true);check(a.g==0&&a.s==0&&a.b==80);
 for(auto s:{"flex:var(--absent)","--z:;flex:var(--z,0 0 70px)","--z:red;flex:var(--z)","--z:var(--z);flex:var(--z)"}){
  auto b=computed(std::string("#x{flex:2 3 90px;")+s+"}");check(b.g==0&&b.s==1&&std::isnan(b.b));
  computed(std::string("#x{flex:2 3 90px;")+s+"}",true,1048576,false);
 }
 auto b=computed("#x{--x:1 1 0;flex-grow:3!important;flex:var(--x)}",true);check(b.g==3&&b.s==1&&b.b==0);
 computed("#x{--x:0 0 80px;flex:var(--x)}",true,1,false);
 // Reuse the existing pending-shorthand removal contract in a live runtime.
 Objects o;check(aether_doc_create(&o.doc)==AETHER_OK);check(aether_doc_parse_html(o.doc,"<body><div id='x'></div></body>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{400,200,1,16,0,0,0};check(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);
 int64_t first,second;int32_t nr;check(aether_style_runtime_add_stylesheet(o.runtime,"#x{flex:0 0 30px}",1,"base",&first,&nr)==AETHER_OK);check(aether_style_runtime_add_stylesheet(o.runtime,"#x{--x:0 0 80px;flex:var(--x)}",1,"override",&second,&nr)==AETHER_OK);
 for(int phase=0;phase<2;++phase){h14::Audit a;auto hook=a.hook();style_runtime_set_value_audit(o.runtime,&hook);aether_style_update_t* up{};int n=aether_style_runtime_resolve_subtree(o.runtime,aether_doc_get_body(o.doc),&up);check(n>=0);aether_style_updates_free(up,n);if(o.styles)aether_style_map_destroy(o.styles);o.styles=nullptr;check(aether_style_runtime_create_resolved_style_map(o.runtime,o.doc,&o.styles)==AETHER_OK);auto v=aether::style::pin_style_map(o.styles);auto st=v->backing()->find(aether_doc_get_element_by_id(o.doc,"x"))->style_view().scalars();check(st.flex_basis.length.value==(phase?30:80));style_runtime_set_value_audit(o.runtime,nullptr);if(!phase)check(aether_style_runtime_remove_stylesheet(o.runtime,second)!=0);}
 size_t oom=0;bool complete=false;std::string value="var(--x,var(--y,0 0 80px))";
 for(long i=0;i<1000;++i){allocation_budget=i;try{const auto accepted=h14::variable_source("flex",value);allocation_budget=-1;check(accepted);complete=true;break;}catch(const std::bad_alloc&){allocation_budget=-1;++oom;}}
 check(complete);auto runtime_failures=runtime_oom();std::cout<<"H14 core checks="<<checks<<" runtime OOM="<<runtime_failures<<" gate OOM="<<oom<<"\n";
}
