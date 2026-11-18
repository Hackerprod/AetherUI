#include "rendering/html_ltr/owned_allocator.h"
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <map>
#include <cstdlib>
#include <unordered_map>
#include <scoped_allocator>
// Test executable only: force the underlying aligned allocation to fail.
// The production allocator contains no global hooks or replacements.
static bool fail_aligned=false;
void* operator new(std::size_t n,std::align_val_t alignment){
 if(fail_aligned)throw std::bad_alloc{};
 void* p=nullptr;if(posix_memalign(&p,static_cast<std::size_t>(alignment),n?n:1))throw std::bad_alloc{};return p;
}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
using namespace aether::rendering::html_ltr;
struct alignas(256) Wide {int value{};};
struct Throws {Throws(){throw std::bad_alloc{};}};
template<class F> void limit(F f){bool failed=false;try{f();}catch(const Error& e){assert(e.status==AETHER_ERROR_LIMIT_EXCEEDED);failed=true;}assert(failed);}
int main(){
 const auto baseline=OwnedBudget::bookkeeping_bytes();OwnedBudget b;
 {
  OwnedAllocator<int> a(b,OwnedCategory::values);auto* p=a.allocate(12);assert(b.total().current==baseline+12*sizeof(int)+a.allocation_overhead());a.deallocate(p,12);assert(b.total().current==baseline);
  assert(!a.allocate(0));a.deallocate(nullptr,0);
  limit([&]{a.allocate(std::numeric_limits<std::size_t>::max());});assert(b.total().current==baseline);
  OwnedAllocator<Wide> wide(a);auto* w=wide.allocate(2);assert(reinterpret_cast<std::uintptr_t>(w)%alignof(Wide)==0);wide.deallocate(w,2);
  fail_aligned=true;bool failed=false;try{(void)a.allocate(4);}catch(const std::bad_alloc&){failed=true;}fail_aligned=false;assert(failed&&b.total().current==baseline);
  auto* retry=a.allocate(4);a.deallocate(retry,4);assert(b.total().current==baseline);
  assert(a==OwnedAllocator<char>(a));
  OwnedBudget other;assert(a!=OwnedAllocator<int>(other,OwnedCategory::values));
 }
 {
  OwnedVector<int> v{OwnedAllocator<int>(b,OwnedCategory::values)};v.reserve(8);const auto first=b.total().current;v.reserve(16);assert(b.total().peak>=first+16*sizeof(int)+OwnedAllocator<int>::allocation_overhead());
  v.assign(16,4);auto copy=v;assert(copy==v);OwnedVector<int> moved{OwnedAllocator<int>(b,OwnedCategory::values)};moved=std::move(copy);assert(moved.size()==16);
  OwnedString text(400,'x',OwnedAllocator<char>(b,OwnedCategory::values));assert(text.size()==400);
  using Pair=std::pair<const int,int>;
  std::map<int,int,std::less<int>,OwnedAllocator<Pair>> tree{std::less<int>{},OwnedAllocator<Pair>(b,OwnedCategory::values)};
  std::unordered_map<int,int,std::hash<int>,std::equal_to<int>,OwnedAllocator<Pair>> hash{0,std::hash<int>{},std::equal_to<int>{},OwnedAllocator<Pair>(b,OwnedCategory::values)};
  for(int i=0;i<128;++i){tree.emplace(i,i);hash.emplace(i,i);}assert(tree.size()==128&&hash.size()==128);
 }
 assert(b.total().current==baseline);
 {
  std::weak_ptr<Wide> weak;
  {auto strong=std::allocate_shared<Wide>(OwnedAllocator<Wide>(b,OwnedCategory::glyphs));weak=strong;strong->value=5;assert(reinterpret_cast<std::uintptr_t>(strong.get())%alignof(Wide)==0);}
  assert(weak.expired()&&b.total().current>baseline);weak.reset();assert(b.total().current==baseline);
  bool failed=false;try{(void)std::allocate_shared<Throws>(OwnedAllocator<Throws>(b,OwnedCategory::glyphs));}catch(const std::bad_alloc&){failed=true;}assert(failed&&b.total().current==baseline);
 }
 {OwnedBudget small(baseline+256);OwnedVector<char> v{OwnedAllocator<char>(small,OwnedCategory::values)};v.assign(32,'a');const auto charge=small.total().current;limit([&]{v.reserve(256);});assert(v.size()==32&&v[0]=='a'&&small.total().current==charge);v.reserve(64);assert(v.size()==32);}
 {OwnedBudget same(b.handle());assert(same.total().current==b.total().current);auto r=same.reserve(OwnedCategory::values,4);assert(b.total().current==baseline+4);}assert(b.total().current==baseline);
 {OwnedBudget first,second;
  using A=OwnedAllocator<int>;
  {OwnedVector<int> left(A(first,OwnedCategory::values)),right(A(second,OwnedCategory::values));
   left.assign(25,1);right.assign(40,2);left=right;
   assert(left.get_allocator()==right.get_allocator()&&left==right&&first.total().current==baseline);
   OwnedVector<int> target(A(first,OwnedCategory::values));target.assign(10,3);target=std::move(right);
   assert(target.get_allocator()==A(second,OwnedCategory::values)&&target.size()==40&&first.total().current==baseline);
   OwnedVector<int> other(A(first,OwnedCategory::values));other.assign(12,7);target.swap(other);
   assert(target.get_allocator()==A(first,OwnedCategory::values)&&target[0]==7&&other.get_allocator()==A(second,OwnedCategory::values)&&other[0]==2);
  }assert(first.total().current==baseline&&second.total().current==baseline);
  {using Outer=OwnedAllocator<OwnedString>;using Inner=OwnedAllocator<char>;using Nested=std::scoped_allocator_adaptor<Outer,Inner>;
   std::vector<OwnedString,Nested> a(Nested{Outer(first,OwnedCategory::values),Inner(first,OwnedCategory::values)});
   std::vector<OwnedString,Nested> z(Nested{Outer(second,OwnedCategory::values),Inner(second,OwnedCategory::values)});
   a.emplace_back(300,'a');z.emplace_back(400,'z');a=z;
   assert(a.front().get_allocator()==Inner(second,OwnedCategory::values)&&a.front()==z.front());
   assert(first.total().current==baseline);
   std::vector<OwnedString,Nested> third(Nested{Outer(first,OwnedCategory::values),Inner(first,OwnedCategory::values)});
   third.emplace_back(500,'q');third.swap(a);
   assert(third.front().get_allocator()==Inner(second,OwnedCategory::values)&&a.front().get_allocator()==Inner(first,OwnedCategory::values));
  }assert(first.total().current==baseline&&second.total().current==baseline);
 }
 std::weak_ptr<Wide> survivor;
 {OwnedBudget local;auto strong=std::allocate_shared<Wide>(OwnedAllocator<Wide>(local,OwnedCategory::glyphs));survivor=strong;}
 assert(survivor.expired());survivor.reset();
 OwnedBudget::Handle retained;{OwnedBudget local;retained=local.handle();}
 {OwnedVector<int> after{OwnedAllocator<int>(retained,OwnedCategory::values)};after.push_back(3);assert(after[0]==3);}assert(retained.total().current==baseline);
 std::puts("owned allocator PASS: rebind, nodes/buckets/strings, old/new, weak lifetime, failure recovery");
}
