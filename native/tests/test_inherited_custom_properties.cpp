// Public ABI regressions. All checks remain active under NDEBUG.
#include <aether/aether.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
static void require(bool x,const char* s){ if(!x){std::fprintf(stderr,"HARNESS ERROR: %s\n",s);std::exit(2);} }
struct Fixture {
 aether_doc_t doc{}; aether_style_runtime_t rt{}; aether_node_t parent{},child{};
 aether_computed_style_t last_parent{},last_child{}; bool have_parent=false, have_child=false;
 Fixture(const char* parent_css,const char* child_css) {
  require(aether_doc_create(&doc)==AETHER_OK,"doc create");
  require(aether_doc_parse_html(doc,"<body><div id='parent'><div id='child'></div></div></body>")==AETHER_OK,"HTML parse");
  parent=aether_doc_get_element_by_id(doc,"parent"); child=aether_doc_get_element_by_id(doc,"child");
  require(parent&&child,"nodes");
  aether_style_runtime_options_t opts{}; aether_style_environment_t env{800,600,1,16,0,0,0};
  require(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"runtime create");
  const std::string css=std::string("#parent{")+parent_css+"}#child{"+child_css+"}";
  int64_t sheet=0;require(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"repro.css",&sheet,nullptr)==AETHER_OK,"stylesheet add");
 }
 ~Fixture(){if(have_child)aether_computed_style_free(&last_child);if(have_parent)aether_computed_style_free(&last_parent);aether_style_runtime_destroy(rt);aether_doc_destroy(doc);}
 int resolve(aether_node_t changed=nullptr){
  aether_style_update_t* updates=nullptr;
  auto body=aether_doc_get_body(doc);
  int n=changed?aether_style_runtime_resolve_state_changes(rt,body,&changed,1,&updates):aether_style_runtime_resolve_subtree(rt,body,&updates);
  require(n>=0,"resolve");
  for(int i=0;i<n;++i){
   aether_node_t node{};aether_computed_style_handle_t style{};
   require(aether_style_runtime_get_update(updates[i],&node,&style,nullptr)==AETHER_OK,"get update");
   if(node==parent||node==child){
    auto& target=node==parent?last_parent:last_child;auto& have=node==parent?have_parent:have_child;
    if(have)aether_computed_style_free(&target);
    require(aether_computed_style_copy(style,&target)==AETHER_OK,"copy style");have=true;
   }
   aether_computed_style_handle_destroy(style);
  }
  aether_style_updates_free(updates,n);require(have_parent&&have_child,"style snapshots");return n;
 }
 void child_inline(const char* css){require(aether_element_set_attribute(child,"style",css)==AETHER_OK,"child inline");}
 void parent_inline(const char* css){require(aether_element_set_attribute(parent,"style",css)==AETHER_OK,"parent inline");}
};
static int checks=0,failures=0;
static void check(const char* label,const aether_css_length& actual,float expected){
 bool ok=actual.unit==AETHER_CSS_UNIT_PX&&std::fabs(actual.value-expected)<.001f;
 ++checks;if(!ok)++failures;
 std::printf("%s | %s | actual=%g unit=%d | expected=%gpx\n",ok?"PASS":"FAIL",label,actual.value,int(actual.unit),expected);
}
static void original_18_checks(){
 struct Case{const char* name;const char* p;const char* c;float expected;};
 const Case cases[]={
  {"CONTROL direct inheritance","--base:20px","width:var(--base,7px)",20},
  {"CONTROL direct override","--base:20px","--base:90px;width:var(--base,7px)",90},
  {"CONTROL same-element alias","","--base:20px;--alias:var(--base);width:var(--alias,7px)",20},
  {"FAIL inherited alias must remain parent value","--base:20px;--alias:var(--base)","--base:90px;width:var(--alias,7px)",20},
  {"FAIL inherited fallback must remain selected parent fallback","--alias:var(--missing,30px)","--missing:90px;width:var(--alias,7px)",30},
  {"FAIL invalid parent alias must not revive at child","--alias:var(--missing)","--missing:90px;width:var(--alias,7px)",7},
  {"FAIL inherited value cannot form new cross-element cycle","--base:20px;--alias:var(--base)","--base:var(--alias);width:var(--alias,7px)",20},
  {"CONTROL redeclaring alias in child resolves locally","--base:20px;--alias:var(--base)","--base:90px;--alias:var(--base);width:var(--alias,7px)",90},
  {"FAIL explicit inherit preserves computed alias","--base:20px;--alias:var(--base)","--base:90px;--alias:inherit;width:var(--alias,7px)",20},
  {"FAIL parent usage does not make child inheritance safe","--base:20px;--alias:var(--base);width:var(--alias)","--base:90px;width:var(--alias,7px)",20}
 };
 for(const auto& c:cases){Fixture f(c.p,c.c);f.resolve();check(c.name,f.last_child.width,c.expected);}
 for(bool incremental:{false,true}){
  std::printf("MODE %s\n",incremental?"incremental":"full");
  Fixture f("--base:20px;--alias:var(--base)","width:var(--alias,7px)");f.resolve();
  check("dynamic start",f.last_child.width,20);
  f.child_inline("--base:90px");f.resolve(incremental?f.child:nullptr);
  check("FAIL child mutation cannot rewrite inherited alias",f.last_child.width,20);
  f.parent_inline("--base:40px");f.resolve(incremental?f.parent:nullptr);
  check("FAIL parent mutation recomputes inherited alias",f.last_child.width,40);
  std::printf("repeat unchanged updates=%d\n",f.resolve(incremental?f.parent:nullptr));
  f.child_inline("");f.resolve(incremental?f.child:nullptr);
  check("CONTROL removing child override",f.last_child.width,40);
 }
 std::printf("SUMMARY %d checks, %d failures\n",checks,failures);
 // Keep the original eighteen expected outcomes unchanged.
 require(checks==18,"original eighteen checks retained");
}

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

