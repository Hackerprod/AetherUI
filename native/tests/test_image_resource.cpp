#ifdef NDEBUG
#error assertions required
#endif
#include <aether/rendering/preparation/image_resource.hpp>
#include <cassert>
#include <cstdlib>
#include <new>
#include <array>
#include <iostream>
static long fail=-1;
void* operator new(std::size_t n) { if(fail==0)throw std::bad_alloc();if(fail>0)--fail;auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p; }
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
using namespace aether::rendering::preparation;
int main(){
 std::array<std::uint8_t,4> p{1,2,3,4};
 auto a=std::get<ImageRef>(ImageResource::copy(1,1,p));p[0]=4;assert(a->pixels()[0]==1);
 assert(a->stride()==4 && a->width()==1 && a->height()==1);
 auto bad=[](auto r){return std::holds_alternative<ImageError>(r);};
 assert(bad(ImageResource::copy(0,1,p)));assert(bad(ImageResource::copy(UINT32_MAX,UINT32_MAX,p)));
 assert(bad(ImageResource::copy(1,2,p)));p[3]=0;assert(bad(ImageResource::copy(1,1,p)));p={1,2,3,4};
 auto b=std::get<ImageRef>(ImageResource::copy(1,1,p));
 std::array<ImageRef,3> refs{a,a,b};
 auto bundle=std::get<std::shared_ptr<const ImageSet>>(ImageSet::create(refs,8,2));
 assert(bundle->bytes()==8 && bundle->resources().size()==2);
 assert(bad(ImageSet::create(refs,7,2)));assert(bad(ImageSet::create(refs,8,1)));
 assert(bad(ImageSet::create(refs,SIZE_MAX,2)));assert(bad(ImageSet::create(refs,8,SIZE_MAX)));
 std::array<ImageRef,1> nil{};assert(bad(ImageSet::create(nil,8,2)));
 assert(!bad(ImageSet::create({},0,0)));
 std::vector<ImageRef> many(65537,a);assert(bad(ImageSet::create(many,8,2)));
 many.resize(65536);assert(!bad(ImageSet::create(many,4,1)));
 int errors=0;
 for(int i=0;i<20;i++){fail=i;auto r=ImageResource::copy(1,1,p);fail=-1;if(bad(r)){assert(std::get<ImageError>(r)==ImageError::OutOfMemory);++errors;}else break;}assert(errors>=2);
 errors=0;
 for(int i=0;i<30;i++){fail=i;auto r=ImageSet::create(refs,8,2);fail=-1;if(bad(r)){assert(std::get<ImageError>(r)==ImageError::OutOfMemory);++errors;}else break;}assert(errors>=4);
 refs={};a.reset();b.reset();many.clear();assert(bundle->resources()[0]->pixels()[2]==3);
 std::cout<<"Image resource tests PASS\n";
}
