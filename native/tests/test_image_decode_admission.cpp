// Compile the decoder with allocator probes to verify rejection occurs before
// pixel allocation, rather than merely failing later on truncated data.
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <algorithm>
static size_t largest_allocation = 0;
static void* probe_malloc(size_t n) { largest_allocation=std::max(largest_allocation,n); return n >= 65536 ? nullptr : std::malloc(n); }
static void* probe_realloc(void* p,size_t n) { largest_allocation=std::max(largest_allocation,n); return n >= 65536 ? nullptr : std::realloc(p,n); }
#define STBI_MALLOC(n) probe_malloc(n)
#define STBI_REALLOC(p,n) probe_realloc(p,n)
#define STBI_FREE(p) std::free(p)
#include "../src/images/image_decode.cpp"
static void require(bool ok,const char* msg) { if(!ok) { std::fprintf(stderr,"%s\n",msg); std::exit(1); } }
static void little32(uint8_t* p,uint32_t n) { for(int i=0;i<4;++i)p[i]=uint8_t(n>>(8*i)); }
int main() {
    for(auto size : {std::pair<uint32_t,uint32_t>{16385,1},{16384,16384}}) {
        // A syntactically valid 24-bit BMP header whose pixels would exceed admission.
        uint8_t bmp[54]{}; bmp[0]='B';bmp[1]='M';little32(bmp+2,54);
        little32(bmp+10,54);little32(bmp+14,40);
        little32(bmp+18,size.first);little32(bmp+22,size.second);
        bmp[26]=1;bmp[28]=24;
        int32_t w=7,h=9; uint8_t* pixels=nullptr; largest_allocation=0;
        require(aether_image_decode(bmp,sizeof(bmp),&w,&h,&pixels)==AETHER_ERROR_PARSE_FAILED,"oversize rejected");
        require(largest_allocation<65536,"pixel allocation happened before rejection");
        require(w==7&&h==9&&!pixels,"failure changed outputs");
    }
    std::puts("OK: oversized image admission precedes pixel allocation");
}
