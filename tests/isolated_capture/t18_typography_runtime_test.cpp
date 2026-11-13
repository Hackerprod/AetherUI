#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/dom/dom_types.h"
#include "../../native/src/css/style_map_types.h"
#include "../../native/src/css/style_revision.h"
#include <aether/aether_contextual_style.h>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
using namespace aether::style;
static long fail_after=-1;
[[gnu::noinline]] void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void operator delete(void* p) noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t) noexcept{std::free(p);}
static unsigned checks=0;
static void checked(bool ok,int line){if(!ok){std::cerr<<"T1.8 failure line="<<line<<" check="<<checks+1<<'\n';std::abort();}++checks;}
#define ck(x) checked((x),__LINE__)
struct Snapshot {
 aether_style_revision_t revision{};aether_style_map_t map{};
 Snapshot()=default;Snapshot(const Snapshot&)=delete;
 Snapshot(Snapshot&& o):revision(o.revision),map(o.map){o.revision=nullptr;o.map=nullptr;}
 ~Snapshot(){aether_style_map_destroy(map);aether_style_revision_destroy(revision);}
 const FrozenNodeRecord* node(aether_node_t n)const{auto* r=map->current->backing()->find(n);ck(r&&r->typography());return r;}
 const TypographyEvidence& evidence(aether_node_t n)const{return node(n)->typography()->evidence;}
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};
 aether_style_environment_t env{400,300,1,16,0,0,0};
 Fixture(const std::string& body,const aether_style_runtime_options_t* options=nullptr){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,body.c_str())==AETHER_OK);ck(aether_style_runtime_create(options,&env,nullptr,nullptr,&runtime)==AETHER_OK);}
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t id(const char* id){auto* n=aether_doc_get_element_by_id(doc,id);ck(n);return n;}
 int64_t sheet(const std::string& css){int64_t handle{};int32_t rules{};ck(aether_style_runtime_add_stylesheet(runtime,css.c_str(),2,"T18 fixture",&handle,&rules)==AETHER_OK);return handle;}
 Snapshot resolve(){aether_style_transaction_t tx{};Snapshot s;ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);ck(aether_style_runtime_commit_revision(runtime,tx,&s.revision)==AETHER_OK);aether_style_transaction_destroy(tx);ck(aether_style_revision_create_map(s.revision,&s.map)==AETHER_OK);return s;}
 void style(aether_node_t n,const char* css){ck(aether_element_set_attribute(n,"style",css)==AETHER_OK);}
};
static void known_impl(const TypographyEvidence& e,int caller){if(e.family_state!=TypographyFieldState::known || e.style_state!=TypographyFieldState::known || e.spacing_state!=TypographyFieldState::known) std::cerr<<"known caller="<<caller<<" states="<<int(e.family_state)<<","<<int(e.style_state)<<","<<int(e.spacing_state)<<" gaps="<<e.coverage_gaps<<"\n";ck(e.family_state==TypographyFieldState::known);ck(e.style_state==TypographyFieldState::known);ck(e.spacing_state==TypographyFieldState::known);ck(!e.coverage_complete);}
#define known(e) known_impl((e),__LINE__)
int main(){
 {Fixture f("<body><div id='parent'><span id='child'>Text</span></div></body>");auto p=f.id("parent"),c=f.id("child");
  f.sheet("#parent{font-family:Arial,\"serif\",serif,sans-serif;font-style:italic;letter-spacing:2px}");
  auto s=f.resolve();known(s.evidence(p));known(s.evidence(c));ck(s.evidence(p).families==s.evidence(c).families);ck(s.evidence(c).provenance[0].inherited);ck(!s.evidence(p).provenance[0].inherited);
  ck(s.evidence(c).families.size()==4);ck(!s.evidence(c).families[1].generic);ck(s.evidence(c).families[2].generic);ck(s.evidence(c).spacing_px==2);
  const auto& r=*s.node(c)->typography();ck(r.generation==s.map->current->generation());ck(r.document_identity==s.map->current->document_identity());ck(r.input_epoch==s.map->current->input_epoch());ck(r.structure_epoch==s.map->current->structure_epoch());ck(r.style_tree_epoch==s.map->current->style_tree_epoch());ck(r.registry_version==1);
  f.style(p,"font-family:Courier,monospace;font-style:oblique;letter-spacing:normal");ck(!aether_style_map_inputs_current(s.map));
  auto fresh=f.resolve();known(fresh.evidence(c));ck(fresh.evidence(c).font_style=="oblique");ck(fresh.evidence(c).spacing_normal);ck(fresh.evidence(c).families.front().name=="Courier");ck(s.evidence(c).font_style=="italic");ck(s.evidence(c).spacing_px==2);ck(fresh.node(c)->typography()->generation>r.generation);
  aether_style_revision_t clone{};ck(aether_style_revision_clone(s.revision,&clone)==AETHER_OK);aether_style_map_t cm{};ck(aether_style_revision_create_map(clone,&cm)==AETHER_OK);ck(cm->current->backing()->find(c)->typography()==s.node(c)->typography());ck(!accepts_override(fresh.map->current.get(),cm->current.get()));aether_style_map_destroy(cm);aether_style_revision_destroy(clone);
 }
 {Fixture f("<body><div id='x' class='match' style='font-style:normal;letter-spacing:0px;font-family:Inline,serif'>T</div></body>");auto x=f.id("x");
  auto h=f.sheet("div{font-style:oblique!important;letter-spacing:3px}.match{font-style:italic!important}#x{font-family:Sheet,monospace}");auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).font_style=="italic");ck(s.evidence(x).provenance[1].important);ck(!s.evidence(x).provenance[1].inline_style);ck(s.evidence(x).spacing_px==0&&!s.evidence(x).spacing_normal);ck(s.evidence(x).families[0].name=="Inline");
  f.style(x,"font-family:Inline,serif;font-style:normal!important;letter-spacing:normal");auto s2=f.resolve();known(s2.evidence(x));ck(s2.evidence(x).font_style=="normal");ck(s2.evidence(x).spacing_normal);ck(aether_style_revision_change_count(s2.revision)>0);
  ck(aether_style_runtime_remove_stylesheet(f.runtime,h));auto s3=f.resolve();ck(s3.node(x)->typography()->sheet_epoch>s2.node(x)->typography()->sheet_epoch);
 }
 {Fixture f("<body><div id='p' style='--family:Variable, sans-serif;--s:italic;--ls:4px'><span id='x' style='font-family:var(--family);font-style:var(--s);letter-spacing:var(--ls)'>T</span></div></body>");auto x=f.id("x"),p=f.id("p");auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).families.size()==2);ck(s.evidence(x).families[0].name=="Variable");ck(s.evidence(x).font_style=="italic");ck(s.evidence(x).spacing_px==4);
  f.style(p,"--family:Updated, monospace;--s:oblique;--ls:0");auto s2=f.resolve();known(s2.evidence(x));ck(s2.evidence(x).families[0].name=="Updated");ck(s2.evidence(x).font_style=="oblique");ck(s2.evidence(x).spacing_px==0&&!s2.evidence(x).spacing_normal);
 }
 {const std::string name(256,'L');Fixture f("<body><div id='x' style='font-family:\""+name+"\",serif;font-style:normal;letter-spacing:2px'>T</div></body>");auto x=f.id("x");auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).families[0].name==name);ck(s.evidence(x).families.size()==2);ck(s.evidence(x).spacing_px==2);}
 {Fixture f("<body><div id='p' style='font-family:Parent,serif;font-style:italic;letter-spacing:3px'><span id='x'>T</span></div></body>");auto x=f.id("x");
  for(const char* keyword:{"inherit","unset"}){std::string css="font-family:"+std::string(keyword)+";font-style:"+keyword+";letter-spacing:"+keyword;f.style(x,css.c_str());auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).font_style=="italic");ck(s.evidence(x).families[0].name=="Parent");ck(s.evidence(x).spacing_px==3);ck(s.evidence(x).provenance[0].inherited);ck(s.evidence(x).provenance[1].inherited);ck(s.evidence(x).provenance[2].inherited);}
  f.style(x,"font-family:initial;font-style:initial;letter-spacing:initial");auto s=f.resolve();ck(s.evidence(x).family_state!=TypographyFieldState::known);ck(s.evidence(x).style_state!=TypographyFieldState::known);ck(s.evidence(x).spacing_state!=TypographyFieldState::known);
 }
 for(const char* css:{"font:italic 16px Other;","all:initial;","font-variation-settings:normal;"}){
  Fixture f("<body><div id='x' style='font-family:A,serif;font-style:italic;letter-spacing:1px;"+std::string(css)+"'>T</div></body>");auto s=f.resolve();const auto& e=s.evidence(f.id("x"));ck(!e.coverage_complete);ck(e.family_state!=TypographyFieldState::known);ck(e.style_state!=TypographyFieldState::known);ck(e.spacing_state!=TypographyFieldState::known);
 }
 {Fixture f("<body><div id='x' style='font-family:A,serif;font-style:normal;letter-spacing:normal'>T</div></body>");auto x=f.id("x");auto before=f.resolve();auto legacy=before.node(x)->style();
  f.style(x,"font-family:A,serif;font-style:italic;letter-spacing:2px");auto after=f.resolve();ck(after.node(x)->style()==legacy);ck(after.node(x)->typography()->evidence!=before.node(x)->typography()->evidence);ck(after.node(x)->declared_impact()!=0);ck(aether_style_revision_change_count(after.revision)>0);
 }
 {Fixture f("<body><div id='x' class='a' style='font-family:A,serif;font-style:normal;letter-spacing:1em'>T</div></body>");auto x=f.id("x");auto s=f.resolve();ck(s.evidence(x).spacing_state==TypographyFieldState::unsupported);ck(!s.evidence(x).coverage_complete);
  f.style(x,"font-family:A,serif;font-style:oblique 10deg;letter-spacing:normal");auto unknown=f.resolve();ck(unknown.evidence(x).style_state!=TypographyFieldState::known);
 }
 {Fixture f("<body><div id='x' class='a'>T</div></body>");auto x=f.id("x");f.sheet(".a{font-family:A,serif;font-style:italic;letter-spacing:1px}.b{font-family:B,monospace;font-style:normal;letter-spacing:2px}.never{all:initial}");auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).families[0].name=="A");ck(aether_element_set_attribute(x,"class","b")==AETHER_OK);auto changed=f.resolve();known(changed.evidence(x));ck(changed.evidence(x).families[0].name=="B");ck(changed.evidence(x).spacing_px==2);
 }
 {Fixture f("<body><div id='p' style='font-family:Parent,serif;font-style:italic;letter-spacing:3px;--mode:inherit'><span id='x' style='font-family:var(--mode);font-style:var(--mode);letter-spacing:var(--mode)'>T</span></div></body>");auto x=f.id("x");auto s=f.resolve();ck(s.evidence(x).family_state==TypographyFieldState::unknown);ck(s.evidence(x).style_state==TypographyFieldState::unknown);ck(s.evidence(x).spacing_state==TypographyFieldState::unknown);
 }
 {Fixture f("<body><div id='p' style='font-family:Parent,serif;font-style:italic;letter-spacing:3px'><span id='x' style='font-family:var(--missing);font-style:var(--missing);letter-spacing:var(--missing)'>T</span></div></body>");auto x=f.id("x");auto s=f.resolve();ck(s.evidence(x).family_state==TypographyFieldState::unknown);ck(s.evidence(x).style_state==TypographyFieldState::unknown);ck(s.evidence(x).spacing_state==TypographyFieldState::unknown);}
 {Fixture f("<body><div id='x' style='font-family:A,serif;font-style:italic;letter-spacing:3px'>T</div></body>");auto x=f.id("x");auto s=f.resolve();aether_style_runtime_clear_cache(f.runtime);auto fresh=f.resolve();known(fresh.evidence(x));ck(fresh.node(x)->typography()->sheet_epoch>s.node(x)->typography()->sheet_epoch);ck(fresh.node(x)->typography()->generation>s.node(x)->typography()->generation);
  // Build a genuine current record except for its retained old typography.
  const auto* r=fresh.node(x);NodeRecordInput ni;
  ni.origin=r->origin();ni.node=r->node();ni.style=r->style();ni.measures=r->measures();ni.custom_properties=r->custom_properties();ni.color_references=r->color_references();ni.typography=s.node(x)->typography();ni.declared_impact=r->declared_impact();ni.declared_inherited_impact=r->declared_inherited_impact();ni.inherited_change_impact=r->inherited_change_impact();ni.document_identity=r->document_identity();ni.node_identity=r->node_identity();ni.node_lifetime=r->node_lifetime();ni.document_lifetime=r->document_lifetime();ni.generated_leaf=r->generated_leaf();
  auto test_bad_metadata=[&](TypographyRecordRef typography){ni.typography=std::move(typography);
  FrozenNodeRef mixed;ck(freeze_node_record(ni,mixed)==AETHER_OK);BackingBuildInput bi;bi.entries=fresh.map->current->backing()->entries();bi.entries[x]=mixed;BackingRef backing;ck(freeze_backing(bi,backing)==AETHER_OK);
  const auto& rev=*fresh.map->current->revision();StyleRevisionInput in;in.contextual=rev.contextual();in.generation=rev.generation();in.document_identity=rev.document_identity();in.input_epoch=rev.input_epoch();in.structure_epoch=rev.structure_epoch();in.style_tree_epoch=rev.style_tree_epoch();in.sheet_epoch=rev.sheet_epoch();in.environment=rev.environment();in.document=rev.document();in.document_lifetime=rev.document_lifetime();in.backing=backing;for(const auto& entry:rev.entries())in.entries.push_back({entry.node(),entry.node()==x?mixed:entry.record(),entry.impact()});StyleRevisionRef output=fresh.map->current->revision();ck(freeze_style_revision(in,output)==AETHER_ERROR_INVALID_STATE);ck(output==fresh.map->current->revision());};
  test_bad_metadata(s.node(x)->typography());
  auto wrong_sheet=std::make_shared<TypographyRecord>(*r->typography());++wrong_sheet->sheet_epoch;test_bad_metadata(wrong_sheet);
  auto wrong_registry=std::make_shared<TypographyRecord>(*r->typography());++wrong_registry->registry_version;test_bad_metadata(wrong_registry);
  aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx)==AETHER_OK);f.sheet("div{font-style:normal}");aether_style_revision_t stale{};ck(aether_style_runtime_commit_revision(f.runtime,tx,&stale)==AETHER_ERROR_INVALID_STATE);ck(!stale);aether_style_transaction_destroy(tx);

 }
 {Fixture f("<body><div id='p' style='font-family:Parent,serif;font-style:italic;letter-spacing:3px'><span id='x'>T</span></div></body>");auto x=f.id("x");
  for(const char* value:{"var(--missing, inherit)","var(--missing, unset)","var(--missing, inh\\65 rit)"}){
   const std::string css="font-family:"+std::string(value)+";font-style:"+value+";letter-spacing:"+value;f.style(x,css.c_str());auto s=f.resolve();known(s.evidence(x));ck(s.evidence(x).font_style=="italic");ck(s.evidence(x).families[0].name=="Parent");ck(s.evidence(x).spacing_px==3);for(const auto& origin:s.evidence(x).provenance)ck(origin.inherited);
  }
 }
 unsigned failures=0;bool completed=false;
 {Fixture f("<body><div id='x' style='font-family:A,serif;font-style:normal;letter-spacing:normal'>T</div></body>");auto x=f.id("x");auto before=f.resolve();const auto old_generation=aether_style_revision_generation(before.revision);
  f.style(x,"font-family:Changed,monospace;font-style:italic;letter-spacing:2px");
  for(long n=0;n<3000&&!completed;++n){aether_style_transaction_t tx{};fail_after=n;auto status=aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx);fail_after=-1;
   if(status==AETHER_OK){ck(tx!=nullptr);aether_style_revision_t fresh{};ck(aether_style_runtime_commit_revision(f.runtime,tx,&fresh)==AETHER_OK);ck(aether_style_revision_generation(fresh)>old_generation);aether_style_revision_destroy(fresh);completed=true;}
   else{ck(status==AETHER_ERROR_OUT_OF_MEMORY);ck(tx==nullptr);++failures;aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==old_generation);aether_style_revision_destroy(retained);ck(before.evidence(x).font_style=="normal");}
   aether_style_transaction_destroy(tx);
  }
 }
 {Fixture f("<body><div style='font-family:Parent;font-style:normal;letter-spacing:normal'><span id='x' style='--s:oblique 10deg;font-style:var(--s)'>T</span></div></body>");auto s=f.resolve();ck(s.evidence(f.id("x")).style_state!=TypographyFieldState::known);ck(!s.evidence(f.id("x")).coverage_complete);}
 {aether_style_runtime_options_t limits{};limits.max_variable_depth=1;limits.max_variable_expansion_length=2;
  Fixture f("<body><div style='font-style:normal'><span id='x' style='--a:var(--b);--b:var(--c);--c:italic;font-style:var(--a)'>T</span></div></body>",&limits);auto s=f.resolve();ck(s.evidence(f.id("x")).style_state!=TypographyFieldState::known);}
 {aether_style_runtime_options_t limits{};limits.max_variable_depth=32;limits.max_variable_expansion_length=3;
  Fixture f("<body><div id='x' style='--s:1234px;letter-spacing:var(--s,0)'>T</div></body>",&limits);auto s=f.resolve();ck(s.evidence(f.id("x")).spacing_state!=TypographyFieldState::known);}
 unsigned commit_failures=0;bool commit_completed=false;
 {Fixture f("<body><div id='x' style='font-family:A,serif;font-style:normal;letter-spacing:normal'>T</div></body>");auto x=f.id("x");auto before=f.resolve();const auto generation=aether_style_revision_generation(before.revision);f.style(x,"font-family:B,serif;font-style:italic;letter-spacing:2px");
  for(long n=0;n<100&&!commit_completed;++n){aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(f.runtime,f.doc,&f.env,&tx)==AETHER_OK);aether_style_revision_t output{};fail_after=n;auto status=aether_style_runtime_commit_revision(f.runtime,tx,&output);fail_after=-1;
   if(status==AETHER_OK){ck(output);ck(aether_style_revision_generation(output)>generation);commit_completed=true;}
   else{ck(status==AETHER_ERROR_OUT_OF_MEMORY);ck(!output);++commit_failures;aether_style_revision_t retained{};ck(aether_style_runtime_retain_revision(f.runtime,&retained)==AETHER_OK);ck(aether_style_revision_generation(retained)==generation);aether_style_revision_destroy(retained);}
   aether_style_revision_destroy(output);aether_style_transaction_destroy(tx);ck(before.evidence(x).font_style=="normal");
  }
 }
 ck(commit_completed); // Commit is allocation-free after successful prepare.
 ck(completed);ck(failures>0);
 std::cout<<"T1.8 runtime evidence PASS checks="<<checks<<" prepare_OOM_points="<<failures<<" commit_OOM_points="<<commit_failures<<" global_coverage=INCOMPLETE native_render=CLOSED\n";
}
