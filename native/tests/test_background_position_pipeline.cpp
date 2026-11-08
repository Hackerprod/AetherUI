#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
static int failures=0, checks=0;
static void must(bool b) { if(!b){std::fprintf(stderr,"fixture failure\n");std::exit(2);} }
static void expect(bool b,const std::string& name){++checks;if(!b){++failures;std::fprintf(stderr,"FAIL %s\n",name.c_str());}}
struct Case {const char* value; float x,y; bool shorthand=false;};
int main(){
 const Case cases[]={
 {"center left",0,50},{"center right",100,50},{"top center",50,0},{"center top",50,0},
 {"top 10px",20,30},{"bottom 25%",20,30},{"left right",20,30},{"10px left",20,30},
 {"left center",0,50},{"20% 30%",20,30},
 {"center left",0,50,true},{"center right",100,50,true},{"top 10px",20,30,true},
 {"bottom 25%",20,30,true},{"left right",20,30,true},
 {"var(--p,90% 90%)",0,50},{"var(--bad,90% 90%)",0,0},
 {"var(--missing,90% 90%)",90,90},{"var(--missing)",0,0},
 {"initial",0,0},{"unset",0,0},{"revert",0,0},{"left right !important",20,30}};
 for(int sheet=0;sheet<2;++sheet) for(const auto& c:cases){
  aether_doc_t d{};must(aether_doc_create(&d)==AETHER_OK);
  std::string decl="background-image:linear-gradient(red,blue);background-position:20% 30%;--p:center left;--bad:top 10px;";
  decl+=c.shorthand?"background:linear-gradient(red,blue) "+std::string(c.value)+" / 40px 20px no-repeat":"background-position:"+std::string(c.value);
  std::string html="<html><body><div id='x' style=\""+(sheet?std::string():decl)+"\"></div></body></html>";
  must(aether_doc_parse_html(d,html.c_str())==AETHER_OK);
  aether_style_runtime_t r{};aether_style_runtime_options_t o{};aether_style_environment_t e{200,120,1,16,0,0,0};
  must(aether_style_runtime_create(&o,&e,nullptr,nullptr,&r)==AETHER_OK);
  if(sheet){int64_t id{};int32_t count{};must(aether_style_runtime_add_stylesheet(r,("#x{"+decl+"}").c_str(),0,"positions.css",&id,&count)==AETHER_OK);}
  aether_style_update_t* u{};int n=aether_style_runtime_resolve_subtree(r,aether_doc_get_document_element(d),&u);must(n>0);bool found=false;
  std::string label=(sheet?"sheet ":"inline ")+std::string(c.shorthand?"shorthand ":"longhand ")+c.value;
  for(int i=0;i<n;++i){aether_node_t node{};aether_computed_style_handle_t h{};must(aether_style_runtime_get_update(u[i],&node,&h,nullptr)==AETHER_OK);
   if(node==aether_doc_get_element_by_id(d,"x")){found=true;aether_computed_style_t s{};must(aether_computed_style_copy(h,&s)==AETHER_OK);expect(s.background_layers.count==1,label+" layer count");
    if(s.background_layers.count==1){auto& l=s.background_layers.items[0];expect(l.position_x.unit==AETHER_CSS_UNIT_PERCENT && l.position_y.unit==AETHER_CSS_UNIT_PERCENT && std::fabs(l.position_x.value-c.x)<.001f && std::fabs(l.position_y.value-c.y)<.001f,label);}
    aether_computed_style_free(&s);}
   aether_computed_style_handle_destroy(h);
  }
  must(found);aether_style_updates_free(u,n);aether_style_runtime_destroy(r);aether_doc_destroy(d);
 }
 std::printf("position pipeline: %d checks, %d failures (active with NDEBUG)\n",checks,failures);return failures?1:0;
}
