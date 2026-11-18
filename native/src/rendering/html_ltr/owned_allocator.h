#pragma once
#include "owned_budget.h"
#include <memory>
#include <string>
#include <vector>

namespace aether::rendering::html_ltr {
// Scoped to interaction-owned storage. Nothing replaces the global allocator or
// the default allocators/layouts used by legacy engine objects.
template<class T> class OwnedAllocator {
 template<class> friend class OwnedAllocator;
 OwnedBudget::Handle domain_;
 OwnedCategory category_{OwnedCategory::temporary};
 struct Header {
  OwnedBudget::Reservation reservation;
  explicit Header(OwnedBudget::Reservation r)noexcept:reservation(std::move(r)){}
 };
 static constexpr std::size_t alignment=alignof(T)>alignof(Header)?alignof(T):alignof(Header);
 static constexpr std::size_t offset=(sizeof(Header)+alignment-1)/alignment*alignment;
public:
 using value_type=T;
 using propagate_on_container_copy_assignment=std::true_type;
 using propagate_on_container_move_assignment=std::true_type;
 using propagate_on_container_swap=std::true_type;
 using is_always_equal=std::false_type;
 template<class U> struct rebind{using other=OwnedAllocator<U>;};
 // Empty allocators reject nonempty allocation, never allocate untracked.
 OwnedAllocator()noexcept=default;
 OwnedAllocator(OwnedBudget& budget,OwnedCategory c)noexcept:domain_(budget.handle()),category_(c){}
 OwnedAllocator(OwnedBudget::Handle domain,OwnedCategory c)noexcept:domain_(std::move(domain)),category_(c){}
 template<class U> OwnedAllocator(const OwnedAllocator<U>& a)noexcept:domain_(a.domain_),category_(a.category_){}
 T* allocate(std::size_t n){
  if(!n)return nullptr;
  const auto payload=OwnedBudget::multiply(n,sizeof(T));
  if(payload>std::numeric_limits<std::size_t>::max()-offset)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  auto reservation=domain_.reserve(category_,payload+offset);
  void* block=::operator new(payload+offset,std::align_val_t(alignment));
  new(block) Header(std::move(reservation));
  return reinterpret_cast<T*>(static_cast<std::byte*>(block)+offset);
 }
 void deallocate(T* p,std::size_t)noexcept{
  if(!p)return;
  auto* block=reinterpret_cast<std::byte*>(p)-offset;
  auto* header=reinterpret_cast<Header*>(block);
  auto reservation=std::move(header->reservation);
  header->~Header();
  ::operator delete(block,std::align_val_t(alignment));
  // reservation dies only after payload/control-block storage is deallocated,
  // including a shared_ptr control block retained by weak_ptr references.
 }
 template<class U> bool operator==(const OwnedAllocator<U>& a)const noexcept{return domain_.same_domain(a.domain_)&&category_==a.category_;}
 template<class U> bool operator!=(const OwnedAllocator<U>& a)const noexcept{return !(*this==a);}
 const OwnedBudget::Handle& domain()const noexcept{return domain_;}
 OwnedCategory category()const noexcept{return category_;}
 static constexpr std::size_t allocation_overhead()noexcept{return offset;}
};
template<class T> using OwnedVector=std::vector<T,OwnedAllocator<T>>;
using OwnedString=std::basic_string<char,std::char_traits<char>,OwnedAllocator<char>>;
}