static void expect(bool ok, const char* label) {
 ++checks; if (!ok) ++failures;
 std::printf("%s | %s\n", ok ? "PASS" : "FAIL", label);
}

// Each copy owns its dynamic fields independently of updates and runtime state.
struct Snapshot {
 aether_computed_style_t style{};
 bool present = false;
 ~Snapshot() { if (present) aether_computed_style_free(&style); }
 Snapshot() = default;
 Snapshot(const Snapshot&) = delete;
 Snapshot& operator=(const Snapshot&) = delete;
 void copy(aether_computed_style_handle_t handle) {
  if (present) aether_computed_style_free(&style);
  require(aether_computed_style_copy(handle, &style) == AETHER_OK, "snapshot copy");
  present = true;
 }
};
struct Tree {
 aether_doc_t doc{};
 aether_style_runtime_t rt{};
 std::map<aether_node_t, Snapshot> snapshots;
 std::set<aether_node_t> authored, emitted;
 struct Pseudo { aether_node_t host; std::string text; aether_css_length width, top, right; };
 std::vector<Pseudo> pseudos;
 Snapshot retained;
 aether_node_t retain_on_next = nullptr;
 Tree(const std::string& css) {
  require(aether_doc_create(&doc)==AETHER_OK,"tree doc");
  require(aether_doc_parse_html(doc,"<body><div id='p'><div id='c'><div id='g'></div></div><div id='s'></div></div></body>")==AETHER_OK,"tree HTML");
  for (const char* id : {"p", "c", "g", "s"}) authored.insert(node(id));
  authored.insert(aether_doc_get_body(doc));
  aether_style_runtime_options_t opts{};
  aether_style_environment_t env{800,600,1,16,0,0,0};
  require(aether_style_runtime_create(&opts,&env,nullptr,nullptr,&rt)==AETHER_OK,"tree runtime");
  int64_t sheet{};
  require(aether_style_runtime_add_stylesheet(rt,css.c_str(),2,"inherited.css",&sheet,nullptr)==AETHER_OK,"tree sheet");
 }
 ~Tree() { destroy_runtime(); aether_doc_destroy(doc); }
 void destroy_runtime() { if(rt) aether_style_runtime_destroy(rt); rt=nullptr; }
 aether_node_t node(const char* id) { auto n=aether_doc_get_element_by_id(doc,id);require(n!=nullptr,"tree node");return n; }
 void inline_style(const char* id,const char* css) { require(aether_element_set_attribute(node(id),"style",css)==AETHER_OK,"tree inline"); }
 int resolve(const char* changed=nullptr,const char* subtree=nullptr) {
  auto root=aether_doc_get_body(doc); auto n=changed?node(changed):nullptr;
  aether_style_update_t* updates=nullptr;
  const int count=changed?aether_style_runtime_resolve_state_changes(rt,root,&n,1,&updates):
    aether_style_runtime_resolve_subtree(rt,subtree?node(subtree):root,&updates);
  return consume(updates,count);
 }
 int environment(float viewport_width) {
  aether_style_environment_t env{viewport_width,600,1,16,0,0,0};
  aether_style_update_t* updates=nullptr;
  const int count=aether_style_runtime_set_environment(rt,&env,aether_doc_get_body(doc),&updates);
  return consume(updates,count);
 }
 int consume(aether_style_update_t* updates,int count) {
  require(count>=0,"tree resolve"); emitted.clear(); pseudos.clear();
  for(int i=0;i<count;++i) {
   aether_node_t target{};aether_computed_style_handle_t handle{};
   require(aether_style_runtime_get_update(updates[i],&target,&handle,nullptr)==AETHER_OK,"tree update");
   if(authored.count(target)) {
    snapshots[target].copy(handle);emitted.insert(target);
    if(target==retain_on_next) { retained.copy(handle);retain_on_next=nullptr; }
   } else {
    Snapshot style;style.copy(handle);
    std::string text;
    for(int j=0;j<aether_node_get_child_count(target);++j) {
     char* value=aether_node_get_text_content(aether_node_get_child(target,j));
     if(value) text+=value;
     aether_string_free(value);
    }
    pseudos.push_back({aether_node_get_parent(target),text,style.style.width,style.style.margin.top,style.style.margin.right});
   }
   aether_computed_style_handle_destroy(handle);
  }
  aether_style_updates_free(updates,count);return count;
 }
 const aether_computed_style_t& style(const char* id) {
  auto it=snapshots.find(node(id));require(it!=snapshots.end()&&it->second.present,"tree snapshot exists");return it->second.style;
 }
 void width(const char* id,float value,const char* label) { check(label,style(id).width,value); }
};

