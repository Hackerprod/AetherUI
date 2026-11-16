#include <aether/aether.h>
#include <cassert>
int main(){aether_doc_t doc=nullptr;assert(aether_doc_create(&doc)==AETHER_OK);assert(doc);aether_doc_destroy(doc);}
