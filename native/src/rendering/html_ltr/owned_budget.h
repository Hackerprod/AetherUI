#pragma once
#include "admission.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace aether::rendering::html_ltr {
// Opt-in interaction storage only. Legacy one-shot accounting is unchanged.
// This ledger measures owned allocation requests, not allocator overhead/RSS.
// Reservations retain the private ledger for their full storage lifetime.
enum class OwnedCategory : std::size_t {
 inputs, document_style, values, glyphs, layout, paint, frame, temporary, count
};
class OwnedBudget final {
 friend struct OwnedBudgetTestAccess;
public:
 static constexpr std::size_t limit_bytes = 256u * 1024u * 1024u;
 struct Usage { std::size_t current{}, peak{}; };
private:
 struct State {
  std::size_t references{1}, limit{};
  Usage total{};
  std::array<Usage,static_cast<std::size_t>(OwnedCategory::count)> categories{};
 };
 // Owner-thread intrusive lifetime: reservations keep the domain alive after
 // the session's budget handle dies. No separately allocated control block.
 static void unref(State* s)noexcept{if(s&&!--s->references)delete s;}
public:
 class Reservation {
  friend class OwnedBudget;
  State* state_{};
  OwnedCategory category_{};
  std::size_t bytes_{};
  Reservation(State* s, OwnedCategory c, std::size_t n) noexcept : state_(s), category_(c), bytes_(n) {++s->references;}
 public:
  Reservation() noexcept = default;
  Reservation(const Reservation&) = delete;
  Reservation& operator=(const Reservation&) = delete;
  Reservation(Reservation&& o) noexcept { swap(o); }
  Reservation& operator=(Reservation&& o) noexcept { if(this!=&o){reset();swap(o);}return *this; }
  ~Reservation(){reset();}
  void swap(Reservation& o) noexcept {std::swap(state_,o.state_);std::swap(category_,o.category_);std::swap(bytes_,o.bytes_);}
  void reset() noexcept {if(state_){state_->total.current-=bytes_;state_->categories[static_cast<std::size_t>(category_)].current-=bytes_;unref(state_);state_=nullptr;bytes_=0;}}
  bool shrink(std::size_t new_bytes) noexcept {
   if(new_bytes>bytes_)return false;
   if(state_){const auto released=bytes_-new_bytes;state_->total.current-=released;state_->categories[static_cast<std::size_t>(category_)].current-=released;}
   bytes_=new_bytes;return true;
  }
  std::size_t bytes() const noexcept{return bytes_;}
 };
private:
 static Reservation acquire(State* state,OwnedCategory category,std::size_t bytes) {
  if(!state)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  auto i=static_cast<std::size_t>(category);
  if(i>=state->categories.size())throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  if(bytes>state->limit-state->total.current)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  if(state->references==std::numeric_limits<std::size_t>::max())throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  state->total.current+=bytes;auto& c=state->categories[i];c.current+=bytes;
  if(state->total.peak<state->total.current)state->total.peak=state->total.current;
  if(c.peak<c.current)c.peak=c.current;
  return Reservation(state,category,bytes);
 }
public:
 // A copyable allocator-domain reference, not another allocation or budget.
 class Handle final {
  friend class OwnedBudget;
  State* state_{};
  explicit Handle(State* s)noexcept:state_(s){if(state_)++state_->references;}
 public:
  Handle()noexcept=default;
  Handle(const Handle& h)noexcept:Handle(h.state_){}
  Handle(Handle&& h)noexcept:state_(std::exchange(h.state_,nullptr)){}
  Handle& operator=(Handle h)noexcept{swap(h);return *this;}
  ~Handle(){unref(state_);}
  void swap(Handle& h)noexcept{std::swap(state_,h.state_);}
  Reservation reserve(OwnedCategory c,std::size_t n)const{return acquire(state_,c,n);}
  bool same_domain(const Handle& h)const noexcept{return state_==h.state_;}
  explicit operator bool()const noexcept{return state_!=nullptr;}
  Usage total()const noexcept{return state_?state_->total:Usage{};}
 };
 Handle handle()const noexcept{return Handle(state_);}
 explicit OwnedBudget(std::size_t limit=limit_bytes) {
  if(limit>limit_bytes)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  if(limit<sizeof(State))throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  // Reserve the known bookkeeping allocation before asking the allocator.
  state_=new State;
  state_->limit=limit;
  state_->total={sizeof(State),sizeof(State)};
  state_->categories[static_cast<std::size_t>(OwnedCategory::temporary)]=state_->total;
 }
 // A lightweight owner of the SAME domain, for a session allocated through
 // its initial stack-created budget. No second ledger or storage allowance.
 explicit OwnedBudget(const Handle& domain):state_(domain.state_) {
  if(!state_)throw Error{AETHER_ERROR_INVALID_ARGUMENT};
  if(state_->references==std::numeric_limits<std::size_t>::max())throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  ++state_->references;
 }
 ~OwnedBudget(){unref(state_);}
 OwnedBudget(const OwnedBudget&)=delete;
 OwnedBudget& operator=(const OwnedBudget&)=delete;
 Reservation reserve(OwnedCategory category,std::size_t bytes) {return acquire(state_,category,bytes);}
 static std::size_t multiply(std::size_t count,std::size_t size){
  if(size&&count>std::numeric_limits<std::size_t>::max()/size)throw Error{AETHER_ERROR_LIMIT_EXCEEDED};
  return count*size;
 }
 Usage total()const noexcept{return state_->total;}
 Usage usage(OwnedCategory c)const {auto i=static_cast<std::size_t>(c);if(i>=state_->categories.size())throw Error{AETHER_ERROR_INVALID_ARGUMENT};return state_->categories[i];}
 std::size_t limit()const noexcept{return state_->limit;}
 std::size_t remaining()const noexcept{return state_->limit-state_->total.current;}
 static constexpr std::size_t bookkeeping_bytes()noexcept{return sizeof(State);}
private:
 State* state_{};
};

