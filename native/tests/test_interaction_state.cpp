#include "rendering/html_ltr/interaction_state.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <thread>
#include <vector>
using namespace aether::rendering::html_ltr;
// Fault replacement belongs only to this unit executable, not production.
static long fail_at=-1,allocation_index=0;
void* operator new(std::size_t n,std::align_val_t align){if(fail_at>=0&&allocation_index++==fail_at)throw std::bad_alloc{};void* p=nullptr;if(posix_memalign(&p,static_cast<std::size_t>(align),n?n:1))throw std::bad_alloc{};return p;}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
namespace aether::rendering::html_ltr {struct InteractionStateTestAccess {static void version(InteractionStateMachine& machine,std::uint64_t value){machine.data_.metadata.state_version=value;}};}
struct Validator final:NativeStateValidator {
 std::size_t calls{};bool reject{},oom{};
 void validate(const InteractionCandidateView& view)override{++calls;if(reject)throw Error{AETHER_ERROR_UNSUPPORTED};if(oom)throw std::bad_alloc{};assert(view.presentations.size()<=32);for(const auto& p:view.presentations)if(p.kind==InteractionControlKind::password&&!p.shows_placeholder){assert(p.text.size()%3==0);for(std::size_t i=0;i<p.text.size();i+=3)assert(std::string_view(p.text.data()+i,3)=="\xe2\x80\xa2");}}
};
static constexpr InteractionControlSeed seeds[]{{InteractionControlKind::text,"","Text"},{InteractionControlKind::password,"","Password"},{InteractionControlKind::button,"",""}};
static InteractionEvent key(InteractionKey k,bool down=true,std::uint32_t modifiers=0){return {static_cast<std::uint32_t>(down?InteractionEventKind::key_down:InteractionEventKind::key_up),static_cast<std::uint32_t>(k),modifiers};}
static InteractionEvent kind(InteractionEventKind k){InteractionEvent e;e.kind=static_cast<std::uint32_t>(k);return e;}
static InteractionEvent text(std::string_view s){auto e=kind(InteractionEventKind::text);e.text=reinterpret_cast<const std::uint8_t*>(s.data());e.text_length=s.size();return e;}
static InteractionEvent pointer(bool down){auto e=kind(down?InteractionEventKind::pointer_down:InteractionEventKind::pointer_up);e.x=10;e.y=10;return e;}
static InteractionStateResult send(InteractionStateMachine& m,Validator& v,InteractionEvent e,std::uint32_t hit=0,bool current=true){return m.dispatch(e,{current,hit},v);}
template<class F> static void error(int expected,F f){bool caught=false;try{f();}catch(const Error& e){caught=true;assert(e.status==expected);}assert(caught);}
static void focus(InteractionStateMachine& m,Validator& v,unsigned id){send(m,v,kind(InteractionEventKind::blur));for(unsigned i=0;i<id;++i)send(m,v,key(InteractionKey::tab));}
static std::string_view shown(const InteractionStateMachine& m,std::size_t i){const auto& s=m.candidate_view().presentations[i].text;return {s.data(),s.size()};}
static void transactional_errors(){
 OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);
 const auto check=[&](InteractionEvent e,int code,unsigned hit=0,bool frame=true){const auto old=m.metadata();const auto calls=v.calls;error(code,[&]{send(m,v,e,hit,frame);});assert(m.metadata()==old&&v.calls==calls);};
 check(pointer(true),AETHER_ERROR_INVALID_STATE,1,false);
 auto e=pointer(true);e.x=std::numeric_limits<float>::quiet_NaN();check(e,AETHER_ERROR_INVALID_ARGUMENT);e=pointer(true);e.y=std::numeric_limits<float>::infinity();check(e,AETHER_ERROR_INVALID_ARGUMENT);e=pointer(true);e.key=1;check(e,AETHER_ERROR_INVALID_ARGUMENT);e=key(InteractionKey::enter);e.modifiers=1;check(e,AETHER_ERROR_INVALID_ARGUMENT);e.modifiers=2;check(e,AETHER_ERROR_UNSUPPORTED);e=key(InteractionKey::enter);e.key=99;check(e,AETHER_ERROR_UNSUPPORTED);e=kind(InteractionEventKind::blur);e.text=reinterpret_cast<const std::uint8_t*>("x");check(e,AETHER_ERROR_INVALID_ARGUMENT);e=kind(InteractionEventKind::blur);e.kind=99;check(e,AETHER_ERROR_UNSUPPORTED);
 for(unsigned f=0;f<4;++f){focus(m,v,f);check(text(std::string_view("\0",1)),AETHER_ERROR_INVALID_ARGUMENT);check(text("\xc0\xaf"),AETHER_ERROR_INVALID_ARGUMENT);check(text("\xed\xa0\x80"),AETHER_ERROR_INVALID_ARGUMENT);check(text("\xe2\x80\xa2"),AETHER_ERROR_UNSUPPORTED);check(text("\xc2\xad"),AETHER_ERROR_UNSUPPORTED);check(text("\xe2\x80\xa2\xc2"),AETHER_ERROR_INVALID_ARGUMENT);std::string too_many(257,'x');check(text(too_many),AETHER_ERROR_LIMIT_EXCEEDED);std::string large(1025,'x');large.back()=char(0xc2);check(text(large),AETHER_ERROR_LIMIT_EXCEEDED);}
 focus(m,v,1);const auto old=m.metadata();v.reject=true;error(AETHER_ERROR_UNSUPPORTED,[&]{send(m,v,text("literal"));});v.reject=false;assert(m.metadata()==old&&shown(m,0)=="Text");
 v.oom=true;bool caught=false;try{send(m,v,key(InteractionKey::tab));}catch(const std::bad_alloc&){caught=true;}v.oom=false;assert(caught&&m.metadata()==old);
 bool wrong_thread=false;std::thread t([&]{try{m.metadata();}catch(const Error& err){wrong_thread=err.status==AETHER_ERROR_INVALID_STATE;}});t.join();assert(wrong_thread);
}
static void editing(){
 OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);
 assert(m.metadata().state_version==1&&m.metadata().focus_id==0&&shown(m,0)=="Text");assert(!send(m,v,text("abc")).changed);
 focus(m,v,1);auto before=m.metadata().state_version;assert(send(m,v,text("&lt;<input>\"'\xc3\xa9")).changed);assert(m.metadata().state_version==before+1&&shown(m,0)=="&lt;<input>\"'\xc3\xa9");assert(m.control_count()==3);send(m,v,key(InteractionKey::backspace));assert(shown(m,0)=="&lt;<input>\"'");
 focus(m,v,2);send(m,v,text("fake\xc3\xa9"));assert(shown(m,1)==std::string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2"));for(int i=0;i<5;++i)send(m,v,key(InteractionKey::backspace));assert(shown(m,1)=="Password");assert(!send(m,v,key(InteractionKey::backspace)).changed);
 std::string full(256,'x');send(m,v,text(full));const auto old=m.metadata();error(AETHER_ERROR_LIMIT_EXCEEDED,[&]{send(m,v,text("a"));});assert(m.metadata()==old&&shown(m,1).size()==768);send(m,v,key(InteractionKey::backspace));send(m,v,text("\xc3\xbf"));assert(shown(m,1).size()==768);
 // Entire scalar validation occurs before the 256 scalar decision.
 focus(m,v,0);std::string bad(257,'x');bad+=char(0xc2);error(AETHER_ERROR_INVALID_ARGUMENT,[&]{send(m,v,text(bad));});
}
static void aggregate(){
 OwnedBudget b;Validator v;constexpr InteractionControlSeed s[]{{InteractionControlKind::text,"",""},{InteractionControlKind::password,"",""}};
 InteractionStateMachine m(b,s,4094,100,80,v);focus(m,v,1);send(m,v,text("x"));focus(m,v,2);send(m,v,text("y"));const auto old=m.metadata();error(AETHER_ERROR_LIMIT_EXCEEDED,[&]{send(m,v,text("z"));});assert(m.metadata()==old);send(m,v,key(InteractionKey::backspace));focus(m,v,1);send(m,v,text("z"));assert(shown(m,0)=="xz");
}
static void gestures(){
 OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);
 send(m,v,key(InteractionKey::tab,true,interaction_shift));assert(m.metadata().focus_id==3);
 assert(send(m,v,key(InteractionKey::enter)).activated_control_id==3);assert(!send(m,v,key(InteractionKey::enter)).changed);send(m,v,key(InteractionKey::tab));assert(!send(m,v,key(InteractionKey::enter)).changed);send(m,v,key(InteractionKey::enter,false));
 focus(m,v,3);send(m,v,key(InteractionKey::space));assert(m.metadata().arm==InteractionArm::space);send(m,v,pointer(true),3);assert(m.metadata().arm==InteractionArm::pointer);assert(!send(m,v,key(InteractionKey::space,false)).activated_control_id);assert(m.metadata().arm==InteractionArm::pointer);assert(send(m,v,pointer(false),3).activated_control_id==3);
 send(m,v,pointer(true),3);send(m,v,key(InteractionKey::space));assert(send(m,v,key(InteractionKey::enter)).activated_control_id==3);assert(send(m,v,pointer(false),3).activated_control_id==3);assert(!send(m,v,key(InteractionKey::space,false)).activated_control_id);send(m,v,key(InteractionKey::enter,false));
 send(m,v,pointer(true),0);assert(!send(m,v,pointer(false),3).activated_control_id);send(m,v,pointer(true),3);assert(!send(m,v,pointer(false),0).activated_control_id);
 send(m,v,pointer(true),3);send(m,v,key(InteractionKey::tab));const auto armed=m.metadata();error(AETHER_ERROR_INVALID_STATE,[&]{send(m,v,pointer(false),3,false);});assert(m.metadata()==armed&&m.metadata().pointer_primary_down);assert(!send(m,v,pointer(false),3).activated_control_id);
 send(m,v,pointer(true),3);send(m,v,kind(InteractionEventKind::pointer_cancel));assert(!send(m,v,pointer(false),3).changed);
 focus(m,v,3);send(m,v,key(InteractionKey::space));send(m,v,key(InteractionKey::tab));assert(!send(m,v,key(InteractionKey::space)).changed);assert(!send(m,v,key(InteractionKey::space,false)).activated_control_id);
 focus(m,v,3);send(m,v,key(InteractionKey::space));const auto old=m.metadata();v.oom=true;bool fail=false;try{send(m,v,key(InteractionKey::space,false));}catch(const std::bad_alloc&){fail=true;}v.oom=false;assert(fail&&m.metadata()==old);assert(send(m,v,key(InteractionKey::space,false)).activated_control_id==3);
 send(m,v,pointer(true),3);const auto down=m.metadata();v.reject=true;error(AETHER_ERROR_UNSUPPORTED,[&]{send(m,v,pointer(false),3);});v.reject=false;assert(m.metadata()==down);assert(send(m,v,pointer(false),3).activated_control_id==3);
}
static void resizing_exhaustion(){
 OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);assert(!m.resize(100,80,v).changed);
 send(m,v,pointer(true),3);m.resize(0,80,v);assert(!m.metadata().pointer_primary_down&&m.metadata().arm==InteractionArm::none&&m.metadata().focus_id==3);error(AETHER_ERROR_INVALID_STATE,[&]{send(m,v,pointer(true),3);});assert(send(m,v,key(InteractionKey::enter)).activated_control_id==3);send(m,v,key(InteractionKey::enter,false));send(m,v,key(InteractionKey::space));m.resize(100,80,v);assert(m.metadata().arm==InteractionArm::space);assert(send(m,v,key(InteractionKey::space,false)).activated_control_id==3);
 const auto old=m.metadata();error(AETHER_ERROR_INVALID_ARGUMENT,[&]{m.resize(8192,8192,v);});assert(m.metadata()==old);
 InteractionStateTestAccess::version(m,UINT64_MAX);assert(!m.resize(100,80,v).changed);assert(!send(m,v,key(InteractionKey::tab,false)).changed);error(AETHER_ERROR_EXHAUSTED,[&]{send(m,v,key(InteractionKey::tab));});assert(m.metadata().state_version==UINT64_MAX);
 OwnedBudget empty_budget;InteractionStateMachine empty(empty_budget,{},0,1,1,v);assert(!send(empty,v,key(InteractionKey::tab)).changed);assert(!send(empty,v,key(InteractionKey::tab,true,1)).changed);
}
// Independent POD transition oracle for every reachable metadata combination.
using Signature=std::tuple<unsigned,unsigned,unsigned,unsigned,bool>;
static Signature signature(const InteractionMetadata& m){return {m.focus_id,unsigned(m.arm),m.arm_id,m.keys_down,m.pointer_primary_down};}
struct Step {InteractionEvent event;unsigned hit;};
static std::pair<InteractionMetadata,unsigned> oracle(InteractionMetadata m,const Step& s){
 const auto old=m;unsigned active=0;const unsigned k=s.event.kind,key_value=s.event.key;const auto clear=[&]{m.arm=InteractionArm::none;m.arm_id=0;};
 if(k==1&&!m.pointer_primary_down){m.pointer_primary_down=true;m.focus_id=s.hit;clear();if(s.hit==3){m.arm=InteractionArm::pointer;m.arm_id=3;}}
 if(k==2&&m.pointer_primary_down){m.pointer_primary_down=false;if(m.arm==InteractionArm::pointer){if(s.hit==m.arm_id&&m.focus_id==m.arm_id)active=m.arm_id;clear();}}
 if(k==3){m.pointer_primary_down=false;if(m.arm==InteractionArm::pointer)clear();}
 if(k==7){m.focus_id=0;m.keys_down=0;m.pointer_primary_down=false;clear();}
 if(k==4&&key_value==1){if(s.event.modifiers)m.focus_id=m.focus_id>1?m.focus_id-1:3;else m.focus_id=m.focus_id==3?1:m.focus_id+1;clear();}
 if(k==4&&key_value==2&&!(m.keys_down&1)){m.keys_down|=1;if(m.focus_id==3)active=3;}
 if(k==5&&key_value==2)m.keys_down&=~1u;
 if(k==4&&key_value==3&&!(m.keys_down&2)){m.keys_down|=2;if(m.focus_id==3&&m.arm!=InteractionArm::pointer){m.arm=InteractionArm::space;m.arm_id=3;}}
 if(k==5&&key_value==3){m.keys_down&=~2u;if(m.arm==InteractionArm::space){if(m.focus_id==m.arm_id)active=m.arm_id;clear();}}
 if(!(m==old))++m.state_version;
 return {m,active};
}
static void reachable(){
 std::vector<Step> options;for(unsigned h=0;h<4;++h){options.push_back({pointer(true),h});options.push_back({pointer(false),h});}
 options.push_back({kind(InteractionEventKind::pointer_cancel),0});options.push_back({kind(InteractionEventKind::blur),0});
 for(unsigned k=1;k<=4;++k)for(bool down:{false,true})options.push_back({key(static_cast<InteractionKey>(k),down),0});
 options.push_back({key(InteractionKey::tab,true,1),0});options.push_back({text(""),0});
 std::vector<std::vector<Step>> paths(1);std::set<Signature> found;found.emplace(0,0,0,0,false);std::size_t edges=0;
 for(std::size_t i=0;i<paths.size();++i){for(const auto& next:options){OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);for(const auto& s:paths[i])send(m,v,s.event,s.hit);const auto before=m.metadata();const auto [expected,activation]=oracle(before,next);const auto calls=v.calls;const auto r=send(m,v,next.event,next.hit);assert(m.metadata()==expected&&r.state_version==expected.state_version&&r.changed==(before.state_version!=expected.state_version)&&r.activated_control_id==activation);assert(v.calls==calls+(r.changed?1:0));++edges;if(found.insert(signature(expected)).second){auto path=paths[i];path.push_back(next);paths.push_back(std::move(path));}}}
 std::printf("reachable metadata states=%zu event edges=%zu PASS\n",paths.size(),edges);assert(paths.size()>=40);
}
static void cycles(){
 OwnedBudget b;Validator v;const std::string baseline(255,'a');
 const InteractionControlSeed s[]{{InteractionControlKind::text,baseline,""},{InteractionControlKind::password,baseline,""},{InteractionControlKind::button,"",""}};
 InteractionStateMachine m(b,s,0,100,80,v);std::uint64_t expected=1;unsigned activations=0;std::size_t warm_max=0;
 const auto event=[&](InteractionEvent e){auto r=send(m,v,e);expected+=r.changed?1:0;activations+=r.activated_control_id?1:0;assert(m.metadata().state_version==expected);};
 const auto select=[&](unsigned id){event(kind(InteractionEventKind::blur));for(unsigned k=0;k<id;++k)event(key(InteractionKey::tab));};
 for(unsigned cycle=0;cycle<10000;++cycle){
  unsigned cp=32+(cycle%95);if(cycle%2)cp=160+(cycle%96);if(cp==173)cp=174;
  char bytes[2]{char(cp),0};std::size_t n=1;if(cp>=128){bytes[0]=char(0xc0|(cp>>6));bytes[1]=char(0x80|(cp&63));n=2;}
  for(unsigned id=1;id<=2;++id){select(id);event(text(std::string_view(bytes,n)));event(key(InteractionKey::backspace));}
  if(cycle%100==0){select(3);event(key(InteractionKey::enter));event(key(InteractionKey::enter));event(key(InteractionKey::enter,false));auto r=m.resize(101,80,v);expected+=r.changed;assert(!r.activated_control_id);r=m.resize(100,80,v);expected+=r.changed;}
  assert(shown(m,0)==baseline&&shown(m,1).size()==255*3);
  if(cycle<200)warm_max=std::max(warm_max,b.total().current);else assert(b.total().current<=warm_max);
 }
 assert(activations==100&&m.metadata().state_version==expected);
 std::printf("state-only cycles=10000 activations=%u version=%llu current=%zu warm_max=%zu PASS (no render)\n",activations,static_cast<unsigned long long>(expected),b.total().current,warm_max);
}
static void faults(){
 std::size_t creation_failures=0;
 for(long point=0;point<100;++point){OwnedBudget b;Validator v;const std::string initial(64,'x');const InteractionControlSeed s[]{{InteractionControlKind::text,initial,"placeholder longer than SSO"},{InteractionControlKind::password,initial,"password placeholder longer than SSO"}};
  const auto bytes=b.total().current;allocation_index=0;fail_at=point;bool success=false;
  try{{InteractionStateMachine m(b,s,63,100,80,v);}success=true;}catch(const std::bad_alloc&){++creation_failures;}
  fail_at=-1;assert(b.total().current==bytes);if(success)break;assert(point<99);
 }
 std::size_t resize_failures=0;
 for(long point=0;point<100;++point){OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);focus(m,v,3);send(m,v,key(InteractionKey::space));const auto before=m.metadata();const auto bytes=b.total().current;allocation_index=0;fail_at=point;bool success=false;
  try{auto r=m.resize(101,80,v);assert(r.changed&&!r.activated_control_id);success=true;}catch(const std::bad_alloc&){fail_at=-1;++resize_failures;assert(m.metadata()==before&&b.total().current==bytes);auto r=m.resize(101,80,v);assert(r.changed&&!r.activated_control_id&&m.metadata().arm==InteractionArm::space);}
  fail_at=-1;if(success)break;assert(point<99);
 }
 {OwnedBudget b;Validator v;InteractionStateMachine m(b,seeds,12,100,80,v);send(m,v,key(InteractionKey::enter));const auto calls=v.calls;allocation_index=0;fail_at=0;assert(!send(m,v,key(InteractionKey::enter)).changed);assert(!send(m,v,key(InteractionKey::tab,false)).changed);assert(!send(m,v,text("")).changed);assert(!send(m,v,text("a\xc3\xa9")).changed);assert(validate_interaction_event(text("a\xc3\xa9"))==2);assert(!m.resize(100,80,v).changed);fail_at=-1;assert(allocation_index==0&&v.calls==calls);}
 std::printf("creation failures=%zu resize failures=%zu no-op allocations=0 PASS\n",creation_failures,resize_failures);
 std::size_t exercised=0;
 for(int operation=0;operation<3;++operation){bool success=false;for(long point=0;point<100&&!success;++point){OwnedBudget b;Validator v;const std::string initial(64,'x');const InteractionControlSeed s[]{{InteractionControlKind::text,initial,"placeholder"},{InteractionControlKind::password,initial,"password"},{InteractionControlKind::button,"",""}};InteractionStateMachine m(b,s,19,100,80,v);focus(m,v,operation?3:1);if(operation==1)send(m,v,key(InteractionKey::space));if(operation==2)send(m,v,pointer(true),3);const auto before=m.metadata();const auto bytes=b.total().current;allocation_index=0;fail_at=point;try{auto r=send(m,v,operation==0?text("appended"):operation==1?key(InteractionKey::space,false):pointer(false),operation==2?3:0);fail_at=-1;assert(r.changed&&r.activated_control_id==(operation?3u:0u));success=true;}catch(const std::bad_alloc&){fail_at=-1;assert(m.metadata()==before&&b.total().current==bytes);auto r=send(m,v,operation==0?text("appended"):operation==1?key(InteractionKey::space,false):pointer(false),operation==2?3:0);assert(r.changed&&r.activated_control_id==(operation?3u:0u));++exercised;}}assert(success);}
 std::printf("state allocation failure points=%zu rollback/retry PASS\n",exercised);assert(exercised>10);
 // A quota failure after construction preserves accepted text and metadata.
 Validator v;const std::string initial(64,'a');const InteractionControlSeed s[]{{InteractionControlKind::text,initial,""}};
 OwnedBudget roomy;std::size_t needed=0;{InteractionStateMachine m(roomy,s,0,100,80,v);needed=roomy.total().current;}
 OwnedBudget tight(needed+16);InteractionStateMachine m(tight,s,0,100,80,v);const auto before=m.metadata();const auto calls=v.calls;error(AETHER_ERROR_LIMIT_EXCEEDED,[&]{send(m,v,key(InteractionKey::tab));});assert(m.metadata()==before&&v.calls==calls);
}
int main(){transactional_errors();editing();aggregate();gestures();resizing_exhaustion();reachable();faults();cycles();std::puts("private state machine PASS; no native frame/raster or public ABI tested");}