static void scope_and_keyword_cases() {
 struct Case { const char* p; const char* c; float width; const char* label; };
 const Case cases[] = {
  {"--base:19px;--alias:var(--base)","--base:81px;--alias:unset;width:var(--alias,11px)",19,"unset inherits terminal alias"},
  {"--alias:19px","--alias:initial;width:var(--alias,11px)",11,"initial blocks ancestor"},
  {"--alias:19px","--alias:revert;width:var(--alias,11px)",11,"existing revert initial-block behavior"},
  {"--a:19px","--a:var(--a,33px);width:var(--a,11px)",11,"local self-reference shadows ancestor"},
  {"","--ok:19px;--a:var(--ok,var(--a));width:var(--a,11px)",19,"unused fallback self-cycle is not an edge"},
  {"","--a:var(--missing,var(--a,33px));width:var(--a,11px)",11,"chosen fallback self-cycle invalidates root"},
  {"","--a:var(--b);--b:var(--a);--c:var(--a,29px);width:var(--c,11px)",29,"external consumer recovers local cycle"},
  {"--a:var(--b);--b:var(--a)","--a:81px;width:var(--b,11px)",11,"child cannot revive parent cycle"},
  {"--Base:19px;--base:37px;--alias:var(--Base)","--Base:81px;width:var(--alias,11px)",19,"case-sensitive inherited dependency"},
  {"--alias:var(--missing)","--alias:inherit;--missing:81px;width:var(--alias,11px)",11,"inherit carries invalid parent result"},
  {"--alias:var(--missing)","--alias:unset;--missing:81px;width:var(--alias,11px)",11,"unset carries invalid parent result"},
 };
 for(const auto& c:cases) { Fixture f(c.p,c.c);f.resolve();check(c.label,f.last_child.width,c.width); }
 {
  Fixture f("--empty:var(--missing,)","width:var(--empty,7px)");f.resolve();
  expect(f.last_child.width.unit==AETHER_CSS_UNIT_AUTO,"valid empty inherited value does not select fallback");
 }
 {
  Fixture f("--size:2em;--alias:var(--size);font-size:10px","--size:9em;font-size:20px;width:var(--alias)");f.resolve();
  check("inherited terminal resolves relative unit against child font, not parent pixels",f.last_child.width,40);
 }
 {
  Tree t("#p{--base:20px;--alias:var(--base)}#c{--base:40px;--local:var(--base);width:var(--alias)}#g{--base:90px;width:var(--alias);height:var(--local)}#s{--base:70px;width:var(--alias)}");
  t.resolve();t.width("c",20,"child inherits parent scope");t.width("g",20,"third generation preserves parent alias");
  check("third generation preserves child alias",t.style("g").height,40);t.width("s",20,"sibling preserves parent alias");
  t.inline_style("c","--base:61px;--alias:var(--base)");t.resolve("c");
  t.width("c",61,"local redeclaration rebinds alias");t.width("g",61,"descendant receives newly computed child alias");
  t.width("s",20,"incremental sibling snapshot remains unchanged");expect(!t.emitted.count(t.node("s")),"incremental child does not emit sibling update");
 }
}

