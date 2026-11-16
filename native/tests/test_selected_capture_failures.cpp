#include "../src/rendering/text/selected_html_capture.h"
#include "../src/rendering/text/font_source.h"
#include <cassert>
#include <cstdlib>
#include <new>
#include <fstream>
#include <iterator>
#include <iostream>
#include <set>
static long fail_at=-1,allocations=0;
void* operator new(size_t n){const auto i=allocations++;if(fail_at==i)throw std::bad_alloc();if(auto*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
using namespace aether::rendering;using FrameRef=std::shared_ptr<const SelectedCaptureFrame>;
struct Cancel:text::truetype::Cancellation {unsigned at,calls=0;explicit Cancel(unsigned a):at(a){}bool cancelled()noexcept override{return ++calls>=at;}};
int main(int argc,char**argv){assert(argc==2);std::ifstream file(argv[1],std::ios::binary);std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});auto font=std::make_shared<const text::FontSource>(std::move(bytes));
 const std::string html="<html><head><style>#target{display:block;width:80px;height:40px;margin:0;background:#abcdef}#leaf{display:block}</style></head><body><div id='target'><div id='leaf'>A</div></div></body></html>";
 auto run=[&](text::truetype::Cancellation*c=nullptr){return capture_selected_html(html,"target","",font,1280,720,80,40,c);};
 Cancel probe(~0u);auto result=run(&probe);if(auto*e=std::get_if<SelectedCaptureFailure>(&result)){std::cerr<<"fixture failed "<<e->reason<<"\n";return 1;}auto stable=std::get<FrameRef>(result);auto pixels=stable->frame.pixels;
 unsigned cancellations=0;for(unsigned i=1;i<=probe.calls;++i){Cancel c(i);auto r=run(&c);assert(std::holds_alternative<SelectedCaptureFailure>(r));assert(std::get<SelectedCaptureFailure>(r).code==text::truetype::HtmlTextError::cancelled);assert(stable->frame.pixels==pixels);++cancellations;}
 allocations=0;{auto r=run();assert(std::holds_alternative<FrameRef>(r));}const long total=allocations;
 std::set<long> sites;for(long i=0;i<std::min(64L,total);++i)sites.insert(i);for(long i=0;i<64;++i)sites.insert((total-1)*i/63);for(long i=std::max(0L,total-32);i<total;++i)sites.insert(i);
 unsigned typed=0,fallback=0,other=0;for(auto site:sites){allocations=0;fail_at=site;auto r=run();fail_at=-1;if(auto*e=std::get_if<SelectedCaptureFailure>(&r)){if(e->code==text::truetype::HtmlTextError::out_of_memory)++typed;else{++other;std::cout<<"non_OOM_failure site="<<site<<" code="<<int(e->code)<<" reason="<<e->reason<<"\n";}}else{assert(std::get<FrameRef>(r)->frame.pixels==pixels);++fallback;}assert(stable->frame.pixels==pixels);}
 auto recovered=run();assert(std::holds_alternative<FrameRef>(recovered));assert(std::get<FrameRef>(recovered)->frame.pixels==pixels);
 std::cout<<"Selected generic oneletter cancellation_points="<<cancellations<<" allocation_sites="<<total<<" sampled="<<sites.size()<<" typed_OOM="<<typed<<" successful_equivalent_fallback="<<fallback<<" other_failures="<<other<<" last_good=intact recovery=identical\n";
 return other?2:0;
}
