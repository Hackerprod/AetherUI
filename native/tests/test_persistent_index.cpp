#include "frozen_backing_fault_support.h"
#include "css/persistent_index.h"
#include <concepts>
#include <cstdio>
#include <map>
#include <random>
#include <type_traits>
using namespace aether::style;
static void need(bool value,const char* why) {
    if (!value) { std::fprintf(stderr,"FAIL persistent index: %s\n",why); std::abort(); }
}
using Index=PersistentIndex<uint64_t>;
static void audit(const Index& index,const std::map<uintptr_t,uint64_t>& expected) {
    need(index.size()==expected.size() && index.empty()==expected.empty(),"count");
    auto ordered=expected.begin();
    for (const auto& [key,value] : index.entries()) {
        need(ordered!=expected.end() && ordered->first==key && ordered->second==value,"ordered contents");
        need(index.find(key) && *index.find(key)==value,"find matches iteration");
        ++ordered;
    }
    need(ordered==expected.end(),"complete enumeration");
}
struct ImmutableValue {
    const int value;
    ImmutableValue()=delete;
    explicit ImmutableValue(int v) : value(v) {}
    ImmutableValue(const ImmutableValue&)=default;
    ImmutableValue& operator=(const ImmutableValue&)=delete;
};
struct alignas(64) AlignedValue { const int value; };
int main() {
    static_assert(std::forward_iterator<Index::ConstIterator>);
    static_assert(std::is_same_v<decltype(std::declval<const Index&>().find(0)),const uint64_t*>);
    constexpr auto max=std::numeric_limits<uintptr_t>::max();
    std::vector<Index::Entry> input{{max,77},{0,88},{1,99}};
    auto boundaries=Index::build(input);
    need(boundaries.size()==3 && *boundaries.find(0)==88 && *boundaries.find(max)==77,"zero and max keys");
    input[0].second=0;
    need(*boundaries.find(max)==77,"import owns copied values");
    bool duplicate=false;
    try { (void)Index::build({{0,1},{0,2}}); } catch (const std::invalid_argument&) { duplicate=true; }
    need(duplicate,"duplicate zero rejected");
    auto immutable=PersistentIndex<ImmutableValue>::build({{2,ImmutableValue{8}},{0,ImmutableValue{9}}});
    auto immutable2=immutable.set(0,ImmutableValue{10});
    need(immutable.find(0)->value==9 && immutable2.find(0)->value==10,"nonassignable nondefault value");

    std::map<uintptr_t,uint64_t> expected;
    std::vector<Index::Entry> initial;
    expected.emplace(0,1);expected.emplace(max,2);
    for (unsigned bit=0;bit<std::numeric_limits<uintptr_t>::digits;++bit) expected.emplace(uintptr_t(1)<<bit,bit+3);
    for (const auto& [key,value]:expected) initial.emplace_back(key,value);
    auto current=Index::build(initial);
    const auto baseline=current; const auto baseline_expected=expected;
    audit(current,expected);
    // The zero path exercises the maximum possible pending-iterator depth.
    fault::arm(fault::Mode::cpp,0); audit(current,expected); fault::disarm();
    need(!fault::hit&&fault::calls==0,"maximum-depth read allocates zero");

    std::mt19937_64 random(0xA37E507);
    std::vector<std::pair<Index,std::map<uintptr_t,uint64_t>>> snapshots;
    for (int step=0;step<12000;++step) {
        uintptr_t key=uintptr_t(random()%2048);
        if (step%1000==0) key=max;
        const uint64_t value=random();
        if (random()%4) { current=current.set(key,value);expected.insert_or_assign(key,value); }
        else { current=current.erase(key);expected.erase(key); }
        if (step%200==0) {audit(current,expected);audit(baseline,baseline_expected);}
        if (step%2000==0) snapshots.emplace_back(current,expected);
    }
    audit(current,expected);
    for (const auto& [index,oracle]:snapshots) audit(index,oracle);
    fault::arm(fault::Mode::cpp,0);
    for (int i=0;i<10;++i) audit(current,expected);
    fault::disarm();need(!fault::hit&&fault::calls==0,"all reads allocate zero");
    // Missing erase is allocation-free even with failure armed.
    const uintptr_t missing=max-7;
    need(!current.find(missing),"missing fixture");
    fault::arm(fault::Mode::cpp,0);auto no_op=current.erase(missing);fault::disarm();
    need(!fault::hit&&fault::calls==0,"absent erase allocation-free");audit(no_op,expected);

    auto sweep=[&](const char* label,auto operation,size_t bound) {
        bool terminal=false;
        for (long ordinal=0;ordinal<10000;++ordinal) {
            fault::arm(fault::Mode::cpp,ordinal);
            try {
                {auto result=operation();need(!fault::hit,"failure never swallowed");}
                terminal=true;
            } catch (const std::bad_alloc&) {need(fault::hit,"only injected failure");}
            need(fault::cpp_alloc==fault::cpp_free,"fault sweep allocation balanced");
            fault::disarm();audit(current,expected);
            if (terminal) {
                need(static_cast<size_t>(ordinal)<=bound,"bounded copied routes");
                std::printf("persistent index %s allocation sites=%ld\n",label,ordinal);break;
            }
        }
        need(terminal,"fault sweep terminates");
    };
    const auto existing=expected.begin()->first;
    sweep("replace",[&]{return current.set(existing,17);},std::numeric_limits<uintptr_t>::digits+1);
    sweep("insert",[&]{return current.set(missing,18);},std::numeric_limits<uintptr_t>::digits+2);
    sweep("remove",[&]{return current.erase(existing);},std::numeric_limits<uintptr_t>::digits);
    sweep("full import",[&]{return Index::build(initial);},2*initial.size()+2);

    // Over-aligned values use aligned allocation and retain the same atomic
    // publication contract; cover that distinct failure family explicitly.
    auto aligned=PersistentIndex<AlignedValue>::build({{0,AlignedValue{3}},{max,AlignedValue{4}}});
    bool aligned_terminal=false;
    for (long ordinal=0;ordinal<10;++ordinal) {
        fault::arm(fault::Mode::aligned,ordinal);
        try { {auto next=aligned.set(0,AlignedValue{5});need(next.find(0)->value==5,"aligned replacement");} aligned_terminal=true; }
        catch (const std::bad_alloc&) {need(fault::hit,"aligned injected failure");}
        need(fault::cpp_alloc==fault::cpp_free,"aligned allocation sweep balanced");fault::disarm();
        need(aligned.find(0)->value==3,"aligned original unchanged");
        if (aligned_terminal) {need(ordinal==2,"aligned leaf and branch covered");break;}
    }
    need(aligned_terminal,"aligned sweep terminates");

    PersistentIndex<std::shared_ptr<const int>> history;
    auto first=std::make_shared<const int>(42);std::weak_ptr<const int> weak_first=first;
    history=history.set(0,first);first.reset();
    for (int i=0;i<3000;++i) {
        std::weak_ptr<const int> previous=*history.find(0);
        history=history.set(0,std::make_shared<const int>(i));
        need(previous.expired(),"replacement retains no history");
    }
    need(weak_first.expired(),"first historical value released");
    auto retained=history;const auto last=*history.find(0);history=history.erase(0);
    need(history.empty()&&!history.find(0)&&retained.find(0)&&*retained.find(0)==last,"last erase preserves retained snapshot");
    need(history.entries().begin()==history.entries().end(),"empty iteration");
    std::puts("PASS persistent index: full key domain, immutable values, differential, snapshots, reads, fault and history");
}
