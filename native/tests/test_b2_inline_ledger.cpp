#define AETHER_HTML_LTR_TESTING
#include "../src/layout/b2_inline_ledger.h"
#include <cassert>
#include <iostream>
#include <memory>
using namespace aether::layout::b2;
template<class E,class F> void rejects(F f){bool thrown=false;try{f();}catch(const E&){thrown=true;}assert(thrown);}
static std::unique_ptr<Ledger> fixture(){
 auto l=std::make_unique<Ledger>();auto c=l->context(1,0,0,0);
 l->origin(10,1,2,3,0,Kind::text,c,"foo");l->origin(11,1,2,3,1,Kind::inline_node,c);l->origin(12,1,2,3,2,Kind::text,c,"foo");l->origin(13,1,2,3,3,Kind::br,c);
 l->freeze();auto line=l->line(c,15,0,60);
 auto a=l->placement(c,line,0,0,none,1,2,3,0,0,30,20,15,3,"foo");auto b=l->placement(c,line,0,1,none,1,2,3,30,0,30,20,15,3,"foo");
 l->span({0,0,3,Fate::visible,a,none,none,0,3,0,0});l->span({2,0,3,Fate::visible,b,none,none,0,3,0,1});l->forced_break(3,line);return l;
}
// Expected canonical IDs are explicit fixture data, not inferred from the
// placement being validated. The second C origin intentionally has another
// authentic owner/style/font; only the first contributor owns the single SP.
static std::unique_ptr<Ledger> collapse_fixture(){
 auto l=std::make_unique<Ledger>();auto c=l->context(1,0,0,0);
 l->origin(9,1,2,3,0,Kind::text,c,"a");l->origin(10,1,2,3,1,Kind::text,c," \t");l->origin(11,4,5,6,2,Kind::text,c,"\r\n\f");l->origin(12,1,2,3,3,Kind::text,c,"b");l->freeze();auto line=l->line(c,15,0,15);
 auto a=l->placement(c,line,0,0,none,1,2,3,0,0,5,20,15,1,"a");auto p=l->placement(c,line,1,1,7,1,2,3,5,0,5,20,15,1," ");auto b=l->placement(c,line,2,2,none,1,2,3,10,0,5,20,15,1,"b");
 l->span({0,0,1,Fate::visible,a,none,none,0,1,0,0});l->span({1,0,2,Fate::contributor,p,7,none,0,1,1,1});l->span({2,0,3,Fate::contributor,p,7,none,0,1,1,1});l->span({3,0,1,Fate::visible,b,none,none,0,1,2,2});return l;
}
struct FailResource:std::pmr::memory_resource{
 bool fail=true;void* do_allocate(size_t n,size_t a)override{if(fail)throw std::bad_alloc();return std::pmr::new_delete_resource()->allocate(n,a);}void do_deallocate(void*p,size_t n,size_t a)override{std::pmr::new_delete_resource()->deallocate(p,n,a);}bool do_is_equal(const std::pmr::memory_resource&o)const noexcept override{return this==&o;}
};
int main(){
 for(size_t q=0;q<size_t(Quota::count);++q){Counters c;c.charge(Quota(q),0);assert(c.used(Quota(q))==0);c.charge(Quota(q),limits[q]);assert(c.used(Quota(q))==limits[q]);rejects<LimitExceeded>([&]{c.charge(Quota(q));});assert(c.used(Quota(q))==limits[q]);}
 assert(checked_add(0,0)==0&&checked_mul(0,SIZE_MAX)==0);rejects<LimitExceeded>([]{checked_add(SIZE_MAX,1);});rejects<LimitExceeded>([]{checked_mul(SIZE_MAX,2);});
 {MemoryBudget m(16);auto zero=m.allocate(0);assert(m.live()==0);m.deallocate(zero,0);volatile size_t huge=SIZE_MAX;rejects<LimitExceeded>([&]{(void)m.allocate(huge);});auto p=m.allocate(16);assert(m.live()==16);rejects<LimitExceeded>([&]{(void)m.allocate(1);});m.deallocate(p,16);assert(m.live()==0&&m.peak()==16);}
 {MemoryBudget m(16);m.reserve_fixed(8);assert(m.live()==8&&m.peak()==8);auto p=m.allocate(8);assert(m.live()==16);rejects<LimitExceeded>([&]{m.reserve_fixed(1);});assert(m.live()==16);m.deallocate(p,8);assert(m.live()==8&&m.peak()==16);m.reserve_fixed(8);rejects<LimitExceeded>([&]{(void)m.allocate(1);});assert(m.live()==16);}
 {FailResource f;MemoryBudget m(16,&f);rejects<std::bad_alloc>([&]{(void)m.allocate(8);});assert(m.live()==0);f.fail=false;auto p=m.allocate(8);m.deallocate(p,8);assert(m.live()==0);}
 assert(scalar_count("\xc2\xbfQu\xc3\xa9\xc2\xa0tal?")==9);rejects<InvalidLedger>([]{scalar_count("\xc0\xaf");});rejects<InvalidLedger>([]{scalar_count("\xed\xa0\x80");});
 fixture()->validate();
 {auto l=fixture();l->test_spans()[0].expected_token=none;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_spans()[0].expected_fragment=none;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_spans()[1].origin=0;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_spans()[0].end=2;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_spans()[0].begin=1;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_breaks().clear();rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->forced_break(3,0);rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_participants().erase(l->test_participants().begin()+1);rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_placements()[0].style=999;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_placements()[0].baseline=16;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();l->test_placements()[1].fragment=0;rejects<InvalidLedger>([&]{l->validate();});}
 {Ledger l;auto c=l.context(1,0,0,0);l.origin(10,1,2,3,0,Kind::text,c,"\xc3\xa9");l.freeze();auto a=l.line(c,15,0,10);auto p=l.placement(c,a,0,0,none,1,2,3,0,0,10,20,15,1,"\xc3\xa9");l.span({0,0,1,Fate::visible,p,none,none,0,1,0,0});rejects<InvalidLedger>([&]{l.validate();});}
 // Multi-origin collapse owns one SP, not one per style or origin.
 collapse_fixture()->validate();
 {auto l=collapse_fixture();l->test_spans()[2].group=8;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_spans()[1].expected_token=none;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_spans()[1].expected_fragment=none;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_placements()[1].token=0;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_placements()[1].fragment=99999;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_placements()[1].owner=l->origins()[2].owner;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_placements()[1].style=l->origins()[2].style;rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=collapse_fixture();l->test_placements()[1].font=l->origins()[2].font;rejects<InvalidLedger>([&]{l->validate();});}

 {Ledger l;auto c=l.context(1,0,0,0);l.origin(10,1,2,3,0,Kind::text,c," \t");l.freeze();auto d=l.decision(c,none,Discard::empty_context);l.span({0,0,2,Fate::discarded,none,none,d});l.validate();l.test_spans()[0].decision=none;rejects<InvalidLedger>([&]{l.validate();});}
 // A fabricated leading discard between visible ranges is not authorized
 // merely by supplying a decision with that label.
 {Ledger l;auto c=l.context(1,0,0,0);l.origin(10,1,2,3,0,Kind::text,c,"a b");l.freeze();auto line=l.line(c,15,0,10);auto a=l.placement(c,line,0,0,none,1,2,3,0,0,5,20,15,1,"a");auto b=l.placement(c,line,1,1,none,1,2,3,5,0,5,20,15,1,"b");auto d=l.decision(c,none,Discard::leading);l.span({0,0,1,Fate::visible,a,none,none,0,1,0,0});l.span({0,1,2,Fate::discarded,none,none,d});l.span({0,2,3,Fate::visible,b,none,none,0,1,1,1});rejects<InvalidLedger>([&]{l.validate();});}
 {auto l=fixture();std::swap(l->test_spans()[0].placement,l->test_spans()[1].placement);rejects<InvalidLedger>([&]{l->validate();});}
 {auto l=fixture();auto before=l->counters().used(Quota::token_visits);l->validate();auto after=l->counters().used(Quota::token_visits);assert(after>before);l->validate();assert(l->counters().used(Quota::token_visits)>after);}
 std::cout<<"B2 ledger standalone checks PASS (not integration gate)\n";
}