// Exact-capacity storage for trivial native payloads. No allocator propagation,
// global allocation hook or PMR changes to unrelated library containers.
// Allocator metadata and rounding are outside logical owned-byte accounting.
template<class T> class OwnedBuffer final {
 static_assert(std::is_trivial_v<T> && std::is_trivially_copyable_v<T>);
 OwnedBudget::Reservation reservation_; // destroyed after storage
 std::unique_ptr<T[]> storage_;
 std::size_t capacity_{};
public:
 using AllocationHook=void(*)();
 OwnedBuffer()noexcept=default;
 OwnedBuffer(const OwnedBuffer&)=delete;
 OwnedBuffer& operator=(const OwnedBuffer&)=delete;
 OwnedBuffer(OwnedBuffer&& o)noexcept{swap(o);}
 OwnedBuffer& operator=(OwnedBuffer&& o)noexcept {if(this!=&o){OwnedBuffer old;old.swap(*this);swap(o);}return *this;}
 void swap(OwnedBuffer& o)noexcept{reservation_.swap(o.reservation_);storage_.swap(o.storage_);std::swap(capacity_,o.capacity_);}
 void allocate(OwnedBudget& budget,OwnedCategory category,std::size_t count,AllocationHook hook=nullptr){
  OwnedBuffer candidate;
  if(count){
   candidate.reservation_=budget.reserve(category,OwnedBudget::multiply(count,sizeof(T)));
   if(hook)hook();
   candidate.storage_.reset(new T[count]{});
   candidate.capacity_=count;
  }
  swap(candidate); // old and new remain charged until old storage is released
 }
 T* data()noexcept{return storage_.get();}
 const T* data()const noexcept{return storage_.get();}
 std::size_t capacity()const noexcept{return capacity_;}
 void reset()noexcept {OwnedBuffer empty;swap(empty);}
};
}
