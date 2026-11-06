#include "dom_types.h"
#include "html_attribute_name.h"
#include "../abi/aether_abi.h"
#include "../primitives/html_space_tokens.h"

static aether_node_s* make_element(const std::string& tag, aether_doc_s* doc) {
    auto* el = new aether_node_s();
    el->type = AETHER_NODE_ELEMENT;
    el->tag_name = tag;
    el->style_node_id = doc->next_style_node_id++;
    return el;
}

AETHER_API char* aether_element_get_tag_name(aether_node_t element) {
    if (!element || element->type != AETHER_NODE_ELEMENT) return nullptr;
    return aether::strdup(element->tag_name.c_str());
}

AETHER_API char* aether_element_get_attribute(aether_node_t element, const char* name) {
    if (!element || element->type != AETHER_NODE_ELEMENT || !name) return nullptr;
    auto it = element->attributes.find(aether::canonical_html_attribute_name(name));
    return (it != element->attributes.end()) ? aether::strdup(it->second.c_str()) : nullptr;
}

AETHER_API int32_t aether_element_get_attribute_count(aether_node_t element) {
    if (!element || element->type != AETHER_NODE_ELEMENT) return 0;
    return static_cast<int32_t>(element->attributes.size());
}

AETHER_API char* aether_element_get_attribute_name(aether_node_t element, int32_t index) {
    if (!element || element->type != AETHER_NODE_ELEMENT || index < 0 ||
        index >= static_cast<int32_t>(element->attributes.size())) return nullptr;
    auto it = element->attributes.begin();
    std::advance(it, index);
    return aether::strdup(it->first.c_str());
}

AETHER_API char* aether_element_get_attribute_value(aether_node_t element, int32_t index) {
    if (!element || element->type != AETHER_NODE_ELEMENT || index < 0 ||
        index >= static_cast<int32_t>(element->attributes.size())) return nullptr;
    auto it = element->attributes.begin();
    std::advance(it, index);
    return aether::strdup(it->second.c_str());
}

AETHER_API aether_status_t aether_element_set_attribute(
    aether_node_t element, const char* name, const char* value)
{
    if (!element || element->type != AETHER_NODE_ELEMENT || !name || !value) {
        return AETHER_ERROR_NULL_ARG;
    }

    const std::string attribute_name = aether::canonical_html_attribute_name(name);
    if (attribute_name == "id") {
        auto* root = element;
        while (root->parent) root = root->parent;
        if (root->type == AETHER_NODE_DOCUMENT) {
            auto* document = static_cast<aether_doc_s*>(root);
            auto existing = element->attributes.find("id");
            if (existing != element->attributes.end()) {
                auto indexed = document->id_index.find(existing->second);
                if (indexed != document->id_index.end() && indexed->second == element) {
                    document->id_index.erase(indexed);
                }
            }
            document->id_index[std::string(value)] = element;
        }
    }

    element->attributes[attribute_name] = value;
    return AETHER_OK;
}

AETHER_API aether_bool_t aether_element_has_attribute(aether_node_t element, const char* name) {
    if (!element || element->type != AETHER_NODE_ELEMENT || !name) return AETHER_FALSE;
    return element->attributes.count(aether::canonical_html_attribute_name(name)) ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_element_has_class(aether_node_t element, const char* class_name) {
    if (!element || element->type != AETHER_NODE_ELEMENT || !class_name) return AETHER_FALSE;
    auto it = element->attributes.find("class");
    if (it == element->attributes.end()) return AETHER_FALSE;
    return aether::html_space_contains(it->second, class_name) ? AETHER_TRUE : AETHER_FALSE;
}

AETHER_API aether_bool_t aether_element_is_required(aether_node_t element) {
    return aether_element_has_attribute(element, "required");
}

AETHER_API aether_bool_t aether_element_is_disabled(aether_node_t element) {
    return aether_element_has_attribute(element, "disabled");
}

AETHER_API aether_bool_t aether_element_is_focusable(aether_node_t element) {
    if (!element || element->type != AETHER_NODE_ELEMENT) return AETHER_FALSE;
    static const char* focusable[] = {"input", "button", "textarea", "select"};
    for (auto* tag : focusable) {
        if (element->tag_name == tag) return AETHER_TRUE;
    }
    return aether_element_has_attribute(element, "tabindex");
}

AETHER_API int32_t aether_element_get_state(aether_node_t element) {
    return element ? element->state : 0;
}

AETHER_API void aether_element_set_state(aether_node_t element, int32_t state) {
    if (element) element->state = state;
}