static void declaration_permutations() {
 std::array<int,4> order{{0,1,2,3}};
 const char* declarations[]={"--a:var(--b) var(--c);","--b:var(--a);","--c:var(--d,33px);","--d:var(--a,44px);"};
 do {
  std::string css;
  for(int index:order) css+=declarations[index];
  css+="width:var(--d,11px);height:var(--c,13px);--independent:17px;margin-top:var(--independent);";
  Fixture f("",css.c_str());f.resolve();
  check("overlapping cycle after earlier semantic failure (all 24 orders)",f.last_child.width,11);
  check("overlapping cycle marks c too (all 24 orders)",f.last_child.height,13);
  check("independent root survives cyclic root failure",f.last_child.margin.top,17);
 } while(std::next_permutation(order.begin(),order.end()));
 std::array<int,3> chain{{0,1,2}};
 const char* aliases[]={"--base:23px;","--middle:var(--base);","--alias:var(--middle);"};
 do {
  std::string css;for(int index:chain)css+=aliases[index];
  Fixture f(css.c_str(),"--base:81px;width:var(--alias,11px)");f.resolve();
  check("complete local winners independent of declaration order",f.last_child.width,23);
 } while(std::next_permutation(chain.begin(),chain.end()));
}

static void updates_and_snapshots() {
 for(bool incremental:{false,true}) {
  Tree t("#p{--base:20px;--alias:var(--base)}#c,#g,#s{width:var(--alias,7px)}#g{background-image:url(retained.png)}");
  t.retain_on_next=t.node("g");t.resolve();
  t.inline_style("p","--base:40px");t.resolve(incremental?"p":nullptr);
  expect(!t.emitted.count(t.node("p")),"custom-property-only parent mutation emits no ordinary parent update");
  t.width("c",40,"parent no-op refresh reaches child");t.width("g",40,"parent no-op refresh reaches grandchild");t.width("s",40,"parent no-op refresh reaches sibling");
  expect(t.resolve(incremental?"p":nullptr)==0,"repeat unchanged traversal emits no updates");
  t.inline_style("p","--base:60px;width:123px");t.resolve(incremental?"p":nullptr);
  expect(t.emitted.count(t.node("p"))!=0,"ordinary parent change replaces parent state");
  t.width("c",60,"parent replacement reaches child");t.width("g",60,"parent replacement reaches grandchild");
  t.inline_style("c","--base:90px;height:var(--alias)");t.resolve("c");
  check("isolated incremental child uses current parent snapshot",t.style("c").height,60);
  check("retained deep copy survives all update arrays and replacements",t.retained.style.width,20);
  t.inline_style("g","background-image:url(latest.png)");t.resolve("g");
  t.destroy_runtime();
  const auto& old_layers=t.retained.style.background_layers;
  const auto& new_layers=t.style("g").background_layers;
  expect(old_layers.count==1 && old_layers.items && old_layers.items[0].image_source &&
         std::string(old_layers.items[0].image_source)=="retained.png",
         "retained snapshot owns original dynamic image string after replacement and runtime destruction");
  expect(new_layers.count==1 && new_layers.items && new_layers.items[0].image_source &&
         std::string(new_layers.items[0].image_source)=="latest.png",
         "latest snapshot owns replacement dynamic image string after runtime destruction");
  check("retained copy survives runtime destruction",t.retained.style.width,20);
  check("latest copied grandchild style survives runtime destruction",t.style("g").width,60);
 }
 {
  Tree t("#p{--base:20px;--alias:var(--base)}#c{width:var(--alias,7px)}");t.resolve();
  t.resolve(nullptr,"c");t.width("c",7,"resolve_subtree keeps null-parent scope contract for isolated root");
  t.resolve();t.width("c",20,"full traversal restores inherited scope");
  t.inline_style("c","height:var(--alias,9px)");t.resolve("c");
  check("state_changes isolated child inherits unlike resolve_subtree",t.style("c").height,20);
 }
}

