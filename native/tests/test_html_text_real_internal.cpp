#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_html_text.h"
#include "../src/rendering/text/real_html_admission.h"
#include "../src/rendering/text/real_html_style_transition.h"
#include "../src/layout/real_simple_text_policy.h"
#include "../src/rendering/text/real_html_mask_registry.h"
#include "html_text_real_cases.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <functional>
#include <type_traits>
using namespace aether::rendering::text;
namespace tt=aether::rendering::text::truetype;
using Bytes=std::vector<std::uint8_t>;
// Fixture serialization shared with independent public suite, not an oracle.
static void u16(Bytes& b,std::size_t p,unsigned v){assert(p+2<=b.size());b[p]=v>>8;b[p+1]=v;}
static void u32(Bytes& b,std::size_t p,std::uint32_t v){assert(p+4<=b.size());u16(b,p,v>>16);u16(b,p+2,v);}
static unsigned r16(const Bytes& b,std::size_t p){return (unsigned(b[p])<<8)|b[p+1];}
static std::uint32_t r32(const Bytes& b,std::size_t p){return (std::uint32_t(r16(b,p))<<16)|r16(b,p+2);}
static void append16(Bytes& b,int v){auto p=b.size();b.resize(p+2);u16(b,p,unsigned(v));}
struct Table{std::uint32_t tag;Bytes bytes;};
static constexpr std::uint32_t tag(const char* s){return std::uint32_t(s[0])<<24|std::uint32_t(s[1])<<16|std::uint32_t(s[2])<<8|std::uint32_t(s[3]);}
static Bytes sfnt(std::vector<Bytes> glyphs,bool short_loca=false,unsigned hmetrics=0,std::vector<int> lsbs={}){
 const unsigned n=glyphs.size();if(!hmetrics)hmetrics=n;if(lsbs.empty())lsbs.resize(n);
 Bytes head(54),maxp(32),hhea(36),hmtx(4*hmetrics+2*(n-hmetrics)),loca((n+1)*(short_loca?2:4)),glyf;
 u32(head,0,0x10000);u32(head,12,0x5f0f3cf5);u16(head,18,1024);u16(head,50,short_loca?0:1);u32(maxp,0,0x10000);u16(maxp,4,n);u32(hhea,0,0x10000);u16(hhea,34,hmetrics);
 for(unsigned i=0;i<n;++i){if(short_loca)u16(loca,i*2,glyf.size()/2);else u32(loca,i*4,glyf.size());glyf.insert(glyf.end(),glyphs[i].begin(),glyphs[i].end());if(glyf.size()%2)glyf.push_back(0);if(i<hmetrics){u16(hmtx,i*4,500+100*i);u16(hmtx,i*4+2,lsbs[i]);}else u16(hmtx,4*hmetrics+2*(i-hmetrics),lsbs[i]);}
 if(short_loca)u16(loca,n*2,glyf.size()/2);else u32(loca,n*4,glyf.size());
 std::vector<Table> tables{{tag("head"),head},{tag("maxp"),maxp},{tag("hhea"),hhea},{tag("hmtx"),hmtx},{tag("loca"),loca},{tag("glyf"),glyf}};
 Bytes out(12+16*tables.size());u32(out,0,0x10000);u16(out,4,tables.size());
 for(unsigned i=0;i<tables.size();++i){while(out.size()%4)out.push_back(0);u32(out,12+16*i,tables[i].tag);u32(out,20+16*i,out.size());u32(out,24+16*i,tables[i].bytes.size());out.insert(out.end(),tables[i].bytes.begin(),tables[i].bytes.end());}return out;
}
static void add_table(Bytes& b,std::uint32_t name,Bytes payload={0,0,0,0}){
 const unsigned n=r16(b,4);const unsigned end=12+16*n;
 b.insert(b.begin()+end,16,0);u16(b,4,n+1);
 for(unsigned i=0;i<n;++i)u32(b,20+16*i,r32(b,20+16*i)+16);
 while(b.size()%4){b.push_back(0);}
 u32(b,end,name);u32(b,end+8,b.size());u32(b,end+12,payload.size());b.insert(b.end(),payload.begin(),payload.end());
}
static auto source(Bytes b){return std::make_shared<const FontSource>(std::move(b),"synthetic Q1c test fixture");}
[[maybe_unused]] static auto face(const Bytes& b){auto r=tt::ValidatedFace::create(source(b));assert(std::holds_alternative<std::shared_ptr<const tt::ValidatedFace>>(r));return std::get<std::shared_ptr<const tt::ValidatedFace>>(r);}
struct Group { unsigned first,last,gid; };
static Bytes format12(std::vector<Group> groups){
 Bytes b(16+12*groups.size());u16(b,0,12);u32(b,4,b.size());u32(b,12,groups.size());
 for(unsigned i=0;i<groups.size();++i){u32(b,16+12*i,groups[i].first);u32(b,20+12*i,groups[i].last);u32(b,24+12*i,groups[i].gid);}return b;
}
struct Seg {unsigned first,last,delta;std::vector<unsigned> gids;};
[[maybe_unused]] static Bytes format4(std::vector<Seg> segments){
 const unsigned n=segments.size();Bytes b(16+8*n);u16(b,0,4);u16(b,6,n*2);
 // Search accelerators deliberately zero: readers must not trust them.
 for(unsigned i=0;i<n;++i){auto& s=segments[i];u16(b,14+2*i,s.last);u16(b,16+2*n+2*i,s.first);u16(b,16+4*n+2*i,s.delta);
  if(!s.gids.empty()){const auto pos=16+6*n+2*i;u16(b,pos,b.size()-pos);for(auto g:s.gids)append16(b,g);}}
 u16(b,2,b.size());return b;
}
struct Rec {unsigned platform,encoding,table;};
static Bytes cmap(std::vector<Bytes> tables,std::vector<Rec> records){
 Bytes b(4+8*records.size());u16(b,2,records.size());std::vector<unsigned> offsets;
 for(const auto& t:tables){offsets.push_back(b.size());b.insert(b.end(),t.begin(),t.end());}
 for(unsigned i=0;i<records.size();++i){u16(b,4+8*i,records[i].platform);u16(b,6+8*i,records[i].encoding);u32(b,8+8*i,offsets.at(records[i].table));}return b;
}
[[maybe_unused]] static Bytes font(const Bytes& c,unsigned count=16){auto b=sfnt(std::vector<Bytes>(count));add_table(b,tag("cmap"),c);return b;}

