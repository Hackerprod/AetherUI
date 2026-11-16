#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <fstream>
#include <set>
#include "../src/rendering/text/document_text_provider.h"
#include <thread>
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static auto provider(aether::rendering::text::DocumentFontCatalog fonts,DocumentTextBudget& b,aether::rendering::text::truetype::Cancellation* c=nullptr){return aether::rendering::text::DocumentTextProvider::create(fonts,b,c);}
static void collision(aether::rendering::text::DocumentTextProvider& p){for(auto&[key,bucket]:p.products_){if(bucket.empty())continue;auto fake=std::make_shared<aether::rendering::text::DocumentLogicalProduct>(*bucket.front());fake->source="zzzz";fake->width=999;bucket.insert(bucket.begin(),fake);}}
static auto& occurrences(aether::rendering::text::DocumentTextProvider& p){return p.occurrences_;}
static void failure(aether::rendering::text::DocumentTextProvider& p){p.rethrow_failure();}
static bool install(aether_layout_engine_s& e,aether_doc_s*d,aether_style_map_s*m,std::shared_ptr<DocumentTextBackend>b,DocumentTextBudget initial={}){return DocumentTextAccess::install(e,d,m,b,{},initial);}
static auto fragment(aether_layout_engine_s*e,std::shared_ptr<const DocumentTextSource>s,aether_node_s*o,const BoxStyleSnapshot&v){return DocumentTextAccess::fragment(e,{s},o,v,s->decoded,{{s->ordinal,0,1,0,1}});}
};}
static long fail=-1;void* operator new(size_t n){if(fail==0)throw std::bad_alloc();if(fail>0)--fail;if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
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
using namespace aether::rendering::text;
std::shared_ptr<const FontSource> font(const char* name){std::ifstream in(name,std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(in)),{});return std::make_shared<const FontSource>(std::move(b),name);}
int main(){auto regular=font("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),bold=font("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");size_t oom=0,success=0;for(long point=0;point<2000;point+=(point<48?1:131)){Fixture g("<html><body><p>A</p><p>A</p></body></html>");DocumentTextBudget b;auto v=DocumentTextTestAccess::provider({regular,bold},b);need(DocumentTextTestAccess::install(*g.engine,g.doc,g.map,v,b),"install");aether_layout_result_t out{};fail=point;auto status=aether_layout_layout(g.engine,g.doc,g.map,1280,720,nullptr,nullptr,&out);fail=-1;need(!g.engine->layout_in_progress,"OOM resets scope");if(status==AETHER_OK){++success;aether_layout_result_destroy(out);}else{need(status==AETHER_ERROR_OUT_OF_MEMORY,"typed layout OOM");need(out==nullptr,"no partial publish");++oom;}}
printf("Real-provider persistent scalar allocation sampling: OOM=%zu success=%zu checks=%u\n",oom,success,checks);}
