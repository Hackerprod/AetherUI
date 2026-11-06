// Private compiler integration: checks stay active with NDEBUG.
#include "style_runtime_types.h"
#include "style_property_registry_internal.h"
#include <cstdio>
#include <cstdlib>
#include <set>
static style_property_registry_s registry;
style_property_registry_s* style_property_registry_get_global(){return &registry;}
static int checks=0;
static void check(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL comments compiler: %s\n",label);std::exit(1);}}
static void compare(const compiled_rule_s& a,const compiled_rule_s& b){
    check(a.rule_id==b.rule_id && a.sheet_handle==b.sheet_handle && a.layer==b.layer && a.source_order==b.source_order,"rule metadata parity");
    check(a.declarations.size()==b.declarations.size(),"property count parity");
    for(size_t i=0;i<a.declarations.size();++i){const auto& x=a.declarations[i];const auto& y=b.declarations[i];check(x.property_id==y.property_id && x.important==y.important && x.declaration_order==y.declaration_order && x.value.mode==y.value.mode && x.value.typed_value==y.value.typed_value && x.value.deferred_source==y.value.deferred_source,"property value and order parity");}
    check(a.custom_declarations.size()==b.custom_declarations.size(),"custom count parity");
    for(size_t i=0;i<a.custom_declarations.size();++i){const auto& x=a.custom_declarations[i];const auto& y=b.custom_declarations[i];check(x.name==y.name && x.source==y.source && x.important==y.important && x.declaration_order==y.declaration_order,"custom value and order parity");}
    check(a.shorthand_declarations.size()==b.shorthand_declarations.size(),"shorthand count parity");
    for(size_t i=0;i<a.shorthand_declarations.size();++i){const auto& x=a.shorthand_declarations[i];const auto& y=b.shorthand_declarations[i];check(x.name==y.name && x.source==y.source && x.important==y.important && x.declaration_order==y.declaration_order,"deferred shorthand parity");}
    check(a.content_declarations.size()==b.content_declarations.size(),"content count parity");
    for(size_t i=0;i<a.content_declarations.size();++i){const auto& x=a.content_declarations[i];const auto& y=b.content_declarations[i];check(x.source==y.source && x.important==y.important && x.declaration_order==y.declaration_order,"content string parity");}
    for(float width:{500.f,800.f,1000.f}){aether_style_environment_t env{width,600,1,16,0,0,0};check(media_condition_matches(a.condition,&env)==media_condition_matches(b.condition,&env),"media parity");}
}
// Conditions can be shared between rules and ancestor branches. Free each node once.
static void gather(media_condition_s* n,std::set<media_condition_s*>& nodes){if(!n||n==media_condition_always()||n==media_condition_never()||n==media_condition_unknown()||!nodes.insert(n).second)return;gather(n->left,nodes);gather(n->right,nodes);gather(n->inner,nodes);}
int main(){
    for(const auto* name:{"width","image","top","right","bottom","left"}) registry.register_property(name,false,0,"0",[](const char* s,std::string& v){v=s;return true;},[](aether_computed_style_t*,const std::string&){});
    registry.register_shorthand("box",[](const char* s,style_shorthand_writer_s& w){for(const auto* name:{"top","right","bottom","left"})w.set(name,s);return true;},{"top","right","bottom","left"});registry.seal();
    const std::string body="{width:1/**/px;--Size:37px/**/;width:var(/**/--Size) !/**/important;image:url('a/**/b');box:var(--Size);content:'x/**/y'}";
    const std::string plain=".card.active,.safe"+body+"@media(min-width:600px){@media(max-width:900px){.card.active"+body+"}}.safe{width:7}";
    const std::string commented="/* ) ] { , */.card/* } ] ) , */.active,/**/.safe"+body+"/* } */@media(min-width:600px){/* { */@media(max-width:900px){.card/**/.active"+body+"}}/**/.safe{width:7}/* eof";
    std::vector<compiled_rule_s> a,b;int32_t ai=41,bi=41;
    check(style_source_compile(plain.c_str(),701,1,a,ai,nullptr,nullptr)==AETHER_OK,"compile control");check(style_source_compile(commented.c_str(),701,1,b,bi,nullptr,nullptr)==AETHER_OK,"compile comments");
    check(a.size()==4 && b.size()==4 && ai==45 && bi==45,"rule counts and next IDs");
    const int orders[]={41,41,43,44};
    for(size_t i=0;i<a.size();++i){compare(a[i],b[i]);check(b[i].rule_id==41+static_cast<int>(i) && b[i].source_order==orders[i] && b[i].sheet_handle==701 && b[i].layer==style_sheet_layer::theme,"literal IDs, source order, handle and layer");check(b[i].selector.steps.size()==1,"comments never create extra selector steps");check((b[i].selector.dependencies&(SELECTOR_DEP_ANCESTOR|SELECTOR_DEP_SIBLING))==0,"no invented relationship dependency");}
    check(b[0].declarations.size()==3 && b[0].custom_declarations.size()==1 && b[0].shorthand_declarations.size()==1 && b[0].content_declarations.size()==1,"nonempty preservation coverage");
    check(b[0].declarations[1].important && b[0].declarations[1].value.mode==compiled_value_mode::deferred,"important deferred var preserved");
    for(float width:{500.f,800.f,1000.f}){aether_style_environment_t env{width,600,1,16,0,0,0};check(media_condition_matches(b[2].condition,&env)==(width==800.f),"both nested media conditions apply");}
    check(style_source_compile(".card/**/.active{width:9}",702,2,b,bi,nullptr,nullptr)==AETHER_OK,"second source compilation");check(b.size()==5 && bi==46 && b.back().sheet_handle==702 && b.back().rule_id==45 && b.back().source_order==45 && b.back().layer==style_sheet_layer::author,"IDs continue across sources and layer changes");
    std::set<media_condition_s*> nodes;for(const auto& r:a)gather(r.condition,nodes);for(const auto& r:b)gather(r.condition,nodes);for(auto* n:nodes)delete n;
    std::printf("selector comments compiler: %d checks passed\n",checks);return 0;
}
