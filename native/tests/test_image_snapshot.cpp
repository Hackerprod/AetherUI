#ifdef NDEBUG
#error assertions required
#endif
#include "../src/rendering/software/software_renderer.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <new>
#include <limits>
#include <iostream>
static long fail=-1;
void* operator new(std::size_t n){if(fail==0)throw std::bad_alloc();if(fail>0)--fail;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace p=aether::rendering::preparation;namespace s=aether::rendering::software;
using namespace aether::rendering;
struct Sink:p::CommandSink{int count{};bool accept(const p::PreparedEvent& e) override {assert(std::get<p::Image>(e.command).resource->pixels()[2]==3);++count;return true;}};
struct Cancel:p::Cancellation{int n;explicit Cancel(int v):n(v){}bool cancelled()noexcept override{return --n==0;}};
int main(){
 std::array<std::uint8_t,4> pixels{1,2,3,4};
 auto resource=std::get<p::ImageRef>(p::ImageResource::copy(1,1,pixels));
 p::Image image{resource,{0,0,1,1},{-1,-2,4,5},.5f};
 assert(p::command_tag(image)==AETHER_PAINT_DRAW_BACKGROUND_IMAGE);
 p::Snapshot input{{image,image}};
 auto result=p::prepare(input);assert(std::holds_alternative<p::PreparedFrame>(result));
 auto frame=std::move(std::get<p::PreparedFrame>(result));assert(frame.images()->bytes()==4&&frame.images()->resources().size()==1);
 input.commands.clear();resource.reset();image.resource.reset();pixels={};
 assert(std::get<p::Image>(frame.events()[0].command).resource->pixels()[2]==3);
 Sink sink;assert(!p::replay(frame,sink)&&sink.count==2);
 image=std::get<p::Image>(frame.events()[0].command);
 auto bad=[&](p::Image v){auto r=p::prepare(p::Snapshot{{v}});assert(std::get<p::Error>(r).code==p::ErrorCode::INVALID_PAYLOAD);};
 auto v=image;v.resource.reset();bad(v);v=image;v.source.width=2;bad(v);v=image;v.source.x=-1;bad(v);
 v=image;v.source.width=0;bad(v);v=image;v.opacity=2;bad(v);v=image;v.destination.x=65536;bad(v);
 v=image;v.source.x=std::numeric_limits<float>::infinity();bad(v);v=image;v.opacity=std::numeric_limits<float>::quiet_NaN();bad(v);
 p::Snapshot distinct;for(int i=0;i<4097;++i){v=image;v.resource=std::get<p::ImageRef>(p::ImageResource::copy(1,1,std::array<std::uint8_t,4>{0,0,0,0}));distinct.commands.push_back(v);}
 assert(std::get<p::Error>(p::prepare(distinct)).code==p::ErrorCode::LIMIT_EXCEEDED);distinct.commands.pop_back();assert(std::holds_alternative<p::PreparedFrame>(p::prepare(distinct)));
 input.commands={image,image};int failures=0;for(int n=0;n<30;++n){fail=n;auto r=p::prepare(input);fail=-1;if(auto* e=std::get_if<p::Error>(&r)){assert(e->code==p::ErrorCode::OUT_OF_MEMORY);++failures;}else break;}assert(failures>=5);
 for(int n=1;n<=6;++n){Cancel c(n);auto r=p::prepare(input,&c);assert(std::get<p::Error>(r).code==p::ErrorCode::CANCELLED);}
 const float nan=std::numeric_limits<float>::quiet_NaN(), inf=std::numeric_limits<float>::infinity();
 for(float f:{nan,inf,-inf}) {v=image;v.destination.width=f;bad(v);v=image;v.destination.y=f;bad(v);v=image;v.source.height=f;bad(v);v=image;v.opacity=f;bad(v);}
 v=image;v.source.x=.5f;v.source.width=.5000001f;bad(v);
 v=image;v.destination.x=-65537;bad(v);v=image;v.destination.height=0;bad(v);v=image;v.opacity=-.01f;bad(v);
 v=image;v.destination={-65536,-65536,131072,131072};v.opacity=0;assert(std::holds_alternative<p::PreparedFrame>(p::prepare(p::Snapshot{{v}})));
 p::Snapshot many;many.commands.assign(65536,image);auto many_result=p::prepare(many);assert(std::holds_alternative<p::PreparedFrame>(many_result));assert(std::get<p::PreparedFrame>(many_result).images()->bytes()==4);
 many.commands.push_back(image);assert(std::get<p::Error>(p::prepare(many)).code==p::ErrorCode::COMMAND_COUNT);
 auto empty=p::prepare(p::Snapshot{});assert(std::holds_alternative<p::PreparedFrame>(empty));assert(!std::get<p::PreparedFrame>(empty).images());
 FrameStore store;assert(store.resize({4,4,1})==FrameStatus::ok);
 auto base=p::prepare(p::Snapshot{{p::Fill{{0,0,4,4},{},{1,0,0,1}}}});assert(s::render(store,std::get<p::PreparedFrame>(base)).status==s::Status::ok);
 std::optional<Frame> before,after;assert(store.copy_confirmed(before)==FrameStatus::ok);
 image.source={.25f,0,.75f,1}; // Fractional crop remains unsupported by raster.
 auto mixed=p::prepare(p::Snapshot{{p::Fill{{0,0,4,4},{},{0,1,0,1}}, image}});assert(s::render(store,std::get<p::PreparedFrame>(mixed)).status==s::Status::unsupported);
 assert(store.copy_confirmed(after)==FrameStatus::ok);assert(before->pixels==after->pixels&&before->sequence==after->sequence);
 assert(s::render(store,frame).status==s::Status::ok);
 std::cout<<"Image snapshot PASS, OOM points "<<failures<<"\n";
}
