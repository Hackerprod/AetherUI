// Parser observations, NOT the B2 admission/ledger gate. Keep repairs separate
// from invalid hierarchies that actually survive this repository's parser.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <aether/aether.h>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
struct Doc {
 aether_doc_t d{};
 explicit Doc(const char* body){assert(aether_doc_create(&d)==AETHER_OK);std::string s="<html><head></head><body>";s+=body;s+="</body></html>";assert(aether_doc_parse_html(d,s.c_str())==AETHER_OK);}
 ~Doc(){aether_doc_destroy(d);}
 aether_node_t id(const char* s){auto n=aether_doc_get_element_by_id(d,s);assert(n);return n;}
};
static void parent(Doc& d,const char* child,const char* owner){assert(aether_node_get_parent(d.id(child))==d.id(owner));}
int main(){
 // DOM-02: the current parser retains these hierarchies. B2 admission must
 // reject the parsed structures, not rely on browser repair assumptions.
 {Doc d("<p id='p'>x<div id='x'>y</div>z</p>");parent(d,"x","p");}
 {Doc d("<label id='p'><input id='x' type='text'></label>");parent(d,"x","p");}
 {Doc d("<a id='p'>x<a id='x'>y</a>z</a>");parent(d,"x","p");}
 {Doc d("<a id='p'><span id='s'><a id='x'>y</a></span></a>");parent(d,"x","s");parent(d,"s","p");}
 // Separate repair case: void input cannot own the apparent source child.
 {Doc d("<div id='p'><input id='i' type='text'><span id='x'>y</span></input></div>");parent(d,"x","p");assert(aether_node_get_child_count(d.id("i"))==0);}
 // DOM-01: exactly two parent text sequences, and one explicitly nested text
 // sequence, not three contexts belonging to the parent.
 {Doc d("<div id='p'>before<div id='x'>inside</div>after</div>");auto p=d.id("p");assert(aether_node_get_child_count(p)==3);assert(aether_node_get_child(p,1)==d.id("x"));assert(aether_node_get_child_count(d.id("x"))==1);parent(d,"x","p");}
 puts("B2 parser observations PASS (not admission or context ledger validation)");
}
