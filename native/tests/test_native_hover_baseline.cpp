// Manual diagnostic baseline. Native API only, never a host PointerMoved benchmark.
#include "../src/style_runtime/style_runtime_core.cpp"
#include "../src/style_runtime/hover_probe.h"
#include <sys/resource.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <map>
using namespace aether::style;
static void need(bool b,const char* why){if(!b){std::cerr<<"FAIL "<<why<<'\n';std::exit(2);}}
static void ok(aether_status_t s){need(s==AETHER_OK,"status");}
struct Canon {
 std::ostringstream o;
 Canon(){o<<std::hexfloat;}
 template<class T> void v(T x) { if constexpr(std::is_enum_v<T>)o<<int(x)<<',';else o<<x<<','; }
 void v(std::string_view s){o<<s.size()<<':'<<s<<',';}
 void v(const std::string& s){v(std::string_view(s));}
 void v(aether_css_length x){v(x.value);v(x.unit);v(x.linear_percent);v(x.linear_em);v(x.linear_rem);v(x.linear_vw);v(x.linear_vh);}
 void v(aether_css_edges x){v(x.left);v(x.top);v(x.right);v(x.bottom);}
 void v(aether_color_rgba x){v(x.r);v(x.g);v(x.b);v(x.a);}
 void v(aether_color_edges x){v(x.left);v(x.top);v(x.right);v(x.bottom);}
 void v(aether_border_style_edges x){v(x.left);v(x.top);v(x.right);v(x.bottom);}
 void v(aether_flex_basis x){v(x.kind);v(x.length);}
 void v(aether_transform_origin x){v(x.x);v(x.y);}
 void v(aether_corner_radius x){v(x.horizontal);v(x.vertical);}
 void v(aether_border_radii x){v(x.top_left);v(x.top_right);v(x.bottom_right);v(x.bottom_left);}
 void record(const FrozenNodeRef& r){
  const auto& s=r->style_view().scalars();
#define FIELD(type,name) v(s.name);
  AETHER_STYLE_SCALAR_FIELDS(FIELD)
#undef FIELD
  v(std::string_view(s.font_family.data()));
  // The corpus deliberately has no payload effects. Reject rather than silently omit one.
  auto p=r->style()->payload_owner();need(p->background_layers().empty()&&p->backdrop_filters().empty()&&p->transforms().empty()&&p->text_shadows().empty()&&p->box_shadows().empty()&&p->text_gradient().stops.empty(),"unexpected payload: extend canonical serializer");
  v(r->declared_impact());v(r->declared_inherited_impact());v(r->inherited_change_impact());
  auto c=r->color_references();v(bool(c));if(c){v(c->background());for(int j=0;j<4;++j)v(c->border(j));v(c->text_shadow_count());v(c->box_shadow_count());}
  for(auto c=r->custom_properties().get();c;c=c->parent()) {std::map<std::string,std::pair<std::string,int>> sorted;for(const auto& e:c->local_entries())sorted[std::string(e.name())]={std::string(e.value()),int(e.status())};v(sorted.size());for(auto& e:sorted){v(e.first);v(e.second.first);v(e.second.second);}}v("scope-end");
  auto m=r->measures();const auto& z=m->scalars();v(z.parent_font_size);v(z.self_font_size);v(z.line_height_kind);v(z.line_height_value);v(z.row_gap);v(z.column_gap);v(z.dependencies);v(z.flex_order);v(z.direction_rtl);v(z.writing_mode);
  need(m->contextual_lengths().empty()&&m->intrinsic_keywords().empty(),"unexpected contextual tree");
  for(auto x:m->background_positions()){v(x.x);v(x.y);}for(auto x:m->background_sizes()){v(x.kind);v(x.x);v(x.y);}
  std::map<std::string,uint32_t> deps;for(auto x:m->property_dependencies()) {auto n=x.name();std::string name;for(size_t j=0;j<n.size();++j)name+=n[j];deps[name]=x.flags();}for(auto& x:deps){v(x.first);v(x.second);}
  auto t=r->typography();v(bool(t));if(t){auto& e=t->evidence;v(e.family_state);v(e.style_state);v(e.font_style);v(e.spacing_state);v(e.spacing_normal);v(e.spacing_px);v(e.size_state);v(e.weight_state);v(e.line_height_state);v(e.size_px);v(e.weight);v(e.line_height_kind);v(e.line_height_value);v(e.line_height_used_px);v(e.size_dependencies);v(e.line_height_dependencies);v(e.coverage_complete);v(e.coverage_gaps);for(auto& f:e.families){v(f.name);v(f.generic);}for(auto& p:e.provenance){v(p.declaration);v(p.inherited);v(p.important);v(p.inline_style);v(p.layer);v(p.source_order);v(p.declaration_order);v(p.resolved_source);v(p.accepted_source);}auto& a=*t->inputs;v(a.source_bytes);v(a.declarations);v(a.rules);v(a.compiled_rules);v(a.expanded_declarations);v(a.compiled_bytes);v(a.losses);v(a.inputs_accounted);v(a.external_pending);v(t->context_unproven);v(t->font_unresolved);v(t->registry_version);need(!t->text_diagnostic,"unexpected text diagnostics");}
 }
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t rt{};aether_node_t root{},a{},b{},scope{};std::vector<std::string> diagnostics;int scenario;int last_state=0;uint64_t state_calls=0,state_changes=0,resolve_ns=0,consume_ns=0,state_ns=0;
 static void diagnostic(const char* c,const char* p,const char* v,const char* m,void* u){auto& out=static_cast<Fixture*>(u)->diagnostics;Canon s;s.v(std::string_view(c?c:""));s.v(std::string_view(p?p:""));s.v(std::string_view(v?v:""));s.v(std::string_view(m?m:""));out.push_back(s.o.str());}
 Fixture(int n,int sc,bool inline_case=false):scenario(sc){std::string html="<html><body><main id='scope'>";for(int i=0;i<n;++i)html+="<div class='card' id='c"+std::to_string(i)+"'"+(inline_case?" style='color:red;unknown-property:1; width:calc(1px +)'":"")+"><i class='leaf'></i></div>";html+="</main></body></html>";ok(aether_doc_create(&doc));ok(aether_doc_parse_html(doc,html.c_str()));root=aether_doc_get_document_element(doc);a=aether_doc_get_element_by_id(doc,"c0");b=aether_doc_get_element_by_id(doc,"c1");scope=aether_doc_get_element_by_id(doc,"scope");ok(aether_style_runtime_create(nullptr,nullptr,diagnostic,this,&rt));std::string css=".card{display:block;background-color:red;color:red}.leaf{display:block}";switch(sc){case 0:case 2:css+=".card:hover{background-color:blue}";break;case 3:css+="#scope:hover .leaf{background-color:blue}";break;case 4:css+=".card:hover + .card{background-color:blue}";break;case 5:css+=".card:hover{background-color:blue;color:blue}.leaf{background-color:inherit;border-color:currentColor}";break;case 6:css+=".card{background-color:red}.card:hover{background-color:currentColor}";break;}ok(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"hover-baseline",nullptr,nullptr));}
 ~Fixture(){aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 void set(aether_node_t node,int value){auto t=std::chrono::steady_clock::now();++state_calls;state_changes+=aether_element_get_state(node)!=value;aether_element_set_state(node,value);state_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-t).count();}
 void state(int k){last_state=k;if(scenario==3){set(scope,k%2?AETHER_STATE_HOVER:0);return;}set(a,k%2?AETHER_STATE_HOVER:0);set(b,k%2?0:AETHER_STATE_HOVER);}
 std::string canonical(){Canon c;for(const auto& entry:rt->revision->entries()){auto r=entry.record();c.record(r);auto t=r->typography();if(t){c.v(t->generation==rt->revision->generation());c.v(t->document_identity==rt->revision->document_identity());c.v(t->input_epoch==rt->revision->input_epoch());c.v(t->structure_epoch==rt->revision->structure_epoch());c.v(t->style_tree_epoch==rt->revision->style_tree_epoch());c.v(t->sheet_epoch==rt->revision->sheet_epoch());}}for(auto& d:diagnostics)c.v(d);return c.o.str();}
 int resolve(bool initial,Canon* output=nullptr){auto before=rt->generation;aether_style_update_t* updates=nullptr;aether_node_t changed[]={scenario==3?scope:a,b};auto resolve_start=std::chrono::steady_clock::now();int n=initial?aether_style_runtime_resolve_subtree(rt,root,&updates):aether_style_runtime_resolve_state_changes(rt,root,changed,2,&updates);auto consume_start=std::chrono::steady_clock::now();resolve_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(consume_start-resolve_start).count();need(rt->generation==before+1,"legacy zero updates must not hide failure");for(int i=0;i<n;++i){aether_node_t node{};uint32_t impact=0;ok(aether_style_runtime_get_update(updates[i],&node,nullptr,&impact));if(output){size_t index=0;for(auto& e:rt->revision->entries()){if(e.node()==node)break;++index;}output->v(index);output->v(impact);}}aether_style_updates_free(updates,n);consume_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-consume_start).count();return n;}
};
static double seconds(timeval t){return t.tv_sec+t.tv_usec/1e6;}
int main(int argc,char** argv){
 bool canonical=argc>1&&std::string(argv[1])=="--canonical";int n=argc>2?std::atoi(argv[2]):18;int sc=argc>3?std::atoi(argv[3]):0;int steps=argc>4?std::atoi(argv[4]):2;need(n>=2&&sc>=0&&sc<7&&steps>0,"arguments");Fixture f(n,sc,canonical);
 if(canonical){Canon u;f.resolve(true,&u);need(!f.diagnostics.empty(),"inline diagnostics exercised");auto old=f.rt->revision;auto initial=f.canonical();Canon original;for(auto& e:old->entries())original.record(e.record());f.state(1);f.resolve(false,&u);auto transition=f.canonical();f.resolve(false,&u);auto nochange=f.canonical();need(old->generation()<f.rt->revision->generation(),"retained revision generation");Canon retained;for(auto& e:old->entries())retained.record(e.record());need(original.o.str()==retained.o.str(),"retained revision unchanged");std::cout<<initial<<'\n'<<transition<<'\n'<<nochange<<'\n'<<u.o.str()<<'\n'<<retained.o.str()<<'\n';auto generation=f.rt->generation;std::string too_large(style_source_accounting_s::max_source_bytes+1,' ');ok(aether_element_set_attribute(f.a,"style",too_large.c_str()));aether_style_update_t* updates=nullptr;auto count=aether_style_runtime_resolve_subtree(f.rt,f.root,&updates);need(count==0&&!updates&&f.rt->generation==generation,"budget rejection leaves publication unchanged");std::cout<<"budget-rejection=verified\n";for(auto& d:f.diagnostics)std::cout<<d<<'\n';return 0;}
 for(int phase=0;phase<3;++phase){if(phase==1){f.state(1);f.resolve(false);f.state(0);f.resolve(false);}aether::hover_probe::reset();f.state_calls=f.state_changes=f.resolve_ns=f.consume_ns=f.state_ns=0;rusage start{},end{};getrusage(RUSAGE_SELF,&start);auto t=std::chrono::steady_clock::now();int updates=0;int repeats=phase==0?1:steps;for(int k=0;k<repeats;++k){if(phase!=0)f.state(phase==1&&sc!=2?k+1:f.last_state);updates+=f.resolve(phase==0);}double wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();getrusage(RUSAGE_SELF,&end);auto p=aether::hover_probe::snapshot();
#if AETHER_HOVER_PROBE
need(p.compute_original==uint64_t(2*n+3)*repeats&&p.compute_pseudo==0,"compute count");need(p.frozen_records==p.compute_original&&p.revision_entries==p.compute_original&&p.retired_entries==0,"freeze entries");need(p.update_copies==uint64_t(updates),"update copy count");need(p.matcher_calls<=p.candidates&&p.inline_compiles==0,"matching inline counters");
#endif
std::cout<<"row,"<<AETHER_HOVER_PROBE<<','<<n<<','<<sc<<','<<phase<<','<<repeats<<','<<updates<<','<<std::setprecision(9)<<wall<<','<<seconds(end.ru_utime)-seconds(start.ru_utime)<<','<<seconds(end.ru_stime)-seconds(start.ru_stime)<<','<<end.ru_maxrss<<','<<f.state_calls<<','<<f.state_changes<<','<<p.compute_original<<','<<p.compute_pseudo<<','<<p.candidates<<','<<p.matcher_calls<<','<<p.inline_compiles;for(size_t j=0;j<p.ns.size();++j){if(j==aether::hover_probe::matching)std::cout<<",NA";else std::cout<<','<<p.ns[j];}std::cout<<','<<f.resolve_ns<<','<<f.consume_ns<<','<<p.frozen_records<<','<<p.revision_entries<<','<<p.retired_entries<<','<<p.update_copies<<','<<f.state_ns;std::cout<<'\n';}
}
