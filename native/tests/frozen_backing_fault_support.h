#pragma once
#include <new>
#include <cstdio>
#include <cstdlib>
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
namespace fault {
enum class Mode { none, cpp, aligned, c };
Mode mode=Mode::none;
long remaining=-1,calls=0,cpp_alloc=0,cpp_free=0,c_alloc=0,c_free=0;
bool hit=false;
bool reject(Mode selected) noexcept {
    if(mode!=selected)return false;
    ++calls;
    if(remaining--==0){hit=true;return true;}
    return false;
}
void arm(Mode selected,long index){remaining=index;calls=cpp_alloc=cpp_free=c_alloc=c_free=0;hit=false;mode=selected;}
void disarm(){mode=Mode::none;}
void* cpp_allocate(size_t size,size_t align=0) {
    if(reject(align ? Mode::aligned : Mode::cpp))throw std::bad_alloc();
    void* p=nullptr;
    if(align) { if(posix_memalign(&p,align<sizeof(void*)?sizeof(void*):align,size?size:1))p=nullptr; }
    else p=__real_malloc(size?size:1);
    if(!p)throw std::bad_alloc();
    if(mode!=Mode::none)++cpp_alloc;
    return p;
}
void cpp_delete(void* p) noexcept {if(p&&mode!=Mode::none)++cpp_free;__real_free(p);}
}
extern "C" void* __wrap_malloc(size_t n) noexcept {
    if(fault::reject(fault::Mode::c))return nullptr;
    void* p=__real_malloc(n);if(p&&fault::mode!=fault::Mode::none)++fault::c_alloc;return p;
}
extern "C" void __wrap_free(void* p) noexcept {if(p&&fault::mode!=fault::Mode::none)++fault::c_free;__real_free(p);}
void* operator new(size_t n){return fault::cpp_allocate(n);}
void* operator new[](size_t n){return fault::cpp_allocate(n);}
void operator delete(void* p) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p) noexcept {fault::cpp_delete(p);}
void operator delete(void* p,size_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,size_t) noexcept {fault::cpp_delete(p);}
void* operator new(size_t n,std::align_val_t a){return fault::cpp_allocate(n,size_t(a));}
void* operator new[](size_t n,std::align_val_t a){return fault::cpp_allocate(n,size_t(a));}
void operator delete(void* p,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete(void* p,size_t,std::align_val_t) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,size_t,std::align_val_t) noexcept {fault::cpp_delete(p);}
void* operator new(size_t n,const std::nothrow_t&) noexcept {try{return fault::cpp_allocate(n);}catch(...){return nullptr;}}
void* operator new[](size_t n,const std::nothrow_t&) noexcept {try{return fault::cpp_allocate(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept {fault::cpp_delete(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept {fault::cpp_delete(p);}
