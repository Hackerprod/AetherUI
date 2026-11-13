#ifdef NDEBUG
#error Assertions required
#endif
#include "../src/rendering/preparation/native_command_source.h"
#include "../src/rendering/display_list_types.h"
#include "../src/rendering/preparation/native_text_validation.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <iostream>
#include <cstdlib>
#include <new>
static long fail_after=-1;
void* operator new(std::size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
using namespace aether::rendering;
using namespace aether::rendering::preparation;
static int checks=0;
#define CHECK(x) do {++checks;assert(x);} while(0)
static std::shared_ptr<const text::ShapedRun> run(std::size_t bytes=1,std::size_t glyphs=1,float baseline=8) {
 auto f=std::make_shared<const text::FontInstance>(std::vector<std::uint8_t>(bytes),"UNVERIFIED",0,640,0);
 return std::make_shared<const text::ShapedRun>(f,"x",std::vector<text::Glyph>(glyphs),aether_text_metrics_t{10,10,baseline});
}
static aether_paint_command_t command() {
 aether_paint_command_t c{};c.tag=AETHER_PAINT_DRAW_TEXT;
 auto& t=c.data.draw_text;t.text=strdup("x");t.rect={3,20,10,10};strcpy(t.font_family,"test");t.font_size=10;t.font_weight=400;t.color={0,0,0,1};return c;
}
static void init(NativePaintList& n,std::shared_ptr<const text::ShapedRun> r) {
 NativeListAccess::list(n)=new aether_display_list_s;
 NativeListAccess::list(n)->commands.push_back(command());n.text_runs.push_back({0,{r,20+r->metrics.baseline}});
}
static bool bad(const SnapshotResult& r,ErrorCode e=ErrorCode::INVALID_PAYLOAD){auto* p=std::get_if<Error>(&r);return p&&p->code==e;}
static bool bad_prepare(const Snapshot& s,ErrorCode e=ErrorCode::INVALID_PAYLOAD){auto r=prepare(s);auto* p=std::get_if<Error>(&r);return p&&p->code==e;}
struct LastCancel:Cancellation{int calls=0,stop;explicit LastCancel(int n):stop(n){}bool cancelled()noexcept override{return ++calls==stop;}};
int main(){
 auto a=run(),b=run();Snapshot captured;
 { NativePaintList n;init(n,a);
   auto result=snapshot_native_opacity(n);CHECK(std::holds_alternative<Snapshot>(result));captured=std::get<Snapshot>(result);
   CHECK(std::get<Text>(captured.commands[0]).native->run==a);CHECK(std::get<Text>(captured.commands[0]).native->baseline==28);
   n.text_runs.clear();CHECK(bad(snapshot_native_opacity(n)));n.text_runs={{0,{a,28}},{0,{a,28}}};CHECK(bad(snapshot_native_opacity(n)));
   n.text_runs={{1,{a,28}}};CHECK(bad(snapshot_native_opacity(n)));n.text_runs={{0,{a,29}}};CHECK(bad(snapshot_native_opacity(n)));
   n.text_runs={{0,{nullptr,28}}};CHECK(bad(snapshot_native_opacity(n)));n.text_runs={{0,{a,std::numeric_limits<float>::quiet_NaN()}}};CHECK(bad(snapshot_native_opacity(n)));
   n.text_runs={{0,{a,28}}};n.events={{0,true,1,0,.5f},{1,false,1,0,1}};
   auto opacity=snapshot_native_opacity(n);CHECK(std::holds_alternative<Snapshot>(opacity));auto& s=std::get<Snapshot>(opacity);CHECK(s.commands.size()==3);CHECK(std::get<Text>(s.commands[1]).native->run==a);CHECK(std::holds_alternative<PreparedFrame>(prepare(s)));
   LastCancel probe(99999);CHECK(std::holds_alternative<Snapshot>(snapshot_native_opacity(n,&probe)));LastCancel final(probe.calls);CHECK(bad(snapshot_native_opacity(n,&final),ErrorCode::CANCELLED));
   auto legacy=snapshot_native_commands(NativeListAccess::list(n));CHECK(std::holds_alternative<Snapshot>(legacy));CHECK(!std::get<Text>(std::get<Snapshot>(legacy).commands[0]).native);
 }
 CHECK(std::get<Text>(captured.commands[0]).native->run==a);auto prepared=prepare(captured);CHECK(std::holds_alternative<PreparedFrame>(prepared));
 auto mutated=captured;std::get<Text>(mutated.commands[0]).native->baseline++;CHECK(bad_prepare(mutated));mutated=captured;std::get<Text>(mutated.commands[0]).text="y";CHECK(bad_prepare(mutated));
 mutated=captured;std::get<Text>(mutated.commands[0]).font_size=11;CHECK(bad_prepare(mutated));mutated=captured;std::get<Text>(mutated.commands[0]).native.reset();CHECK(bad_prepare(mutated));
 mutated=captured;std::get<Text>(mutated.commands[0]).native->run=b;CHECK(std::holds_alternative<PreparedFrame>(prepare(mutated)));
 // Border compaction must consume text identity using original raw indices.
 for(std::size_t sides=2;sides<=4;++sides){
  NativePaintList n;init(n,a);auto* list=NativeListAccess::list(n);
  for(std::size_t j=0;j<sides;++j){aether_paint_command_t c{};c.tag=AETHER_PAINT_FILL_BORDER_SIDE;auto& d=c.data.fill_border_side;d.outer_rect={0,0,20,20};d.inner_rect={1,1,18,18};d.side=int(j);d.style=AETHER_BORDER_STYLE_SOLID;d.color={0,0,0,1};list->commands.push_back(c);}
  list->border_groups.push_back({1,sides});list->commands.push_back(command());n.text_runs.push_back({sides+1,{b,28}});
  n.events={{0,true,1,0,.5f},{1,true,2,1,.7f},{sides+1,false,2,0,1},{sides+2,false,1,0,1}};
  auto r=snapshot_native_opacity(n);CHECK(std::holds_alternative<Snapshot>(r));auto& ss=std::get<Snapshot>(r);CHECK(ss.commands.size()==7);CHECK(std::get<Text>(ss.commands[1]).native->run==a);CHECK(std::get<Text>(ss.commands[5]).native->run==b);CHECK(std::holds_alternative<PreparedFrame>(prepare(ss)));
  auto saved=n.text_runs;n.text_runs[1].command_index=2;CHECK(bad(snapshot_native_opacity(n)));n.text_runs=saved;std::swap(n.text_runs[0],n.text_runs[1]);CHECK(bad(snapshot_native_opacity(n)));n.text_runs=saved;
  // All allocation failures return errors and leave the immutable source intact.
  int failures=0;for(int limit=0;limit<100;++limit){fail_after=limit;auto attempt=snapshot_native_opacity(n);fail_after=-1;if(auto* e=std::get_if<Error>(&attempt)){CHECK(e->code==ErrorCode::OUT_OF_MEMORY);++failures;}else break;CHECK(n.text_runs[0].placement.run==a);CHECK(n.text_runs[1].placement.run==b);}CHECK(failures>0);
 }
 int prepare_failures=0;for(int limit=0;limit<100;++limit){fail_after=limit;auto attempt=prepare(captured);fail_after=-1;if(auto* e=std::get_if<Error>(&attempt)){CHECK(e->code==ErrorCode::OUT_OF_MEMORY);++prepare_failures;}else break;CHECK(std::get<Text>(captured.commands[0]).native->run==a);}CHECK(prepare_failures>0);
 // Resource budgets: same identity charged once, distinct instances charged separately.
 auto t=std::get<Text>(captured.commands[0]);t.native->run=run(text::max_font_bytes);NativeTextBudget budget;
 for(int i=0;i<100;++i)CHECK(!budget.add(t));CHECK(budget.font_bytes==text::max_font_bytes);
 std::vector<std::shared_ptr<const text::ShapedRun>> retained{t.native->run};for(int i=0;i<3;++i){t.native->run=run(text::max_font_bytes);retained.push_back(t.native->run);CHECK(!budget.add(t));}t.native->run=run(text::max_font_bytes);CHECK(budget.add(t)==ErrorCode::LIMIT_EXCEEDED);
 Snapshot many;many.native_text_required=true;t.native->run=run(1,text::max_glyphs);for(int i=0;i<17;++i)many.commands.push_back(t);CHECK(bad_prepare(many,ErrorCode::LIMIT_EXCEEDED));
 // Absolute baseline is not modified by transforms or scroll during preparation.
 captured.commands.insert(captured.commands.begin(),PushScroll{1,2,7});captured.commands.insert(captured.commands.begin()+1,PushTransform{{1,0,0,1,4,5}});captured.commands.push_back(PopTransform{});captured.commands.push_back(PopScroll{});
 auto transformed=prepare(captured);CHECK(std::holds_alternative<PreparedFrame>(transformed));const auto& e=std::get<PreparedFrame>(transformed).events()[2];CHECK(std::get<Text>(e.command).native->baseline==28);CHECK(e.scroll_y==-7);CHECK(e.matrix.dy==5);
 std::cout<<"snapshot checks="<<checks<<"\n";
}
