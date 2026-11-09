#include "dom_types.h"
#include "html_attribute_name.h"
#include "../abi/aether_abi.h"
#include "../primitives/html_space_tokens.h"

AETHER_API char* aether_element_get_tag_name(aether_node_t element) {
    if (!element || element->type != AETHER_NODE_ELEMENT) return nullptr;
    return aether::strdup(element->tag_name.c_str());
}

AETHER_API char* aether_element_get_attribute(aether_node_t element, const char* name) {
    if (!element || element->type != AETHER_NODE_ELEMENT || !name) return nullptr;
    try {
        auto it = element->attributes.find(aether::canonical_html_attribute_name(name));
        return (it != element->attributes.end()) ? aether::strdup(it->second.c_str()) : nullptr;
    } catch(...) { return nullptr; }
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

    if (!aether_dom_can_touch(element)) return AETHER_ERROR_INVALID_STATE;
    try {
        const auto attribute_name=aether::canonical_html_attribute_name(name);
        auto attributes=element->attributes; attributes[attribute_name]=value;
        std::string base;
        if (attribute_name=="value") base=value;
        // No live id-cache mutation is required: lookup rebuilds locally.
        element->attributes.swap(attributes);
        if (attribute_name=="value") {
            element->authored_value.swap(base); element->authored_value_present=true;
            element->runtime_value_present=false;
        }
        aether_node_touch_typed(element,aether::dom::MutationKind::attribute);
        return AETHER_OK;
    } catch(const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch(...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API aether_bool_t aether_element_has_attribute(aether_node_t element, const char* name) {
    if (!element || element->type != AETHER_NODE_ELEMENT || !name) return AETHER_FALSE;
    try { return element->attributes.count(aether::canonical_html_attribute_name(name)) ? AETHER_TRUE : AETHER_FALSE; }
    catch(...) { return AETHER_FALSE; }
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
    if (element && element->state != state) {
        if (!aether_dom_can_touch(element)) {
            if (auto owner=element->document_owner.lock()) owner->sticky_status=AETHER_ERROR_INVALID_STATE;
            return;
        }
        const int32_t old_state = element->state;
        for (unsigned i=0; i<32; ++i) if ((static_cast<uint32_t>(old_state)^static_cast<uint32_t>(state)) & (uint32_t{1}<<i)) {
            if (element->identity->state_bit_write_serial[i]==UINT64_MAX) {
                if (auto owner=element->document_owner.lock()) owner->sticky_status=AETHER_ERROR_INVALID_STATE;
                return;
            }
        }
        for (unsigned i=0; i<32; ++i) if ((static_cast<uint32_t>(old_state)^static_cast<uint32_t>(state)) & (uint32_t{1}<<i)) ++element->identity->state_bit_write_serial[i];
        element->state = state;
        aether_node_touch_typed(element, aether::dom::MutationKind::state, false, old_state, state);
    }
}
