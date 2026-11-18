#include "../src/rendering/html_ltr/owned_font_transport.h"
#include "../src/rendering/text/real_font_metrics.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <new>
using namespace aether::rendering;
namespace h=html_ltr;namespace tt=text::truetype;
static long fail_after=-1,allocations=0;
void* operator new(size_t n){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,size_t)noexcept{std::free(p);}void operator delete[](void* p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t alignment){if(fail_after==0)throw std::bad_alloc();if(fail_after>0)--fail_after;++allocations;void* p=nullptr;if(posix_memalign(&p,size_t(alignment),n?n:1))throw std::bad_alloc();return p;}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}void operator delete(void* p,size_t,std::align_val_t)noexcept{std::free(p);}
static auto read(const char* p){std::ifstream f(p,std::ios::binary);return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),{});}
int main(int argc,char** argv){
 assert(argc==2);auto bytes=read(argv[1]);h::OwnedBudget budget;auto original=budget.total().current;
 struct LegacyLayout {std::vector<uint8_t> bytes;std::string digest,provenance;};
 assert(text::FontSource::legacy_reservation_bytes()==sizeof(LegacyLayout)+128);
 auto source=std::allocate_shared<h::owned_text::Source>(h::OwnedAllocator<h::owned_text::Source>(budget,h::OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(bytes));
 auto alias=h::owned_text::legacy_source(source);const auto* address=source->bytes.data();assert(alias->bytes.empty()&&alias->shares_bytes()&&alias->byte_view().data()==address&&alias->byte_view().size()==bytes.size());assert(std::string_view(alias->digest)==std::string_view(source->digest));
 auto copied=std::make_shared<const text::FontSource>(bytes);assert(!copied->shares_bytes()&&copied->byte_view().data()==copied->bytes.data()&&copied->digest==alias->digest);
 auto ar=tt::RealFontBinding::create(alias,1024),cr=tt::RealFontBinding::create(copied,1024);assert(std::holds_alternative<std::shared_ptr<const tt::RealFontBinding>>(ar)&&std::holds_alternative<std::shared_ptr<const tt::RealFontBinding>>(cr));
 auto a=std::get<std::shared_ptr<const tt::RealFontBinding>>(std::move(ar)),c=std::get<std::shared_ptr<const tt::RealFontBinding>>(std::move(cr));assert(a->font()->byte_view().data()==address);
 const auto am=tt::read_vertical_metrics(a),cm=tt::read_vertical_metrics(c);assert(std::get<tt::FontVerticalMetrics>(am).ascent==std::get<tt::FontVerticalMetrics>(cm).ascent);
 for(uint32_t cp=32;cp<256;++cp){auto x=a->map()->lookup(cp),y=c->map()->lookup(cp);assert(x.index()==y.index());if(auto m=std::get_if<tt::Mapped>(&x))assert(m->glyph_id==std::get<tt::Mapped>(y).glyph_id);}
 // Reuse the same immutable adapter through many operations, not new source copies.
 const auto before=allocations;for(size_t i=0;i<10000;++i){auto ref=a->font();assert(ref->byte_view().data()==address&&ref->byte_view().size()==bytes.size());}assert(allocations==before);
 const auto start=allocations;{auto other=h::owned_text::legacy_source(source);}const auto points=allocations-start;const auto retained=budget.total().current;
 for(long i=0;i<points;++i){fail_after=i;try{auto other=h::owned_text::legacy_source(source);assert(false);}catch(const std::bad_alloc&){}fail_after=-1;assert(budget.total().current==retained);}
 source.reset();alias.reset();assert(a->font()->byte_view().data()==address&&a->font()->byte_view().size()==bytes.size());a.reset();c.reset();copied.reset();ar=tt::BindingError::cancelled;cr=tt::BindingError::cancelled;assert(budget.total().current==original);
 // Malformed backing is read from the alias span, not silently from empty bytes.
 {uint8_t bad[]{0};auto s=std::allocate_shared<h::owned_text::Source>(h::OwnedAllocator<h::owned_text::Source>(budget,h::OwnedCategory::inputs),budget.handle(),std::span<const uint8_t>(bad));auto r=tt::RealFontBinding::create(h::owned_text::legacy_source(s),1024);assert(std::get<tt::BindingError>(r)==tt::BindingError::malformed);}
 assert(budget.total().current==original);std::cout<<"font bridge equivalence/lifetime/10000 no-copy operations/OOM="<<points<<"/malformed/cargo legacy PASS\n";
}
