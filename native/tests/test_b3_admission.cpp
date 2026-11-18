// Private compiler/accounting regression; no renderer shortcut or fixture edits.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/style_runtime/style_runtime_types.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct Result {style_source_accounting_s accounting;size_t properties;};
static Result compile(const std::string& css){
 std::vector<compiled_property_declaration_s> p;std::vector<custom_property_declaration_s> c;std::vector<compiled_shorthand_declaration_s> s;style_source_accounting_s a;
 const auto status=style_source_compile_inline(css.c_str(),p,c,s,nullptr,nullptr,nullptr,&a);if(status!=AETHER_OK)std::fprintf(stderr,"compiler status%d css=%s\n",status,css.c_str());assert(status==AETHER_OK);return {a,p.size()};
}
int main(){
 aether_style_runtime_options_t options{};aether_style_environment_t env{640,480,1,16,0,0,0};aether_style_runtime_t runtime{};assert(aether_style_runtime_create(&options,&env,nullptr,nullptr,&runtime)==AETHER_OK);
 for(const char* v:{"auto"," AUTO ","/*a*/auto/*b*/","\\61 uto"})for(const char* n:{"min-width","min-height"}){
  auto r=compile(std::string(n)+":"+v);assert(r.properties==1&&r.accounting.losses==0&&r.accounting.attribution.complete&&r.accounting.b3_subset&&!r.accounting.b0_subset);
 }
 for(const char* v:{"0","0px","1px","8192px","10%","1em","calc(1px + 2px)"}){
  auto r=compile(std::string("min-height:")+v);assert(r.properties==1&&r.accounting.losses==0);
 }
 for(const char* v:{"none","-1px","auto auto","auto 0px","max-content","potato"}){
  auto r=compile(std::string("min-height:")+v);assert(r.properties==0&&r.accounting.losses!=0);
 }
 for(const char* text:{"min-width:none","min-height:none","max-width:auto","max-height:auto","width:101%","height:50%"}){
  auto r=compile(text);assert(!r.accounting.b3_subset&&r.accounting.b3_has_value_rejection());
  auto missing=r.accounting;missing.b3_rejected_values.fill(0);assert(!missing.b3_has_value_rejection()&&!missing.b3_subset);
  auto corrupt=r.accounting;corrupt.b3_first_rejection_kind=99;assert(!corrupt.b3_has_value_rejection());
  auto again=compile(std::string(text)+";width:10px;unknown-property:1");assert(again.accounting.losses&&again.accounting.b3_has_value_rejection());
 }
 for(const char* text:{"min-width:auto;max-width:none","padding:54px 52px","border:1px solid #34394b","gap:20px","display:flex;flex-direction:column;flex-wrap:nowrap;align-items:start;justify-content:space-between","width:52%;height:800px;box-sizing:border-box"}){auto r=compile(text);assert(r.accounting.b3_subset&&r.accounting.losses==0);}
 for(const char* text:{"flex:0 1 auto","flex-grow:0","flex-shrink:1","flex-basis:auto","order:0","align-self:auto","align-content:stretch","flex-wrap:wrap","flex-direction:row-reverse","align-items:baseline","gap:1%","width:max-content","--x:20px;width:var(--x)","width:calc(1px + 2px)"})assert(!compile(text).accounting.b3_subset);
 auto a=compile("width:10px").accounting;auto b=compile("color:#123456;min-height:none").accounting;a.merge(b);assert(a.b3_has_value_rejection()&&a.b3_first_rejection_declaration==2&&a.b3_first_rejection_kind==1&&a.losses);
 aether_style_runtime_destroy(runtime);
 puts("B3 admission parser and bounded rejection metadata PASS");
}
