#include <aether/aether_dom_sync.h>
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#include <new>
static long failure=-1;void* operator new(std::size_t n){if(failure==0)throw std::bad_alloc();if(failure>0)--failure;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}void* operator new[](std::size_t n){return ::operator new(n);}void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::abort();}}while(0)
static aether_dom_string_v1_t str(const char* s){return {s,std::strlen(s)};}
static aether_dom_token_v1_t token(aether_node_t n){aether_dom_token_v1_t t{};CHECK(aether_dom_read_token_v1(n,&t)==AETHER_OK);return t;}
static aether_dom_edit_t prepare(aether_dom_edit_spec_v1_t& s){auto t=token(s.target);aether_dom_edit_t e=nullptr;CHECK(aether_dom_prepare_edit_v1(&t,&s,&e)==AETHER_OK);return e;}
static void commit(aether_dom_edit_t e){aether_dom_token_v1_t t{};failure=0;auto status=aether_dom_edit_commit_v1(e,&t);failure=-1;CHECK(status==AETHER_OK);aether_dom_edit_destroy_v1(e);}
static void edit(aether_dom_edit_spec_v1_t s){commit(prepare(s));}
int main(){
    aether_doc_t d=nullptr;CHECK(aether_doc_create(&d)==AETHER_OK);CHECK(aether_doc_parse_html(d,"<body><input id='a' value='base'><p id='b'></p></body>")==AETHER_OK);auto* a=aether_doc_get_element_by_id(d,"a");auto* b=aether_doc_get_element_by_id(d,"b");auto* body=a->parent;
    aether_dom_edit_spec_v1_t s{};s.target=a;s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value=str("");s.value_present=1;edit(s);CHECK(a->runtime_value_present&&a->attributes.at("value").empty());s.kind=AETHER_DOM_SET_ATTRIBUTE;s.name=str("VALUE");s.value=str("new base");edit(s);CHECK(a->authored_value=="new base"&&a->attributes.at("value").empty());s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value={};s.value_present=0;edit(s);CHECK(!a->runtime_value_present&&a->attributes.at("value")=="new base");
    s.kind=AETHER_DOM_REMOVE_ATTRIBUTE;s.name=str("value");edit(s);CHECK(!a->authored_value_present&&!a->attributes.contains("value"));s.kind=AETHER_DOM_SET_RUNTIME_VALUE;s.value=str("override");s.value_present=1;edit(s);CHECK(aether_element_set_attribute(a,"value","R2")==AETHER_OK);CHECK(!a->runtime_value_present&&a->authored_value=="R2");
    s.kind=AETHER_DOM_SET_STATE;s.state=9;auto e=prepare(s);aether_element_set_state(a,4);aether_dom_token_v1_t out{};CHECK(aether_dom_edit_commit_v1(e,&out)==AETHER_ERROR_INVALID_STATE&&a->state==4);aether_dom_edit_destroy_v1(e);edit(s);CHECK(a->state==9);
    auto before=token(a);e=prepare(s);commit(e);CHECK(token(a).observation_serial==before.observation_serial);
    aether_dom_attribute_v1_t attrs[]={{str("É"),str("UP")},{str("é"),str("lo")}};
    aether_dom_node_spec_v1_t nodes[4]{};nodes[0].type=AETHER_NODE_ELEMENT;nodes[0].tag=str("BR");nodes[0].parent_index=-1;nodes[0].attributes=attrs;nodes[0].attribute_count=2;
    nodes[1].type=AETHER_NODE_TEXT;nodes[1].parent_index=0;nodes[1].text=str("");nodes[2].type=AETHER_NODE_TEXT;nodes[2].parent_index=0;nodes[2].text=str("raw </style> &");nodes[3].type=AETHER_NODE_COMMENT;nodes[3].parent_index=0;nodes[3].text=str("comment");
    s={};s.kind=AETHER_DOM_INSERT_SUBTREE;s.target=b;s.nodes=nodes;s.node_count=4;s.index=-1;
    bool success=false;for(long n=0;n<200;++n){auto base=token(b);const auto ids=d->next_style_node_id;const auto owned=d->editable_nodes.size();e=nullptr;failure=n;auto status=aether_dom_prepare_edit_v1(&base,&s,&e);failure=-1;if(status==AETHER_OK){aether_node_t fresh=nullptr;CHECK(aether_dom_edit_created_node_v1(e,0,&fresh)==AETHER_OK);commit(e);CHECK(fresh==b->children.front()&&fresh->tag_name=="br"&&fresh->children.size()==3&&fresh->children[0]->text.empty()&&fresh->attributes.size()==2);success=true;break;}CHECK(status==AETHER_ERROR_OUT_OF_MEMORY);CHECK(token(b).observation_serial==base.observation_serial&&d->next_style_node_id==ids&&d->editable_nodes.size()==owned&&b->children.empty());}CHECK(success);
    s={};s.kind=AETHER_DOM_INSERT_CHILD;s.target=b;s.child=a;s.index=0;edit(s);CHECK(a->parent==b&&a->identity->disconnect_serial==0);s.kind=AETHER_DOM_REMOVE_CHILD;edit(s);CHECK(!a->parent&&!a->identity->attached&&a->identity->disconnect_serial==1);
    s.kind=AETHER_DOM_SET_ATTRIBUTE;s.target=a;s.name=str("x");const char bad[]={'a',0,'b'};s.value={bad,3};auto t=token(a);e=nullptr;CHECK(aether_dom_prepare_edit_v1(&t,&s,&e)==AETHER_ERROR_INVALID_STATE&&!e&&a->attributes.count("x")==0);
    s={};s.target=b;s.kind=AETHER_DOM_CLEAR_CHILDREN;edit(s);CHECK(b->children.empty());
    aether_doc_destroy(d);std::puts("typed DOM edits PASS");
}
