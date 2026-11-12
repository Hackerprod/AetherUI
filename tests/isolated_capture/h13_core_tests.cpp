#define main capture_entry_unused
#include "main.cpp"
#undef main
#include <cassert>
static long allocation_budget=-1;
void* operator new(size_t n){if(allocation_budget==0)throw std::bad_alloc();if(allocation_budget>0)--allocation_budget;if(auto* v=std::malloc(n?n:1))return v;throw std::bad_alloc();}
void operator delete(void*v)noexcept{std::free(v);}void operator delete(void*v,size_t)noexcept{std::free(v);}
static size_t checks=0;
static void check(bool v){if(!v)std::cerr<<"failed check "<<checks<<"\n";assert(v);++checks;}
static aether_text_metrics_t empty_measure(const char*,const aether_computed_style_t*,float,void*){return {};}
int main(){
 style_property_registry_init();
 for(auto value:{"0","0px","1px","12.5px","+2px","1e2px"})check(h3::literal_limit(value,1));
 for(auto value:{"-1px","10%","2em","1e99px","none","1 px","calc(1px)","var(--a)","0x1px","1.px","1e-999px"})check(!h3::literal_limit(value,1));
 check(!h3::literal_limit("10px",2));
 for(auto decl:{"min-width:20px","max-height:30px!important","min-height:0px;max-height:none"})h3::authored(decl,false);
 std::string authored="min-width:120px;min-height:40px;max-width:140px!important;max-height:60px";
 size_t failures=0;bool complete=false;
 for(long i=0;i<1000;++i){allocation_budget=i;try{h3::authored(authored,false);allocation_budget=-1;complete=true;break;}catch(const std::bad_alloc&){allocation_budget=-1;++failures;}}
 check(complete&&failures>0);
 Objects o;check(aether_doc_create(&o.doc)==AETHER_OK);check(aether_doc_parse_html(o.doc,"<html><body><div id='parent'><canvas id='c' width='100' height='50'></canvas><div id='peer'></div></div></body></html>")==AETHER_OK);
 aether_style_runtime_options_t options{};aether_style_environment_t env{500,200,1,16,0,0,0};
 check(aether_style_runtime_create(&options,&env,nullptr,nullptr,&o.runtime)==AETHER_OK);
 int64_t sheet;int32_t rules;
 check(aether_style_runtime_add_stylesheet(o.runtime,"body{margin:0}#parent{display:flex;width:400px;height:150px;align-items:flex-start}canvas{display:block;width:100px;height:50px;border:2px solid red;min-width:140px;max-height:30px}#peer{width:20px;height:10px;flex-shrink:0}",0,"h13",&sheet,&rules)==AETHER_OK);
 check(aether_layout_create(empty_measure,nullptr,&o.engine)==AETHER_OK);set_initial_canvas_mode(o.engine,true);
 auto* canvas=aether_doc_get_element_by_id(o.doc,"c");
 auto resolve=[&](){aether_style_update_t* updates{};int n=aether_style_runtime_resolve_subtree(o.runtime,aether_doc_get_body(o.doc),&updates);check(n>=0);aether_style_updates_free(updates,n);if(o.styles)aether_style_map_destroy(o.styles);o.styles=nullptr;check(aether_style_runtime_create_resolved_style_map(o.runtime,o.doc,&o.styles)==AETHER_OK);};
 for(int phase=0;phase<2;++phase){resolve();auto expected=h3::before(o.styles,{canvas});check(expected.size()==1&&expected[0].w==(phase?100:140)&&expected[0].h==(phase?50:30));
 if(o.layout)aether_layout_result_destroy(o.layout);o.layout=nullptr;
 check(aether_layout_layout(o.engine,o.doc,o.styles,500,200,nullptr,nullptr,&o.layout)==AETHER_OK);h3::after(o.layout,o.styles,expected);
 // Removing the limiting sheet and re-adding the explicit dimensions removes
 // limits without modifying the DOM or carrying admission across captures.
 if(!phase){check(aether_style_runtime_remove_stylesheet(o.runtime,sheet)!=0);check(aether_style_runtime_add_stylesheet(o.runtime,"body{margin:0}#parent{display:flex;width:400px;height:150px;align-items:flex-start}canvas{display:block;width:100px;height:50px;border:2px solid red}#peer{width:20px;height:10px;flex-shrink:0}",0,"h13-reset",&sheet,&rules)==AETHER_OK);}
 }
 std::cout<<"H13 core checks="<<checks<<" authored OOM="<<failures<<"\n";
}
