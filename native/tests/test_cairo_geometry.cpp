#ifdef NDEBUG
#error "Pure Cairo geometry tests require assertions"
#endif
#include "../src/rendering/cairo/cairo_geometry.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <thread>
using namespace aether::rendering;
using namespace aether::rendering::cairo_adapter;
namespace {
prep::Fill fill() {return {{0,0,10,20},{},{1,0,0,1}};}
Plan plan(prep::Snapshot s,float dpi=1) {
 auto frame=prep::prepare(s);assert(std::holds_alternative<prep::PreparedFrame>(frame));
 auto p=preflight(std::get<prep::PreparedFrame>(frame),dpi,{0,0,0,0});
 assert(std::holds_alternative<Plan>(p));return std::move(std::get<Plan>(p));
}
}
int main() {
 auto square=rounded_path({1,2,10,20},{});
 assert(square && square->count==10 && square->segments[0].coordinates[0]==1);
 auto round=rounded_path({0,0,10,10},{10,10,10,10,10,10,10,10});
 assert(round && round->segments[0].coordinates[0]==5);
 assert(std::abs(round->segments[2].coordinates[0]-(10-5*(1-.5522847498307936)))<1e-12);
 assert(rounded_path({0,0,0,1},{})->count==0);
 assert(!rounded_path({0,0,-1,1},{}));
 assert(!rounded_path({0,0,1,1},{-1,0,0,0,0,0,0,0}));
 const auto big=std::numeric_limits<float>::max();
 auto extreme=rounded_path({big,big,big,big},{});
 assert(extreme && std::isfinite(extreme->segments[2].coordinates[4]));
 auto scaled=classify({2,0,0,3,4,5},2);
 assert(scaled.kind==MatrixKind::valid && scaled.value.xx==4 && scaled.value.y0==10);
 assert(classify({0,0,0,1,0,0},1).kind==MatrixKind::singular);
 assert(classify({big,0,0,big,big,big},big).kind==MatrixKind::valid);
 assert(classify(prep::identity,0).kind==MatrixKind::invalid);
 assert(classify(prep::identity,std::numeric_limits<float>::infinity()).kind==MatrixKind::invalid);
 assert(classify({std::numeric_limits<float>::quiet_NaN(),0,0,1,0,0},1).kind==MatrixKind::invalid);
 const prep::PushTransform singular{{0,0,0,1,0,0}};
 const prep::PushClip clip{{0,0,10,10},{}};
 auto p=plan({{singular,fill(),prep::PopTransform{},fill()}});
 assert(p[1].action==Action::none && p[3].action==Action::fill && p[3].matrix.xx==1);
 p=plan({{clip,singular,fill(),prep::PopClip{},fill(),prep::PopTransform{},fill()}});
 assert(p[0].action==Action::push_clip && p[3].action==Action::pop_clip);
 assert(p[4].action==Action::none && p[6].action==Action::fill);
 p=plan({{singular,clip,fill(),prep::PopClip{},prep::PopTransform{},fill()}});
 assert(p[1].action==Action::empty_clip && p[2].action==Action::none && p[5].action==Action::fill);
 p=plan({{clip,prep::PushTransform{{2,0,0,2,3,4}},prep::PopClip{},fill(),prep::PopTransform{},fill()}},2);
 assert(p[2].action==Action::pop_clip && p[3].matrix.xx==4 && p[3].matrix.x0==6);
 assert(p[5].matrix.xx==2);
 p=plan({{prep::PushTransform{{1,0,0,1,3,4}},prep::PushTransform{{2,0,0,2,0,0}},fill(),prep::PopTransform{},prep::PopTransform{}}});
 assert(p[2].matrix.xx==2 && p[2].matrix.x0==3);
 p=plan({{prep::Stroke{{0,0,1,1},{},0,{0,0,0,1}},prep::BeginChunk{1},prep::EndChunk{1}}});
 assert(p[0].action==Action::stroke && p[0].width==.25 && p[1].action==Action::none);
 auto text=prep::Text{"hello",{0,0,1,1},"sans",12,400,AETHER_TEXT_ALIGN_START,{0,0,0,1},AETHER_FALSE};
 auto prepared=prep::prepare({{fill(),text}});assert(std::holds_alternative<prep::PreparedFrame>(prepared));
 auto rejected=preflight(std::get<prep::PreparedFrame>(prepared),1,{0,0,0,0});
 assert(std::get<Error>(rejected).code==ErrorCode::unsupported_command && std::get<Error>(rejected).index==1);
 rejected=preflight(std::get<prep::PreparedFrame>(prepared),1,{2,0,0,0});
 assert(std::get<Error>(rejected).code==ErrorCode::invalid_color);
 auto empty=prep::prepare({{}});
 assert(std::holds_alternative<prep::PreparedFrame>(empty));
 rejected=preflight(std::get<prep::PreparedFrame>(empty),0,{0,0,0,0});
 assert(std::get<Error>(rejected).code==ErrorCode::invalid_matrix);
 FrameStore store;Viewport v{9,9,9};
 assert(store.current_viewport(v)==FrameStatus::ok && v.width==0);
 // Even a suspended viewport must not be used to skip preflight.
 rejected=preflight(std::get<prep::PreparedFrame>(prepared),v.dpi,{0,0,0,0});
 assert(std::get<Error>(rejected).code==ErrorCode::unsupported_command);
 assert(store.resize({2,3,4})==FrameStatus::ok && store.current_viewport(v)==FrameStatus::ok);
 assert(v.width==2 && v.height==3 && v.dpi==4);
 std::thread other([&] {Viewport unchanged{7,8,9};assert(store.current_viewport(unchanged)==FrameStatus::wrong_thread);assert(unchanged.width==7 && unchanged.height==8 && unchanged.dpi==9);});other.join();
}
