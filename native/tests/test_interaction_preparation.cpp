#include "../src/rendering/html_ltr/interaction_preparation.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <limits>
#include <optional>
using namespace aether::rendering;
namespace h=html_ltr;
namespace p=preparation;
static_assert(!std::is_constructible_v<h::InteractionPreparedFrame,h::OwnedVector<h::InteractionPreparedEvent>&&>);
namespace {
bool enabled{};std::size_t fail_at{},calls{},tracked_live{};
struct Item{void* p;std::size_t n;};Item items[65536]{};std::size_t count{};
h::OwnedBudget* domain{};std::size_t base{};bool uncovered{};
void* alloc(std::size_t n,std::size_t align){
 if(enabled&&++calls==fail_at)return nullptr;
 align=std::max(align,alignof(void*));
 if(n>std::numeric_limits<std::size_t>::max()-align-sizeof(void*))return nullptr;
 void* raw=std::malloc((n?n:1)+align+sizeof(void*));if(!raw)return nullptr;
 const auto start=reinterpret_cast<std::uintptr_t>(raw)+sizeof(void*);
 void* ptr=reinterpret_cast<void*>((start+align-1)&~std::uintptr_t(align-1));
 reinterpret_cast<void**>(ptr)[-1]=raw;
 if(ptr&&enabled){assert(count<65536);items[count++]={ptr,n};tracked_live+=n;if(tracked_live>domain->total().current-base)uncovered=true;}
 return ptr;
}
void release(void* ptr){if(!ptr)return;for(std::size_t i=0;i<count;++i)if(items[i].p==ptr){tracked_live-=items[i].n;items[i]=items[--count];break;}std::free(reinterpret_cast<void**>(ptr)[-1]);}
void start(h::OwnedBudget& b,std::size_t fail=0){assert(!tracked_live&&!count);domain=&b;base=b.total().current;calls=0;fail_at=fail;uncovered=false;enabled=true;}
void stop(){enabled=false;assert(!uncovered);domain=nullptr;}
using Views=h::OwnedVector<h::InteractionCommandView>;
Views fixture(h::OwnedBudget& b){
 Views v(h::OwnedAllocator<h::InteractionCommandView>(b,h::OwnedCategory::paint));
 v.emplace_back(p::PushClip{{0,0,100,100},{}});v.emplace_back(p::BeginChunk{1});
 const std::uint8_t bytes[]{1,2,3};
 auto source=std::allocate_shared<h::owned_text::Source>(h::OwnedAllocator<h::owned_text::Source>(b,h::OwnedCategory::inputs),b.handle(),std::span(bytes));
 for(unsigned i=0;i<16;++i){
  auto font=std::allocate_shared<h::owned_text::Font>(h::OwnedAllocator<h::owned_text::Font>(b,h::OwnedCategory::glyphs),source,(8+i)*64);
  auto run=std::allocate_shared<h::owned_text::Run>(h::OwnedAllocator<h::owned_text::Run>(b,h::OwnedCategory::glyphs),b.handle(),font);
  run->source="Literal text exceeds small string storage \xc3\xa9";run->glyphs.push_back({1,0,64,0,0,0});run->metrics={20,24,18};
  v.emplace_back(h::InteractionTextView{{run->source.data(),run->source.size()},{0,0,20,24},"AetherLTR",float(8+i),400,AETHER_TEXT_ALIGN_START,{0,0,0,1},0,h::InteractionNativeText{run,18}});
 }
 p::BorderGroup border;border.outer={0,0,20,20};border.inner={1,1,18,18};border.sides[0]={true,AETHER_BORDER_STYLE_SOLID,{1,0,0,1}};v.emplace_back(border);
 v.emplace_back(p::EndChunk{1});v.emplace_back(p::PopClip{});return v;
}
void check_frame(const h::InteractionPreparedFrame& f){assert(f.events().size()==21);assert(std::holds_alternative<p::BorderGroup>(f.events()[18].command));for(const auto& e:f.events()){assert(e.matrix.m11==1&&e.matrix.m22==1&&e.scroll_depth==0);}}
}
void* operator new(std::size_t n){if(auto* p=alloc(n,alignof(std::max_align_t)))return p;throw std::bad_alloc{};}
void* operator new[](std::size_t n){return ::operator new(n);}
void* operator new(std::size_t n,std::align_val_t a){if(auto* p=alloc(n,std::size_t(a)))return p;throw std::bad_alloc{};}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}void operator delete[](void* p,std::size_t)noexcept{release(p);}
void operator delete(void* p,std::align_val_t)noexcept{release(p);}void operator delete[](void* p,std::align_val_t)noexcept{release(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{release(p);}void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{release(p);}
int main(){
 h::OwnedBudget budget;const auto empty=budget.total().current;auto input=fixture(budget);const auto baseline=budget.total().current;
 std::size_t snapshot_calls{},prepare_calls{};
 {start(budget);auto s=h::interaction_snapshot(budget,input);snapshot_calls=calls;assert(std::holds_alternative<h::InteractionSnapshot>(s));stop();}assert(!tracked_live&&budget.total().current==baseline);
 for(std::size_t fail=1;fail<=snapshot_calls;++fail){start(budget,fail);{auto s=h::interaction_snapshot(budget,input);assert(std::holds_alternative<p::Error>(s));assert(std::get<p::Error>(s).code==p::ErrorCode::OUT_OF_MEMORY);}stop();assert(!tracked_live&&budget.total().current==baseline);}
 auto snapshot_result=h::interaction_snapshot(budget,input);assert(std::holds_alternative<h::InteractionSnapshot>(snapshot_result));const auto& snapshot=std::get<h::InteractionSnapshot>(snapshot_result);const auto snapshot_base=budget.total().current;
 {start(budget);auto f=h::interaction_prepare(budget,snapshot);prepare_calls=calls;assert(std::holds_alternative<h::InteractionPreparedFrame>(f));check_frame(std::get<h::InteractionPreparedFrame>(f));stop();}assert(!tracked_live&&budget.total().current==snapshot_base);
 auto confirmed=h::interaction_prepare(budget,snapshot);assert(std::holds_alternative<h::InteractionPreparedFrame>(confirmed));const auto confirmed_base=budget.total().current;
 for(std::size_t fail=1;fail<=prepare_calls;++fail){start(budget,fail);{auto f=h::interaction_prepare(budget,snapshot);assert(std::holds_alternative<p::Error>(f));assert(std::get<p::Error>(f).code==p::ErrorCode::OUT_OF_MEMORY);}stop();assert(!tracked_live&&budget.total().current==confirmed_base);check_frame(std::get<h::InteractionPreparedFrame>(confirmed));}
 {auto held=budget.reserve(h::OwnedCategory::temporary,budget.remaining());auto f=h::interaction_prepare(budget,snapshot);assert(std::get<p::Error>(f).code==p::ErrorCode::LIMIT_EXCEEDED);}assert(budget.total().current==confirmed_base);
 {auto f=h::interaction_prepare(budget,snapshot);assert(std::holds_alternative<h::InteractionPreparedFrame>(f));check_frame(std::get<h::InteractionPreparedFrame>(f));}
 {h::OwnedBudget other;auto f=h::interaction_prepare(other,snapshot);assert(std::get<p::Error>(f).code==p::ErrorCode::INVALID_PAYLOAD);}
 // Error metadata and scope semantics remain shared with legacy prepare.
 {h::InteractionCommandView pop=p::PopClip{};auto s=h::interaction_snapshot(budget,std::span(&pop,1));auto f=h::interaction_prepare(budget,std::get<h::InteractionSnapshot>(s));const auto& e=std::get<p::Error>(f);assert(e.code==p::ErrorCode::STACK_UNDERFLOW&&e.index==0&&e.tag==AETHER_PAINT_POP_CLIP);p::Snapshot legacy;legacy.commands.emplace_back(p::PopClip{});auto old=p::prepare(legacy);const auto& oe=std::get<p::Error>(old);assert(e.code==oe.code&&e.index==oe.index&&e.tag==oe.tag);}
 // Valid POD scopes/BorderGroup use the exact same kernel and primitive validator.
 {h::InteractionCommandView v[]{p::PushTransform{{1,0,0,1,3,4}},p::BeginOpacity{1,0,.5f},p::Fill{{0,0,4,4},{},{1,0,0,1}},p::EndOpacity{1},p::PopTransform{}};auto s=h::interaction_snapshot(budget,v);auto f=h::interaction_prepare(budget,std::get<h::InteractionSnapshot>(s));assert(std::holds_alternative<h::InteractionPreparedFrame>(f));p::Snapshot old;old.commands={p::PushTransform{{1,0,0,1,3,4}},p::BeginOpacity{1,0,.5f},p::Fill{{0,0,4,4},{},{1,0,0,1}},p::EndOpacity{1},p::PopTransform{}};auto lf=p::prepare(old);const auto& a=std::get<h::InteractionPreparedFrame>(f).events();const auto& z=std::get<p::PreparedFrame>(lf).events();assert(a.size()==z.size());for(std::size_t i=0;i<a.size();++i){assert(a[i].command.index()==z[i].command.index());assert(!std::memcmp(&a[i].matrix,&z[i].matrix,sizeof(p::Matrix)));assert(a[i].transform_depth==z[i].transform_depth);}}
 // Scope depth exhaustion and cancellation are shared, not wrapper shortcuts.
 {Views deep(h::OwnedAllocator<h::InteractionCommandView>(budget,h::OwnedCategory::paint));for(unsigned i=0;i<257;++i)deep.emplace_back(p::PushClip{{0,0,1,1},{}});auto ss=h::interaction_snapshot(budget,deep);auto f=h::interaction_prepare(budget,std::get<h::InteractionSnapshot>(ss));assert(std::get<p::Error>(f).code==p::ErrorCode::LIMIT_EXCEEDED);}
 {struct Cancel:p::Cancellation{bool cancelled()noexcept override{return true;}} cancel;auto f=h::interaction_prepare(budget,snapshot,&cancel);assert(std::get<p::Error>(f).code==p::ErrorCode::CANCELLED);assert(budget.total().current==confirmed_base);}
 {h::InteractionSnapshot candidate(budget.handle());bool threw=false;try{candidate.reserve(p::max_commands+1);}catch(const h::Error& e){threw=e.status==AETHER_ERROR_LIMIT_EXCEEDED;}assert(threw);}
 {h::InteractionCommandView bad=h::InteractionTextView{std::string_view("\xc0\xaf",2),{0,0,1,1},"AetherLTR",16,400,AETHER_TEXT_ALIGN_START,{0,0,0,1},0,{}};auto s=h::interaction_snapshot(budget,std::span(&bad,1),false);assert(std::get<p::Error>(s).code==p::ErrorCode::INVALID_UTF8);}
 assert(budget.total().current==confirmed_base);
 std::optional<h::InteractionPreparedFrame> survivor;{h::OwnedBudget local;auto views=fixture(local);auto s=h::interaction_snapshot(local,views);auto f=h::interaction_prepare(local,std::get<h::InteractionSnapshot>(s));survivor.emplace(std::get<h::InteractionPreparedFrame>(std::move(f)));}check_frame(*survivor);survivor.reset();
 std::printf("portable preparation PASS: %zu snapshot + %zu prepare allocation failures; shared kernel metadata/scopes; live bytes reserved before new; baseline=%zu fixture=%zu\n",snapshot_calls,prepare_calls,empty,baseline-empty);
}