struct P{int x,y;bool on;};
// Independent fixture writer uses signed 16-bit deltas (no compact flag tricks).
static Bytes glyph(const std::vector<std::vector<P>>& contours){
 Bytes b(10);u16(b,0,contours.size());int xmin=32767,ymin=32767,xmax=-32768,ymax=-32768;unsigned count=0;
 for(const auto& c:contours){assert(!c.empty());count+=c.size();append16(b,count-1);for(auto p:c){xmin=std::min(xmin,p.x);ymin=std::min(ymin,p.y);xmax=std::max(xmax,p.x);ymax=std::max(ymax,p.y);}}
 if(!contours.empty()){u16(b,2,xmin);u16(b,4,ymin);u16(b,6,xmax);u16(b,8,ymax);}append16(b,0);
 for(const auto& c:contours)for(auto p:c)b.push_back(p.on?1:0);
 int x=0,y=0;for(const auto& c:contours)for(auto p:c){assert(p.x-x>=-32768&&p.x-x<=32767);append16(b,p.x-x);x=p.x;}
 for(const auto& c:contours)for(auto p:c){assert(p.y-y>=-32768&&p.y-y<=32767);append16(b,p.y-y);y=p.y;}
 return b;
}
using Binding=std::shared_ptr<const tt::RealFontBinding>;
using Run=std::shared_ptr<const tt::PreparedSimpleRun>;
using E=tt::RunError;
static_assert(!std::is_default_constructible_v<tt::PreparedSimpleRun>);
static_assert(!std::is_default_constructible_v<tt::HtmlNormalizationProof>);
static_assert(!std::is_copy_constructible_v<tt::HtmlNormalizationProof>);
static_assert(!std::is_move_constructible_v<tt::HtmlNormalizationProof>);
static_assert(!std::is_copy_constructible_v<tt::PreparedSimpleRun>);
static_assert(!std::is_move_constructible_v<tt::PreparedSimpleRun>);

