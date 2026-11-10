// Data-state string allocation probes; shared-core operator-new interposition.
#include <new>
#include <cstdlib>
static std::size_t fail_size=0;
static bool fault_delivered=false;
static int matching_allocations_to_skip=0;
void* operator new(std::size_t n){if(fail_size && n==fail_size && matching_allocations_to_skip--==0){fail_size=0;fault_delivered=true;throw std::bad_alloc();}if(void*p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
void operator delete(void*p,std::size_t)noexcept{std::free(p);}
void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return ::operator new[](n);}catch(...){return nullptr;}}
void operator delete(void*p,const std::nothrow_t&) noexcept {std::free(p);}
void operator delete[](void*p,const std::nothrow_t&) noexcept {std::free(p);}
#define AETHER_TEST_ALIGNED_REJECT() false
#include "fault_inject_aligned.h"

#include <aether/aether.h>
#include <cstdio>
#include <string>
#include <cstring>
// This parser's legacy ABI does not promise rollback on OOM. Probe the two
// allocation boundaries for the source buffer and normalized text, catch legacy exceptions,
// and require a successful retry with exact bytes. No transactional claim.
int main(){
 int failures=0;
 std::string source,normalized;
 for(int i=0;i<1024;++i){source+="A\r\nB";normalized+="A\nB";}
 const std::string html="<body><div id=target>"+source+"</div></body>";
 for(int boundary=0;boundary<3;++boundary){
  const std::size_t size=boundary==0?html.size()+1:normalized.size()+1;
  aether_doc_t doc{};
  if(aether_doc_create(&doc)!=AETHER_OK)return 2;
  if(aether_doc_parse_html(doc,"<body><p id=old>old</p></body>")!=AETHER_OK)return 2;
  bool threw=false; aether_status_t status=AETHER_OK;
  fault_delivered=false;fail_size=size;matching_allocations_to_skip=boundary==2?1:0;
  try{status=aether_doc_parse_html(doc,html.c_str());}catch(const std::bad_alloc&){threw=true;}
  fail_size=0;
  if(!fault_delivered || (!threw && status!=AETHER_ERROR_OUT_OF_MEMORY))++failures;
  if(size==html.size()+1 && !aether_doc_get_element_by_id(doc,"old"))++failures;
  if(aether_doc_parse_html(doc,html.c_str())!=AETHER_OK)++failures;
  auto target=aether_doc_get_element_by_id(doc,"target");
  char* value=target?aether_node_get_text_content(aether_node_get_child(target,0)):nullptr;
  if(!value || normalized!=value)++failures;
  if(aether_doc_get_element_by_id(doc,"old"))++failures;
  aether_string_free(value);
  aether_doc_destroy(doc);
  std::printf("Source allocation size %zu: injected=%d, legacy exception=%d, retry verified\n",size,int(fault_delivered),int(threw));
 }
 std::printf("html-data-newlines-oom: %d failures\n",failures);return failures?1:0;
}
