// Private benchmark. No fonts/render authority; timing excludes fixture construction.
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/style_map_types.h"
#include "../../native/src/css/style_revision.h"
#include "../../native/src/style_runtime/typography_scope_probe.h"
#include <aether/aether_contextual_style.h>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <sys/resource.h>
using namespace aether::style;
static void ck(bool b){if(!b) std::abort();}
static uint64_t hash(const std::string& s){uint64_t h=1469598103934665603ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}return h;}
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_environment_t env{400,300,1,16,0,0,0};
 std::vector<aether_node_t> nodes;std::string diagnostics;
 static void diag(const char* a,const char* b,const char* c,const char* d,void* u){auto& s=static_cast<Fixture*>(u)->diagnostics;for(auto p:{a,b,c,d}){s+=p?p:"";s+='|';}}
 Fixture(bool deep,int fields,int invalid){
  std::string declarations="font-family:Arial;font-style:normal;letter-spacing:0px;font-size:16px;font-weight:400;line-height:1.5;";
  if(fields>=1)declarations+="font-family:var(--family);";
  if(fields>=3)declarations+="font-style:var(--style);letter-spacing:var(--space);";
  if(fields>=6)declarations+="font-size:var(--size);font-weight:var(--weight);line-height:var(--line);";
  std::string html="<body>";const int count=deep?64:128;
  for(int i=0;i<count;++i){std::string style="--family:Arial;--style:italic;--space:1px;--size:18px;--weight:500;--line:1.5;";for(int j=0;j<12;++j)style+="--pad"+std::to_string(j)+":x;";if(invalid==1|| (invalid==2&&i==0))style+="--bad:var(--bad);";html+="<div id='n"+std::to_string(i)+"' style='"+style+declarations+"'>";if(!deep)html+="</div>";}
  if(deep)for(int i=0;i<count;++i)html+="</div>";html+="</body>";
  ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);ck(aether_style_runtime_create(nullptr,&env,diag,this,&runtime)==AETHER_OK);
  for(int i=0;i<count;++i){nodes.push_back(aether_doc_get_element_by_id(doc,("n"+std::to_string(i)).c_str()));ck(nodes.back());}
 }
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 void run(const char* name,int sample,const char* phase){
  diagnostics.clear();TypographyScopeCounters counters; aether_style_transaction_t tx{};aether_style_revision_t revision{};
  const auto wall=std::chrono::steady_clock::now();const auto cpu=std::clock();
  {TypographyScopeProbe probe(counters);ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);ck(aether_style_runtime_commit_revision(runtime,tx,&revision)==AETHER_OK);aether_style_transaction_destroy(tx);}
  const double cpu_ms=1000.0*(std::clock()-cpu)/CLOCKS_PER_SEC;
  const double wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-wall).count();
  aether_style_map_t map{};ck(aether_style_revision_create_map(revision,&map)==AETHER_OK);std::ostringstream output;output<<std::setprecision(17);
  for(auto n:nodes){auto r=map->current->backing()->find(n);ck(r&&r->typography());const auto& e=r->typography()->evidence;const auto& s=r->style()->view().scalars();
   output<<s.font_size<<','<<s.font_weight<<','<<s.line_height<<','<<int(e.family_state)<<','<<int(e.style_state)<<','<<int(e.spacing_state)<<','<<e.font_style<<','<<e.spacing_normal<<','<<e.spacing_px<<','<<e.coverage_gaps<<','<<e.coverage_complete<<';';
   output<<int(e.size_state)<<','<<int(e.weight_state)<<','<<int(e.line_height_state)<<','<<e.size_px<<','<<e.weight<<','<<int(e.line_height_kind)<<','<<e.line_height_value<<','<<e.line_height_used_px<<','<<e.size_dependencies<<','<<e.line_height_dependencies<<';';
   for(auto& f:e.families)output<<f.name<<':'<<f.generic<<';';
   for(auto& p:e.provenance)output<<p.accepted_source<<':'<<p.resolved_source<<':'<<p.declaration<<p.inherited<<p.important<<p.inline_style<<':'<<p.layer<<':'<<p.source_order<<':'<<p.declaration_order<<';';
  }
  rusage usage{};getrusage(RUSAGE_SELF,&usage);
  ck(counters.queries<=counters.node_computations);
  std::cout<<name<<','<<sample<<','<<phase<<','<<wall_ms<<','<<cpu_ms<<','<<usage.ru_maxrss<<','<<counters.node_computations<<','<<counters.queries<<','<<counters.scopes<<','<<counters.terminals<<','<<counters.uncertain_exits<<','<<hash(output.str())<<','<<hash(diagnostics)<<'\n';
  aether_style_map_destroy(map);aether_style_revision_destroy(revision);
 }
};
int main(int argc,char** argv){const int repeats=argc>1?std::atoi(argv[1]):7;
 std::cout<<"case,sample,phase,wall_ms,cpu_ms,process_peak_rss_kib,nodes,queries,scopes,terminals,uncertain_exits,output_hash,diagnostics_hash\n";
 for(bool deep:{false,true})for(int fields:{0,1,3,6})for(int invalid:{0,1,2}){
  const std::string name=std::string(deep?"deep":"wide")+"_f"+std::to_string(fields)+"_invalid"+std::to_string(invalid);
  for(int sample=0;sample<repeats;++sample){Fixture f(deep,fields,invalid);f.run(name.c_str(),sample,"resolve");ck(aether_element_set_attribute(f.nodes.front(),"class","mutation")==AETHER_OK);f.run(name.c_str(),sample,"mutation");f.run(name.c_str(),sample,"no_change");}
 }
}
