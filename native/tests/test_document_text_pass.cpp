#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <thread>
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static auto mapped(aether_layout_engine_s*e,const std::vector<std::shared_ptr<const DocumentTextSource>>&s,aether_node_s*o,const BoxStyleSnapshot&v,const std::string&t,const std::vector<DocumentTextRange>&r){return DocumentTextAccess::fragment(e,s,o,v,t,r);}
static void beginpass(aether_layout_engine_s*e,const std::vector<std::shared_ptr<const DocumentTextSource>>&s){DocumentTextAccess::begin_pass(e,s);}
static void endpass(aether_layout_engine_s*e){DocumentTextAccess::finish_pass(e);}
static void abort(aether_layout_engine_s*e){DocumentTextAccess::abort_pass(e);}
static auto budget(aether_layout_engine_s*e){return DocumentTextAccess::normalization_budget(e);}
static bool install(aether_layout_engine_s& e,aether_doc_s*d,aether_style_map_s*m,std::shared_ptr<DocumentTextBackend>b){return DocumentTextAccess::install(e,d,m,b,{});}
static auto fragment(aether_layout_engine_s*e,std::shared_ptr<const DocumentTextSource>s,aether_node_s*o,const BoxStyleSnapshot&v){return DocumentTextAccess::fragment(e,{s},o,v,s->decoded,{{s->ordinal,0,1,0,1}});}
};}
unsigned checks=0;void need(bool b,const char*m){++checks;if(!b){fprintf(stderr,"FAIL %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
template<class F>void rejects(F f,const char*m){bool b=false;try{f();}catch(const std::invalid_argument&){b=true;}need(b,m);}
struct Backend:DocumentTextBackend{unsigned validation=0,measurement=0; aether::rendering::text::Measurement measure(const std::shared_ptr<const DocumentTextFragment>&,DocumentTextBudget&) override{++measurement;throw std::invalid_argument("TEST_BACKEND_STOP");}void validate_style(const aether::style::FrozenNodeRef&,const BoxStyleSnapshot&)override{++validation;}};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};
 aether_style_environment_t env{1280,720,1,16,0,0,0};
 Fixture(){ok(aether_doc_create(&doc),"doc");ok(aether_doc_parse_html(doc,"<html><body><section id='parent'><div id='outside'></div><div id='target'>A<span id='a'></span>B<div id='b'>C</div></div></section></body></html>"),"parse");ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"runtime");sheet(aether::style::web_capture_defaults_css(),0);sheet("html{font-family:Arial;color:#112233}#parent{margin:100px;width:900px}#outside{height:200px}#target{display:flex;width:420px;min-height:72px;border:2px solid black;box-sizing:border-box;align-items:flex-start}#a,#b{width:42px;height:36px;flex-shrink:0}");resolve();ok(aether_layout_create(nullptr,nullptr,&engine),"engine");}
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 void sheet(const char*s,int layer=1){int64_t h{};ok(aether_style_runtime_add_stylesheet(runtime,s,layer,"test",&h,nullptr),"css");}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&tx),"prepare");aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next),"commit");aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);revision=next;aether_style_map_destroy(map);map=nullptr;ok(aether_style_revision_create_map(revision,&map),"map");}
};
// Prepared against candidate 4739b644. Execute only after approved homogeneous build.
int main(){
for(int mode=0;mode<5;++mode){Fixture f;auto base=aether::style::pin_style_map(f.map);auto p=f.node("target");auto record=base->backing()->find_owner(p);StyleLookup lookup(base,nullptr,record->style());BoxStyleSnapshot style(record->style(),record->measures());auto backend=std::make_shared<Backend>();need(DocumentTextTestAccess::install(*f.engine,f.doc,f.map,backend),"install");DocumentTextAccess::begin(f.engine,f.doc,lookup);auto a=DocumentTextAccess::source(f.engine,p,p->children[0],style);auto b=DocumentTextAccess::source(f.engine,p,p->children[2],style);need(a->decoded=="A"&&b->decoded=="B","distinct DOM text");need(a->dom_ordinal<b->dom_ordinal,"actual DOM order");need(DocumentTextTestAccess::budget(f.engine).decoded_scalars==2,"decoded source once");
if(mode==0){rejects([&]{DocumentTextTestAccess::beginpass(f.engine,{b,a});},"reordered complete vector rejected");rejects([&]{DocumentTextTestAccess::beginpass(f.engine,{a,a});},"duplicate source rejected");}
DocumentTextTestAccess::beginpass(f.engine,{a,b});auto fa=DocumentTextTestAccess::fragment(f.engine,a,p,style);
auto calls=backend->measurement;rejects([&]{DocumentTextAccess::measure(f.engine,fa,style,p);},"incomplete pass cannot measure");need(backend->measurement==calls,"no callback before pass proof");
if(mode==1){rejects([&]{DocumentTextTestAccess::endpass(f.engine);},"missing B coverage");DocumentTextTestAccess::abort(f.engine);continue;}
if(mode==2){DocumentTextTestAccess::fragment(f.engine,a,p,style);DocumentTextTestAccess::fragment(f.engine,b,p,style);rejects([&]{DocumentTextTestAccess::endpass(f.engine);},"duplicate A coverage");DocumentTextTestAccess::abort(f.engine);continue;}
DocumentTextTestAccess::fragment(f.engine,b,p,style);DocumentTextTestAccess::endpass(f.engine);
if(mode==3){auto before=DocumentTextTestAccess::budget(f.engine);DocumentTextTestAccess::beginpass(f.engine,{a,b});DocumentTextTestAccess::fragment(f.engine,a,p,style);DocumentTextTestAccess::fragment(f.engine,b,p,style);DocumentTextTestAccess::endpass(f.engine);auto after=DocumentTextTestAccess::budget(f.engine);need(after.decoded_scalars==before.decoded_scalars,"repass no redecode");need(after.normalization_steps>before.normalization_steps,"repass charges work");}
if(mode==4){ok(aether_element_set_attribute(p,"style","color:red"),"mutate");auto prior=backend->measurement;rejects([&]{DocumentTextAccess::measure(f.engine,fa,style,p);},"mutation invalidates proof");need(prior==backend->measurement,"stale before callback");}
DocumentTextAccess::finish(f.engine);
}
for(int mode=0;mode<3;++mode){Fixture f;auto p=f.node("target");p->children[0]->text=mode==0?"AA":"  ";if(mode==1)f.sheet("#target{white-space:pre}");f.resolve();auto base=aether::style::pin_style_map(f.map);auto record=base->backing()->find_owner(p);StyleLookup lookup(base,nullptr,record->style());BoxStyleSnapshot style(record->style(),record->measures());auto backend=std::make_shared<Backend>();need(DocumentTextTestAccess::install(*f.engine,f.doc,f.map,backend),"overlap install");DocumentTextAccess::begin(f.engine,f.doc,lookup);auto a=DocumentTextAccess::source(f.engine,p,p->children[0],style);DocumentTextTestAccess::beginpass(f.engine,{a});auto mapped=[&]{DocumentTextTestAccess::mapped(f.engine,{a},p,style,mode==0?"A":" ",{{a->ordinal,0,1,0,1},{a->ordinal,1,1,0,1}});DocumentTextTestAccess::endpass(f.engine);};if(mode<2){rejects(mapped,mode==0?"AA cannot collapse to A":"PRE spaces cannot collapse");DocumentTextTestAccess::abort(f.engine);}else{mapped();DocumentTextAccess::finish(f.engine);}}
printf("Document pass proof checks=%u; collector integration NOT_TESTED\n",checks);
}
