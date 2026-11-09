#include "dom_style_source.h"
namespace {
void text(aether_node_s* node,std::string& result){if(node->is_generated)return;if(node->type==AETHER_NODE_TEXT)result+=node->text;else for(auto* child:node->children)text(child,result);}
void collect(aether_node_s* node,std::string& result,bool& first){if(node->is_generated)return;if(node->type==AETHER_NODE_ELEMENT&&node->tag_name=="style"){if(!first)result+='\n';first=false;text(node,result);}for(auto* child:node->children)collect(child,result,first);}
}
aether_status_t aether_dom_collect_style_source(aether_node_s* root,std::string& output)noexcept{
    if(!root)return AETHER_ERROR_NULL_ARG;
    try{std::string candidate;bool first=true;collect(root,candidate,first);output.swap(candidate);return AETHER_OK;}catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