static Bytes testfont(bool missing=false,unsigned advance=600,unsigned units=1024){
 auto b=sfnt({{},glyph({{{-80,-120,true},{800,-120,true},{800,600,true},{-80,600,true}}}),{}},false,0,{0,-80,0});
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("head"))u16(b,r32(b,20+16*i)+18,units);
 // Deliberately overlap the rectangle glyph at small advances; whitespace is empty.
 for(unsigned i=0;i<r16(b,4);++i)if(r32(b,12+16*i)==tag("hmtx")){auto off=r32(b,20+16*i);u16(b,off+4,advance);u16(b,off+8,256);}
 std::vector<Group> groups;for(unsigned cp=32;cp<=255;++cp)if((cp<=126||cp>=160)&&cp!=173&&!(missing&&cp==66))groups.push_back({cp,cp,(cp==32||cp==160)?2u:1u});
 add_table(b,tag("cmap"),cmap({format12(groups)},{{3,10,0}}));return b;
}
static Binding bind(unsigned size=1024,bool missing=false,unsigned adv=600,unsigned units=1024){auto r=tt::RealFontBinding::create(source(testfont(missing,adv,units)),size);assert(std::holds_alternative<Binding>(r));return std::get<Binding>(r);}
namespace aether::layout {
// Only this test translation unit defines the approved friend; production has
// the identical forward declaration, no hooks and no exported test API.
class RealHtmlTextTestAccess {
public:
 static int registry_checks(){
  int n=0;auto font=bind()->font();
  auto mask=[&](unsigned w=4,unsigned h=4,unsigned stride=4,int left=0,int top=0,unsigned byte=255,unsigned gid=1,unsigned px=0,unsigned py=0){return std::make_shared<const GlyphMaskA8>(font,gid,font->raster_flags,px,py,w,h,stride,left,top,std::vector<std::uint8_t>(stride*h,std::uint8_t(byte)));};
  auto noop=[]{};tt::RealHtmlMaskRegistry registry;auto base=mask();registry.add(base,noop);assert(registry.masks_.size()==1);++n;
  registry.add(base,noop);registry.add(mask(),noop);assert(registry.masks_.size()==1&&registry.masks_.front()==base);++n;
  for(auto bad:std::vector<std::shared_ptr<const GlyphMaskA8>>{mask(3),mask(4,3),mask(4,4,5),mask(4,4,4,1),mask(4,4,4,0,1),mask(4,4,4,0,0,0)}){
   bool invalid=false;try{registry.add(bad,noop);}catch(tt::HtmlTextError e){invalid=e==tt::HtmlTextError::invalid_input;}assert(invalid);assert(registry.masks_.size()==1&&registry.masks_.front()==base);++n;
  }
  registry.add(mask(4,4,4,0,0,255,2),noop);registry.add(mask(4,4,4,0,0,255,1,1),noop);registry.add(mask(4,4,4,0,0,255,1,0,1),noop);assert(registry.masks_.size()==4);++n;
  auto otherfont=bind()->font();auto other=std::make_shared<const GlyphMaskA8>(otherfont,1,otherfont->raster_flags,0,0,4,4,4,0,0,std::vector<std::uint8_t>(16,255));registry.add(other,noop);assert(registry.masks_.size()==5);++n;
  tt::RealHtmlMaskRegistry large;large.add(mask(32,32,32),noop);unsigned ticks=0;bool cancelled=false;
  try{large.add(mask(32,32,32),[&]{if(++ticks==3)throw tt::HtmlTextError::cancelled;});}catch(tt::HtmlTextError e){cancelled=e==tt::HtmlTextError::cancelled;}
  assert(cancelled&&ticks==3&&large.masks_.size()==1);++n;
  for(unsigned offset:{255u,256u,1023u}){auto bytes=std::vector<std::uint8_t>(1024,255);bytes[offset]=0;auto bad=std::make_shared<const GlyphMaskA8>(font,1,font->raster_flags,0,0,32,32,32,0,0,std::move(bytes));bool invalid=false;try{large.add(bad,noop);}catch(tt::HtmlTextError e){invalid=e==tt::HtmlTextError::invalid_input;}assert(invalid&&large.masks_.size()==1);++n;}
  std::weak_ptr<const GlyphMaskA8> lifetime=base;base.reset();assert(!lifetime.expired());++n;
  return n;
 }
 static bool install(aether_layout_engine_s* e,aether_doc_s* d,aether_style_map_s* m,aether_node_s* o,Run r,std::shared_ptr<const tt::HtmlNormalizationProof> proof){return RealSimpleTextAccess::install(*e,d,m,o,std::move(r),std::move(proof));}
 static bool install_blocks(aether_layout_engine_s* e,aether_doc_s* d,aether_style_map_s* m,const std::vector<aether_node_s*>& owners,const std::vector<Run>& runs,const std::vector<std::shared_ptr<const tt::HtmlNormalizationProof>>& proofs){return RealSimpleTextAccess::install_blocks(*e,d,m,owners,runs,proofs);}
 static auto transition(aether_doc_s* d){return std::shared_ptr<const tt::RealHtmlStyleTransition>(new tt::RealHtmlStyleTransition(d));}
 static bool accepts(const std::shared_ptr<const tt::RealHtmlStyleTransition>& t,aether_doc_s* d,aether_style_map_s* m){return t->accepts(d,m);}
 static auto policy(aether_layout_engine_s* e){return e->real_text_policy_;}
 static void valid(const std::shared_ptr<RealSimpleTextPolicy>& p){p->valid();}
 static auto style(const std::shared_ptr<RealSimpleTextPolicy>& p){assert(p->records_.size()==1);return p->records_[0].style_;}
 static void conflict(aether_layout_engine_s* e,int kind,bool on){
  // Presence-only aliases own a real allocation. No fake policy is dereferenced:
  // install/begin must reject the conflict before touching another channel.
  static auto lifetime=std::make_shared<int>(1);
  if(kind==0)e->synthetic_text_policy_=on?std::shared_ptr<const SyntheticTextPolicy>(lifetime,reinterpret_cast<const SyntheticTextPolicy*>(lifetime.get())):nullptr;
  if(kind==1)e->text_selection_=on?std::shared_ptr<const MeasurementSelection>(lifetime,reinterpret_cast<const MeasurementSelection*>(lifetime.get())):nullptr;
  if(kind==2)e->text_measurer=on?+[](const char*,const aether_computed_style_t*,float,void*)->aether_text_metrics_t{throw std::runtime_error("callback invoked");}:nullptr;
  if(kind==3)e->contextual_text_measurer=on?+[](const char*,const aether_computed_style_t*,float,float,void*)->aether_text_metrics_t{throw std::runtime_error("callback invoked");}:nullptr;
 }
};
}
namespace al=aether::layout;
using Test=al::RealHtmlTextTestAccess;
static int checks=0;
template<class F> static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}assert(rejected);++checks;}
struct Fixture {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_node_s* owner{};Run run;std::shared_ptr<const tt::HtmlNormalizationProof> normalization;
 Fixture(std::string text="AB",Binding binding={},bool nowrap=false){
  if(!binding)binding=bind();auto css=q2c_cases::style();if(nowrap){auto pos=css.find("white-space:pre");assert(pos!=std::string::npos);css.replace(pos,15,"white-space:nowrap");}
  const auto raw=q2c_cases::document(q2c_cases::owner(text,css));
  auto proof=tt::admit_real_html(raw,1024,{3072,4096});assert(std::holds_alternative<tt::HtmlAdmission>(proof));
  assert(aether_doc_create(&doc)==AETHER_OK);assert(aether_doc_parse_html(doc,raw.c_str())==AETHER_OK);
  owner=tt::verify_real_html_dom(std::get<tt::HtmlAdmission>(proof),doc);assert(owner);
  aether_style_environment_t env{256,128,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  assert(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK);assert(aether_style_runtime_commit_revision(runtime,tx,&rev)==AETHER_OK);assert(aether_style_revision_create_map(rev,&map)==AETHER_OK);
  assert(aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
  // Authentic authority comes only from the successful owning operation.
  // Never construct/copy/mutate a proof or expose its private factory.
  auto result=tt::render_real_html_text(raw,binding,{3072,4096},256,128);
  using Output=std::shared_ptr<const tt::HtmlTextFrame>;assert(std::holds_alternative<Output>(result));auto frame=std::get<Output>(result);run=frame->run;normalization=frame->normalization;assert(normalization);
 }
 ~Fixture(){aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
 auto lookup(){return al::StyleLookup(aether::style::pin_style_map(map),nullptr,{});}
 void install(){assert(Test::install(engine,doc,map,owner,run,normalization));}
 void begin(){engine->layout_in_progress=true;auto s=lookup();al::RealSimpleTextAccess::begin(engine,doc,s);}
};
int main(){
 {Fixture f;f.install();f.begin();auto p=Test::policy(f.engine);auto s=Test::style(p);auto m=al::RealSimpleTextAccess::measure(f.engine,f.owner,s);assert(m.run&&m.run->source=="AB");++checks;
  assert(!Test::install(f.engine,f.doc,f.map,f.owner,f.run,f.normalization));++checks;
  assert(!al::RealSimpleTextAccess::owns(f.engine,f.doc));++checks;
  rejects([&]{al::RealSimpleTextAccess::measure(f.engine,f.doc,s);});
  al::RealSimpleTextAccess::clear(f.engine);rejects([&]{al::RealSimpleTextAccess::measure(f.engine,f.owner,s);});
 }
 for(int kind=0;kind<4;++kind){Fixture f;Test::conflict(f.engine,kind,true);assert(!Test::install(f.engine,f.doc,f.map,f.owner,f.run,f.normalization));assert(!Test::policy(f.engine));++checks;Test::conflict(f.engine,kind,false);f.install();f.begin();Test::conflict(f.engine,kind,true);auto s=f.lookup();rejects([&]{al::RealSimpleTextAccess::begin(f.engine,f.doc,s);});Test::conflict(f.engine,kind,false);}
 {Fixture f,g;assert(!Test::install(f.engine,g.doc,f.map,f.owner,f.run,f.normalization));++checks;assert(!Test::install(f.engine,f.doc,f.map,g.owner,f.run,f.normalization));++checks;assert(!Test::policy(f.engine));f.install();f.engine->layout_in_progress=true;auto s=g.lookup();rejects([&]{al::RealSimpleTextAccess::begin(f.engine,f.doc,s);});s=f.lookup();rejects([&]{al::RealSimpleTextAccess::begin(f.engine,g.doc,s);});}
 {Fixture f("AB"),g("A");rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,g.run,f.normalization);});assert(!Test::policy(f.engine));++checks;}
 // Perturb one retained identity gate at a time, with no concurrent mutation.
 for(int gate=0;gate<7;++gate){Fixture f;f.install();f.begin();auto p=Test::policy(f.engine);auto s=Test::style(p);auto lookup=f.lookup();
  if(gate==0)++f.doc->identity->mutation_epoch;
  if(gate==1)++f.doc->identity->structure_epoch;
  if(gate==2)++f.doc->identity->style_tree_epoch;
  if(gate==3)f.owner->identity->attached=false;
  if(gate==4)f.owner->identity->alive=false;
  if(gate==5)f.owner->children.front()->identity->attached=false;
  if(gate==6)f.owner->children.front()->identity->alive=false;
  rejects([&]{al::RealSimpleTextAccess::begin(f.engine,f.doc,lookup);});rejects([&]{al::RealSimpleTextAccess::measure(f.engine,f.owner,s);});rejects([&]{al::RealSimpleTextAccess::finish(f.engine);});
 }
 {Fixture f;f.install();f.begin();auto p=Test::policy(f.engine);auto s=Test::style(p);auto lookup=f.lookup();int rejected=0;std::thread t([&]{for(auto fn:std::vector<std::function<void()>>{[&]{al::RealSimpleTextAccess::begin(f.engine,f.doc,lookup);},[&]{al::RealSimpleTextAccess::measure(f.engine,f.owner,s);},[&]{al::RealSimpleTextAccess::finish(f.engine);}})try{fn();}catch(const std::runtime_error& e){assert(std::string(e.what())=="real text wrong thread");++rejected;}});t.join();assert(rejected==3);checks+=3;}
 {Fixture f;f.install();f.begin();auto p=Test::policy(f.engine);auto style=Test::style(p);auto lookup=f.lookup();aether_doc_destroy(f.doc);f.doc=nullptr;rejects([&]{Test::valid(p);});rejects([&]{al::RealSimpleTextAccess::begin(f.engine,nullptr,lookup);});rejects([&]{al::RealSimpleTextAccess::measure(f.engine,nullptr,style);});rejects([&]{al::RealSimpleTextAccess::finish(f.engine);});
  bool wrong_thread_first=false;std::thread t([&]{try{Test::valid(p);}catch(const std::runtime_error& e){wrong_thread_first=std::string(e.what())=="real text wrong thread";}});t.join();assert(wrong_thread_first);++checks;}

