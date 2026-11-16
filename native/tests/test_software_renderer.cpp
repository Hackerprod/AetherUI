#include "elliptic_t5_transition.h"
#ifdef NDEBUG
#error "Assertions required"
#endif
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <limits>
#include <new>
#include <thread>
#include <filesystem>
#include <system_error>
using namespace aether::rendering;
namespace p=aether::rendering::preparation;
namespace s=aether::rendering::software;
p::PreparedFrame prepare(std::vector<p::Command> commands) {
 auto r=p::prepare(p::Snapshot{std::move(commands)});
 assert(std::holds_alternative<p::PreparedFrame>(r));return std::move(std::get<p::PreparedFrame>(r));
}
Frame get(FrameStore& store) {std::optional<Frame> out; assert(store.copy_confirmed(out)==FrameStatus::ok);assert(out);return *out;}
p::Fill fill(float x,float y,float w,float h,p::Color c={1,0,0,1}) {return {{x,y,w,h},{},c};}
void pixel(const Frame& f,int x,int y,std::array<int,4> expected) {
 const auto n=std::size_t(y)*f.stride+std::size_t(x)*4;
 for(int i=0;i<4;++i) assert(f.pixels[n+std::size_t(i)]==expected[std::size_t(i)]);
}
struct Cancel : p::Cancellation {int count{},at;explicit Cancel(int n):at(n){} bool cancelled() noexcept override {return ++count==at;}};
int main(int argc,char** argv) {
 if(argc!=1&&argc!=2){std::fputs("usage: renderer-test [existing-empty-evidence-directory]\n",stderr);return 2;}
 const std::string evidence_directory=argc==2?argv[1]:"";
 if(argc==2){
  // POSIX evidence spelling policy: reject before any filesystem query.
  if(evidence_directory.empty()||evidence_directory.back()=='/'||std::filesystem::path(evidence_directory).filename()=="."||std::filesystem::path(evidence_directory).filename()==".."){
   std::fputs("renderer evidence directory must exist, be real and empty\n",stderr);return 2;
  }
  std::error_code error;const auto status=std::filesystem::symlink_status(evidence_directory,error);
  bool valid=!error&&std::filesystem::is_directory(status)&&!std::filesystem::is_symlink(status);
  if(valid){const bool empty=std::filesystem::is_empty(evidence_directory,error);valid=!error&&empty;}
  if(!valid){std::fputs("renderer evidence directory must exist, be real and empty\n",stderr);return 2;}
 }
 FrameStore store;assert(store.resize({4,4,1})==FrameStatus::ok);
 auto integer=prepare({fill(1,1,2,2)});auto result=s::render(store,integer);
 assert(result.status==s::Status::ok && result.pixel_budget==20);
 auto f=get(store);pixel(f,1,1,{0,0,255,255});pixel(f,0,0,{0,0,0,0});pixel(f,3,3,{0,0,0,0});
 auto fractional=prepare({fill(.25f,.5f,1,1)});assert(s::render(store,fractional).status==s::Status::ok);
 f=get(store);pixel(f,0,0,{0,0,96,96});pixel(f,1,0,{0,0,32,32});pixel(f,0,1,{0,0,96,96});
 auto clipping=prepare({p::PushClip{{.5f,0,1,2},{}},p::PushClip{{.75f,.25f,1,2},{}},fill(.25f,0,3,3),p::PopClip{},p::PopClip{}});
 assert(s::render(store,clipping).status==s::Status::ok);f=get(store);
 pixel(f,0,0,{0,0,48,48});pixel(f,1,0,{0,0,96,96});pixel(f,2,0,{0,0,0,0});
 auto blend=prepare({fill(0,0,4,4,{1,0,0,1}),fill(0,0,1,1,{0,0,1,.5f})});
 assert(s::render(store,blend).status==s::Status::ok);f=get(store);pixel(f,0,0,{128,0,127,255});
 auto reverse=prepare({fill(0,0,1,1,{0,0,1,.5f}),fill(0,0,4,4,{1,0,0,1})});
 assert(s::render(store,reverse).status==s::Status::ok);pixel(get(store),0,0,{0,0,255,255});
 auto invisible=prepare({fill(-100,-100,1,1),fill(0,0,0,2),p::PushClip{{-5,-5,1,1},{}},fill(0,0,4,4),p::PopClip{}});
 assert(s::render(store,invisible).status==s::Status::ok);f=get(store);for(auto c:f.pixels) assert(c==0);
 auto identity=prepare({p::BeginChunk{1},p::PushTransform{},fill(0,0,1,1),p::PopTransform{},p::EndChunk{1}});
 assert(s::render(store,identity).status==s::Status::ok);
 const auto saved=get(store);
 auto unsupported=prepare({fill(0,0,4,4),p::Text{"x",{},"sans",12,400,0,{0,0,0,1},0}});
 assert(s::render(store,unsupported).status==s::Status::unsupported);
 assert(get(store).pixels==saved.pixels && get(store).sequence==saved.sequence);
 auto rounded=fill(0,0,1,1);rounded.radii.top_left_x=1;
 elliptic_transition::Bytes expected_fill(64,0);expected_fill[2]=255;expected_fill[3]=255;
 const elliptic_transition::Evidence fill_evidence{evidence_directory,17,0,0},clip_evidence{evidence_directory,16,0,0};
 elliptic_transition::check("renderer-degenerate-fill-former-reject",4,4,{rounded},expected_fill,nullptr,&fill_evidence);
 elliptic_transition::check("renderer-degenerate-clip-empty-former-reject",4,4,{p::PushClip{{0,0,1,1},rounded.radii},p::PopClip{}},elliptic_transition::Bytes(64,0),nullptr,&clip_evidence);
 // Preserve both negative assertions using an effective side-sum violation.
 rounded.radii.top_left_x=2;rounded.radii.top_left_y=1;
 auto preserved=[&]{const auto now=get(store);assert(now.sequence==saved.sequence&&now.viewport.width==saved.viewport.width&&now.viewport.height==saved.viewport.height&&now.viewport.dpi==saved.viewport.dpi&&now.stride==saved.stride&&now.pixels==saved.pixels);};
 assert(s::render(store,prepare({rounded})).status==s::Status::unsupported);preserved();
 assert(s::render(store,prepare({p::PushClip{{0,0,1,1},rounded.radii},p::PopClip{}})).status==s::Status::unsupported);preserved();
 auto trans=p::identity;trans.m11=2; // G2 permits translations, not scale.
 assert(s::render(store,prepare({p::PushTransform{trans},p::PopTransform{}})).status==s::Status::unsupported);
 assert(store.resize({4,4,2})==FrameStatus::ok);assert(s::render(store,integer).status==s::Status::unsupported);
 assert(store.resize({4,4,1})==FrameStatus::ok);
 auto invalid=fill(0,0,-1,1);assert(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 invalid=fill(std::numeric_limits<float>::infinity(),0,1,1);assert(std::holds_alternative<p::Error>(p::prepare({{invalid}})));
 auto huge=prepare({fill(-std::numeric_limits<float>::max(),0,std::numeric_limits<float>::max(),1)});
 assert(s::render(store,huge).status==s::Status::ok);pixel(get(store),0,0,{0,0,0,0});
 assert(store.resize({8192,2048,1})==FrameStatus::ok);
 assert(store.resize({8192,2049,1})==FrameStatus::invalid_viewport);
 // Clear + three full fills exceeds 50M. Reject before allocating candidate.
 auto budget=prepare({fill(0,0,8192,2048),fill(0,0,8192,2048),fill(0,0,8192,2048)});
 assert(s::render(store,budget).status==s::Status::limit_exceeded);
 assert(store.resize({4,4,1})==FrameStatus::ok);
 assert(s::render(store,integer).status==s::Status::ok);
 const auto old=get(store);
 Cancel count(1000000);assert(s::render(store,integer,&count).status==s::Status::ok);
 const int calls=count.count; const auto before=get(store);
 // Every callback boundary, including last just before commit, preserves confirmed.
 for(int i=1;i<=calls;++i) {Cancel c(i);assert(s::render(store,integer,&c).status==s::Status::cancelled);auto unchanged=get(store);assert(unchanged.sequence==before.sequence && unchanged.pixels==before.pixels);}
 bool fail=false;FrameStore oom([&]{if(fail) throw std::bad_alloc{};});assert(oom.resize({4,4,1})==FrameStatus::ok);
 assert(s::render(oom,integer).status==s::Status::ok);fail=true;
 assert(s::render(oom,integer).status==s::Status::allocation_failed);fail=false;assert(get(oom).sequence==1);
 std::thread other([&]{auto r=s::render(store,integer);assert(r.status==s::Status::frame_failed && r.frame_status==FrameStatus::wrong_thread);});other.join();
 assert(get(store).pixels==old.pixels);
}
