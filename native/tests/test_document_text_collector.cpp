#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <thread>
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static bool install(aether_layout_engine_s& e,aether_doc_s*d,aether_style_map_s*m,std::shared_ptr<DocumentTextBackend>b){return DocumentTextAccess::install(e,d,m,b,{});}
static auto fragment(aether_layout_engine_s*e,std::shared_ptr<const DocumentTextSource>s,aether_node_s*o,const BoxStyleSnapshot&v){return DocumentTextAccess::fragment(e,{s},o,v,s->decoded,{{s->ordinal,0,1,0,1}});}
};}
unsigned checks=0;void need(bool b,const char*m){++checks;if(!b){fprintf(stderr,"FAIL %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
template<class F>void rejects(F f,const char*m){bool b=false;try{f();}catch(const std::invalid_argument&){b=true;}need(b,m);}
struct Backend:DocumentTextBackend {std::vector<std::shared_ptr<const DocumentTextFragment>> seen; std::shared_ptr<const aether::rendering::text::FontInstance> font=std::make_shared<const aether::rendering::text::FontInstance>(std::vector<uint8_t>{1},"test-only mock",0,1024,0);aether::rendering::text::Measurement measure(const std::shared_ptr<const DocumentTextFragment>& f,DocumentTextBudget&)override{seen.push_back(f);aether_text_metrics_t m{float(f->normalized.size()*8),20,15};std::vector<aether::rendering::text::Glyph> glyphs;for(size_t i=0;i<f->normalized.size();++i)glyphs.push_back({1,uint32_t(i),512,0,0,0});auto run=std::make_shared<const aether::rendering::text::ShapedRun>(font,f->normalized,glyphs,m);return {m,run};}void validate_style(const aether::style::FrozenNodeRef&,const BoxStyleSnapshot&)override{}};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_revision_t revision{};aether_style_map_t map{};aether_layout_engine_t engine{};
 aether_style_environment_t env{1280,720,1,16,0,0,0};
 Fixture(const char*html){ok(aether_doc_create(&doc),"doc");ok(aether_doc_parse_html(doc,html),"parse");ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime),"runtime");sheet(aether::style::web_capture_defaults_css(),0);sheet("html{font-family:Arial;color:#112233}#parent{margin:100px;width:900px}#outside{height:200px}#target{display:flex;width:420px;min-height:72px;border:2px solid black;box-sizing:border-box;align-items:flex-start}#a,#b{width:42px;height:36px;flex-shrink:0}");resolve();ok(aether_layout_create(nullptr,nullptr,&engine),"engine");}
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(revision);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(doc,id);}
 void sheet(const char*s,int layer=1){int64_t h{};ok(aether_style_runtime_add_stylesheet(runtime,s,layer,"test",&h,nullptr),"css");}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),&env,&tx),"prepare");aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(runtime,tx,&next),"commit");aether_style_transaction_destroy(tx);aether_style_revision_destroy(revision);revision=next;aether_style_map_destroy(map);map=nullptr;ok(aether_style_revision_create_map(revision,&map),"map");}
};
int main(){
struct Case{const char*html;const char*css;const char*expected;bool reject;};
Case cases[]={
{"<html><body><p id='p'>A\r<!--x-->\nB</p></body></html>","#p{white-space:pre}","AB",false},
{"<html><body><p id='p'>A\r<br>\nB</p></body></html>","#p{white-space:pre}","AB",false},
{"<html><body><p id='p'>A\n\nB\n</p></body></html>","#p{white-space:pre}","AB",false},
{"<html><body><p id='p'>  A <span> \t B</span> </p></body></html>","","A B",false},
{"<html><body><p id='p'>inter<span>oper</span><!--c-->ability</p></body></html>","","interoperability",false},
{"<html><body><p id='p'> \t </p></body></html>","","",false},
{"<html><body><p id='p'>A  B</p></body></html>","#p{white-space:pre}","A  B",false},
{"<html><body><p id='p'>A<span>B</span></p></body></html>","span{white-space:nowrap}","",true},
{"<html><body><p id='p'>A\tB</p></body></html>","#p{white-space:pre}","",true},
{"<html><body><div id='p'> A </div></body></html>","#p{display:flex}","A",false},
{"<html><body><div id='p'>  </div></body></html>","#p{display:flex}","",false},
{"<html><body><p id='p'></p></body></html>","","",false}};
for(auto c:cases){Fixture f(c.html);if(std::string(c.html).find('\r')!=std::string::npos){auto n=f.node("p")->children.front();need(n->text=="A\n","parser normalized raw CR");n->text="A\r";}if(auto p=f.node("p"))for(auto n:p->children)if(n->type==AETHER_NODE_TEXT){printf("DOMBYTES ");for(unsigned char ch:n->text)printf("%02x",ch);printf("\n");}f.sheet(c.css);f.resolve();auto backend=std::make_shared<Backend>();need(DocumentTextTestAccess::install(*f.engine,f.doc,f.map,backend),"install");auto old=f.engine->contextual_text_measurer;auto olddata=f.engine->contextual_text_measurer_data;
aether_layout_set_contextual_text_measurer(f.engine,[](const char*,const aether_computed_style_t*,float,float,void*){return aether_text_metrics_t{};},reinterpret_cast<void*>(1));need(f.engine->contextual_text_measurer==old&&f.engine->contextual_text_measurer_data==olddata,"setter does not mutate transaction engine");aether_layout_result_t result{};auto status=aether_layout_layout(f.engine,f.doc,f.map,1280,720,nullptr,nullptr,&result);if(c.reject){need(status!=AETHER_OK,"negative rejects");need(backend->seen.empty(),"negative before backend");}else{need(status==AETHER_OK,"collector layout");if(auto box=aether_layout_result_find_primary_box(result,f.node("p"))){auto h=aether_layout_box_get_height(box);printf("GEOMETRY html=%s h=%g\n",c.html,h);float expected=(*c.expected?20:0);if(std::string(c.html).find('\r')!=std::string::npos)expected=std::string(c.html).find("<br>")!=std::string::npos?80:40;else if(std::string(c.html).find("A\n\nB\n")!=std::string::npos)expected=60;need(h==expected,"independent line geometry");}std::map<uint64_t,std::string> passes;std::map<uint64_t,bool> occurrence;for(auto&v:backend->seen)if(!occurrence[v->occurrence]){passes[v->normalization_pass]+=v->normalized;occurrence[v->occurrence]=true;}if(*c.expected){need(!passes.empty(),"actual collector measured");for(auto&[pass,text]:passes){if(text!=c.expected)fprintf(stderr,"pass %llu text=[%s] expected=[%s]\n",(unsigned long long)pass,text.c_str(),c.expected);need(text==c.expected,"normalized collector bytes");}}else need(backend->seen.empty(),"empty text no measurement");}need(!f.engine->layout_in_progress,"scope reset");aether_layout_result_destroy(result);}
printf("Collector independent checks=%u mock metrics only\n",checks);}
