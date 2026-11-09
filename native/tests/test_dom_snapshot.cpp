#include <aether/aether_dom_sync.h>
#include "../src/dom/dom_types.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
#include <string>
static std::string value(aether_dom_string_v1_t s){return {s.data,s.length};}
static size_t find(aether_dom_snapshot_t s,aether_node_t target){size_t n=0;CHECK(aether_dom_snapshot_count_v1(s,&n)==AETHER_OK);for(size_t i=0;i<n;++i){aether_dom_snapshot_node_v1_t v{};CHECK(aether_dom_snapshot_node_v1(s,i,&v)==AETHER_OK);if(v.node==target)return i;}std::abort();}
int main(){
    aether_doc_t d=nullptr;CHECK(aether_doc_create(&d)==AETHER_OK);CHECK(aether_doc_parse_html(d,"<body><input id='a' value='base'><p>text</p></body>")==AETHER_OK);
    auto* a=aether_doc_get_element_by_id(d,"a");auto* body=a->parent;aether_dom_node_lease_t lease=nullptr;CHECK(aether_dom_retain_node_v1(a,&lease)==AETHER_OK);
    aether_dom_token_v1_t t0{},t1{};CHECK(aether_dom_read_token_v1(a,&t0)==AETHER_OK);
    CHECK(aether_node_remove_child(body,a)==AETHER_OK);CHECK(aether_element_set_attribute(a,"VaLuE","new")==AETHER_OK);aether_element_set_state(a,9);
    CHECK(aether_dom_read_token_v1(a,&t1)==AETHER_OK);CHECK(t1.observation_serial==t0.observation_serial+3);
    aether_dom_snapshot_t s=nullptr;CHECK(aether_dom_acquire_snapshot_v1(d,&lease,1,&s)==AETHER_OK);auto i=find(s,a);aether_dom_snapshot_node_v1_t v{};CHECK(aether_dom_snapshot_node_v1(s,i,&v)==AETHER_OK);CHECK(v.alive&&!v.attached&&v.state==9&&v.parent_index==-1&&v.disconnect_serial==1);CHECK(v.authored_value_present&&!v.runtime_value_present&&value(v.authored_value)=="new");
    CHECK(aether_element_set_attribute(a,"value","later")==AETHER_OK);CHECK(aether_doc_parse_html(d,"<body>replacement</body>")==AETHER_OK);CHECK(!a->identity->alive);
    CHECK(aether_dom_snapshot_node_v1(s,i,&v)==AETHER_OK&&value(v.authored_value)=="new");
    aether_dom_snapshot_t retired=nullptr;CHECK(aether_dom_acquire_snapshot_v1(d,&lease,1,&retired)==AETHER_OK);CHECK(aether_dom_snapshot_node_v1(retired,find(retired,a),&v)==AETHER_OK);CHECK(!v.alive&&value(v.authored_value)=="later");
    auto weak=a->editable_storage;auto docweak=d->editable_storage;aether_doc_destroy(d);aether_dom_node_lease_destroy_v1(lease);CHECK(!weak.expired()&&!docweak.expired());
    aether_dom_snapshot_destroy_v1(s);CHECK(!weak.expired());aether_dom_snapshot_destroy_v1(retired);CHECK(weak.expired()&&docweak.expired());
}
