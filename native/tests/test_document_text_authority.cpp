#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <cstring>
#include <thread>
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static bool install(aether_layout_engine_s& e,aether_doc_s*d,aether_style_map_s*m,std::shared_ptr<DocumentTextBackend>b){return DocumentTextAccess::install(e,d,m,b,{});}
static void begin_pass(aether_layout_engine_s* e,const std::shared_ptr<const DocumentTextSource>& s){DocumentTextAccess::begin_pass(e,{s});}
static void finish_pass(aether_layout_engine_s* e){DocumentTextAccess::finish_pass(e);}
static auto fragment(aether_layout_engine_s*e,std::shared_ptr<const DocumentTextSource>s,aether_node_s*o,const BoxStyleSnapshot&v){return DocumentTextAccess::fragment(e,{s},o,v,s->decoded,{{s->ordinal,0,1,0,1}});}
};}
unsigned checks=0;void need(bool b,const char*m){++checks;if(!b){fprintf(stderr,"FAIL %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
template<class F>void rejects(F f,const char*m){bool b=false;try{f();}catch(const std::invalid_argument&){b=true;}need(b,m);}
template<class F>void rejects_exact(F f,const char* expected,const char* label){
 bool rejected=false;
 try{f();}catch(const std::invalid_argument& e){rejected=std::strcmp(e.what(),expected)==0;}
 need(rejected,label);
}
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
int main(){Fixture f;auto base=aether::style::pin_style_map(f.map);auto p=f.node("target");auto t=p->children.front();need(t->type==AETHER_NODE_TEXT,"real text");MutableLayoutOverlay overlay;auto ps=base->backing()->find_owner(p);auto style=ps->style();auto values=style->view().scalars();values.font_size=23;auto custom=std::make_shared<const aether::style::FrozenStyle>(values,std::make_shared<const aether::style::FrozenStylePayload>(aether::style::ImmutableArray<aether::style::FrozenBackgroundLayer>{}, aether::style::ImmutableArray<aether_backdrop_filter>{}, aether::style::FrozenGradient{}, aether::style::ImmutableArray<aether_transform_operation>{}, aether::style::ImmutableArray<aether_shadow>{}, aether::style::ImmutableArray<aether_shadow>{}));
auto make=[&](aether_node_s* text){auto n=std::make_shared<aether_node_s>();n->type=AETHER_NODE_ELEMENT;n->parent=p;n->style_origin=p;n->anonymous_flex_owner=p;n->children={text};overlay.insert(n,custom,ps->measures());return n;};
auto a=make(t),b=make(t);StyleLookup lookup(base,&overlay,style);BoxStyleSnapshot av(custom,ps->measures()),pv(style,ps->measures());auto backend=std::make_shared<Backend>();need(DocumentTextTestAccess::install(*f.engine,f.doc,f.map,backend),"install");DocumentTextAccess::begin(f.engine,f.doc,lookup);
auto source=DocumentTextAccess::source(f.engine,a.get(),t,av);need(source->decoded=="A","original text copied");
need(DocumentTextAccess::source(f.engine,a.get(),t,av)==source,"owner source cache");
auto source2=DocumentTextAccess::source(f.engine,b.get(),t,av);need(source2!=source,"owner-specific sources");
// Missing-pass is an explicit negative, not the accidental failure of the first
// positive. The two source owners share DOM order, so use separate real passes.
rejects_exact([&]{DocumentTextTestAccess::fragment(f.engine,source,a.get(),av);},
 "DOCUMENT_TEXT_FRAGMENT","fragment before pass rejected at lifecycle guard");
DocumentTextTestAccess::begin_pass(f.engine,source);
rejects_exact([&]{DocumentTextTestAccess::fragment(f.engine,source,b.get(),av);},
 "DOCUMENT_TEXT_CROSS_OWNER","active-pass cross overlay source rejected at ownership guard");
auto frag=DocumentTextTestAccess::fragment(f.engine,source,a.get(),av);
need(frag->owner_record()==ps,"parent anchor not fabricated record");
need(frag->style().font_size==23,"overlay style retained");
need(frag->normalized=="A","valid fragment preserves original text");
DocumentTextTestAccess::finish_pass(f.engine);
// Successful finish proves the failed cross-owner attempt consumed no coverage.
DocumentTextTestAccess::begin_pass(f.engine,source2);
auto frag2=DocumentTextTestAccess::fragment(f.engine,source2,b.get(),av);
need(frag2!=frag&&frag2->owner_record()==ps&&frag2->normalized=="A"&&frag2->style().font_size==23,
 "second overlay owner is independently authentic in its own pass");
DocumentTextTestAccess::finish_pass(f.engine);
// Successful finish proves independent coverage for the second source owner.
rejects_exact([&]{DocumentTextTestAccess::fragment(f.engine,source,a.get(),av);},
 "DOCUMENT_TEXT_FRAGMENT","fragment after pass rejected without automatic pass");
rejects([&]{DocumentTextAccess::source(f.engine,a.get(),t,pv);},"parent style substitution");auto sibling=p->children[2];need(sibling->type==AETHER_NODE_TEXT,"sibling text");rejects([&]{DocumentTextAccess::source(f.engine,a.get(),sibling,av);},"nonmember same parent");auto other=f.node("b")->children.front();auto bad=make(other);rejects([&]{DocumentTextAccess::source(f.engine,bad.get(),other,av);},"member wrong parent");
auto calls=backend->validation;std::thread thread([&]{rejects([&]{DocumentTextAccess::source(f.engine,a.get(),t,av);},"wrong thread source");DocumentTextAccess::finish(f.engine);});thread.join();need(backend->validation==calls,"wrong thread no backend");need(DocumentTextAccess::source(f.engine,a.get(),t,av)==source,"wrong thread finish no mutation");
DocumentTextAccess::finish(f.engine);rejects([&]{DocumentTextAccess::source(f.engine,a.get(),t,av);},"finished source");rejects([&]{DocumentTextAccess::measure(f.engine,frag,av,a.get());},"finished measure");rejects([&]{DocumentTextAccess::begin(f.engine,f.doc,lookup);},"no restart");need(frag->normalized=="A"&&frag->style().font_size==23,"retained immutable proof");need(backend->measurement==0,"no unintended measurement");printf("A-context overlay authority checks=%u; collector/RAII integration NOT_TESTED\n",checks);}