 {Fixture f("");f.install();f.begin();auto p=Test::policy(f.engine);auto m=al::RealSimpleTextAccess::measure(f.engine,f.owner,Test::style(p));assert(!m.run&&m.metrics.width==0&&m.metrics.height==64);++checks;}
 {Fixture f;f.install();auto lookup=f.lookup();rejects([&]{al::RealSimpleTextAccess::begin(f.engine,f.doc,lookup);});}
 {Fixture f;auto token=f.doc->identity;++token->mutation_epoch;assert(!Test::install(f.engine,f.doc,f.map,f.owner,f.run,f.normalization));assert(!Test::policy(f.engine));++checks;}
 {Fixture f;aether_style_map_t legacy{};assert(aether_style_runtime_create_resolved_style_map(f.runtime,f.doc,&legacy)==AETHER_OK);assert(!Test::install(f.engine,f.doc,legacy,f.owner,f.run,f.normalization));assert(!Test::policy(f.engine));++checks;aether_style_map_destroy(legacy);}
 {Fixture f,g;f.install();f.begin();g.install();auto wrong=Test::style(Test::policy(g.engine));rejects([&]{al::RealSimpleTextAccess::measure(f.engine,f.owner,wrong);});auto changed=Test::style(Test::policy(f.engine));changed.font_size+=1;rejects([&]{al::RealSimpleTextAccess::measure(f.engine,f.owner,changed);});}
 // Returned immutable transport remains valid without owner/doc/map/policy.
 std::shared_ptr<const ShapedRun> retained;
 {Fixture f;f.install();f.begin();retained=al::RealSimpleTextAccess::measure(f.engine,f.owner,Test::style(Test::policy(f.engine))).run;}
 assert(retained&&retained->source=="AB"&&!retained->font->bytes.empty());++checks;
 // Software guard rejects even equal duplicate keys; Q2c must deduplicate first.
 {namespace prep=aether::rendering::preparation;namespace sw=aether::rendering::software;
  aether::rendering::FrameStore store;assert(store.resize({4,4,1})==aether::rendering::FrameStatus::ok);
  auto prepared=prep::prepare(prep::Snapshot{{prep::Fill{{0,0,4,4},{},{1,0,0,1}}}});assert(std::holds_alternative<prep::PreparedFrame>(prepared));auto& frame=std::get<prep::PreparedFrame>(prepared);
  assert(sw::render(store,frame).status==sw::Status::ok);std::optional<aether::rendering::Frame> before,after;assert(store.copy_confirmed(before)==aether::rendering::FrameStatus::ok);
  auto font=bind()->font();auto a=std::make_shared<const GlyphMaskA8>(font,1,font->raster_flags,0,0,1,1,1,0,0,std::vector<std::uint8_t>{255});
  for(int variant=0;variant<3;++variant){auto b=std::make_shared<const GlyphMaskA8>(font,1,font->raster_flags,0,0,1,1,1,variant==1?1:0,0,std::vector<std::uint8_t>{std::uint8_t(variant==2?0:255)});std::vector<std::shared_ptr<const GlyphMaskA8>> masks{a,b};assert(sw::render_a8(store,frame,masks).status==sw::Status::invalid_mask);assert(store.copy_confirmed(after)==aether::rendering::FrameStatus::ok);assert(before->sequence==after->sequence&&before->pixels==after->pixels);++checks;}
 }
 // Q2d authentic-proof transplant negatives, controlling every other input.
 {auto binding=bind();Fixture f("AB",binding),same("AB",binding);assert(Test::install(f.engine,f.doc,f.map,f.owner,f.run,same.normalization));++checks;}
 {Fixture f("AB"),other("AB");assert(f.normalization->normalized_output==other.normalization->normalized_output);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,f.run,other.normalization);});assert(!Test::policy(f.engine));}
 {auto binding=bind();Fixture f("AB",binding),other("AB",binding,true);assert(f.normalization->normalized_output==other.normalization->normalized_output);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,f.run,other.normalization);});assert(!Test::policy(f.engine));}
 {auto binding=bind();Fixture f("AB",binding),other("A<!--piece-->B",binding);assert(f.normalization->decoded_input==other.normalization->decoded_input&&f.normalization->normalized_output==other.normalization->normalized_output);assert(f.normalization->pieces!=other.normalization->pieces);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,f.run,other.normalization);});assert(!Test::policy(f.engine));}
 {auto binding=bind();Fixture f("A  B",binding,true),other("A B",binding,true);assert(f.normalization->normalized_output==other.normalization->normalized_output);assert(f.normalization->decoded_input!=other.normalization->decoded_input);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,f.run,other.normalization);});assert(!Test::policy(f.engine));}
 {auto binding=bind();Fixture f("A B",binding,true),other("A<!--piece--> B",binding,true);assert(f.normalization->decoded_input==other.normalization->decoded_input&&f.normalization->normalized_output==other.normalization->normalized_output);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,f.run,other.normalization);});assert(!Test::policy(f.engine));}
 {auto binding=bind();Fixture f("AB",binding),other("A",binding);rejects([&]{Test::install(f.engine,f.doc,f.map,f.owner,other.run,f.normalization);});assert(!Test::policy(f.engine));}
 {Fixture f;assert(!Test::install(f.engine,f.doc,f.map,f.owner,f.run,nullptr));assert(!Test::policy(f.engine));++checks;}
 {std::shared_ptr<const tt::HtmlNormalizationProof> proof;Run retained_run;{Fixture f("A  B",{},true);f.install();f.begin();proof=f.normalization;retained_run=f.run;assert(al::RealSimpleTextAccess::measure(f.engine,f.owner,Test::style(Test::policy(f.engine))).run->source=="A B");}assert(proof->decoded_input=="A  B"&&proof->normalized_output=="A B"&&proof->binding==retained_run->binding&&retained_run->source_text=="A B");++checks;}
 // Q2e private transition seam: genuine style publication, not an epoch-only substitute.
 // A mutation between checkpoint and publication must not be legitimized by rebase.
 unsigned transition_checks=0,collection_checks=0;
 for(int gate=0;gate<3;++gate){Fixture f;auto checkpoint=Test::transition(f.doc);
  if(gate==1)assert(aether_element_set_attribute(f.owner,"style",(q2c_cases::style()+";background-color:#112233").c_str())==AETHER_OK);
  if(gate==2)assert(aether_element_set_attribute(f.owner->parent,"id","mutated-wrapper")==AETHER_OK);
  aether_style_environment_t env{256,128,1,16,0,0,0};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t map{};
  assert(aether_style_runtime_prepare_revision(f.runtime,f.doc,&env,&tx)==AETHER_OK);
  assert(aether_style_runtime_commit_revision(f.runtime,tx,&rev)==AETHER_OK);
  assert(aether_style_revision_create_map(rev,&map)==AETHER_OK);
  assert(Test::accepts(checkpoint,f.doc,map)==(gate==0));++transition_checks;
  if(gate==0){assert(!Test::accepts(checkpoint,f.doc,f.map));++transition_checks;}

  aether_style_map_destroy(map);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);
 }
 // Direct helper safety/gate isolation complements the real publication cases.
 for(int gate=0;gate<3;++gate){Fixture f;auto checkpoint=Test::transition(f.doc);
  if(gate==0)++f.doc->identity->structure_epoch;
  if(gate==1)++f.doc->identity->mutation_epoch;
  if(gate==2)++f.doc->identity->non_state_epoch;
  assert(!Test::accepts(checkpoint,f.doc,f.map));++transition_checks;
 }
 {Fixture f;auto checkpoint=Test::transition(f.doc);bool accepted=true;std::thread thread([&]{accepted=Test::accepts(checkpoint,f.doc,f.map);});thread.join();assert(!accepted);++transition_checks;}
 {Fixture f,g;auto checkpoint=Test::transition(f.doc);assert(!Test::accepts(checkpoint,f.doc,g.map));assert(!Test::accepts(checkpoint,g.doc,g.map));transition_checks+=2;}
 {Fixture f;auto checkpoint=Test::transition(f.doc);auto* old=f.doc;aether_doc_destroy(f.doc);f.doc=nullptr;assert(!Test::accepts(checkpoint,old,f.map));assert(!Test::accepts(checkpoint,nullptr,f.map));transition_checks+=2;}
 {Fixture f;auto checkpoint=Test::transition(f.doc);f.doc->identity->alive=false;assert(!Test::accepts(checkpoint,f.doc,f.map));++transition_checks;}
 {Fixture f;auto checkpoint=Test::transition(f.doc);assert(aether_element_set_attribute(f.owner,"id","mutated")==AETHER_OK);assert(!Test::accepts(checkpoint,f.doc,f.map));++transition_checks;}
 // Each supplied run/proof is authentic; only the shared-binding/linebox gate differs.
 for(int gate=0;gate<3;++gate){auto binding=bind();Fixture first("AB",binding),second("AB",gate==1?bind():binding);
  auto raw=q2c_cases::document(q2c_cases::owner("AB"));auto pos=raw.find("</body>");auto extra=q2c_cases::owner("AB");extra.replace(extra.find("aether-text"),11,"other-text");raw.insert(pos,extra);
  aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t map{};aether_layout_engine_t engine{};
  assert(aether_doc_create(&doc)==AETHER_OK&&aether_doc_parse_html(doc,raw.c_str())==AETHER_OK);
  aether_style_environment_t env{256,256,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&runtime)==AETHER_OK);
  assert(aether_style_runtime_prepare_revision(runtime,doc,&env,&tx)==AETHER_OK&&aether_style_runtime_commit_revision(runtime,tx,&rev)==AETHER_OK&&aether_style_revision_create_map(rev,&map)==AETHER_OK&&aether_layout_create(nullptr,nullptr,&engine)==AETHER_OK);
  Run second_run=second.run;if(gate==2){auto result=tt::build_simple_ltr_run(binding,"AB",{3072,4097});assert(std::holds_alternative<Run>(result));second_run=std::get<Run>(result);}
  bool installed=Test::install_blocks(engine,doc,map,{aether_doc_get_element_by_id(doc,"aether-text"),aether_doc_get_element_by_id(doc,"other-text")},{first.run,second_run},{first.normalization,second.normalization});
  assert(installed==(gate==0));assert(bool(Test::policy(engine))==(gate==0));++collection_checks;
  aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);
 }
 checks+=Test::registry_checks();
 std::cout<<"Q2d internal checks="<<checks<<" PASS\n";
 std::cout<<"Q2e private style-transition checks="<<transition_checks<<" mixed-binding/LineBox collection checks="<<collection_checks<<" PASS\n";
 std::cout<<"Scope: private helper seam with real publication and mutation, no production callback injection; not a whole-wrapper mutation hook.\n";
}
