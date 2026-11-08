#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif
static int failures=0, checks=0;
static void must(bool b){if(!b){std::fprintf(stderr,"fixture failure\n");std::exit(2);}}
static const char* names[]={"top-left","top-right","bottom-right","bottom-left"};
static aether_corner_radius corner(const aether_border_radii& r,int i){return i==0?r.top_left:i==1?r.top_right:i==2?r.bottom_right:r.bottom_left;}
static void axis(aether_css_length v,float expected,bool percent,const std::string& label){
 ++checks;float actual=v.value;
 bool ok=std::fabs(actual-expected)<.001f && (percent?v.unit==AETHER_CSS_UNIT_PERCENT:(v.unit==AETHER_CSS_UNIT_PX || v.unit==AETHER_CSS_UNIT_LINEAR));
 if(!ok){++failures;std::fprintf(stderr,"FAIL %s expected=%g%s actual=%g unit=%d\n",label.c_str(),expected,percent?"%":"px",actual,v.unit);}}
struct Case{const char* value;float x,y;bool xp=false,yp=false;};
static void run(int selected,int sheet,const Case& c,const std::string& variables="",const std::string& parent=""){
 aether_doc_t d{};must(aether_doc_create(&d)==AETHER_OK);
 std::string prop="border-"+std::string(names[selected])+"-radius";
 std::string decl=variables+prop+":7px;"+prop+":"+c.value+";";
 std::string html="<html><body style=\""+parent+"\"><div id='x' style=\""+(sheet?std::string():decl)+"\"></div></body></html>";
 must(aether_doc_parse_html(d,html.c_str())==AETHER_OK);
 aether_style_runtime_t r{};aether_style_runtime_options_t o{};aether_style_environment_t e{200,120,1,16,0,0,0};must(aether_style_runtime_create(&o,&e,nullptr,nullptr,&r)==AETHER_OK);
 if(sheet){int64_t id{};int32_t count{};must(aether_style_runtime_add_stylesheet(r,("#x{"+decl+"}").c_str(),0,"radii.css",&id,&count)==AETHER_OK);}
 aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(r,aether_doc_get_document_element(d),&u);must(n>0);bool found=false;
 std::string label=std::string(names[selected])+(sheet?" sheet ":" inline ")+c.value;
 for(int i=0;i<n;++i){aether_node_t node{};aether_computed_style_handle_t h{};must(aether_style_runtime_get_update(u[i],&node,&h,nullptr)==AETHER_OK);
  if(node==aether_doc_get_element_by_id(d,"x")){found=true;aether_computed_style_t s{};must(aether_computed_style_copy(h,&s)==AETHER_OK);
   for(int k=0;k<4;++k){auto v=corner(s.border_radii,k);axis(v.horizontal,k==selected?c.x:0,k==selected&&c.xp,label+" "+names[k]+" X");axis(v.vertical,k==selected?c.y:0,k==selected&&c.yp,label+" "+names[k]+" Y");}aether_computed_style_free(&s);}
  aether_computed_style_handle_destroy(h);}
 must(found);aether_style_updates_free(u,n);aether_style_runtime_destroy(r);aether_doc_destroy(d);
}
int main(){
#if defined(__unix__) || defined(__APPLE__)
 Dl_info info{};must(dladdr(reinterpret_cast<void*>(aether_abi_version),&info)!=0);std::printf("loaded=%s\n",info.dli_fname);
#endif
 const Case grammar[]={
 {"10px",10,10},{"10px 20px",10,20},{"25% 50%",25,50,true,true},{"0 20px",0,20},
 {"7px/**/11px",7,11},{"7%11px",7,11,true,false},{"calc(10px + 2px) 20px",12,20},
 {"calc(10px/**/ + 2px) 20px",12,20},{"calc(20px / 2) 20px",10,20},{"calc(0px - 5px) 20px",-5,20},
 {"10px / 20px",7,7},{"10px 20px 30px",7,7},{"10px 20px 30px 40px",7,7},{"-1px 20px",7,7},
 {"10px -1px",7,7},{"auto",7,7},{"10",7,7},{"10/**/px",7,7},{"calc/**/(10px)",7,7},
 {"10px,20px",7,7},{"10px 20px)",7,7},{"calc(1px + 2%) 20px]",7,7},{"1qu 2px",7,7},{"calc(0) 20px",7,7}};
 const Case vars[]={{"var(--pair)",10,20},{"var(--alias)",10,20},{"var(--missing,10px 20px)",10,20},
 {"var(--bad,10px 20px)",0,0},{"var(--empty,10px 20px)",0,0},{"var(--missing)",0,0},
 {"var(--initial)",0,0},{"var(--unset)",0,0},{"var(--revert)",0,0},{"var(--inherit)",31,43}};
 for(int k=0;k<4;++k){std::string parent="border-"+std::string(names[k])+"-radius:31px 43px";
  for(int sheet=0;sheet<2;++sheet){for(const auto& c:grammar)run(k,sheet,c);
   for(const auto& c:vars)run(k,sheet,c,"--pair:10px 20px;--alias:var(--pair);--bad:10px / 20px;--empty:;--initial:initial;--unset:unset;--revert:revert;--inherit:inherit;",parent);
   run(k,sheet,{"10px / 20px !important",7,7});}
  for(const auto& c:{Case{"initial",0,0},Case{"unset",0,0},Case{"revert",0,0},Case{"inherit",31,43}})run(k,0,c,"",parent);
 }
 std::printf("corner pipeline: %d checks, %d failures (active with NDEBUG)\n",checks,failures);return failures?1:0;
}
