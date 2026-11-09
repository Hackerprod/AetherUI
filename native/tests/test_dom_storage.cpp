#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
static long failure=-1;
void* operator new(std::size_t n) { if(failure==0) throw std::bad_alloc(); if(failure>0)--failure; if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc(); }
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);} void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);} void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}}while(0)
static aether_doc_t make(){aether_doc_t d=nullptr;CHECK(aether_doc_create(&d)==AETHER_OK);CHECK(aether_doc_parse_html(d,"<body><div id='a'><span>old</span></div><p id='b'></p></body>")==AETHER_OK);return d;}
int main(){
    for(long n=0;n<10;++n){aether_doc_t d=reinterpret_cast<aether_doc_t>(1);failure=n;const auto s=aether_doc_create(&d);failure=-1;if(s==AETHER_OK)aether_doc_destroy(d);else CHECK(s==AETHER_ERROR_OUT_OF_MEMORY&&d==nullptr);}
    bool parse_success=false;
    for(long n=0;n<250;++n){auto* d=make();auto* before=aether_doc_get_element_by_id(d,"a");auto pin=aether_editable_lease(before);auto root=aether_editable_lease(d);const auto epoch=d->identity->mutation_epoch,obs=d->owner_control->observation_serial;const auto ids=d->next_style_node_id;failure=n;const auto s=aether_doc_parse_html(d,"<body><section id='new'><i>changed</i></section></body>");failure=-1;if(s==AETHER_OK){CHECK(!before->identity->alive&&aether_doc_get_element_by_id(d,"new"));parse_success=true;}else{CHECK(s==AETHER_ERROR_OUT_OF_MEMORY);CHECK(before->identity->alive&&aether_doc_get_element_by_id(d,"a")==before);CHECK(d->identity->mutation_epoch==epoch&&d->owner_control->observation_serial==obs&&d->next_style_node_id==ids);}aether_doc_destroy(d);CHECK(!root->node->identity->alive);if(parse_success)break;}
    CHECK(parse_success);
    bool attribute_success=false;
    for(long n=0;n<30;++n){auto* d=make();auto* a=aether_doc_get_element_by_id(d,"a");auto attrs=a->attributes;const auto epoch=d->identity->mutation_epoch;failure=n;auto s=aether_element_set_attribute(a,"VeryLongAttributeNameToForceAllocation","a long value that requires dynamic allocation");failure=-1;if(s==AETHER_OK)attribute_success=true;else{CHECK(s==AETHER_ERROR_OUT_OF_MEMORY);CHECK(a->attributes==attrs&&d->identity->mutation_epoch==epoch);}aether_doc_destroy(d);if(attribute_success)break;}
    CHECK(attribute_success);
    auto* d=make();auto* a=aether_doc_get_element_by_id(d,"a");auto* b=aether_doc_get_element_by_id(d,"b");auto* body=a->parent;auto* span=a->children.front();auto apin=aether_editable_lease(a);auto root=aether_editable_lease(d);auto spanweak=span->editable_storage;auto owner=d->owner_control;auto token=a->identity;
    auto serial=owner->observation_serial;CHECK(aether_node_remove_child(body,a)==AETHER_OK);CHECK(!a->identity->attached&&a->identity->disconnect_serial==1&&span->identity->disconnect_serial==1);
    const auto doc_epoch=d->identity->mutation_epoch;aether_element_set_state(a,4);CHECK(owner->observation_serial==serial+2&&d->identity->mutation_epoch==doc_epoch&&a->state==4);
    CHECK(aether_node_append_child(body,a)==AETHER_OK);CHECK(a->identity->disconnect_serial==1&&a->identity->attached);
    CHECK(aether_node_append_child(b,a)==AETHER_OK);CHECK(a->identity->disconnect_serial==1);
    auto* dest=make();const auto did=dest->next_style_node_id;auto dest_root=aether_editable_lease(dest);CHECK(aether_node_append_child(aether_doc_get_body(dest),a)==AETHER_OK);CHECK(a->identity==token&&a->identity->disconnect_serial==2&&a->style_node_id>=did&&a->document_owner.lock()==dest->owner_control);CHECK(d->editable_nodes.count(a)==0&&dest->editable_nodes.count(a)==1);
    aether_status_t wrong=AETHER_OK;std::thread t([&]{wrong=aether_element_set_attribute(a,"x","bad");});t.join();CHECK(wrong==AETHER_ERROR_INVALID_STATE&&a->attributes.count("x")==0);
    aether_doc_destroy(d);CHECK(root->node->identity->alive==false&&static_cast<aether_doc_s*>(root->node.get())->editable_nodes.empty());CHECK(token->alive);
    aether_doc_destroy(dest);CHECK(!token->alive&&dest_root->node->identity->alive==false&&spanweak.expired());CHECK(apin->node.get()==a);
    root.reset();dest_root.reset();apin.reset();CHECK(token->storage.expired());
    std::puts("DOM storage and legacy strong guarantees PASS");
}