static void shorthand_and_pseudo() {
 {
  Fixture f("--base:12px 24px;--alias:var(--base)","--base:90px;margin:var(--alias);padding:var(--alias)");f.resolve();
  check("deferred margin top uses inherited terminal",f.last_child.margin.top,12);
  check("deferred margin right uses inherited terminal",f.last_child.margin.right,24);
  check("deferred padding bottom uses inherited terminal",f.last_child.padding.bottom,12);
  check("deferred padding left uses inherited terminal",f.last_child.padding.left,24);
 }
 for(bool redeclare:{false,true}) {
  std::string css="#p{--size:20px;--alias:var(--size);--word:'HOST';--text:var(--word)}#p::before{--size:90px;--word:'PSEUDO';content:var(--text);width:var(--alias);margin:var(--alias);";
  if(redeclare)css+="--alias:var(--size);--text:var(--word);";
  css+="}";
  Tree t(css);t.resolve();
  expect(t.pseudos.size()==1,"one generated pseudo from inherited content");
  if(!t.pseudos.empty()) {
   const auto& p=t.pseudos.front();
   expect(p.host==t.node("p"),"pseudo belongs to expected host");
   expect(p.text==(redeclare?"PSEUDO":"HOST"),"pseudo content uses host terminal unless redeclared locally");
   check("pseudo longhand uses same computed scope as content",p.width,redeclare?90:20);
   check("pseudo shorthand uses same computed scope as longhand",p.top,redeclare?90:20);
   check("pseudo shorthand all edges",p.right,redeclare?90:20);
  }
 }
}

static void invalidation_and_names() {
 {
  Tree t("#p{--alias:var(--missing)}#c{--missing:90px;width:var(--alias,7px)}#g{width:var(--alias,7px)}");
  t.resolve();t.width("c",7,"invalid parent initially blocks child revival");
  t.inline_style("p","--missing:31px");t.resolve("p");
  t.width("c",31,"invalid parent alias becomes valid after dependency appears");
  t.width("g",31,"valid recomputation propagates through three generations");
  t.inline_style("p","");t.resolve("p");
  t.width("c",7,"valid parent alias becomes invalid after dependency removed");
  t.width("g",7,"invalid recomputation propagates through three generations");
 }
 {
  Tree t("#p{--base:20px;--alias:var(--base)}#p:hover{--base:41px}#c{--base:90px;width:var(--alias,7px)}");
  t.resolve();t.width("c",20,"hover baseline inherited alias");
  aether_element_set_state(t.node("p"),AETHER_STATE_HOVER);t.resolve("p");
  t.width("c",41,"parent hover recomputes alias before descendant consumption");
  aether_element_set_state(t.node("p"),0);t.resolve("p");t.width("c",20,"hover removal restores alias");
 }
 {
  Tree t("#p{--base:20px;--alias:var(--base)}@media(min-width:1000px){#p{--base:43px}}#c{--base:90px;width:var(--alias,7px)}");
  t.resolve();t.width("c",20,"media baseline inherited alias");
  t.environment(1200);t.width("c",43,"viewport environment recomputes inherited alias");
  t.environment(800);t.width("c",20,"viewport restoration recomputes alias");
  int64_t sheet{};
  require(aether_style_runtime_add_stylesheet(t.rt,"#p{--base:67px}",2,"temporary.css",&sheet,nullptr)==AETHER_OK,"temporary sheet");
  t.resolve("c");t.width("c",67,"sheet invalidation incremental path rebuilds absent parent state");
  require(aether_style_runtime_remove_stylesheet(t.rt,sheet)!=0,"remove temporary sheet");
  t.resolve("c");t.width("c",20,"sheet removal incremental path restores base alias");
 }
 {
  Fixture f("--base:19px!important;--base:27px;--alias:var(--base)","--base:81px!important;width:var(--alias,7px)");
  f.parent_inline("--base:35px");f.resolve();check("important parent winner beats ordinary inline before alias computation",f.last_child.width,19);
  f.parent_inline("--base:45px!important");f.resolve(f.parent);check("important inline replacement feeds inherited terminal",f.last_child.width,45);
 }
 struct Named { const char* parent; const char* child; const char* label; };
 const Named names[]={
  {"--café:19px;--alias:var(--café)","--café:81px;width:var(--alias,7px)","Unicode names retain defining scope"},
  {"--\\61:19px;--alias:var(--a)","--a:81px;width:var(--alias,7px)","hex escaped declaration name matches canonical dependency"},
  {"--a\\!b:19px;--alias:var(--a\\!b)","--a\\!b:81px;width:var(--alias,7px)","escaped punctuation root name is not synthesized unescaped"},
  {"--a\\)b:19px;--alias:var(--a\\)b)","--a\\)b:81px;width:var(--alias,7px)","escaped closing parenthesis remains part of canonical name"}
 };
 for(const auto& n:names) {Fixture f(n.parent,n.child);f.resolve();check(n.label,f.last_child.width,19);}
}

int main() {
 original_18_checks();
 scope_and_keyword_cases();
 declaration_permutations();
 updates_and_snapshots();
 shorthand_and_pseudo();
 invalidation_and_names();
 std::printf("SUMMARY inherited custom properties: %d checks, %d failures\n",checks,failures);
 return failures?1:0;
}
