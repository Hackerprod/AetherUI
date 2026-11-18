#include <new>
#include <cstdlib>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <string>
#include <limits>
// Test-only allocation interposition; never linked into the production DSO.
namespace {struct alignas(std::max_align_t) Block{std::size_t bytes;};std::size_t live_blocks=0,live_bytes=0,calls=0,fail_at=std::numeric_limits<std::size_t>::max();bool injection=false;}
void* operator new(std::size_t n){if(injection&&calls++==fail_at)throw std::bad_alloc{};auto* h=static_cast<Block*>(std::malloc(sizeof(Block)+(n?n:1)));if(!h)throw std::bad_alloc{};h->bytes=n;++live_blocks;live_bytes+=n;return h+1;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(!p)return;auto* h=static_cast<Block*>(p)-1;--live_blocks;live_bytes-=h->bytes;std::free(h);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
#include "dom/dom_types.h"
static unsigned long long hash(unsigned long long h,const std::string& s){for(unsigned char c:s)h=(h^c)*1099511628211ull;return (h^255)*1099511628211ull;}
static unsigned long long signature(const aether_node_s* n,unsigned long long h=1469598103934665603ull){h^=static_cast<unsigned>(n->type);h*=1099511628211ull;h^=static_cast<unsigned long long>(n->style_node_id);h=hash(hash(h,n->tag_name),n->text);for(const auto& [k,v]:n->attributes)h=hash(hash(h,k),v);for(auto* c:n->children)h=signature(c,h);return h;}
static void parse_faults(const std::string& html){
 aether_doc_t original{};aether_doc_create(&original);assert(aether_doc_parse_html(original,html.c_str())==AETHER_OK);const auto expected=signature(original);aether_doc_destroy(original);
 std::size_t failed=0;
 for(std::size_t nth=0;;++nth){const auto before=live_blocks,bytes=live_bytes;aether_doc_t doc{};aether_doc_create(&doc);calls=0;fail_at=nth;injection=true;bool threw=false;try{assert(aether_doc_parse_html(doc,html.c_str())==AETHER_OK);}catch(const std::bad_alloc&){threw=true;}injection=false;
  assert(aether_doc_parse_html(doc,html.c_str())==AETHER_OK&&signature(doc)==expected);aether_doc_destroy(doc);if(live_blocks!=before||live_bytes!=bytes)std::fprintf(stderr,"allocation failure index %zu retained %zu blocks / %zu bytes after destruction\n",nth,live_blocks-before,live_bytes-bytes);assert(live_blocks==before&&live_bytes==bytes);if(!threw)break;++failed;
 }
 std::printf("parser fixture: %zu failed allocations, retry/IDs/content/leak counters PASS\n",failed);
}
int main(){
 const std::string long_text(96,'q');
 const std::string fixtures[]={"<div>"+long_text+"<p>a</p>"+long_text+"<p>b</p>"+long_text+"</div>","<script>"+long_text+"</script>","<textarea>"+long_text+"</textarea>","<div><+<+<+<+<+</div>","<div><!--"+long_text+"--><!--x--><!--"+long_text+"--></div>"};
 for(const auto& html:fixtures)parse_faults(html);
 std::puts("parser RAII: all allocation failures, retry, IDs/content/order, no orphan allocations PASS");
}
