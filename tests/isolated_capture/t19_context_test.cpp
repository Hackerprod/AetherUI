#ifdef NDEBUG
#error Assertions required
#endif
#include "../../native/src/layout/layout_types.h"
#include "../../native/src/style_runtime/typography_context.h"
#include <iostream>
#include <cstdlib>
using namespace aether::style;
using C=TypographyComparison;
static unsigned checks=0;
static void checked(bool ok,int line){if(!ok){std::cerr<<"T1.9 context fail line="<<line<<'\n';std::abort();}++checks;}
#define ck(x) checked((x),__LINE__)
struct Snapshot {
 aether_style_revision_t revision{};aether_style_map_t map{};
 Snapshot()=default;Snapshot(const Snapshot&)=delete;
 Snapshot(Snapshot&& o):revision(o.revision),map(o.map){o.revision=nullptr;o.map=nullptr;}
 ~Snapshot(){aether_style_map_destroy(map);aether_style_revision_destroy(revision);}
 TypographyContext context(aether_node_t n)const{return TypographyContext::bind(map->current,map->current->backing()->find_owner(n));}
};
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};
 aether_style_environment_t env{400,300,1,16,0,0,0};
 Fixture(const std::string& body){ck(aether_doc_create(&doc)==AETHER_OK);ck(aether_doc_parse_html(doc,body.c_str())==AETHER_OK);ck(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);}
 ~Fixture(){aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 aether_node_t id(const char* s){auto* n=aether_doc_get_element_by_id(doc,s);ck(n);return n;}
 Snapshot resolve(){Snapshot s;aether_style_transaction_t tx{};ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);ck(aether_style_runtime_commit_revision(runtime,tx,&s.revision)==AETHER_OK);aether_style_transaction_destroy(tx);ck(aether_style_revision_create_map(s.revision,&s.map)==AETHER_OK);return s;}
};
static const std::string base="font-family:Arial,serif;font-style:normal;letter-spacing:0px;font-size:16px;font-weight:400;line-height:1.5;";
struct NeverProvider final:aether::rendering::text::ContextualMeasurementProvider {
 aether::rendering::text::ContextualMeasurement measure(std::shared_ptr<const aether::rendering::text::NativeInlineParagraph>,size_t,size_t,float)const override {std::abort();}
};
int main(){
 {Fixture f("<body><div id='a' style='"+base+"'>AV</div><div id='b' style='"+base+"'>AV</div><div id='unknown'>AV</div></body>");
  auto a=f.id("a"),b=f.id("b"),u=f.id("unknown");auto old=f.resolve();
  auto ca=old.context(a),cb=old.context(b);ck(ca.valid());ck(cb.valid());
  ck(compare_typography_context(ca,cb)==C::equal_for_tracked_fields);
  ck(compare_typography_context(old.context(u),old.context(u))==C::unproven);
  ck(!TypographyContext::bind({},{}).valid());
  auto authentic=old.map->current->backing()->find_owner(a);
  FrozenNodeRef borrowed(authentic.get(),[](const FrozenNodeRecord*){});
  ck(!TypographyContext::bind(old.map->current,borrowed).valid());
  ck(compare_typography_context(TypographyContext::bind({},{}),ca)==C::unproven);
  // Equal real CSS contexts are not fixture authority; no provider invocation.
  aether_layout_engine_s engine;
  auto font=std::make_shared<const aether::rendering::text::FontInstance>(std::vector<uint8_t>{1},"SYNTHETIC_T19",0,1024,0);
  auto seal=std::make_shared<const aether::rendering::text::SyntheticFaceSeal>(font,"T19");
  ck(!aether::layout::SyntheticTextTestIssuer::install(engine,f.doc,old.map,{}, {a},seal,std::make_shared<NeverProvider>()));
  for(const std::string change:{"font-family:Courier","font-style:italic","letter-spacing:2px","font-size:20px","font-weight:700","line-height:24px"}) {
   ck(aether_element_set_attribute(b,"style",(base+change).c_str())==AETHER_OK);auto next=f.resolve();
   ck(compare_typography_context(next.context(a),next.context(b))==C::different);
   ck(compare_typography_context(ca,next.context(b))==C::unproven);
   auto mixed=TypographyContext::bind(next.map->current,old.map->current->backing()->find_owner(a));
   ck(!mixed.valid());ck(compare_typography_context(mixed,next.context(a))==C::unproven);
  }
  // Old immutable context remains internally valid after mutation. It cannot be
  // paired with a new revision, but retaining it does not read mutable DOM state.
  ck(ca.valid());ck(compare_typography_context(ca,cb)==C::equal_for_tracked_fields);
 }
 {Fixture f("<body><div id='p' style='"+base+"'><span id='n' style='font-size:20px'>AV</span></div><div id='q' style='"+base+"line-height:24px'><span id='x' style='font-size:20px'>AV</span></div></body>");
  auto p=f.id("p"),q=f.id("q"),n=f.id("n"),x=f.id("x");auto s=f.resolve();
  auto ep=s.context(p).evidence(),eq=s.context(q).evidence(),en=s.context(n).evidence(),ex=s.context(x).evidence();
  ck(ep&&eq&&en&&ex);ck(ep->line_height_used_px==24);ck(eq->line_height_used_px==24);
  ck(ep->line_height_kind==TypographyLineHeightKind::number);ck(eq->line_height_kind==TypographyLineHeightKind::absolute_css_px);
  ck(en->line_height_used_px==30);ck(ex->line_height_used_px==24);
  ck(compare_typography_context(s.context(p),s.context(q))==C::different);
  ck(compare_typography_context(s.context(n),s.context(x))==C::different);
 }
 {Fixture f("<body><div id='a' style='font-size:16px'>A</div><div id='b' style='font-size:20px'>B</div></body>");auto a=f.id("a"),b=f.id("b");auto s=f.resolve();
  // Known difference wins over unknown unrelated fields, never over identity.
  ck(compare_typography_context(s.context(a),s.context(b))==C::different);
  Fixture other("<body><div id='b' style='font-size:20px'>B</div></body>");auto ob=other.id("b");auto os=other.resolve();
  ck(compare_typography_context(s.context(a),os.context(ob))==C::unproven);
 }
 // The existing T1.7 issuer remains the only admission route. Partial
 // descriptors cannot refresh its exact-version capability after CSS mutation.
 {aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_map_t map{};
  ck(aether_doc_create(&doc)==AETHER_OK);
  ck(aether_doc_parse_html(doc,"<body><div id='target' style='font-family:SyntheticTest;font-size:16px;font-weight:400;white-space:nowrap;direction:ltr'>AV</div></body>")==AETHER_OK);
  aether_style_environment_t env{400,300,1,16,0,0,0};
  std::shared_ptr<const aether::layout::SyntheticFixtureAuthority> authority;
  ck(aether::layout::SyntheticTextTestIssuer::create_fixture_styles(doc,env,&runtime,&map,authority));
  auto* n=aether_doc_get_element_by_id(doc,"target");ck(n);
  auto previous=TypographyContext::bind(map->current,map->current->backing()->find_owner(n));ck(previous.valid());
  auto font=std::make_shared<const aether::rendering::text::FontInstance>(std::vector<uint8_t>{1},"SYNTHETIC_T19",0,1024,0);
  auto seal=std::make_shared<const aether::rendering::text::SyntheticFaceSeal>(font,"T19");
  auto provider=std::make_shared<NeverProvider>();aether_layout_engine_s engine;
  ck(aether::layout::SyntheticTextTestIssuer::install(engine,doc,map,authority,{n},seal,provider));
  ck(aether_element_set_attribute(n,"style","font-family:SyntheticTest;font-size:20px;font-weight:400;white-space:nowrap;direction:ltr")==AETHER_OK);
  Snapshot fresh;aether_style_transaction_t tx{};
  ck(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);
  ck(aether_style_runtime_commit_revision(runtime,tx,&fresh.revision)==AETHER_OK);aether_style_transaction_destroy(tx);
  ck(aether_style_revision_create_map(fresh.revision,&fresh.map)==AETHER_OK);
  ck(compare_typography_context(previous,fresh.context(n))==C::unproven);
  ck(!aether::layout::SyntheticTextTestIssuer::install(engine,doc,fresh.map,authority,{n},seal,provider));
  aether_style_map_destroy(map);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 }
 std::cout<<"T1.9 context PASS checks="<<checks<<" partial equality is not authority\n";
}
