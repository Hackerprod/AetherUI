#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include "../src/layout/layout_types.h"
#include "../src/layout/document_root_layout.h"
#include "../src/layout/document_text_context.h"
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include "../src/rendering/document_viewport_capture.h"
#include "../src/rendering/display_list_types.h"
#include <thread>
using namespace aether::layout;
namespace aether::layout {class DocumentTextTestAccess {public:
static auto root(aether_layout_engine_t e,aether_doc_t d,aether_style_map_t m,aether_layout_result_t*o){return DocumentRootLayoutAccess::layout(e,d,m,1280,720,o);}
static auto viewport(aether_doc_s*d,aether_layout_result_s*l,aether_style_map_s*m,aether::rendering::text::truetype::ViewportOffset o,DocumentTextBudget&b,std::function<void()> check={}){return aether::rendering::DocumentViewportAuthority::create(d,l,m,1280,720,o,b,check);}
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
for(const char* overflow:{"visible","auto"}){
 Fixture f("<html><body><div id='p'></div></body></html>");std::string css=std::string("html{overflow:")+overflow+"}#p{height:1000.5px;margin-top:20px;margin-bottom:30px}";f.sheet(css.c_str());f.resolve();
 for(unsigned pass=0;pass<2;++pass){aether_layout_result_t l{};ok(DocumentTextTestAccess::root(f.engine,f.doc,f.map,&l),"private rootlayout");need(l->root->border_box.y==20,"CSS collapsed rootY20");need(l->root->border_box.height==1000.5,"height preserved");DocumentTextBudget b;auto auth=DocumentTextTestAccess::viewport(f.doc,l,f.map,{},b);need(auth.diagnostics().extent_height==1050.5,"CSS extent1050.5");need(auth.diagnostics().range_y==330.5,"double scrollrange330.5");aether_layout_result_destroy(l);}
 aether_layout_result_t old{};ok(aether_layout_layout(f.engine,f.doc,f.map,1280,720,nullptr,nullptr,&old),"legacy layout");need(old->root->border_box.y==8,"legacyY8 unchanged");aether_layout_result_destroy(old);
}
for(const char* css:{"html{padding:1px}","html{margin:1px}","html{border:1px solid black}","html{width:100px}","html{position:relative}","html{overflow:hidden}","html{direction:rtl}","html{transform:translateX(1px)}"}){
 Fixture f("<html><body><div></div></body></html>");f.sheet(css);f.resolve();aether_layout_result_t l{};need(DocumentTextTestAccess::root(f.engine,f.doc,f.map,&l)!=AETHER_OK,"unsupported HTML geometry rejects");need(l==nullptr,"rejection no result");
}
{Fixture f("<html><body id='body'><div id='p'></div></body></html>");auto html=aether_doc_get_document_element(f.doc),body=f.node("body");ok(aether_node_remove_child(html,body),"detach body");ok(aether_node_remove_child(f.doc,html),"detach HTML");ok(aether_node_append_child(f.doc,body),"standalone body");need(aether_doc_get_document_element(f.doc)==body,"actual standaloneBODY");f.sheet("#p{height:100px;margin-top:20px}");f.resolve();aether_layout_result_t old{},fresh{};ok(aether_layout_layout(f.engine,f.doc,f.map,1280,720,nullptr,nullptr,&old),"standalone legacy");ok(DocumentTextTestAccess::root(f.engine,f.doc,f.map,&fresh),"standalone new");need(old->root->border_box.y==fresh->root->border_box.y&&old->root->border_box.height==fresh->root->border_box.height,"standalone unchanged");aether_layout_result_destroy(old);aether_layout_result_destroy(fresh);}

struct Case{const char* css;float root,child,leaf;};
for(auto c:{Case{"#p{margin-top:20px}#leaf{height:10px}",20,20,20},Case{"#p{margin-top:0}#leaf{height:10px}",8,8,8},Case{"body{padding-top:1px}#p{margin-top:20px}#leaf{height:10px}",8,29,29},Case{"body{border-top:1px solid black}#p{margin-top:20px}#leaf{height:10px}",8,29,29},Case{"#p{margin-top:20px}#leaf{margin-top:200px;height:10px}",200,200,200}}){
 Fixture f("<html><body><div id='p'><div id='leaf'></div></div></body></html>");f.sheet(c.css);f.resolve();aether_layout_result_t l{},old{};ok(DocumentTextTestAccess::root(f.engine,f.doc,f.map,&l),"nested new");ok(aether_layout_layout(f.engine,f.doc,f.map,1280,720,nullptr,nullptr,&old),"nested legacy");
 auto* child=l->root->children.at(0);auto* leaf=child->children.at(0);printf("nested root=%g child=%g leaf=%g expected=%g,%g,%g\n",l->root->border_box.y,child->border_box.y,leaf->border_box.y,c.root,c.child,c.leaf);
 need(l->root->border_box.y==c.root&&child->border_box.y==c.child&&leaf->border_box.y==c.leaf,"independent absolute nested positions");
 float delta=c.root-old->root->border_box.y;std::vector<std::pair<aether_layout_box_s*,aether_layout_box_s*>> stack{{l->root,old->root}};while(!stack.empty()){auto [a,b]=stack.back();stack.pop_back();for(auto rect:{std::pair{a->border_box,b->border_box},std::pair{a->content_box,b->content_box},std::pair{a->padding_box,b->padding_box},std::pair{a->margin_box,b->margin_box}}){need(rect.first.y==rect.second.y+delta,"each box translated exactly once");need(rect.first.x==rect.second.x&&rect.first.width==rect.second.width&&rect.first.height==rect.second.height,"other geometry unchanged");}need(a->children.size()==b->children.size(),"tree unchanged");for(size_t i=0;i<a->children.size();++i)stack.push_back({a->children[i],b->children[i]});}
 aether_layout_result_destroy(l);aether_layout_result_destroy(old);
}
printf("Root placement independent checks=%u PASS\n",checks);}
