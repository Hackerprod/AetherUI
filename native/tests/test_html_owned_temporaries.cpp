#include <new>
#include <cstdlib>
#include <cstddef>
#include <cassert>
#include <cstdio>
#include <string>
#include <limits>
// Test-only allocation interposition; never linked into the production DSO.
namespace {struct alignas(std::max_align_t) Block{std::size_t bytes;void* base;};std::size_t live_blocks=0,live_bytes=0,calls=0,fail_at=std::numeric_limits<std::size_t>::max();bool injection=false;}
static void* allocate(std::size_t n,std::size_t alignment){
 if(injection&&calls++==fail_at)throw std::bad_alloc{};
 alignment=alignment<alignof(Block)?alignof(Block):alignment;
 const auto offset=(sizeof(Block)+alignment-1)/alignment*alignment;
 void* base=nullptr;if(posix_memalign(&base,alignment,offset+(n?n:1)))throw std::bad_alloc{};
 auto* p=static_cast<unsigned char*>(base)+offset;auto* h=reinterpret_cast<Block*>(p)-1;
 h->bytes=n;h->base=base;++live_blocks;live_bytes+=n;return p;
}
void* operator new(std::size_t n){return allocate(n,alignof(std::max_align_t));}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(!p)return;auto* h=static_cast<Block*>(p)-1;--live_blocks;live_bytes-=h->bytes;std::free(h->base);}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
void* operator new(std::size_t n,std::align_val_t a){return allocate(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::align_val_t)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{::operator delete(p);}
#include "dom/owned_parser.h"
using namespace aether::rendering::html_ltr;
#include "dom/dom_types.h"
static unsigned long long hash(unsigned long long h,const std::string& s){for(unsigned char c:s)h=(h^c)*1099511628211ull;return (h^255)*1099511628211ull;}
static unsigned long long signature(const aether_node_s* n,unsigned long long h=1469598103934665603ull){h^=static_cast<unsigned>(n->type);h*=1099511628211ull;h^=static_cast<unsigned long long>(n->style_node_id);h=hash(hash(h,n->tag_name),n->text);for(const auto& [k,v]:n->attributes)h=hash(hash(h,k),v);for(auto* c:n->children)h=signature(c,h);return h;}
static void parse_faults(const std::string& html){
 aether_doc_t original{};aether_doc_create(&original);assert(aether_doc_parse_html(original,html.c_str())==AETHER_OK);const auto expected=signature(original);aether_doc_destroy(original);
 OwnedBudget budget;const auto baseline=budget.total().current;std::size_t failed=0;
 for(std::size_t nth=0;;++nth){const auto before=live_blocks,bytes=live_bytes;aether_doc_t doc{};aether_doc_create(&doc);calls=0;fail_at=nth;injection=true;
  const auto code=aether::dom::parse_html_owned_temporaries(doc,html.c_str(),budget.handle());injection=false;
  assert(code==AETHER_OK||code==AETHER_ERROR_OUT_OF_MEMORY);assert(budget.total().current==baseline);
  assert(aether::dom::parse_html_owned_temporaries(doc,html.c_str(),budget.handle())==AETHER_OK&&signature(doc)==expected);
  assert(budget.total().current==baseline);aether_doc_destroy(doc);assert(live_blocks==before&&live_bytes==bytes);if(code==AETHER_OK)break;++failed;
 }
 assert(budget.usage(OwnedCategory::temporary).peak>baseline);
 {OwnedBudget tight(OwnedBudget::bookkeeping_bytes());aether_doc_t doc{};aether_doc_create(&doc);assert(aether::dom::parse_html_owned_temporaries(doc,html.c_str(),tight.handle())==AETHER_ERROR_LIMIT_EXCEEDED);assert(tight.total().current==OwnedBudget::bookkeeping_bytes());aether_doc_destroy(doc);}
 std::printf("owned temporaries fixture: %zu allocation failures/retry/quota/domain release PASS\n",failed);
}
int main(){
 const std::string long_text(96,'q');
 const std::string fixtures[]={"<div>"+long_text+"<p>a</p>"+long_text+"<p>b</p>"+long_text+"</div>","<script>"+long_text+"</script>","<textarea>"+long_text+"</textarea>","<div><+<+<+<+<+</div>","<div><!--"+long_text+"--><!--x--><!--"+long_text+"--></div>","<div TITLE=\""+long_text+"&amp;\" title=\"ignored\">&#xE1;&lt;\r\n"+long_text+"</div>"};
 for(const auto& html:fixtures)parse_faults(html);
 std::puts("parser temporary storage PASS; persistent DOM NOT covered by this test");
}
