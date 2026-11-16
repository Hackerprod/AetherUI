#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../src/style_runtime/ua_stylesheet.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <initializer_list>
#include <fstream>
#include <iterator>
#include <functional>
#include "../src/dom/dom_types.h"
#include "../src/css/computed_style_handle.h"
#include "../src/css/frozen_measure_read.h"
namespace {
unsigned checks=0;
void need(bool b,const char* m){++checks;if(!b){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
void ok(aether_status_t s,const char*m){need(s==AETHER_OK,m);}
const char baseline[]=
#include "ua_legacy_18165b8.inc"
;
struct Flat{aether_computed_style_t s{};aether::style::FrozenMeasuresRef measures;aether::style::FrozenNodeRef record;~Flat(){aether_computed_style_free(&s);} Flat()=default;Flat(const Flat&)=delete;};
struct F{
 aether_doc_t d{};aether_style_runtime_t r{};aether_style_revision_t rev{};aether_style_environment_t env{1280,720,1,16,0,0,0};
 F(const char* ua){ok(aether_doc_create(&d),"doc");ok(aether_doc_parse_html(d,"<html id='html'><body id='body'><div id='leaf'></div><h1 id='heading'>Title</h1><button id='control'>OK</button></body></html>"),"parse");ok(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&r),"runtime");if(ua)css(ua,0);}
 ~F(){aether_style_revision_destroy(rev);aether_style_runtime_destroy(r);aether_doc_destroy(d);}
 aether_node_t node(const char* id){return aether_doc_get_element_by_id(d,id);}
 void css(const char* s,int layer=1){int64_t h{};int32_t count{};ok(aether_style_runtime_add_stylesheet(r,s,layer,"test",&h,&count),"sheet");}
 void set(const char* id,const std::string& s){ok(aether_element_set_attribute(node(id),"style",s.c_str()),"inline");}
 void resolve(){aether_style_transaction_t tx{};ok(aether_style_runtime_prepare_revision(r,aether_doc_get_document_element(d),&env,&tx),"prepare");aether_style_revision_t next{};ok(aether_style_runtime_commit_revision(r,tx,&next),"commit");aether_style_transaction_destroy(tx);aether_style_revision_destroy(rev);rev=next;}
 void get(const char* id,Flat& f){unsigned n=0;for(int i=0;i<aether_style_revision_style_count(rev);++i){aether_node_t node{};aether_computed_style_handle_t h{};uint32_t impact{};ok(aether_style_revision_get_style(rev,i,&node,&h,&impact),"get");if(node==this->node(id)){ok(aether_computed_style_copy(h,&f.s),"copy");f.measures=h->measures;f.record=h->record;++n;}aether_computed_style_handle_destroy(h);}need(n==1,"unique result");}
 void expect(const char* id,const char* font,float size,float line){Flat f;get(id,f);if(!f.s.font_family||strcmp(f.s.font_family,font)||fabs(f.s.font_size-size)>.001||fabs(f.s.line_height-line)>.001){fprintf(stderr,"actual %s font=%s size=%g line=%g expected %s/%g/%g\n",id,f.s.font_family,f.s.font_size,f.s.line_height,font,size,line);need(false,"typography");}++checks;}
};
bool equal(const aether_computed_style_t&a,const aether_computed_style_t&b,const char*p){if(!strcmp(p,"font-family"))return std::string(a.font_family?a.font_family:"")==std::string(b.font_family?b.font_family:"");if(!strcmp(p,"font-size"))return fabs(a.font_size-b.font_size)<.001;if(!strcmp(p,"line-height"))return fabs(a.line_height-b.line_height)<.001;return a.color.r==b.color.r&&a.color.g==b.color.g&&a.color.b==b.color.b&&a.color.a==b.color.a;}
}
int main(int argc,char**argv){
 const char* web=aether::style::web_capture_defaults_css();
 need(strlen(baseline)==strlen(aether_style_runtime_defaults_css()),"legacy length");need(!memcmp(baseline,aether_style_runtime_defaults_css(),sizeof(baseline)),"baseline exact including NUL");
 std::string expected(baseline);std::string from="body { margin: 8px; color: #202124; font-family: \"Segoe UI\"; font-size: 16px; line-height: 1.25; }";
 size_t p=expected.find(from);need(p!=std::string::npos,"fixed baseline declaration");expected.replace(p,from.size(),"html, body:root { color: #202124; font-family: \"Segoe UI\"; font-size: 16px; line-height: 1.25; }\nbody { margin: 8px; }");need(expected==web,"only approved literal delta");
 F legacy(baseline),modern(web);legacy.css("html{font-family:Arial;line-height:1.45}");modern.css("html{font-family:Arial;line-height:1.45}");
 for(int i=0;i<3;++i){modern.resolve();legacy.resolve();modern.expect("body","Arial",16,1.45);modern.expect("leaf","Arial",16,1.45);legacy.expect("body","Segoe UI",16,1.25);modern.expect("heading","Arial",32,1.45);modern.expect("control","Segoe UI",14,1.45);}
 {F f(web);f.resolve();for(auto id:{"html","body","leaf"})f.expect(id,"Segoe UI",16,1.25);Flat body;f.get("body",body);need(body.s.margin.left.value==8,"body margin");f.set("body","font-family:monospace;font-size:21px;line-height:1.7");f.resolve();f.expect("leaf","monospace",21,1.7);f.env.viewport_width=640;f.resolve();f.expect("leaf","monospace",21,1.7);}
 for(const char* prop:{"color","font-family","font-size","line-height"})for(const char* target:{"html","body","leaf"})for(const char* keyword:{"inherit","initial","unset"}){
  F f(web),initial(nullptr),old(baseline);f.css("html{color:#123456;font-family:Arial;font-size:23px;line-height:1.7}body{color:#345678;font-family:monospace;font-size:19px;line-height:1.6}");old.css("html{color:#123456;font-family:Arial;font-size:23px;line-height:1.7}body{color:#345678;font-family:monospace;font-size:19px;line-height:1.6}");old.set(target,std::string(prop)+":"+keyword);old.resolve();f.set(target,std::string(prop)+":"+keyword);f.resolve();initial.resolve();Flat got,want;f.get(target,got);
  if(!strcmp(keyword,"initial")||!strcmp(target,"html"))initial.get("html",want);else f.get(!strcmp(target,"leaf")?"body":"html",want);
  if(!strcmp(prop,"line-height")){need(bool(got.measures)&&bool(want.measures),"contextual measures");auto g=aether::style::MeasuresReadView(*got.measures).line_height();auto w=aether::style::MeasuresReadView(*want.measures).line_height();need(g.kind==w.kind&&fabs(g.value-w.value)<.001,"normative contextual line-height kind/value");}
  if(!equal(got.s,want.s,prop)){Flat prior;old.get(target,prior);need(equal(got.s,prior.s,prop),"keyword differs from legacy baseline");fprintf(stderr,"KNOWN legacy scalar discrepancy (contextual NORMAL verified): %s %s %s got=%g blank-initial-scalar=%g\n",target,prop,keyword,got.s.line_height,want.s.line_height);need(!strcmp(prop,"line-height")&&!strcmp(keyword,"initial"),"only documented scalar discrepancy");need(aether::style::MeasuresReadView(*got.measures).line_height().kind==measure_line_height_kind_e::normal,"initial contextual NORMAL despite scalar");need(aether::style::MeasuresReadView(*prior.measures).line_height().kind==measure_line_height_kind_e::normal,"legacy contextual NORMAL");need(got.record->typography()->evidence.line_height_state==aether::style::TypographyFieldState::unknown && prior.record->typography()->evidence.line_height_state==aether::style::TypographyFieldState::unknown,"metadata remains honestly unknown in both profiles");for(auto* x:{&got,&prior}){auto view=aether::style::MeasuresReadView(*x->measures);need(fabs(aether::style::measure_used_line_height(&view,x->record->style()->view().scalars())-x->s.font_size*1.2f)<.001,"actual contextual used normal line-height");fprintf(stderr,"metadata %s kind=%d value=%g used=%g size=%g state=%d\n",x==&got?"web":"baseline",(int)x->record->typography()->evidence.line_height_kind,x->record->typography()->evidence.line_height_value,x->record->typography()->evidence.line_height_used_px,x->s.font_size,(int)x->record->typography()->evidence.line_height_state);}}++checks;
 }

 // BODY-only legacy resolution is deliberately isolated from its HTML parent.
 {F f(baseline);f.css("html{font-family:Arial;line-height:1.45}");aether_style_update_t* updates{};int n=aether_style_runtime_resolve_subtree(f.r,f.node("body"),&updates);need(n>=0,"legacy BODY resolve");aether_style_updates_free(updates,n);aether_style_map_t map{};ok(aether_style_runtime_create_resolved_style_map(f.r,f.d,&map),"legacy map");auto h=aether_style_map_get(map,f.node("leaf"));Flat got;ok(aether_computed_style_copy(h,&got.s),"legacy copy");need(!strcmp(got.s.font_family,"Segoe UI")&&fabs(got.s.line_height-1.25)<.001,"legacy BODY isolation unchanged");aether_computed_style_handle_destroy(h);aether_style_map_destroy(map);}
 // Manually constructed BODY document element, not parser-created BODY under HTML.
 {F f(web);aether_style_revision_destroy(f.rev);f.rev=nullptr;aether_doc_destroy(f.d);ok(aether_doc_create(&f.d),"manual doc");auto* body=new aether_node_s();body->type=AETHER_NODE_ELEMENT;body->tag_name="body";body->attributes["id"]="body";body->style_node_id=1;body->parent=f.d;f.d->children.push_back(body);f.d->id_index["body"]=body;f.resolve();f.expect("body","Segoe UI",16,1.25);}

 if(argc==2){
 F f(web);aether_doc_destroy(f.d);ok(aether_doc_create(&f.d),"original doc");std::ifstream file(argv[1],std::ios::binary);std::string raw((std::istreambuf_iterator<char>(file)),{});need(raw.size()==474495,"original bytes");ok(aether_doc_parse_html(f.d,raw.c_str()),"original parse");
 std::vector<aether_node_t> items; aether_node_t target{};unsigned elements=0;
 std::function<void(aether_node_t,bool)> visit=[&](auto n,bool selected){if(n->type==AETHER_NODE_ELEMENT)++elements;if(n->attributes.count("id")&&n->attributes.at("id")=="FLEX-001")selected=true;
 if(n->tag_name=="style"){std::string text;for(auto c:n->children)if(c->type==AETHER_NODE_TEXT)text+=c->text;f.css(text.c_str());}
 if(selected&&n->attributes.count("class")){const auto& c=n->attributes.at("class");if(c=="flex-target demo-box")target=n;if(c=="item")items.push_back(n);}for(auto c:n->children)visit(c,selected);};visit(f.d,false);need(elements==6686,"whole original parsed");need(target&&items.size()==3,"real FLEX001 nodes");f.resolve();
 auto read=[&](aether_node_t n,Flat& out){unsigned found=0;for(int i=0;i<aether_style_revision_style_count(f.rev);++i){aether_node_t current{};aether_computed_style_handle_t h{};uint32_t impact{};ok(aether_style_revision_get_style(f.rev,i,&current,&h,&impact),"original result");if(current==n){ok(aether_computed_style_copy(h,&out.s),"original copy");out.measures=h->measures;++found;}aether_computed_style_handle_destroy(h);}need(found==1,"original exactly once");};
 Flat container;read(target,container);need(container.s.width.value==420&&container.s.min_height.value==72,"original container dimensions");
 const int colors[3][3]={{207,228,245},{217,239,217},{255,226,189}};
 for(size_t i=0;i<items.size();++i){Flat item;read(items[i],item);need(!strcmp(item.s.font_family,"Arial")&&item.s.font_size==16&&fabs(item.s.line_height-1.45)<.001,"original author typography");need(item.s.width.unit==AETHER_CSS_UNIT_AUTO&&item.s.min_width.value==42&&item.s.min_height.value==36,"original auto/min sizes");need(item.s.padding.left.value==11&&item.s.padding.top.value==8&&item.s.border_width.left.value==2,"original padding/border");need(fabs(item.s.background_color.r-colors[i][0]/255.f)<.0001&&fabs(item.s.background_color.g-colors[i][1]/255.f)<.0001&&fabs(item.s.background_color.b-colors[i][2]/255.f)<.0001,"original nth colors");}
 puts("PASS intact original FLEX001 computed styles; no render/parity claim");
 }else puts("NOT_RUN original FLEX001 (pass its path as argument)");
 puts("PASS web UA policy regression checks (known ABI scalar discrepancy; metadata unknown, not typography coverage)");printf("checks=%u\n",checks);
}
