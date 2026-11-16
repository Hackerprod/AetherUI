#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/text/real_html_text.h"
#include "../src/rendering/text/real_html_admission.h"
#include "../src/rendering/text/real_html_style_transition.h"
#include "../src/rendering/text/real_html_blocks_images.h"
#include "../src/rendering/preparation/native_command_source.h"
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
class RealHtmlTextTestAccess {
public:
 static bool install(aether_layout_engine_s* e,aether_doc_s* d,aether_style_map_s* m,aether_node_s* owner,Run run,std::shared_ptr<const tt::HtmlNormalizationProof> proof){return RealSimpleTextAccess::install_blocks(*e,d,m,{owner},{std::move(run)},{std::move(proof)});}
};
}
namespace r=aether::rendering;
namespace prep=r::preparation;
namespace sw=r::software;
struct Context {
 aether_doc_t doc{};aether_style_runtime_t runtime{};aether_style_transaction_t tx{};aether_style_revision_t rev{};aether_style_map_t map{};aether_layout_engine_t engine{};aether_layout_result_t layout{};aether_display_list_builder_t builder{};
 ~Context(){aether_display_list_builder_destroy(builder);aether_layout_result_destroy(layout);aether_layout_destroy(engine);aether_style_map_destroy(map);aether_style_revision_destroy(rev);aether_style_transaction_destroy(tx);aether_style_runtime_destroy(runtime);aether_doc_destroy(doc);}
};
static r::Frame pixels(r::FrameStore& store){std::optional<r::Frame> frame;assert(store.copy_confirmed(frame)==r::FrameStatus::ok&&frame);return *frame;}
int main(){
 std::unique_ptr<prep::PreparedFrame> retained;
 std::vector<std::shared_ptr<const GlyphMaskA8>> masks;
 r::Frame expected;
 std::weak_ptr<const prep::ImageResource> image_life;
 std::weak_ptr<const FontInstance> font_life;
 std::weak_ptr<const tt::RealFontBinding> binding_life;
 std::weak_ptr<const tt::PreparedSimpleRun> run_life;
 std::weak_ptr<const tt::HtmlNormalizationProof> proof_life;
 std::weak_ptr<node_identity_s> doc_life;
 {
  auto binding=bind();binding_life=binding;font_life=binding->font();
  auto image=std::get<prep::ImageRef>(prep::ImageResource::copy(2,2,std::vector<std::uint8_t>{0,0,255,255,0,255,0,255,255,0,0,255,0,255,255,255}));image_life=image;
  r::NativeImageCatalog catalog{{"hero",image}};
  auto raw=q2c_cases::document(q2c_cases::owner("A",q2c_cases::style()+";background-image:url(hero);background-repeat:no-repeat;background-position:0px 0px;background-size:auto"));
  auto result=tt::render_real_html_blocks_images(raw,{"aether-text"},binding,{3072,4096},256,128,catalog);
  using Output=std::shared_ptr<const tt::HtmlBlocksImagesFrame>;assert(std::holds_alternative<Output>(result));auto output=std::get<Output>(result);
  assert(output->blocks.owners.size()==1&&output->targets.size()==1);
  auto run=output->blocks.owners[0].run;auto proof=output->blocks.owners[0].normalization;run_life=run;proof_life=proof;
  assert(run->glyphs.size()==1);masks.push_back(run->glyphs[0].mask);
  Context c;assert(aether_doc_create(&c.doc)==AETHER_OK&&aether_doc_parse_html(c.doc,raw.c_str())==AETHER_OK);doc_life=c.doc->identity;
  aether_style_environment_t env{256,128,1,16,0,0,0};assert(aether_style_runtime_create(nullptr,&env,nullptr,nullptr,&c.runtime)==AETHER_OK);
  assert(aether_style_runtime_prepare_revision(c.runtime,c.doc,&env,&c.tx)==AETHER_OK&&aether_style_runtime_commit_revision(c.runtime,c.tx,&c.rev)==AETHER_OK&&aether_style_revision_create_map(c.rev,&c.map)==AETHER_OK);
  assert(aether_layout_create(nullptr,nullptr,&c.engine)==AETHER_OK);
  assert(aether::layout::RealHtmlTextTestAccess::install(c.engine,c.doc,c.map,aether_doc_get_element_by_id(c.doc,"aether-text"),run,proof));
  assert(aether_layout_layout(c.engine,c.doc,c.map,256,128,nullptr,nullptr,&c.layout)==AETHER_OK);
  assert(aether_display_list_builder_create(nullptr,nullptr,&c.builder)==AETHER_OK);
  std::unique_ptr<r::NativePaintList> list;assert(r::build_native_opacity(c.builder,c.layout,c.map,nullptr,r::NativeScrollInput{},catalog,list)==AETHER_OK);
  assert(list->images.size()==1&&list->text_runs.size()==1);
  auto snapshot=prep::snapshot_native_opacity(*list);assert(std::holds_alternative<prep::Snapshot>(snapshot));
  auto prepared=prep::prepare(std::get<prep::Snapshot>(snapshot));assert(std::holds_alternative<prep::PreparedFrame>(prepared));retained=std::make_unique<prep::PreparedFrame>(std::move(std::get<prep::PreparedFrame>(prepared)));
  r::FrameStore store;assert(store.resize({256,128,1})==r::FrameStatus::ok);assert(sw::render_a8(store,*retained,masks).status==sw::Status::ok);expected=pixels(store);
  assert(expected.pixels==output->blocks.frame.pixels);
 }
 // No Context, NativePaintList, Snapshot, caller catalog/image, font binding,
 // wrapper result, source proof or PreparedSimpleRun survives this point.
 assert(doc_life.expired()&&binding_life.expired()&&run_life.expired()&&proof_life.expired());
 assert(!image_life.expired()&&!font_life.expired()&&retained&&retained->images());
 for(int repeat=0;repeat<2;++repeat){r::FrameStore store;assert(store.resize({256,128,1})==r::FrameStatus::ok);assert(sw::render_a8(store,*retained,masks).status==sw::Status::ok);auto later=pixels(store);assert(later.pixels==expected.pixels&&later.stride==expected.stride);}
 retained.reset();assert(image_life.expired());masks.clear();assert(font_life.expired());
 std::cout<<"Q2f prepared+masks lifetime PASS: destroyed caller/DOM/list/snapshot/output/run/proof/binding; rerender=2 exact; final image/font release verified.\n";
}
